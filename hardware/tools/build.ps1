# Regenerate everything from design.py, then run ERC + DRC + simulation.
#   powershell -ExecutionPolicy Bypass -File hardware\tools\build.ps1            (keeps existing routing)
#   powershell -ExecutionPolicy Bypass -File hardware\tools\build.ps1 -Fresh     (re-place PCB: DISCARDS ROUTING)
param([switch]$Fresh)
$ErrorActionPreference = "Stop"
$kicad = "C:\Program Files\KiCad\10.0\bin"
$tools = $PSScriptRoot
$hw = Split-Path $tools -Parent

Push-Location $tools
try {
    python design.py
    python gen_sch.py
    python gen_bom.py
    & "$kicad\python.exe" gen_fp.py
    python gen_step_models.py
    if ($Fresh -or -not (Test-Path "$hw\wallet.kicad_pcb")) {
        & "$kicad\python.exe" gen_pcb.py
        Write-Host "Placement regenerated (unrouted). Route with: & '$kicad\python.exe' route.py <path-to-freerouting-2.1.0.jar> 30"
    } else {
        & "$kicad\python.exe" sync_values.py
        & "$kicad\python.exe" models3d.py
        Write-Host "PCB kept; values synced. Connection/footprint changes need 'Update PCB from Schematic' (F8) in KiCad."
    }
} finally { Pop-Location }

Push-Location $hw
try {
    & "$kicad\kicad-cli.exe" sch erc --severity-all -o erc.rpt wallet.kicad_sch
    & "$kicad\kicad-cli.exe" pcb drc --refill-zones --schematic-parity --severity-error -o drc.rpt wallet.kicad_pcb
    & "$kicad\kicad-cli.exe" sch export pdf -o wallet_schematic.pdf wallet.kicad_sch
    & "$kicad\python.exe" sim\run_all.py
    if ($LASTEXITCODE -ne 0) { Write-Host "SIMULATION FAILURES: see hardware\sim\report.md" -ForegroundColor Red }
} finally { Pop-Location }
