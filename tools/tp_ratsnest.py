"""Hide or bring back the ratsnest lines of test points parked off the board edge.

  python tp_ratsnest.py BOARD.kicad_pcb disable   every TP* footprint outside the board outline gets its pad moved to
                                                  a net named <net>_DISABLED_<ref>, so it draws no ratsnest line
  python tp_ratsnest.py BOARD.kicad_pcb restore   every pad on a ..._DISABLED_... net goes back to its own net
  python tp_ratsnest.py BOARD.kicad_pcb list      show what is disabled now

Run with KiCad's Python (C:\Program Files\KiCad\10.0\bin\python.exe) and with the board closed in KiCad.
"Update PCB from Schematic" also puts the nets back.
"""
import sys
import pcbnew

MARK = "_DISABLED_"
path, mode = sys.argv[1], sys.argv[2]
b = pcbnew.LoadBoard(path)
edge = b.GetBoardEdgesBoundingBox()
n = 0
for f in b.GetFootprints():
    ref = f.GetReference()
    for p in f.Pads():
        name = p.GetNetname()
        if MARK in name:
            if mode == "list":
                print(ref, name)
                n += 1
            elif mode == "restore":
                orig = name.split(MARK)[0]
                net = b.FindNet(orig)
                if net is None:
                    print("net is gone, left alone:", ref, name)
                    continue
                p.SetNet(net)
                print(ref, name, "->", orig)
                n += 1
        elif mode == "disable" and ref.startswith("TP") and name and not edge.Contains(f.GetPosition()):
            net = pcbnew.NETINFO_ITEM(b, name + MARK + ref)
            b.Add(net)
            p.SetNet(net)
            print(ref, name, "->", net.GetNetname())
            n += 1
print(n, "pads")
if mode in ("disable", "restore") and n:
    b.Save(path)
    print("saved")
