# Shrink the board from 100x112 to 100x100 mm (JLC's cheapest size).
# - Rips up all tracks and vias except the RF feeds (ESP32 + LoRa) and USB D+/D-.
# - The ESP32 RF/USB parts and the LoRa parts move as two rigid blocks with their kept tracks.
# - Every other part goes to its position scaled 100/112 in y, then is nudged to the nearest free spot
#   (courtyards, same side; through-hole parts block both sides). Edge parts only move in y.
# - Outline, pours, keep-outs and board graphics follow.
import sys, math, pcbnew

P = sys.argv[1] if len(sys.argv) > 1 else "ESP32S3_Audio_Cam_POE.kicad_pcb"
b = pcbnew.LoadBoard(P)
MM = pcbnew.FromMM
M = pcbnew.ToMM
Y0, YOLD, YNEW = 50.0, 162.0, 150.0
S = (YNEW - Y0) / (YOLD - Y0)
def sy(y): return Y0 + (y - Y0) * S

KEEP = {'/RF1-ANT', 'Net-(ANT1-Pad2)', '/RF1', '/LORA1', '/LORA1-ANT', 'Net-(ANT3-Pad2)',
        '/USB_D+', '/USB_D-', '/USB_PRT_D+', '/USB_PRT_D-'}
GROUPS = {"esp": ({"U7", "L3", "C12", "C15", "ANT1", "R86", "J1", "U3", "R10", "R11"}, -1.0),
          "lora": ({"U19", "ANT3", "C104", "C105", "C106", "R85"}, -1.0)}
EDGE_Y_ONLY = {"J9", "CN1", "CARD1", "H1", "H2", "H3"}
FIXED_DY = {"J9": -12.0}
CL = 0.2

# 1. rip up
gone = [t for t in b.GetTracks() if t.GetNetname() not in KEEP]
for t in gone: b.Remove(t)
print("removed tracks/vias:", len(gone), "kept:", len(b.GetTracks()))

def box(f):
    c = f.GetCourtyard(pcbnew.B_CrtYd if f.IsFlipped() else pcbnew.F_CrtYd)
    bb = c.BBox() if c.OutlineCount() else f.GetBoundingBox(False)
    r = [M(bb.GetX()), M(bb.GetY()), M(bb.GetRight()), M(bb.GetBottom())]
    for pd in f.Pads():  # pads can stick out of a courtyard
        q = pd.GetBoundingBox()
        r = [min(r[0], M(q.GetX())), min(r[1], M(q.GetY())), max(r[2], M(q.GetRight())), max(r[3], M(q.GetBottom()))]
    return r

parts = []
for f in b.GetFootprints():
    tht = any(p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH) for p in f.Pads())
    parts.append(dict(f=f, r=f.GetReference(), B=f.IsFlipped(), tht=tht, bx=box(f)))
grp_of = {r: g for g, (refs, _) in GROUPS.items() for r in refs}

# keep-outs: antenna ones belong to their group, the rest scale
kos = [z for z in b.Zones() if z.GetIsRuleArea()]
def ko_box(z):
    bb = z.GetBoundingBox(); return [M(bb.GetX()), M(bb.GetY()), M(bb.GetRight()), M(bb.GetBottom())]
ko_dy = {}
for z in kos:
    n = z.GetZoneName()
    if "ANT1" in n: ko_dy[n] = GROUPS["esp"][1]
    elif "ANT3" in n: ko_dy[n] = GROUPS["lora"][1]
    else:
        bx = ko_box(z); c = (bx[1] + bx[3]) / 2; ko_dy[n] = sy(c) - c
def isect(a, k): return a[0] < k[2] and k[0] < a[2] and a[1] < k[3] and k[1] < a[3]
other_ko = []  # parts that were not in these keep-outs must not land in them
for z in kos:
    if "ANT" not in z.GetZoneName():
        bx = ko_box(z); d = ko_dy[z.GetZoneName()]; other_ko.append((bx, [bx[0], bx[1] + d, bx[2], bx[3] + d]))
ant_ko = []
for z in kos:
    n = z.GetZoneName()
    if "ANT" in n:
        bx = ko_box(z); d = ko_dy[n]; ant_ko.append(("esp" if "ANT1" in n else "lora", [bx[0], bx[1] + d, bx[2], bx[3] + d]))

# spatial grid of placed boxes
G = 5.0
grid = {}
def cells(bx):
    for i in range(int(bx[0] // G) - 1, int(bx[2] // G) + 2):
        for j in range(int(bx[1] // G) - 1, int(bx[3] // G) + 2):
            yield (i, j)
def add(p, bx):
    for c in cells(bx): grid.setdefault(c, []).append((p, bx))
def clash(p, bx):
    if bx[0] < 50.5 or bx[2] > 149.5 or bx[1] < 50.5: return True
    if bx[3] > YNEW - 0.5 and p['r'] not in FIXED_DY: return True
    for g, k in ant_ko:
        if grp_of.get(p['r']) != g and bx[0] < k[2] and k[0] < bx[2] and bx[1] < k[3] and k[1] < bx[3]: return True
    for old, k in other_ko:
        if 'bx' in p and not isect(p['bx'], old) and isect(bx, k): return True
    seen = set()
    for c in cells(bx):
        for q, qb in grid.get(c, ()):
            if id(q) in seen: continue
            seen.add(id(q))
            if not (q['B'] == p['B'] or q['tht'] or p['tht']): continue
            if bx[0] < qb[2] + CL and qb[0] < bx[2] + CL and bx[1] < qb[3] + CL and qb[1] < bx[3] + CL: return True
    return False

def shifted(bx, dx, dy): return [bx[0] + dx, bx[1] + dy, bx[2] + dx, bx[3] + dy]
moves = {}
# groups first (fixed)
for p in parts:
    g = grp_of.get(p['r'])
    if g:
        d = (0.0, GROUPS[g][1]); moves[p['r'] + str(id(p))] = d; p['mv'] = d; add(p, shifted(p['bx'], *d))
# kept tracks block the top side (both sides for vias)
for t in b.GetTracks():
    q = t.GetBoundingBox(); w = 0.1
    via = t.GetClass() == "PCB_VIA"
    tp = dict(r="trk", B=(t.GetLayer() == pcbnew.B_Cu), tht=via)
    add(tp, [M(q.GetX()) - w, M(q.GetY()) - 1.0 - w, M(q.GetRight()) + w, M(q.GetBottom()) - 1.0 + w])
# board cut-outs inside footprints (BT1) block both sides; they follow their footprint
cut = {}
for p in parts:
    for g in p['f'].GraphicalItems():
        if g.GetLayerName() == "Edge.Cuts":
            q = g.GetBoundingBox(); cut.setdefault(p['r'], []).append([M(q.GetX()) - 0.5, M(q.GetY()) - 0.5, M(q.GetRight()) + 0.5, M(q.GetBottom()) + 0.5])
order = [p for p in parts if 'mv' not in p]
order.sort(key=lambda p: (not (p['r'][0] == 'H' and p['r'][1:].isdigit()), p['r'] not in FIXED_DY, p['r'] not in EDGE_Y_ONLY, -(p['bx'][2] - p['bx'][0]) * (p['bx'][3] - p['bx'][1])))
fails = []
for p in order:
    bx = p['bx']; cy = (bx[1] + bx[3]) / 2
    dy0 = FIXED_DY.get(p['r'], sy(cy) - cy)
    if p['r'][0] == 'H' and p['r'][1:].isdigit():  # mounting holes stay in the corners
        dy0 = -12.0 if cy > 140 else 0.0; best = (0.0, dy0); p['mv'] = best; add(p, shifted(bx, *best)); continue
    yonly = p['r'] in EDGE_Y_ONLY or p['r'] in FIXED_DY
    best = None
    step = 0.25
    for ring in range(0, 121):
        rad = ring * step
        cands = []
        if ring == 0: cands = [(0, 0)]
        else:
            n = max(8, int(2 * math.pi * rad / step))
            for k in range(n):
                a = 2 * math.pi * k / n
                cands.append((0 if yonly else round(rad * math.cos(a) / step) * step, round(rad * math.sin(a) / step) * step))
        for dx, ddy in cands:
            if p['r'] in FIXED_DY and ddy: continue
            if not clash(p, shifted(bx, dx, dy0 + ddy)): best = (dx, dy0 + ddy); break
        if best: break
    if not best:
        fails.append(p['r']); best = (0, dy0)
    p['mv'] = best; add(p, shifted(bx, *best))
    for cb in cut.get(p['r'], ()): add(dict(r="cut", B=False, tht=True), shifted(cb, *best))

for p in parts:
    dx, dy = p['mv']; p['f'].Move(pcbnew.VECTOR2I(MM(dx), MM(dy)))
# kept tracks follow their group
for t in b.GetTracks():
    n = t.GetNetname()
    g = "lora" if "LORA" in n or "ANT3" in n else "esp"
    t.Move(pcbnew.VECTOR2I(0, MM(GROUPS[g][1])))

# zones
for z in b.Zones():
    if z.GetIsRuleArea():
        z.Move(pcbnew.VECTOR2I(0, MM(ko_dy[z.GetZoneName()]))); continue
    o = z.Outline()
    for i in range(o.TotalVertices()):
        v = o.CVertex(i); y = M(v.y)
        if z.GetNetname() in ("COM", "+3V3"): ny = min(y, YNEW)
        else: ny = sy(y)
        o.SetVertex(i, pcbnew.VECTOR2I(v.x, MM(ny)))
    z.UnFill()

# outline and board graphics
for d in b.GetDrawings():
    if d.GetLayerName() == "Edge.Cuts" and d.GetClass() == "PCB_SHAPE":
        s, e = d.GetStart(), d.GetEnd()
        d.SetStart(pcbnew.VECTOR2I(s.x, MM(YNEW) if M(s.y) > 100 else s.y))
        d.SetEnd(pcbnew.VECTOR2I(e.x, MM(YNEW) if M(e.y) > 100 else e.y))
    else:
        c = d.GetBoundingBox().GetCenter(); y = M(c.y)
        d.Move(pcbnew.VECTOR2I(0, MM(sy(y) - y)))

b.Save(P)
big = sorted(parts, key=lambda p: -abs(p['mv'][0]) - abs(p['mv'][1] - (sy((p['bx'][1] + p['bx'][3]) / 2) - (p['bx'][1] + p['bx'][3]) / 2)))
print("could not place cleanly:", fails)
print("largest nudges:", [(p['r'], round(p['mv'][0], 1), round(p['mv'][1], 1)) for p in big[:15]])
