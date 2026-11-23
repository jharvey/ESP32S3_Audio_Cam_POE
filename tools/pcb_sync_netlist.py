# Bring the board in line with a schematic netlist (re-runnable, also on a routed copy):
#   * footprints that are no longer in the schematic are removed;
#   * new parts are placed where given on the command line (REF=x,y,rot in mm / degrees);
#   * every pad's net is set from the netlist; renamed nets keep their tracks;
#   * copper that no longer fits is removed: nets gone from every pad, segments / vias touching a
#     pad of another net, then the dangling stubs left behind on the nets that changed.
#   Locked (hand-routed) copper is never removed; conflicts there are listed instead.
# usage: python pcb_sync_netlist.py board.kicad_pcb netlist.xml [U22=106,121.3,90 ...]
import os, sys
import xml.etree.ElementTree as ET
import pcbnew

path, netxml = sys.argv[1], sys.argv[2]
places = {}
for a in sys.argv[3:]:
    ref, v = a.split("=")
    x, y, r = (float(t) for t in v.split(","))
    places[ref] = (x, y, r)

b = pcbnew.LoadBoard(path)
mm = pcbnew.FromMM
here = os.path.dirname(os.path.abspath(path))
LIBS = {"JLCPCB": os.path.join(here, "JLCPCB-Kicad-Library", "footprints", "JLCPCB.pretty")}
STD = r"C:\Program Files\KiCad\10.0\share\kicad\footprints"

tree = ET.parse(netxml)
want = {}
for n in tree.iter("net"):
    for x in n.iter("node"):
        want[(x.get("ref"), x.get("pin"))] = n.get("name")
comps = {c.get("ref"): c for c in tree.iter("comp")}

def net(name):
    n = b.FindNet(name)
    if not n:
        n = pcbnew.NETINFO_ITEM(b, name)
        b.Add(n)
    return n

# 1. parts gone from the schematic (board-only items like mounting holes have no reference in it)
gone_fp = [f for f in b.GetFootprints()
           if f.GetReference() not in comps and not f.GetReference().startswith(("H", "TP", "MH", "LOGO", "REF"))
           and f.GetPath().AsString()]
for f in gone_fp:
    print("removed", f.GetReference())
for f in gone_fp:
    b.Delete(f)

# 2. new parts
onboard = {f.GetReference() for f in b.GetFootprints()}
for ref, c in comps.items():
    if ref in onboard or ref.startswith("#"):
        continue
    if ref not in places:
        print("NOT PLACED (give %s=x,y,rot): %s" % (ref, c.findtext("footprint")))
        continue
    lib, name = c.findtext("footprint").split(":", 1)
    f = pcbnew.FootprintLoad(LIBS.get(lib, os.path.join(STD, lib + ".pretty")), name)
    f.SetFPID(pcbnew.LIB_ID(lib, name))
    f.SetParent(b)
    f.SetReference(ref)
    f.SetValue(c.findtext("value"))
    tstamps = c.findtext("tstamps") or ""
    sheet = c.find("sheetpath")
    f.SetPath(pcbnew.KIID_PATH((sheet.get("tstamps") if sheet is not None else "/") + tstamps.split()[0]))
    for fld in c.iter("field"):
        f.SetField(fld.get("name"), fld.text or "")
    for fld in f.GetFields():
        if fld.GetName() not in ("Reference", "Value"):
            fld.SetVisible(False)
    b.Add(f)
    x, y, r = places[ref]
    f.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
    f.SetOrientationDegrees(r)
    print("placed", ref, c.findtext("value"), "at", places[ref])

# 3. pad nets
changed = []
for f in b.GetFootprints():
    for p in f.Pads():
        k = (f.GetReference(), p.GetNumber())
        if k in want and p.GetNetname() != want[k]:
            changed.append((p, p.GetNetname(), want[k]))
            p.SetNet(net(want[k]))
print("%d pads changed net" % len(changed))

# 4. copper that no longer fits
moved = {}
for p, a, w in changed:
    moved.setdefault(a, set()).add(w)
still = {p.GetNetname() for f in b.GetFootprints() for p in f.Pads()}
rename = {a: list(ws)[0] for a, ws in moved.items() if len(ws) == 1 and a not in still and a}
for t in b.GetTracks():
    if t.GetNetname() in rename:
        t.SetNet(net(rename[t.GetNetname()]))
for z in b.Zones():
    if z.GetNetname() in rename:
        z.SetNet(net(rename[z.GetNetname()]))

padnets = {p.GetNetname() for f in b.GetFootprints() for p in f.Pads()}
locked_conflicts = []
gone = [0]

def delete(items, why):
    for t in items:
        if t.IsLocked():
            locked_conflicts.append((t.GetNetname(), why))
        else:
            b.Delete(t)
            gone[0] += 1

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

BIG = ("COM", "Vdrive", "+3V3", "+3V3A", "VBUS")
nets_touched = ({w for _, _, w in changed} | {a for _, a, _ in changed} | set(rename.values())) - set(BIG)
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
            and not t.IsLocked() and (not connected(t.GetStart(), t) or not connected(t.GetEnd(), t))]
    vias = [t for t in b.GetTracks() if t.Type() == pcbnew.PCB_VIA_T and t.GetNetname() in nets_touched and not t.IsLocked()
            and sum(1 for u in b.GetTracks() if u.Type() == pcbnew.PCB_TRACE_T and u.GetNetname() == t.GetNetname()
                    and ((u.GetStart() - t.GetPosition()).EuclideanNorm() < mm(0.05) or (u.GetEnd() - t.GetPosition()).EuclideanNorm() < mm(0.05))) < 2
            and not any(p.GetNetname() == t.GetNetname() and p.HitTest(t.GetPosition()) for p in pads)]
    if not dang and not vias:
        break
    delete(dang + vias, "dangling")

pcbnew.ZONE_FILLER(b).Fill(b.Zones())
b.Save(path)
print("removed %d track/via items" % gone[0])
for c in locked_conflicts:
    print("LOCKED, left in place:", c)
print("saved")
