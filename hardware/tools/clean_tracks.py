"""
Remove Freerouting leftovers from the routed board, without changing any connection:
  - duplicate track segments (the same segment stored several times, either direction)
  - dead-end spurs: segments with an end that touches no other track, via, pad or zone
    (Fusion's JLCPCB rules flag these as "Wire Stub"; KiCad as "track has unconnected end")
Repeats until nothing changes. Refuses to save if the number of unconnected items changes.

KiCad's python crashes or breaks when items are removed through it, so it is only used to
decide what goes; the segments are deleted from the file text by uuid.

Run with KiCad's python:  & "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" tools/clean_tracks.py
route.py runs it after every session import.
"""
import os
import re

import pcbnew

PCB = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "wallet.kicad_pcb"))


def unconnected(path):
    b = pcbnew.LoadBoard(path)
    b.BuildConnectivity()
    return b.GetConnectivity().GetUnconnectedCount(True)


def to_remove(board):
    """uuids of duplicate segments and dead-end spurs."""
    tracks = [t for t in board.GetTracks() if t.GetClass() == "PCB_TRACK"]
    vias = [t for t in board.GetTracks() if t.GetClass() == "PCB_VIA"]
    pads = [p for fp in board.GetFootprints() for p in fp.Pads()]
    zones = list(board.Zones())
    gone, seen, dup = set(), set(), 0
    for i, t in enumerate(tracks):
        um = lambda v: round(v / 1000)        # compare at 1 um: copies can differ by a few nm
        a, b = (um(t.GetStart().x), um(t.GetStart().y)), (um(t.GetEnd().x), um(t.GetEnd().y))
        key = (t.GetLayer(), t.GetNetCode(), t.GetWidth(), min(a, b), max(a, b))
        if key in seen:
            gone.add(i)
            dup += 1
        seen.add(key)

    def dist(p, a, b):
        ax, ay, bx, by, px, py = a.x, a.y, b.x, b.y, p.x, p.y
        L = (bx - ax) ** 2 + (by - ay) ** 2
        k = 0 if L == 0 else max(0.0, min(1.0, ((px - ax) * (bx - ax) + (py - ay) * (by - ay)) / L))
        return ((px - ax - k * (bx - ax)) ** 2 + (py - ay - k * (by - ay)) ** 2) ** 0.5

    # segments lying entirely on top of another segment of the same net (copper fully covered)
    for i, t in enumerate(tracks):
        if i in gone:
            continue
        for j, o in enumerate(tracks):
            if j != i and j not in gone and o.GetLayer() == t.GetLayer() and o.GetNetCode() == t.GetNetCode() \
                    and o.GetLength() > t.GetLength() and o.GetWidth() >= t.GetWidth():
                slack = (o.GetWidth() - t.GetWidth()) / 2 + 1000      # 1 um
                if dist(t.GetStart(), o.GetStart(), o.GetEnd()) <= slack and \
                        dist(t.GetEnd(), o.GetStart(), o.GetEnd()) <= slack:
                    gone.add(i)
                    dup += 1
                    break

    def attached(i, e):
        t = tracks[i]
        for j, o in enumerate(tracks):
            if j != i and j not in gone and o.GetLayer() == t.GetLayer() and o.GetNetCode() == t.GetNetCode() \
                    and (o.GetStart() == e or o.GetEnd() == e or o.HitTest(e)):
                return True
        if any(v.GetNetCode() == t.GetNetCode() and v.HitTest(e) for v in vias):
            return True
        if any(p.GetNetCode() == t.GetNetCode() and p.IsOnLayer(t.GetLayer()) and p.HitTest(e) for p in pads):
            return True
        return any(z.GetNetCode() == t.GetNetCode() and z.IsOnLayer(t.GetLayer())
                   and z.HitTestFilledArea(t.GetLayer(), e) for z in zones)

    spurs = 0
    while True:
        new = [i for i, t in enumerate(tracks) if i not in gone
               and (not attached(i, t.GetStart()) or not attached(i, t.GetEnd()))]
        if not new:
            break
        gone.update(new)
        spurs += len(new)
    return {tracks[i].m_Uuid.AsString() for i in gone}, dup, spurs


def delete_by_uuid(text, uuids):
    for u in uuids:
        k = text.index(f'(uuid "{u}")')
        s = text.rfind("(segment", 0, k)
        depth, i = 0, s
        while True:
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        text = text[:text.rfind("\n", 0, s)] + text[i + 1:]
    return text


def main(path=PCB):
    before = unconnected(path)
    uuids, dup, spurs = to_remove(pcbnew.LoadBoard(path))
    if not uuids:
        print("clean_tracks: nothing to remove")
        return
    original = open(path, encoding="utf8").read()
    # write in place: the zones must be re-poured with this project's rules (wallet.kicad_pro)
    with open(path, "w", encoding="utf8", newline="\n") as fh:
        fh.write(delete_by_uuid(original, uuids))
    after = unconnected(path)
    if after != before:
        with open(path, "w", encoding="utf8", newline="\n") as fh:
            fh.write(original)
        raise SystemExit(f"clean_tracks: unconnected items {before} -> {after}; board restored")
    b = pcbnew.LoadBoard(path)
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    b.Save(path)
    print(f"clean_tracks: removed {dup} duplicate or covered segments and {spurs} dead-end spurs; "
          f"unconnected items unchanged ({before})")


if __name__ == "__main__":
    main()
