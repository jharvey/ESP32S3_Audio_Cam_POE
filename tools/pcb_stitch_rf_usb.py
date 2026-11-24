# Ground (COM) stitching vias, 0.6/0.3 mm, along both sides of the RF feeds and USB D+/D- tracks.
#   python pcb_stitch_rf_usb.py board.kicad_pcb [drc.json]
# With a DRC report, removes the stitching vias it names (run, DRC, run again).
import sys, json, math, pcbnew

P = sys.argv[1]
b = pcbnew.LoadBoard(P)
MM, M = pcbnew.FromMM, pcbnew.ToMM
TAG = 0.6  # via diameter marks ours: only COM vias of this size are touched

if len(sys.argv) > 2:
    bad = set()
    for v in json.load(open(sys.argv[2]))["violations"]:
        for it in v["items"]:
            if it["description"].startswith("Via [COM]"):
                bad.add((round(it["pos"]["x"], 2), round(it["pos"]["y"], 2)))
    n = 0
    for t in list(b.GetTracks()):
        if t.GetClass() == "PCB_VIA" and t.GetNetname() == "COM":
            p = t.GetPosition()
            if (round(M(p.x), 2), round(M(p.y), 2)) in bad: b.Remove(t); n += 1
    b.Save(P); print("removed", n); sys.exit()

RF = {'/RF1-ANT', 'Net-(ANT1-Pad2)', '/RF1', '/LORA1', '/LORA1-ANT', 'Net-(ANT3-Pad2)'}
USB = {'/USB_D+', '/USB_D-', '/USB_PRT_D+', '/USB_PRT_D-'}
com = b.FindNet("COM")
segs = [t for t in b.GetTracks() if t.GetClass() == "PCB_TRACK" and t.GetNetname() in RF | USB]
kos = [z for z in b.Zones() if z.GetIsRuleArea() and "ANT" in z.GetZoneName()]

def near_any(x, y, d):
    for t in segs:
        a, c = t.GetStart(), t.GetEnd()
        ax, ay, cx, cy = M(a.x), M(a.y), M(c.x), M(c.y)
        dx, dy = cx - ax, cy - ay; L = dx * dx + dy * dy or 1e-9
        u = max(0, min(1, ((x - ax) * dx + (y - ay) * dy) / L))
        if math.hypot(ax + u * dx - x, ay + u * dy - y) < d: return True
    return False

placed = []
for t in segs:
    rf = t.GetNetname() in RF
    step = 2.0 if rf else 3.0
    off = M(t.GetWidth()) / 2 + 0.3 + 0.45
    a, c = t.GetStart(), t.GetEnd()
    ax, ay, cx, cy = M(a.x), M(a.y), M(c.x), M(c.y)
    L = math.hypot(cx - ax, cy - ay)
    if L < 0.5: continue
    ux, uy = (cx - ax) / L, (cy - ay) / L
    for k in range(int(L // step) + 1):
        s = min(L, k * step + (L - step * int(L // step)) / 2)
        for side in (-1, 1):
            x, y = ax + ux * s - uy * off * side, ay + uy * s + ux * off * side
            if near_any(x, y, off - 0.01): continue
            if any(math.hypot(x - px, y - py) < 1.2 for px, py in placed): continue
            if any(z.Outline().Contains(pcbnew.VECTOR2I(MM(x), MM(y))) for z in kos): continue
            if not (50.6 < x < 149.4 and 50.6 < y < 149.4): continue
            v = pcbnew.PCB_VIA(b); v.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
            v.SetWidth(MM(TAG)); v.SetDrill(MM(0.3)); v.SetNet(com); b.Add(v); placed.append((x, y))
b.Save(P)
print("stitching vias added:", len(placed))
