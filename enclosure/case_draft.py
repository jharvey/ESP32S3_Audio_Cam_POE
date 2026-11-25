# Draft case for the intercom board (CadQuery): a back shell and a front cover, screwed together through the
# board's four M3 holes. First draft to look at, not yet a tested print.
#   pip install cadquery ; python enclosure/case_draft.py     -> enclosure/out/*.step, *.stl, *.svg
# Positions come from board_features.json (run board_features.py after the layout changes).
#
# Axes: x right, y up (KiCad's y is flipped here), z towards the room. z = 0 is the board's front face.
import json, os
import cadquery as cq

here = os.path.dirname(os.path.abspath(__file__))
B = json.load(open(os.path.join(here, "board_features.json")))
W, H = B["w"], B["h"]

# ---- numbers to tune -------------------------------------------------------------------------------------
PCB_T = 1.6
GAP = 0.5        # board edge to the inside of the wall
WALL = 2.4
FLOOR = 2.4      # back of the shell (against the room wall)
FACE = 2.0       # front panel thickness
BACK_H = 23.0    # room behind the board: the 18650 holder BT1 (about 21 mm)
FRONT_H = 15.0   # room in front of the board: the RJ45 J9 (about 13.5 mm) is the tallest part
POST_D = 7.0     # standoffs under the board
PILOT_D = 2.6    # M3 self-tapping screw into the standoff (4.0 for a brass heat-set insert)
SCREW_D = 3.4    # M3 clearance through the cover
HEAD_D = 6.5     # screw head recess in the front face
BTN_D = 6.5      # holes for the TALK and OFFICE button caps
MIC_D = 1.5      # microphone ports
CAM_D = 12.0     # camera lens opening
SPK_D = 36.0     # speaker grille (the speaker hangs in the cover; its place is a guess for now)
SPK_AT = (58.0, 52.0)

def Y(y): return -y  # KiCad y -> model y
def part(r): return B["parts"].get(r)
def box_of(r, grow=0.0):
    x0, y0, x1, y1 = part(r)["box"]
    return (x0 - grow, y0 - grow, x1 + grow, y1 + grow)
def slab(x0, y0, x1, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, Y(y1), z0))
def cyl(x, y, d, z0, z1):
    return cq.Workplane("XY").circle(d / 2).extrude(z1 - z0).translate((x, Y(y), z0))

IN0, IN1 = -GAP, W + GAP            # inside of the walls (x); the same numbers serve y with H
OUT0, OUT1 = IN0 - WALL, IN1 + WALL
Z_FLOOR = -PCB_T - BACK_H           # inside face of the back
Z_BACK = Z_FLOOR - FLOOR
Z_FACE = FRONT_H                    # inside face of the front

# ---- back shell: floor, walls up to the board's front face, a standoff under each mounting hole ----------
back = slab(OUT0, -GAP - WALL, OUT1, H + GAP + WALL, Z_BACK, 0).edges("|Z").fillet(3.0)
back = back.cut(slab(IN0, -GAP, IN1, H + GAP, Z_FLOOR, 1))
for hx, hy in B["holes"]:
    back = back.union(cyl(hx, hy, POST_D, Z_FLOOR, -PCB_T)).cut(cyl(hx, hy, PILOT_D, Z_FLOOR + 1.0, 0))
# two keyhole slots to hang it on wall screws, and a cable hole behind the RJ45
for kx in (W * 0.25, W * 0.75):
    back = back.cut(cyl(kx, 30, 8.5, Z_BACK - 1, Z_FLOOR + 1)).cut(slab(kx - 2.2, 22, kx + 2.2, 30, Z_BACK - 1, Z_FLOOR + 1))
j9 = box_of("J9", 1.0)
back = back.cut(slab(j9[0], H - 12, j9[2], H + GAP + WALL + 1, Z_BACK - 1, Z_FLOOR + 1))  # cable can also come from the wall box
# vents in the lower wall, behind the PoE module
u14 = box_of("U14")
for i in range(5):
    vx = u14[0] + 1 + i * 4.0
    back = back.cut(slab(vx, H + GAP - 1, vx + 2.0, H + GAP + WALL + 1, Z_FLOOR + 4, -PCB_T - 3))

# ---- front cover: face, walls down to the board's front face, a post over each mounting hole --------------
cover = slab(OUT0, -GAP - WALL, OUT1, H + GAP + WALL, 0, Z_FACE + FACE).edges("|Z").fillet(3.0)
cover = cover.cut(slab(IN0, -GAP, IN1, H + GAP, -1, Z_FACE))
for hx, hy in B["holes"]:
    cover = cover.union(cyl(hx, hy, POST_D, 0, Z_FACE))
    cover = cover.cut(cyl(hx, hy, SCREW_D, -1, Z_FACE + FACE + 1)).cut(cyl(hx, hy, HEAD_D, Z_FACE + FACE - 1.2, Z_FACE + FACE + 1))
# display window
d = box_of("DISP1", -2.0)
cover = cover.cut(slab(d[0], d[1], d[2], d[3], Z_FACE - 1, Z_FACE + FACE + 1))
# TALK (PB1) and OFFICE (PB7) button caps
for r in ("PB1", "PB7"):
    cover = cover.cut(cyl(part(r)["x"], part(r)["y"], BTN_D, Z_FACE - 1, Z_FACE + FACE + 1))
# microphones: a tube from the face down to just above each mic (gasket goes between), with a small port
for r in ("U16", "U17"):
    mx, my = part(r)["x"], part(r)["y"]
    cover = cover.union(cyl(mx, my, 6.0, 1.5, Z_FACE)).cut(cyl(mx, my, 3.0, 1.0, Z_FACE - 0.01)).cut(cyl(mx, my, MIC_D, Z_FACE - 1, Z_FACE + FACE + 1))
# camera: opening over the middle of the camera pad
k = B["keepouts"].get("camera heatsink")
if k: cover = cover.cut(cyl((k[0] + k[2]) / 2, (k[1] + k[3]) / 2, CAM_D, Z_FACE - 1, Z_FACE + FACE + 1))
# status LED D9: light hole
if part("D9"): cover = cover.cut(cyl(part("D9")["x"], part("D9")["y"], 3.0, Z_FACE - 1, Z_FACE + FACE + 1))
# speaker grille: slots inside a circle
sx, sy = SPK_AT
grille = None
for i in range(-4, 5):
    s = slab(sx - SPK_D / 2, sy + i * 4.0 - 1.0, sx + SPK_D / 2, sy + i * 4.0 + 1.0, Z_FACE - 1, Z_FACE + FACE + 1)
    grille = s if grille is None else grille.union(s)
cover = cover.cut(grille.intersect(cyl(sx, sy, SPK_D, Z_FACE - 2, Z_FACE + FACE + 2)))
# vents in the face over the PoE module
for i in range(4):
    vy = u14[1] + 2 + i * 5.0
    cover = cover.cut(slab(u14[0] + 2, vy, u14[2] - 2, vy + 2.0, Z_FACE - 1, Z_FACE + FACE + 1))
# side openings: RJ45 (bottom), USB-C (right), microSD and audio jack (left)
cover = cover.cut(slab(j9[0], H - 3, j9[2], H + GAP + WALL + 1, -1, 14.5))
if part("J1"):
    b1 = box_of("J1", 1.0); cover = cover.cut(slab(W - 3, b1[1], W + GAP + WALL + 1, b1[3], -1, 4.5))
if part("CARD1"):
    b1 = box_of("CARD1", 0.5); cover = cover.cut(slab(-GAP - WALL - 1, b1[1], 3, b1[3], -1, 3.0))
if part("CN1"):
    b1 = box_of("CN1", 0.5); cover = cover.cut(slab(-GAP - WALL - 1, b1[1], 3, b1[3], -1, 6.5))

# ---- a stand-in for the board, to check the fit ----------------------------------------------------------
board = slab(0, 0, W, H, -PCB_T, 0)
for hx, hy in B["holes"]: board = board.cut(cyl(hx, hy, 3.2, -PCB_T - 1, 1))
bt = box_of("BT1"); board = board.union(slab(bt[0], bt[1], bt[2], bt[3], -PCB_T - 21, -PCB_T))
board = board.union(slab(j9[0] + 1, j9[1] + 1, j9[2] - 1, j9[3] - 1, 0, 13.5))
board = board.union(slab(u14[0], u14[1], u14[2], u14[3], 0, 12.0))
dd = box_of("DISP1"); board = board.union(slab(dd[0], dd[1], dd[2], dd[3], 0, 8.0))

out = os.path.join(here, "out"); os.makedirs(out, exist_ok=True)
for name, s in (("case_back", back), ("case_cover", cover), ("board_standin", board)):
    cq.exporters.export(s, os.path.join(out, name + ".step"))
    cq.exporters.export(s, os.path.join(out, name + ".stl"), tolerance=0.05)
asm = back.union(cover.translate((0, 0, 30)))  # exploded view
opt = {"width": 900, "height": 700, "showAxes": False, "projectionDir": (0.6, -0.9, 0.7), "strokeWidth": 0.3}
cq.exporters.export(asm, os.path.join(out, "exploded.svg"), opt=opt)
cq.exporters.export(cover, os.path.join(out, "cover_front.svg"), opt=dict(opt, projectionDir=(0, 0, 1)))
bb = back.union(cover).val().BoundingBox()
print("outside: %.1f x %.1f x %.1f mm" % (bb.xlen, bb.ylen, bb.zlen))
