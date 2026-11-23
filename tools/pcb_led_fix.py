# Board side of the LED changes (re-runnable on a routed copy of the board):
#  D3/D4 turned 180 deg in place (cathode onto the routed resistor track, anode to +3V3 with a via to In2),
#  D14 blue (C2293), R66 1k (C11702).
import sys
import pcbnew

path = sys.argv[1]
b = pcbnew.LoadBoard(path)
fps = {f.GetReference(): f for f in b.GetFootprints()}
mm = pcbnew.FromMM

def net(name):
    n = b.FindNet(name)
    if not n:
        n = pcbnew.NETINFO_ITEM(b, name)
        b.Add(n)
    return n

v3 = net("+3V3")
def clear_spot(pad, own, mynet="+3V3"):
    # nearest spot 0.95-2.0 mm from the pad centre where a 0.6 mm via keeps 0.25 mm from other nets' copper
    import math
    c = pad.GetPosition()
    best = None
    for r10 in range(95, 205, 10):
        for k in range(16):
            ang = k * math.pi / 8
            x = c.x + mm(r10 / 100.0 * math.cos(ang)); y = c.y + mm(r10 / 100.0 * math.sin(ang))
            pt = pcbnew.VECTOR2I(int(x), int(y))
            ok = True
            for t in b.GetTracks():
                if t.GetNetname() != mynet and t.HitTest(pt, mm(0.3 + 0.25)):
                    ok = False; break
            if ok:
                for g in b.GetFootprints():
                    for q in g.Pads():
                        # ground pads get a wider berth so the COM pour still reaches them
                        gap = 1.1 if (q.GetNetname() == "COM" and mynet != "COM") else 0.25
                        if q.GetNetname() != mynet and q.HitTest(pt, mm(0.3 + gap)):
                            ok = False; break
                    if not ok: break
            if ok:
                return (pcbnew.ToMM(pt.x), pcbnew.ToMM(pt.y))
    raise SystemExit("no clear via spot near " + own)

for led in ("D3", "D4"):
    f = fps[led]
    pads = {p.GetNumber(): p for p in f.Pads()}
    if pads["1"].GetNetname() == "+3V3" or pads["2"].GetNetname() == "+3V3":
        print(led, "already done")
        continue
    old = pads["2"].GetNetname()          # Net-(D3-Pad2): the resistor side
    newname = "Net-(%s-Pad1)" % led
    n = net(newname)
    c = f.GetPosition()
    f.SetOrientationDegrees(f.GetOrientationDegrees() + 180)
    f.SetPosition(c)
    for t in b.GetTracks():
        if t.GetNetname() == old:
            t.SetNet(n)
    for g in b.GetFootprints():
        for p in g.Pads():
            if p.GetNetname() == old:
                p.SetNet(n)
    pads["1"].SetNet(n)
    pads["2"].SetNet(v3)
    a = pads["2"].GetPosition()
    via_at = clear_spot(pads["2"], led)
    via = pcbnew.PCB_VIA(b)
    via.SetPosition(pcbnew.VECTOR2I(mm(via_at[0]), mm(via_at[1])))
    via.SetWidth(mm(0.6))
    via.SetDrill(mm(0.3))
    via.SetNet(v3)
    b.Add(via)
    t = pcbnew.PCB_TRACK(b)
    t.SetStart(a)
    t.SetEnd(via.GetPosition())
    t.SetWidth(mm(0.25))
    t.SetLayer(pcbnew.F_Cu)
    t.SetNet(v3)
    b.Add(t)
    print(led, "turned; pad1", newname, "pad2 +3V3 via at", via_at)

# C36's ground pad reached the COM pour through D4's old ground pad: join it to C37's ground pad (vias to GND there).
com = net("COM")
c36 = [p for p in fps["C36"].Pads() if p.GetNetname() == "COM"][0]
c37 = [p for p in fps["C37"].Pads() if p.GetNetname() == "COM"][0]
if not any(t.Type() == pcbnew.PCB_TRACE_T and t.GetNetname() == "COM" and t.GetStart() == c36.GetPosition() for t in b.GetTracks()):
    t = pcbnew.PCB_TRACK(b)
    t.SetStart(c36.GetPosition()); t.SetEnd(c37.GetPosition()); t.SetWidth(mm(0.3)); t.SetLayer(pcbnew.F_Cu); t.SetNet(com); b.Add(t)
    print("C36 ground joined to C37")

def setfield(f, name, val):
    for fld in f.GetFields():
        if fld.GetName() == name:
            fld.SetText(val)
            return
    print("no field", name, "on", f.GetReference())

fps["D14"].SetValue("Blue")
setfield(fps["D14"], "LCSC", "C2293")
setfield(fps["D14"], "Description", "Blue 0805 LED Indication - Discrete ROHS (KENTO KT-0805B)")
fps["R66"].SetValue("1kΩ")
setfield(fps["R66"], "LCSC", "C11702")

pcbnew.ZONE_FILLER(b).Fill(b.Zones())
b.BuildConnectivity()
for led in ("D3", "D4"):
    p = [p for p in fps[led].Pads() if p.GetNumber() == "2"][0]
    print(led, "pad2 net", p.GetNetname())
b.Save(path)
print("saved")
