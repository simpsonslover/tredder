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

    import_ses(ses)


def jlc_vias(board, ring=0.15):
    """Freerouting adds its own vias at 0.6/0.4 mm (0.1 mm copper ring); JLCPCB's rules want
    >= 0.15 mm. Drill them smaller and keep the copper, so no clearance changes."""
    n = 0
    for v in board.GetTracks():
        if v.GetClass() == "PCB_VIA":
            d = pcbnew.ToMM(v.GetWidth(pcbnew.F_Cu))
            if (d - pcbnew.ToMM(v.GetDrillValue())) / 2 < ring - 1e-6:
                v.SetDrill(pcbnew.FromMM(round(d - 2 * ring, 2)))
                n += 1
    return n


def import_ses(ses):
    """Import a routed session. Boards from before the battery moved onto J2 had BT1 as a
    board-only footprint with no library id, which makes ImportSpecctraSES fail: drop it
    from the session and the board for the import, then redraw the battery."""
    import add_battery
    text = open(ses, encoding="utf8").read()
    # BT1 has no library id, so its block is "(component <empty> (place BT1 ...))": drop the whole block
    clean = re.sub(r"\n\s*\(component\s*\n\s*\(place BT1 [^)]*\)\s*\n\s*\)", "", text)
    if clean != text:
        ses = ses[:-4] + "_nobt1.ses"
        open(ses, "w", encoding="utf8").write(clean)
    board = pcbnew.LoadBoard(PCB)
    bt1 = board.FindFootprintByReference("BT1")
    if bt1:
        board.Remove(bt1)
    assert pcbnew.ImportSpecctraSES(board, ses), "SES import failed"
    print("vias re-drilled for a 0.15 mm ring:", jlc_vias(board))
    if bt1:
        add_battery.add(board, (100.0, 100.0))
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.Save(PCB)
    import clean_tracks  # Freerouting leaves duplicate segments and dead-end spurs
    clean_tracks.main(PCB)
    print("routed ->", PCB)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--import":
        import_ses(sys.argv[2])  # re-import an existing .ses without routing again
    else:
        main(*sys.argv[1:])
