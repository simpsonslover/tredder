<#
  Tredder dev stack: Android emulator + Expo app + tredder device emulator, wired together.

    powershell -ExecutionPolicy Bypass -File tools\devstack\devstack.ps1            # everything
    powershell -ExecutionPolicy Bypass -File tools\devstack\devstack.ps1 -SetupOnly # SDK + AVD only
    ... -SkipBuild     reuse the installed app (no native rebuild)
    ... -NoEmulator    only start the tredder device (e.g. for a physical phone over USB)
    ... -Headless      Android emulator without a window
    ... -TsSimulator   use the TypeScript behaviour simulator (browser window) instead of
                       the real firmware emulator

  What it does
    1. Installs the Android SDK pieces it needs into %LOCALAPPDATA%\Android\Sdk (no admin).
    2. Makes sure the emulator has hardware acceleration (AEHD driver: needs ONE admin prompt).
    3. Creates the AVD "tredder_phone" (Android 16 / API 36, x86_64, Google APIs).
    4. Builds (if needed) and starts the tredder emulator: the REAL firmware (firmware/app +
       firmware/core) on emulated board hardware, phone link on ws://127.0.0.1:8765.
    5. Boots the Android emulator and tunnels the device port + Metro into it (adb reverse).
    6. Builds + installs the app (Gradle), starts Metro in its own window and opens the app.
  Every step is idempotent: re-running skips what is already done.
#>
param(
  [switch]$SetupOnly,
  [switch]$SkipBuild,
  [switch]$NoEmulator,
  [switch]$Headless,
  [switch]$TsSimulator,
  [int]$SimPort = 8765,
  [int]$UiPort = 8766
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Sdk = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
$Api = 36
$Image = "system-images;android-$Api;google_apis;x86_64"
$Avd = 'tredder_phone'

function Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }
function Ok($msg) { Write-Host "    $msg" -ForegroundColor Green }
function Warn($msg) { Write-Host "    $msg" -ForegroundColor Yellow }

function Find-Java {
  if ($env:JAVA_HOME -and (Test-Path "$env:JAVA_HOME\bin\java.exe")) { return $env:JAVA_HOME }
  $cands = @(Get-ChildItem 'C:\Program Files\Eclipse Adoptium', 'C:\Program Files\Java', 'C:\Program Files\Microsoft' -Directory -ErrorAction SilentlyContinue |
      Where-Object { $_.Name -match 'jdk-(17|21)' } | Sort-Object Name -Descending)
  if ($cands) { return $cands[0].FullName }
  throw 'JDK 17 or 21 not found. Install Temurin 17/21 (https://adoptium.net) and re-run.'
}

function Use-SdkEnv {
  $env:ANDROID_HOME = $Sdk
  $env:ANDROID_SDK_ROOT = $Sdk
  $env:JAVA_HOME = Find-Java
  $env:Path = "$Sdk\platform-tools;$Sdk\emulator;$Sdk\cmdline-tools\latest\bin;$env:JAVA_HOME\bin;$env:Path"
}

function Install-CmdlineTools {
  if (Test-Path "$Sdk\cmdline-tools\latest\bin\sdkmanager.bat") { Ok 'cmdline-tools present'; return }
  $xml = (Invoke-WebRequest -UseBasicParsing 'https://dl.google.com/android/repository/repository2-3.xml').Content
  $m = [regex]::Match($xml, '<remotePackage path="cmdline-tools;latest">[\s\S]*?<url>(commandlinetools-win-[^<]+)</url>')
  if (-not $m.Success) { throw 'could not find cmdline-tools download in Google repository index' }
  $zip = Join-Path $env:TEMP $m.Groups[1].Value
  Ok "downloading $($m.Groups[1].Value)"
  Invoke-WebRequest -UseBasicParsing "https://dl.google.com/android/repository/$($m.Groups[1].Value)" -OutFile $zip
  $tmp = Join-Path $env:TEMP 'tredder-cmdline-tools'
  if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
  Expand-Archive $zip $tmp
  New-Item -ItemType Directory -Force "$Sdk\cmdline-tools" | Out-Null
  Move-Item "$tmp\cmdline-tools" "$Sdk\cmdline-tools\latest"
  Remove-Item $zip
}

# Windows PowerShell 5.1 turns any stderr line from a native tool into a terminating
# error when $ErrorActionPreference = 'Stop'. Run native tools with 'Continue'.
function Native([scriptblock]$cmd) {
  $prev = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try { & $cmd 2>&1 | ForEach-Object { "$_" } } finally { $ErrorActionPreference = $prev }
}

# cmdline-tools 23+: `android sdk` replaces the deprecated sdkmanager (slash-separated package ids).
function AndroidCli([string[]]$a) {
  Native { & "$Sdk\cmdline-tools\latest\bin\android.exe" --no-metrics "--sdk=$Sdk" @a } | Where-Object { $_ -notmatch 'UseAllWindowsProcessorGroups' }
}

function Install-SdkPackages {
  $pkgs = @('platform-tools', 'emulator', "platforms/android-$Api", "system-images/android-$Api/google_apis/x86_64", 'extras/google/Android_Emulator_Hypervisor_Driver', 'cmake/3.31.5')
  $installed = (AndroidCli @('sdk', 'list')) -join "`n"
  $cut = $installed.IndexOf('Available packages')
  if ($cut -ge 0) { $installed = $installed.Substring(0, $cut) }
  $missing = $pkgs | Where-Object { $installed -notmatch [regex]::Escape($_) }
  if (-not $missing) { Ok 'SDK packages present'; return }
  Ok "installing: $($missing -join ', ') (several GB on first run)"
  AndroidCli (@('sdk', 'install') + $missing) | Where-Object { $_ -notmatch '^\s*\[' } | Out-Host
  $installed = (AndroidCli @('sdk', 'list')) -join "`n"
  foreach ($p in $missing) { if ($installed -notmatch [regex]::Escape($p)) { throw "failed to install $p" } }
}

# Long-path-aware Ninja (>= 1.12) for the native build; see short-cxx.init.gradle.
function Install-Ninja {
  $dir = Join-Path $Sdk 'ninja-1.13.2'
  if (Test-Path "$dir\ninja.exe") { Ok 'ninja 1.13.2 present'; return }
  New-Item -ItemType Directory -Force $dir | Out-Null
  $zip = Join-Path $env:TEMP 'ninja-win.zip'
  Invoke-WebRequest -UseBasicParsing 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip' -OutFile $zip
  Expand-Archive $zip $dir -Force
  Remove-Item $zip
  Ok 'installed ninja 1.13.2'
}

function Ensure-LongPaths {
  $v = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -Name LongPathsEnabled -ErrorAction SilentlyContinue).LongPathsEnabled
  if ($v -eq 1) { Ok 'Windows long paths enabled'; return }
  Warn 'Windows long paths are off; the native build needs them. Enabling (admin prompt)...'
  Start-Process powershell -Verb RunAs -Wait -ArgumentList '-NoProfile -Command Set-ItemProperty HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem -Name LongPathsEnabled -Value 1'
}

function Ensure-Acceleration {
  $check = (Native { & "$Sdk\emulator\emulator.exe" -accel-check }) -join "`n"
  if ($check -match 'is installed and usable|WHPX.*usable|AEHD.*usable') { Ok ($check -split "`n" | Select-Object -Last 1); return $true }
  Warn 'Emulator hardware acceleration is not available.'
  Warn 'Installing the Android Emulator Hypervisor Driver (AEHD) needs administrator rights: approve the Windows prompt.'
  $inst = Get-ChildItem "$Sdk\extras\google\Android_Emulator_Hypervisor_Driver" -Filter silent_install.bat -Recurse | Select-Object -First 1
  if (-not $inst) { throw 'AEHD package not found in the SDK' }
  try {
    Start-Process -FilePath $inst.FullName -WorkingDirectory $inst.DirectoryName -Verb RunAs -Wait
  } catch {
    Warn 'Admin prompt was declined. Alternative: enable "Windows Hypervisor Platform" in "Turn Windows features on or off", reboot, re-run.'
    return $false
  }
  $check = (Native { & "$Sdk\emulator\emulator.exe" -accel-check }) -join "`n"
  if ($check -match 'usable') { Ok 'acceleration OK'; return $true }
  Warn $check
  Warn 'Still no acceleration. Make sure SVM (AMD-V) is enabled in BIOS, then re-run.'
  return $false
}

function Ensure-Avd {
  $list = (Native { & "$Sdk\cmdline-tools\latest\bin\avdmanager.bat" list avd -c }) -join "`n"
  if ($list -match "(?m)^$Avd\s*$") { Ok "AVD $Avd exists"; return }
  Native { 'no' | & "$Sdk\cmdline-tools\latest\bin\avdmanager.bat" create avd -n $Avd -k $Image -d pixel_7 --force } | Out-Host
  if (-not (Test-Path (Join-Path $env:USERPROFILE ".android\avd\$Avd.avd"))) { throw "AVD creation failed" }
  $cfg = Join-Path $env:USERPROFILE ".android\avd\$Avd.avd\config.ini"
  # 3 GB RAM, hardware keyboard, no boot-animation wait
  Add-Content $cfg "hw.ramSize=3072`r`nhw.keyboard=yes`r`ndisk.dataPartition.size=6G"
  Ok "created AVD $Avd"
}

function Stop-OldSimulator {
  # A TypeScript simulator from an earlier run holds the port: stop that process only.
  $owners = Get-NetTCPConnection -LocalPort $SimPort -State Listen -ErrorAction SilentlyContinue | Select-Object -ExpandProperty OwningProcess -Unique
  foreach ($procId in $owners) {
    $cmd = (Get-CimInstance Win32_Process -Filter "ProcessId=$procId" -ErrorAction SilentlyContinue).CommandLine
    if ($cmd -match 'src[\\/]server\.ts') { Stop-Process -Id $procId -Force; Ok "stopped the old TypeScript simulator (pid $procId)" }
  }
}

function Start-TsSimulator {
  $simDir = Join-Path $Root 'sim'
  if (-not (Test-Path "$simDir\node_modules")) { Push-Location $simDir; npm install --no-audit --no-fund | Out-Host; Pop-Location }
  $busy = Get-NetTCPConnection -LocalPort $SimPort -State Listen -ErrorAction SilentlyContinue
  if ($busy) { Ok "something already listens on :$SimPort (assuming the simulator)"; return }
  Start-Process -FilePath 'cmd.exe' -ArgumentList "/k title tredder simulator && npx tsx src/server.ts --port $SimPort --ui-port $UiPort" -WorkingDirectory $simDir
  for ($i = 0; $i -lt 60 -and -not (Get-NetTCPConnection -LocalPort $UiPort -State Listen -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 500 }
  Start-Process "http://localhost:$UiPort"
  Ok "TypeScript simulator: protocol ws://localhost:$SimPort, device window http://localhost:$UiPort"
}

# The real firmware on emulated hardware (firmware/platform/emu), rebuilt when any firmware source changed.
function Start-Device {
  $fw = Join-Path $Root 'firmware'
  $exe = Join-Path $fw 'build-emu\tredder-emu.exe'
  $newest = Get-ChildItem (Join-Path $fw 'core'), (Join-Path $fw 'app'), (Join-Path $fw 'platform\emu') -Recurse -Include *.c, *.h |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if (-not (Test-Path $exe) -or (Get-Item $exe).LastWriteTime -lt $newest.LastWriteTime) {
    $py = Get-Command python -ErrorAction SilentlyContinue
    if (-not $py -or -not (Test-Path 'C:\msys64\mingw64\bin\gcc.exe')) {
      Warn 'Cannot build the firmware emulator (needs Python + MSYS2 MinGW-w64 gcc: pacman -S mingw-w64-x86_64-gcc).'
      Warn 'Falling back to the TypeScript simulator.'
      Start-TsSimulator
      return
    }
    Ok 'building the firmware emulator (python firmware/tools/build_emu.py)...'
    Native { & python (Join-Path $fw 'tools\build_emu.py') } | Out-Host
    if (-not (Test-Path $exe)) { throw 'firmware emulator build failed (see above)' }
  }
  if (Get-Process tredder-emu -ErrorAction SilentlyContinue) { Ok 'tredder emulator already running'; return }
  Stop-OldSimulator
  if (Get-NetTCPConnection -LocalPort $SimPort -State Listen -ErrorAction SilentlyContinue) { throw "port $SimPort is in use by another program" }
  Start-Process -FilePath $exe -ArgumentList "--port $SimPort" -WorkingDirectory (Split-Path $exe)
  for ($i = 0; $i -lt 40 -and -not (Get-NetTCPConnection -LocalPort $SimPort -State Listen -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 250 }
  Ok "tredder emulator: real firmware, phone link ws://127.0.0.1:$SimPort (state in %LOCALAPPDATA%\tredder-emu)"
}

function Start-Emulator {
  $serial = (Native { & adb devices }) | Select-String '^emulator-\d+\s+device' | ForEach-Object { ($_ -split '\s+')[0] } | Select-Object -First 1
  if (-not $serial) {
    $emuArgs = @('-avd', $Avd, '-no-snapshot-save', '-gpu', 'auto')
    if ($Headless) { $emuArgs += '-no-window' }
    Start-Process -FilePath "$Sdk\emulator\emulator.exe" -ArgumentList $emuArgs
    Ok 'booting emulator (first boot takes a few minutes)...'
    Native { & adb wait-for-device } | Out-Null
    do { Start-Sleep 3; $b = (Native { & adb shell getprop sys.boot_completed }) -join '' } while ($b.Trim() -ne '1')
    $serial = (Native { & adb devices }) | Select-String '^emulator-\d+\s+device' | ForEach-Object { ($_ -split '\s+')[0] } | Select-Object -First 1
  }
  Ok "emulator ready: $serial"
  Native { & adb -s $serial reverse "tcp:$SimPort" "tcp:$SimPort" } | Out-Null   # app's ws://localhost:8765 -> simulator on the PC
  Native { & adb -s $serial reverse tcp:8081 tcp:8081 } | Out-Null               # Metro
  Ok "adb reverse tcp:$SimPort (simulator) and tcp:8081 (Metro)"
  return $serial
}

# ---------------------------------------------------------------- main
Step 'Android SDK'
New-Item -ItemType Directory -Force $Sdk | Out-Null
Use-SdkEnv
Ok "JAVA_HOME = $env:JAVA_HOME"
Install-CmdlineTools
Install-SdkPackages
Install-Ninja
Ensure-LongPaths
Step 'Emulator acceleration'
$accel = Ensure-Acceleration
Step 'Virtual device'
Ensure-Avd
if ($SetupOnly) { Step 'Setup done'; exit 0 }

$free = [math]::Round((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB, 1)
if ($free -lt 5) { Warn "Only $free GB RAM free: the emulator + Gradle build need ~5 GB. Close other programs (games, browsers) if it gets slow." }

Step 'Tredder device'
if ($TsSimulator) { Start-TsSimulator } else { Start-Device }
if ($NoEmulator) { Step 'Device running (no Android emulator requested)'; exit 0 }
if (-not $accel) { throw 'Cannot start the emulator without hardware acceleration (see messages above).' }

Step 'Android emulator'
$serial = Start-Emulator

Step 'Expo app'
# Windows MAX_PATH: native modules (react-native-worklets) write CMake objects whose paths repeat
# the full project path; from C:\Users\...\Desktop that exceeds 260 characters. A Gradle init script
# (only active for this app) moves the CMake staging dir to C:/tcx. No admin, no edits to android/.
$initDir = Join-Path $env:USERPROFILE '.gradle\init.d'
New-Item -ItemType Directory -Force $initDir | Out-Null
Copy-Item (Join-Path $PSScriptRoot 'short-cxx.init.gradle') (Join-Path $initDir 'tredder-short-cxx.gradle') -Force
Ok 'short native build paths: ~/.gradle/init.d/tredder-short-cxx.gradle'
# Only the emulator's CPU type: ~4x faster than building all four ABIs.
$env:ORG_GRADLE_PROJECT_reactNativeArchitectures = 'x86_64'
$appDir = Join-Path $Root 'app'
$apk = Join-Path $appDir 'android\app\build\outputs\apk\debug\app-debug.apk'
Push-Location $appDir
try {
  if (-not (Test-Path node_modules)) { npm install --no-audit --no-fund | Out-Host }
  $installed = ((Native { & adb -s $serial shell pm list packages com.tredder.wallet }) -join "`n") -match 'com\.tredder\.wallet'
  if (-not $SkipBuild -or -not $installed) {
    if (-not (Test-Path 'android\gradlew.bat')) { Native { npx expo prebuild --platform android --no-install } | Out-Host }
    # Development client for the emulator's CPU only (first build ~10-20 min, later ~1-2 min).
    Ok 'building the app (Gradle assembleDebug)...'
    Push-Location android
    try { Native { & .\gradlew.bat app:assembleDebug --console=plain } | Select-String -Pattern 'BUILD |FAILURE|error:|What went wrong' | Out-Host }
    finally { Pop-Location }
    if (-not (Test-Path $apk)) { throw 'app build failed (run "cd app\android; .\gradlew app:assembleDebug" to see the full log)' }
    Ok 'installing the app on the emulator'
    $r = (Native { & adb -s $serial install -r $apk }) -join "`n"
    if ($r -notmatch 'Success') { throw "adb install failed: $r" }
  } else { Ok 'app already installed (-SkipBuild)' }

  # Metro (the JS bundler) in its own window; the development build loads its JavaScript from it.
  if (-not (Get-NetTCPConnection -LocalPort 8081 -State Listen -ErrorAction SilentlyContinue)) {
    Start-Process -FilePath 'cmd.exe' -ArgumentList '/k title Metro (tredder app) && npx expo start --dev-client --port 8081' -WorkingDirectory $appDir
  }
  $ok = $false
  for ($i = 0; $i -lt 120 -and -not $ok; $i++) {
    try {
      $c = (Invoke-WebRequest -UseBasicParsing http://localhost:8081/status -TimeoutSec 2).Content
      if ($c -is [byte[]]) { $c = [Text.Encoding]::ASCII.GetString($c) }  # PS 5.1 returns bytes for text/plain without charset
      $ok = $c -match 'running'
    } catch { Start-Sleep 1 }
  }
  if (-not $ok) { throw 'Metro did not start (see the "Metro" window)' }
  Ok 'Metro running on :8081'
  Native { & adb -s $serial shell am force-stop com.tredder.wallet } | Out-Null
  Native { & adb -s $serial shell monkey -p com.tredder.wallet -c android.intent.category.LAUNCHER 1 } | Out-Null
  $up = $false
  for ($i = 0; $i -lt 30 -and -not $up; $i++) {
    Start-Sleep 1
    $up = ((Native { & adb -s $serial shell dumpsys activity activities }) -join "`n") -match 'ResumedActivity.*com\.tredder\.wallet'
  }
  if ($up) { Ok 'app open on the emulator' } else { Warn 'the app did not come to the front: open "Tredder Wallet" from the emulator app drawer' }
} finally { Pop-Location }

Step 'Ready'
Write-Host @"
    1. In the tredder emulator window, set up the device: create a wallet, or import the public
       test phrase (abandon x 11, then about). Unlock it with your PIN.
    2. In the app: Device tab, then 'Connect to tredder simulator'. Approve the pairing on the
       tredder emulator (OK, then PIN).
    Keys in the tredder window: arrows, Enter = OK, Esc = Back. Typing a digit or letter makes
    the emulator press the buttons that select it.
"@
