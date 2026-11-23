# D16-D18 (MISC1-3 output indicators): IR LED (IR26-21C) -> green 0805 KT-0805G (C2297).
# Swaps the footprint in place, keeps nets and the schematic link, joins the old pad spots to the new
# pads with short top-layer tracks, renames Net-(Dn-A) to Net-(Dn-Pad2) as the schematic now names it.
# Re-runnable (skips parts already done): run on a routed copy of the board as well.
import os, sys
import pcbnew

path = sys.argv[1]
b = pcbnew.LoadBoard(path)
lib = os.path.join(os.path.dirname(os.path.abspath(path)), "JLCPCB-Kicad-Library", "footprints", "JLCPCB.pretty")
mm = pcbnew.FromMM

# MISC.OUT1 / MISC.OUT3 crossed between the IR pads (router tracks): move them to below the new LED.
REROUTE = {
    "D16": ("/MISC.OUT1", [(142.9, 157.2, 144.35, 158.65), (144.35, 158.65, 144.95, 158.65), (144.95, 158.65, 145.5, 159.22)],
            [(142.9, 157.2, 142.9, 159.5), (142.9, 159.5, 143.2, 159.8), (143.2, 159.8, 145.0, 159.8), (145.0, 159.8, 145.5, 159.3),
             (145.5, 159.3, 145.5, 159.22)]),
    "D18": ("/MISC.OUT3", [(142.9, 143.2, 144.35, 144.65), (144.35, 144.65, 144.85, 144.65), (144.85, 144.65, 145.5, 145.3)],
            [(142.9, 143.2, 142.9, 145.5), (142.9, 145.5, 143.3, 145.9), (143.3, 145.9, 145.0, 145.9), (145.0, 145.9, 145.5, 145.4),
             (145.5, 145.4, 145.5, 145.3)]),
}

def reroute(ref):
    if ref not in REROUTE:
        return
    netname, old, new = REROUTE[ref]
    def same(t, seg):
        a = (round(pcbnew.ToMM(t.GetStart().x), 2), round(pcbnew.ToMM(t.GetStart().y), 2), round(pcbnew.ToMM(t.GetEnd().x), 2), round(pcbnew.ToMM(t.GetEnd().y), 2))
        return a == seg or a == (seg[2], seg[3], seg[0], seg[1])
    found = [t for t in b.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T and t.GetNetname() == netname and any(same(t, sg) for sg in old)]
    if len(found) != len(old):
        print(ref, "WARNING: %s is routed differently here; reroute it by hand around %s" % (netname, ref))
        return
    w, n = found[0].GetWidth(), found[0].GetNet()
    for t in found:
        b.Remove(t)
    for x0, y0, x1, y1 in new:
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(pcbnew.VECTOR2I(mm(x0), mm(y0))); t.SetEnd(pcbnew.VECTOR2I(mm(x1), mm(y1)))
        t.SetWidth(w); t.SetLayer(pcbnew.F_Cu); t.SetNet(n); b.Add(t)
    print(ref, netname, "moved below the LED")

def com_via(pad):
    # A ground via beside the cathode (the moved track fences it off from the pour): nearest spot
    # 0.8-1.6 mm away that keeps clear of other nets' copper.
    import math
    c = pad.GetPosition()
    for r10 in range(80, 165, 5):
        for k in range(24):
            ang = k * math.pi / 12
            pt = pcbnew.VECTOR2I(int(c.x + mm(r10 / 100.0 * math.cos(ang))), int(c.y + mm(r10 / 100.0 * math.sin(ang))))
            mid = pcbnew.VECTOR2I((pt.x + c.x) // 2, (pt.y + c.y) // 2)
            ok = all(t.GetNetname() == "COM" or not (t.HitTest(pt, mm(0.3 + 0.13)) or t.HitTest(mid, mm(0.125 + 0.13)))
                     for t in b.GetTracks())
            ok = ok and all(q.GetNetname() == "COM" or not (q.HitTest(pt, mm(0.3 + 0.13)) or q.HitTest(mid, mm(0.125 + 0.13)))
                            for g in b.GetFootprints() for q in g.Pads())
            if ok:
                v = pcbnew.PCB_VIA(b)
                v.SetPosition(pt); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(pad.GetNet()); b.Add(v)
                t = pcbnew.PCB_TRACK(b)
                t.SetStart(c); t.SetEnd(pt); t.SetWidth(mm(0.25)); t.SetLayer(pcbnew.F_Cu); t.SetNet(pad.GetNet()); b.Add(t)
                return (round(pcbnew.ToMM(pt.x), 2), round(pcbnew.ToMM(pt.y), 2))
    return None

for ref in ("D16", "D17", "D18"):
    old = [f for f in b.GetFootprints() if f.GetReference() == ref][0]
    if old.GetFPID().GetLibItemName() == "D_0805":
        print(ref, "already green 0805")
        continue
    oldpads = {p.GetNumber(): (p.GetPosition(), p.GetNet()) for p in old.Pads()}
    anode_old = oldpads["2"][1].GetNetname()
    new = pcbnew.FootprintLoad(lib, "D_0805")
    new.SetFPID(pcbnew.LIB_ID("JLCPCB", "D_0805"))
    new.SetParent(b)
    new.SetReference(ref)
    new.SetValue("Green")
    new.SetPath(old.GetPath())
    for fld in old.GetFields():
        if fld.GetName() not in ("Reference", "Value", "Footprint"):
            new.SetField(fld.GetName(), fld.GetText())
    for fld in new.GetFields():
        if fld.GetName() not in ("Reference", "Value"):
            fld.SetVisible(False)
    for fld in new.GetFields():
        if fld.GetName() == "LCSC":
            fld.SetText("C2297")
        if fld.GetName() == "Datasheet":
            fld.SetText("https://www.lcsc.com/datasheet/lcsc_datasheet_1806151820_Hubei-KENTO-Elec-KT-0805G_C2297.pdf")
        if fld.GetName() == "Description":
            fld.SetText("Emerald 0805 LED Indication - Discrete ROHS")
    b.Add(new)
    # 0805 pads are 1.9 mm apart (IR part 2.8 mm): keep the anode end near the old anode pad and its via,
    # which frees the old cathode-pad spot for the MISC.OUT track that used to pass between the IR pads.
    o1, o2 = oldpads["1"][0], oldpads["2"][0]
    mid = pcbnew.VECTOR2I((o1.x + o2.x) // 2, (o1.y + o2.y) // 2)
    d = pcbnew.VECTOR2I(o2.x - o1.x, o2.y - o1.y)
    L = d.EuclideanNorm()
    shift = mm({"D16": 0.25, "D17": 0.25, "D18": 0.1}[ref])  # clears the J17-J19 courtyards just above
    new.SetPosition(pcbnew.VECTOR2I(mid.x + int(d.x * shift / L), mid.y + int(d.y * shift / L)))
    # same side for pad 1 (cathode) as before
    for rot in (old.GetOrientationDegrees(), old.GetOrientationDegrees() + 180):
        new.SetOrientationDegrees(rot)
        p1 = [p for p in new.Pads() if p.GetNumber() == "1"][0]
        if (p1.GetPosition() - oldpads["1"][0]).EuclideanNorm() < (p1.GetPosition() - oldpads["2"][0]).EuclideanNorm():
            break
    newname = "Net-(%s-Pad2)" % ref
    nn = b.FindNet(newname)
    if not nn:
        nn = pcbnew.NETINFO_ITEM(b, newname)
        b.Add(nn)
    for t in b.GetTracks():
        if t.GetNetname() == anode_old:
            t.SetNet(nn)
    for g in b.GetFootprints():
        for p in g.Pads():
            if p.GetNetname() == anode_old:
                p.SetNet(nn)
    for p in new.Pads():
        net = oldpads[p.GetNumber()][1] if p.GetNumber() == "1" else nn
        p.SetNet(net)
        if p.GetNumber() == "1":
            continue  # cathode: COM pour (the old cathode spot now carries the moved MISC.OUT track)
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(oldpads[p.GetNumber()][0])
        t.SetEnd(p.GetPosition())
        t.SetWidth(mm(0.3))
        t.SetLayer(pcbnew.F_Cu)
        t.SetNet(net)
        b.Add(t)
    b.Remove(old)
    reroute(ref)
    if ref in REROUTE:
        cat = [p for p in new.Pads() if p.GetNumber() == "1"][0]
        print(ref, "cathode ground via at", com_via(cat))
    print(ref, "-> green 0805, rot", new.GetOrientationDegrees(), [(p.GetNumber(), p.GetNetname()) for p in new.Pads()])

pcbnew.ZONE_FILLER(b).Fill(b.Zones())
b.Save(path)
print("saved")
