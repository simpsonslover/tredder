"""
Autoroute wallet.kicad_pcb with Freerouting (run with KiCad's python).

    "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" route.py <freerouting.jar> [passes]

In1/In2 are passed to the router as power planes (GND / +3V3) so no signal
is routed on them. The result is a starting point: review every trace.
"""
import os
import re
import subprocess
import sys
import tempfile

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.abspath(os.path.join(HERE, "..", "wallet.kicad_pcb"))


def main(jar, passes="30"):
    work = tempfile.mkdtemp(prefix="wallet_route_")
    dsn, ses = os.path.join(work, "wallet.dsn"), os.path.join(work, "wallet.ses")
    board = pcbnew.LoadBoard(PCB)
    assert pcbnew.ExportSpecctraDSN(board, dsn)

    s = open(dsn, encoding="utf8").read()
    for layer in ("In1.Cu", "In2.Cu"):
        s = s.replace(f"(layer {layer}\n      (type signal)", f"(layer {layer}\n      (type power)")
    s = re.sub(r"\n    \(plane GND \(polygon (F|B)\.Cu[^)]*\)\)", "", s)  # outer pours: KiCad fills them
    open(dsn, "w", encoding="utf8").write(s)

    cmd = ["java", "-jar", jar, "-de", dsn, "-do", ses, "-mp", passes,
           f"--router.max_passes={passes}", "--gui.enabled=false"]
    print(" ".join(cmd))
    subprocess.run(cmd, cwd=os.path.dirname(jar), check=True, timeout=1500)

    board = pcbnew.LoadBoard(PCB)
    assert pcbnew.ImportSpecctraSES(board, ses), "SES import failed"
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.Save(PCB)
    print("routed ->", PCB)


if __name__ == "__main__":
    main(*sys.argv[1:])
