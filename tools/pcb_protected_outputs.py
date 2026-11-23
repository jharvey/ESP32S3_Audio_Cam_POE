# Board side of tools/sch_protected_outputs.py (re-runnable on a routed copy):
#   Q2-Q8 SOT-23 P-FETs -> SY6288AAAC (SOT-23-5) in place; SW1/SW2 (rotary switches) removed;
#   every pad's net set from the schematic netlist; copper that no longer fits is removed
#   (nets that vanished, segments touching pads whose net changed, then dangling stubs).
#   Locked (hand-routed) copper is never removed: conflicts there are reported instead.
# usage: python pcb_protected_outputs.py board.kicad_pcb netlist.xml
import os, sys
import xml.etree.ElementTree as ET
import pcbnew

path, netxml = sys.argv[1], sys.argv[2]
b = pcbnew.LoadBoard(path)
mm = pcbnew.FromMM
lib = os.path.join(os.path.dirname(os.path.abspath(path)), "JLCPCB-Kicad-Library", "footprints", "JLCPCB.pretty")
FP5 = "SOT-23-5_L3.0-W1.7-P0.95-LS2.8-BL"

want = {}
comps = {}
for n in ET.parse(netxml).iter("net"):
    for x in n.iter("node"):
        want[(x.get("ref"), x.get("pin"))] = n.get("name")
for c in ET.parse(netxml).iter("comp"):
    comps[c.get("ref")] = c

def net(name):
    n = b.FindNet(name)
    if not n:
        n = pcbnew.NETINFO_ITEM(b, name)
        b.Add(n)
    return n

fps = {f.GetReference(): f for f in b.GetFootprints()}

# 1. rotary switches out
for ref in ("SW1", "SW2"):
    if ref in fps:
        b.Remove(fps[ref])
        print("removed", ref)

# 2. FETs -> SY6288 (same centre and rotation)
for ref in ("Q2", "Q3", "Q4", "Q5", "Q6", "Q7", "Q8"):
    old = [f for f in b.GetFootprints() if f.GetReference() == ref][0]
    if old.GetFPID().GetLibItemName() == FP5:
        continue
    new = pcbnew.FootprintLoad(lib, FP5)
    new.SetFPID(pcbnew.LIB_ID("JLCPCB", FP5))
    new.SetParent(b)
    new.SetReference(ref)
    new.SetValue(comps[ref].findtext("value"))
    new.SetPath(old.GetPath())
    for fld in comps[ref].iter("field"):
        new.SetField(fld.get("name"), fld.text or "")
    for fld in new.GetFields():
        if fld.GetName() not in ("Reference", "Value"):
            fld.SetVisible(False)
    b.Add(new)
    new.SetPosition(old.GetPosition())
    new.SetOrientationDegrees(old.GetOrientationDegrees())
    b.Remove(old)
    print(ref, "->", new.GetValue())

# 3. every pad's net from the netlist
changed = []  # pads whose net changed
for f in b.GetFootprints():
    for p in f.Pads():
        k = (f.GetReference(), p.GetNumber())
        if k not in want:
            continue
        if p.GetNetname() != want[k]:
            changed.append((p, p.GetNetname(), want[k]))
            p.SetNet(net(want[k]))
for p, a, w in changed:
    print("pad %s.%s: %s -> %s" % (p.GetParentFootprint().GetReference(), p.GetNumber(), a, w))

# 4. copper that no longer fits
# Nets that were only renamed (every pad of the old net moved to one new net): tracks follow.
moved = {}
for p, a, w in changed:
    moved.setdefault(a, set()).add(w)
still = {p.GetNetname() for f in b.GetFootprints() for p in f.Pads()}
rename = {a: list(ws)[0] for a, ws in moved.items() if len(ws) == 1 and a not in still}
for t in b.GetTracks():
    if t.GetNetname() in rename:
        t.SetNet(net(rename[t.GetNetname()]))
for z in b.Zones():
    if z.GetNetname() in rename:
        z.SetNet(net(rename[z.GetNetname()]))

padnets = {p.GetNetname() for f in b.GetFootprints() for p in f.Pads()}
locked_conflicts = []
gone = 0

def delete(items, why):
    global gone
    for t in items:
        if t.IsLocked():
            locked_conflicts.append((t.GetNetname(), why))
        else:
            b.Delete(t)
            gone += 1

delete([t for t in b.GetTracks() if t.GetNetname() and t.GetNetname() not in padnets], "net no longer on any pad")

pads = [p for f in b.GetFootprints() for p in f.Pads()]
def wrong_pad(t):
    if t.Type() == pcbnew.PCB_VIA_T:
        pts, layer = [t.GetPosition()], None
    else:
        pts, layer = [t.GetStart(), t.GetEnd()], t.GetLayer()
    for p in pads:
        if p.GetNetname() == t.GetNetname():
            continue
        if layer is not None and not p.IsOnLayer(layer):
            continue
        if any(p.HitTest(q) for q in pts):
            return True
    return False
delete([t for t in b.GetTracks() if wrong_pad(t)], "touches a pad of another net")

nets_touched = {w for _, _, w in changed} | set(rename.values())
def connected(pt, t):
    for p in pads:
        if p.GetNetname() == t.GetNetname() and p.HitTest(pt) and (t.Type() == pcbnew.PCB_VIA_T or p.IsOnLayer(t.GetLayer())):
            return True
    for u in b.GetTracks():
        if u.m_Uuid == t.m_Uuid or u.GetNetname() != t.GetNetname():
            continue
        if u.Type() == pcbnew.PCB_VIA_T:
            if (u.GetPosition() - pt).EuclideanNorm() < mm(0.05):
                return True
        elif u.GetLayer() == t.GetLayer():
            if (u.GetStart() - pt).EuclideanNorm() < mm(0.05) or (u.GetEnd() - pt).EuclideanNorm() < mm(0.05):
                return True
    return False

for _ in range(40):
    dang = [t for t in b.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T and t.GetNetname() in nets_touched
            and t.GetNetname() not in ("COM", "Vdrive") and not t.IsLocked()
            and (not connected(t.GetStart(), t) or not connected(t.GetEnd(), t))]
    vias = [t for t in b.GetTracks() if t.Type() == pcbnew.PCB_VIA_T and t.GetNetname() in nets_touched
            and t.GetNetname() not in ("COM", "Vdrive") and not t.IsLocked()
            and sum(1 for u in b.GetTracks() if u.Type() == pcbnew.PCB_TRACE_T and u.GetNetname() == t.GetNetname()
                    and ((u.GetStart() - t.GetPosition()).EuclideanNorm() < mm(0.05) or (u.GetEnd() - t.GetPosition()).EuclideanNorm() < mm(0.05))) < 2
            and not any(p.GetNetname() == t.GetNetname() and p.HitTest(t.GetPosition()) for p in pads)]
    if not dang and not vias:
        break
    delete(dang + vias, "dangling")

pcbnew.ZONE_FILLER(b).Fill(b.Zones())
b.Save(path)
print("removed %d track/via items" % gone)
for c in locked_conflicts:
    print("LOCKED, left in place:", c)
print("saved")
