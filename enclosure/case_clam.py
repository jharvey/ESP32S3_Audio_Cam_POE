# Clam shell case for the intercom board (CadQuery). Draft to look at, not yet a tested print.
#   1. The back plate is flat and is screwed to the wall (or to a single-gang wall box).
#   2. The board is screwed to four posts on the back plate and the Ethernet cable is plugged in.
#   3. The front cover hangs on two hooks at the top, swings shut and is held by one screw from below.
# No 18650 holder in this version. Two loose button caps sit in the cover over TALK and OFFICE.
#   python enclosure/case_clam.py     -> enclosure/out_clam/*.step, *.stl, preview_*.png
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
GAP = 2.5        # board edge to the inside of the cover wall (room for the back plate's lip)
WALL = 2.8
FLOOR = 2.4      # back plate thickness
FACE = 2.0       # front panel thickness
BACK_H = 3.0     # room behind the board: use countersunk wall screws; the cable goes through the hole in the bay
FRONT_H = 14.8   # room in front of the board. The PoE module U14 sets it: the gull-wing "M" package is 13.85 mm tall,
                 # 14.30 max (Silvertel Ag9900M(T) drawing, Docs/; the SDAPO DP9900M clone's own drawing is NOT checked).
                 # The RJ45 J9 stands 9.05 mm above the board and 2.5 mm below it (its 3D model).
BAY = 45.0       # room below the board for the RJ45 plug and the cable's bend (0 = no bay)
LIP_T, LIP_H = 1.6, 4.0   # lip on the back plate that the cover fits around
FIT = 0.2        # play between lip and cover wall
POST_D = 7.0     # posts under the board
PILOT_D = 2.6    # M3 self-tapping screw (4.0 for a brass heat-set insert)
SCREW_D = 3.4    # M3 clearance
HEAD_D = 6.5     # screw head recess
WALL_SCREW_D = 4.5
BOX_PITCH = 83.3 # screw spacing of a US single-gang wall box
HOOK_W = 12.0    # the two hooks at the top
HOOK_X = (25.0, 75.0)
LOCK_X = 35.0    # the security screw (from below), clear of the RJ45 plug; use a tamper-proof M3 (pin Torx)
SNAP_X = (20.0, 60.0)     # two snap tongues at the bottom: the cover clicks shut without the screw
SNAP_W, SNAP_T, SNAP_H = 10.0, 1.2, 10.5
SNAP_Z = 9.0     # height of the snap ridge above the back plate
SNAP_R = 0.7     # ridge radius; it sits 0.5 mm into a groove in the cover wall
KNOCK_X = (46.0, 54.0)    # thin spot in the bottom wall to break out for a cable run on the wall surface
BTN_W, BTN_H = 14.0, 9.5   # button cap: a rounded rectangle, 0.375 in tall
BTN_R = 2.0      # its corner radius
BTN_FLANGE = 1.0 # rim behind the face that keeps the cap in
CAP_DY = 2.5     # the cap sits this much lower than its switch, to stay clear of the display window
BTN_PROUD = 1.5  # how far the cap stands out of the face
SW_TOP = 3.5     # top of the push button above the board: A GUESS, check the part
SW_TRAVEL = 0.25 # how far the switch moves to click: A GUESS, check the part
STOP_EXTRA = 0.15  # the cap's two legs land on the board this much after the click, so a hard press goes into the FR4
LEG_X0, LEG_HALF_W = 2.95, 0.9   # legs stand left and right of the switch, between its two pads on each side
BODY_Z0 = 9.5    # the cap is solid above this height (the display next to it is about 8 mm tall: A GUESS)
LABEL_H, LABEL_DEPTH = 3.6, 0.6   # engraved text height and depth
CORNER_R = 7.0   # the case's four corners
FACE_R = 2.5     # rounding of the front edge
CARD_W, CARD_H = 80.0, 42.0   # the use card (paper or laminated, up to 0.8 mm thick), in the area below the board
CARD_TOP = 99.5  # its top edge (KiCad y); it slides down into its holder from above
CARD_RAIL, CARD_LIP, CARD_SLOT = 2.4, 2.0, 0.9   # rail width, how far the rail grips over the card, room for the card
MIC_D = 1.5      # microphone ports
CAM_D = 12.0     # camera lens opening
SPK_D = 36.0     # speaker grille (the speaker hangs in the cover; its place is a guess for now)
SPK_AT = (50.0, 56.0)   # centred left to right, below the buttons

def Y(y): return -y  # KiCad y -> model y
def part(r): return B["parts"].get(r)
# Buttons, spread out, each cap centred over a switch that is already on the board. The row under the display is
# PB7 (x 23.50), PB1 (33.05), PB2 (42.50), PB4 (51.95), PB5 (61.45), all at y 25.49.
# TALK on the left is PB1 (wired as TALK). OFFICE on the right is over PB5, which is wired as DOWN today:
# the firmware (or the board) has to make that switch the OFFICE button.
TALK_AT = (part("PB1")["x"], part("PB1")["y"])
OFFICE_AT = (61.45, 25.49)
SD_OPEN = False   # microSD is reached only with the cover off
def box_of(r, grow=0.0):
    x0, y0, x1, y1 = part(r)["box"]
    return (x0 - grow, y0 - grow, x1 + grow, y1 + grow)
def slab(x0, y0, x1, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, Y(y1), z0))
def cyl(x, y, d, z0, z1):
    return cq.Workplane("XY").circle(d / 2).extrude(z1 - z0).translate((x, Y(y), z0))
def cyl_x(y, z, d, x0, x1):  # a cylinder along x
    return cq.Workplane("XY").circle(d / 2).extrude(x1 - x0).rotate((0, 0, 0), (0, 1, 0), 90).translate((x0, Y(y), z))
def cyl_y(x, z, d, y0, y1):  # a cylinder along y, from KiCad y0 to y1
    return cq.Workplane("XY").circle(d / 2).extrude(y1 - y0).rotate((0, 0, 0), (1, 0, 0), 90).translate((x, Y(y0), z))

XL_IN, XR_IN = -GAP, W + GAP                    # inside of the cover walls
YT_IN, YB_IN = -GAP, H + max(BAY, GAP)
XL, XR, YT, YB = XL_IN - WALL, XR_IN + WALL, YT_IN - WALL, YB_IN + WALL   # outside
Z_FLOOR = -PCB_T - BACK_H                       # front face of the back plate
Z_BACK = Z_FLOOR - FLOOR                        # against the wall
Z_FACE = FRONT_H                                # inside face of the front
Z_TOP = Z_FACE + FACE
j9 = box_of("J9", 1.0); u14 = box_of("U14")

# ---- back plate ------------------------------------------------------------------------------------------
back = slab(XL, YT, XR, YB, Z_BACK, Z_FLOOR).edges("|Z").fillet(CORNER_R)
l0 = (XL_IN + FIT, YT_IN + FIT, XR_IN - FIT, YB_IN - FIT)
lip = slab(l0[0], l0[1], l0[2], l0[3], Z_FLOOR, Z_FLOOR + LIP_H).edges("|Z").fillet(CORNER_R - WALL - FIT).cut(
      slab(l0[0] + LIP_T, l0[1] + LIP_T, l0[2] - LIP_T, l0[3] - LIP_T, Z_FLOOR - 1, Z_FLOOR + LIP_H + 1).edges("|Z").fillet(CORNER_R - WALL - FIT - LIP_T))
back = back.union(lip)
for hx, hy in B["holes"]:                       # posts for the board
    back = back.union(cyl(hx, hy, POST_D, Z_FLOOR, -PCB_T)).cut(cyl(hx, hy, PILOT_D, Z_FLOOR + 1.0, 0))
for hx in HOOK_X:                               # hooks at the top, pointing up into pockets in the cover wall
    back = back.union(slab(hx - HOOK_W / 2, YT_IN - 1.5, hx + HOOK_W / 2, YT_IN + FIT + LIP_T, Z_FLOOR + 1.5, Z_FLOOR + LIP_H))
# block at the bottom that takes the cover screw
LOCK_Z = Z_FLOOR + 5.0
back = back.union(slab(LOCK_X - 5, YB_IN - FIT - 10, LOCK_X + 5, YB_IN - FIT, Z_FLOOR, Z_FLOOR + 9.5))
back = back.cut(cyl_y(LOCK_X, LOCK_Z, PILOT_D, YB_IN - FIT - 9, YB + 1))
# snap tongues at the bottom: free-standing strips with a round ridge that clicks into the cover wall
for sx_ in SNAP_X:
    back = back.cut(slab(sx_ - SNAP_W / 2 - 1.5, YB_IN - FIT - LIP_T - 0.1, sx_ + SNAP_W / 2 + 1.5, YB_IN, Z_FLOOR, Z_FLOOR + LIP_H + 1))
    back = back.union(slab(sx_ - SNAP_W / 2, YB_IN - FIT - SNAP_T, sx_ + SNAP_W / 2, YB_IN - FIT, Z_FLOOR, Z_FLOOR + SNAP_H))
    back = back.union(cyl_x(YB_IN - FIT, Z_FLOOR + SNAP_Z, 2 * SNAP_R, sx_ - SNAP_W / 2 + 1, sx_ + SNAP_W / 2 - 1))
# gap in the lip where a surface cable comes in through the knock-out
back = back.cut(slab(KNOCK_X[0], YB_IN - FIT - LIP_T - 0.1, KNOCK_X[1], YB_IN, Z_FLOOR, Z_FLOOR + LIP_H + 1))
# wall screws: slots for four anchors, and two holes on a single-gang wall box
def slot(x, y, d, dx, dy):
    s = cyl(x - dx, y - dy, d, Z_BACK - 1, Z_FLOOR + 1).union(cyl(x + dx, y + dy, d, Z_BACK - 1, Z_FLOOR + 1))
    return s.union(slab(x - dx - (d / 2 if dy else 0), y - dy - (d / 2 if dx else 0), x + dx + (d / 2 if dy else 0), y + dy + (d / 2 if dx else 0), Z_BACK - 1, Z_FLOOR + 1))
yb = YB_IN - 20
for x, y, dx, dy in ((20, 20, 2, 0), (80, 20, 2, 0), (20, yb, 0, 2), (80, yb, 0, 2)):
    back = back.cut(slot(x, y, WALL_SCREW_D, dx, dy))
for y in (H / 2 - BOX_PITCH / 2, H / 2 + BOX_PITCH / 2):
    back = back.cut(slot(W / 2, y, 4.0, 0, 1.5))
# the cable comes through a hole in the bay, right behind the plug, and goes straight into the wall
if BAY >= 30:
    back = back.cut(slab(40, H + 6, 62, H + 32, Z_BACK - 1, Z_FLOOR + 1).edges("|Z").fillet(4.0))

# ---- front cover -----------------------------------------------------------------------------------------
cover = slab(XL, YT, XR, YB, Z_FLOOR, Z_TOP).edges("|Z").fillet(CORNER_R).faces(">Z").edges().fillet(FACE_R)
cover = cover.cut(slab(XL_IN, YT_IN, XR_IN, YB_IN, Z_FLOOR - 1, Z_FACE).edges("|Z").fillet(CORNER_R - WALL))
for hx in HOOK_X:                               # pockets for the hooks (closed to the outside)
    cover = cover.cut(slab(hx - HOOK_W / 2 - 0.4, YT_IN - 1.9, hx + HOOK_W / 2 + 0.4, YT_IN + 0.1, Z_FLOOR + 1.2, Z_FLOOR + LIP_H + 0.8))
cover = cover.cut(cyl_y(LOCK_X, LOCK_Z, SCREW_D, YB_IN - 1, YB + 1)).cut(cyl_y(LOCK_X, LOCK_Z, HEAD_D, YB - 1.4, YB + 1))
for sx_ in SNAP_X:                              # grooves for the snap ridges
    cover = cover.cut(cyl_x(YB_IN, Z_FLOOR + SNAP_Z, 2 * SNAP_R + 0.2, sx_ - SNAP_W / 2, sx_ + SNAP_W / 2))
# knock-out for a surface cable: the bottom wall is left 0.6 mm thick here
cover = cover.cut(slab(KNOCK_X[0], YB_IN - 0.1, KNOCK_X[1], YB - 0.6, Z_FLOOR - 0.1, Z_FLOOR + 7))
# display window
d = box_of("DISP1", -2.0)
cover = cover.cut(slab(d[0], d[1], d[2], d[3], Z_FACE - 1, Z_TOP + 1))
# a box from the face down to the display, so you see the digits and not the inside of the case
DISP_TOP = 3.85  # height of the display above the board (SLS0563, from the parts list)
cover = cover.union(slab(d[0] - 1.2, d[1] - 1.2, d[2] + 1.2, d[3] + 1.2, DISP_TOP + 0.3, Z_FACE).cut(slab(d[0], d[1], d[2], d[3], DISP_TOP, Z_FACE + 1)))
# TALK (PB1) and OFFICE (PB7): a guide under the face, bored for the caps
btn = [TALK_AT, OFFICE_AT]
def rrect(w, h, r, z0, z1, cx=0.0, cy=0.0):  # rounded rectangle block, centre at KiCad (cx, cy)
    return cq.Workplane("XY").box(w, h, z1 - z0, centered=(True, True, False)).edges("|Z").fillet(r).translate((cx, Y(cy), z0))
FW, FH = BTN_W + 2 * BTN_FLANGE, BTN_H + 2 * BTN_FLANGE
for bx, by in btn: cover = cover.union(rrect(FW + 2.8, FH + 2.8, BTN_R + 2.2, Z_FACE - 6, Z_FACE, bx, by + CAP_DY))
for bx, by in btn:
    cover = cover.cut(rrect(FW + 0.4, FH + 0.4, BTN_R + 1.0, Z_FACE - 7, Z_FACE - 0.01, bx, by + CAP_DY))
    cover = cover.cut(rrect(BTN_W + 0.4, BTN_H + 0.4, BTN_R + 0.2, Z_FACE - 1, Z_TOP + 1, bx, by + CAP_DY))
# engraved labels under the buttons
def label(txt, x, y, halign):
    return cq.Workplane("XY").text(txt, LABEL_H, LABEL_DEPTH + 0.5, halign=halign, valign="center", kind="bold").translate((x, Y(y), Z_TOP - LABEL_DEPTH))
gap = BTN_W / 2 + 1.8                           # labels sit outside their buttons, clear of the speaker grille
engraved = [label("TALK", TALK_AT[0] - gap, TALK_AT[1] + CAP_DY, "right"), label("OFFICE", OFFICE_AT[0] + gap, OFFICE_AT[1] + CAP_DY, "left")]
for e_ in engraved: cover = cover.cut(e_)
# microphones: a tube from the face down to just above each mic (gasket goes between), with a small port
for r in ("U16", "U17"):
    mx, my = part(r)["x"], part(r)["y"]
    cover = cover.union(cyl(mx, my, 6.0, 1.5, Z_FACE)).cut(cyl(mx, my, 3.0, 1.0, Z_FACE - 0.01)).cut(cyl(mx, my, MIC_D, Z_FACE - 1, Z_TOP + 1))
# camera: opening over the middle of the camera pad
k = B["keepouts"].get("camera heatsink")
if k: cover = cover.cut(cyl((k[0] + k[2]) / 2, (k[1] + k[3]) / 2, CAM_D, Z_FACE - 1, Z_TOP + 1))
# status LED D9: light hole
if part("D9"): cover = cover.cut(cyl(part("D9")["x"], part("D9")["y"], 3.0, Z_FACE - 1, Z_TOP + 1))
# speaker grille: slots inside a circle
sx, sy = SPK_AT
grille = None
for i in range(-4, 5):
    s = slab(sx - SPK_D / 2, sy + i * 4.0 - 1.0, sx + SPK_D / 2, sy + i * 4.0 + 1.0, Z_FACE - 1, Z_TOP + 1)
    grille = s if grille is None else grille.union(s)
cover = cover.cut(grille.intersect(cyl(sx, sy, SPK_D, Z_FACE - 2, Z_TOP + 2)))
# vents: in the face over the PoE module, and in the bottom wall below it
for i in range(4):
    vy = u14[1] + 2 + i * 5.0
    cover = cover.cut(slab(u14[0] + 2, vy, u14[2] - 2, vy + 2.0, Z_FACE - 1, Z_TOP + 1))
for i in range(5):
    vx = u14[0] + 1 + i * 4.0
    cover = cover.cut(slab(vx, YB_IN - 1, vx + 2.0, YB + 1, 2, Z_FACE - 3))
if BAY < 30:  # no bay: the cable leaves through the bottom wall
    cover = cover.cut(slab(j9[0], YB_IN - 1, j9[2], YB + 1, -1, 14.5))
# side openings: USB-C (right, wide enough for the plug's body), microSD and audio jack (left)
USB_OPEN = False  # USB-C is reached only with the cover off
if USB_OPEN and part("J1"):
    b1 = box_of("J1", 2.5); cover = cover.cut(slab(XR_IN - 1, b1[1], XR + 1, b1[3], -2.5, 6.0))
if SD_OPEN and part("CARD1"):
    b1 = box_of("CARD1", 0.5); cover = cover.cut(slab(XL - 1, b1[1], XL_IN + 1, b1[3], -0.5, 3.0))
if part("CN1"):
    b1 = box_of("CN1", 0.5); cover = cover.cut(slab(XL - 1, b1[1], XL_IN + 1, b1[3], -0.5, 6.5))

# ---- use card holder on the face: two side rails and a bottom rail that grip the card's edges. The card slides
# down from the top; a gap in the bottom rail lets a finger push it back up to read its other side. -------------
cx0, cx1 = W / 2 - CARD_W / 2 - 0.3, W / 2 + CARD_W / 2 + 0.3
cy0, cy1 = CARD_TOP, CARD_TOP + CARD_H + 0.3
zt = Z_TOP + CARD_SLOT + 0.9
holder = slab(cx0 - CARD_RAIL, cy0, cx0, cy1 + CARD_RAIL, Z_TOP - 0.2, zt)
holder = holder.union(slab(cx1, cy0, cx1 + CARD_RAIL, cy1 + CARD_RAIL, Z_TOP - 0.2, zt))
holder = holder.union(slab(cx0 - CARD_RAIL, cy1, cx1 + CARD_RAIL, cy1 + CARD_RAIL, Z_TOP - 0.2, zt))
holder = holder.union(slab(cx0, cy0, cx0 + CARD_LIP, cy1, Z_TOP + CARD_SLOT, zt))
holder = holder.union(slab(cx1 - CARD_LIP, cy0, cx1, cy1, Z_TOP + CARD_SLOT, zt))
holder = holder.union(slab(cx0, cy1 - CARD_LIP, cx1, cy1, Z_TOP + CARD_SLOT, zt))
holder = holder.edges("|Z").fillet(0.9)
holder = holder.cut(cyl(W / 2, cy1 + CARD_RAIL, 18.0, Z_TOP, zt + 1))   # finger gap in the bottom rail
cover = cover.union(holder)
use_card = slab(cx0 + 0.3, cy1 - 0.3 - CARD_H, cx1 - 0.3, cy1 - 0.3, Z_TOP + 0.1, Z_TOP + 0.6)   # the card itself, for the pictures only

# ---- looks: an engraved ring round the speaker and round the status light, a bevel on the camera opening ------
def ring(x, y, d_out, width):
    return cyl(x, y, d_out, Z_TOP - LABEL_DEPTH, Z_TOP + 1).cut(cyl(x, y, d_out - 2 * width, Z_TOP - LABEL_DEPTH - 1, Z_TOP + 2))
rings = [ring(sx, sy, SPK_D + 6.0, 0.9)] + ([ring(part("D9")["x"], part("D9")["y"], 7.0, 0.8)] if part("D9") else [])
for e_ in rings: cover = cover.cut(e_)
# the paint in the engraving, for the pictures only: a thin layer on the bottom of every engraved shape
paint = None
for e_ in engraved + rings:
    p_ = e_.intersect(slab(XL, YT, XR, YB, Z_TOP - LABEL_DEPTH + 0.02, Z_TOP - LABEL_DEPTH + 0.2))
    paint = p_ if paint is None else paint.union(p_)
if k:
    bevel = cq.Workplane("XY").circle(CAM_D / 2).workplane(offset=1.2).circle(CAM_D / 2 + 1.2).loft()
    cover = cover.cut(bevel.translate(((k[0] + k[2]) / 2, Y((k[1] + k[3]) / 2), Z_TOP - 1.19)))

# ---- button cap (print two): cap through the face, flange behind it, stem down to the switch --------------
cap = rrect(BTN_W, BTN_H, BTN_R, Z_FACE - 0.3, Z_TOP + BTN_PROUD, 0, CAP_DY).faces(">Z").edges().fillet(0.8)
cap = cap.union(rrect(FW, FH, BTN_R + 0.8, BODY_Z0, Z_FACE - 0.3, 0, CAP_DY))       # body, guided in the cover
cap = cap.union(cyl(0, 0, 4.0, SW_TOP, BODY_Z0))                                    # stem, rests on the switch
R = 4.5
legs = slab(-R, -LEG_HALF_W, R, LEG_HALF_W, SW_TOP + 1.5, BODY_Z0)                  # bridge over the switch
for s in (-1, 1):                                                                   # two legs down to just above the board
    legs = legs.union(slab(min(s * LEG_X0, s * R), -LEG_HALF_W, max(s * LEG_X0, s * R), LEG_HALF_W, SW_TRAVEL + STOP_EXTRA, SW_TOP + 1.5))
cap = cap.union(legs)
caps = None
for bx, by in btn:
    c = cap.translate((bx, Y(by), 0)); caps = c if caps is None else caps.union(c)

# ---- a stand-in for the board, to check the fit ----------------------------------------------------------
board = slab(0, 0, W, H, -PCB_T, 0)
for hx, hy in B["holes"]: board = board.cut(cyl(hx, hy, 3.2, -PCB_T - 1, 1))
board = board.union(slab(j9[0] + 1, j9[1] + 1, j9[2] - 1, j9[3] - 1, -PCB_T - 2.5, 9.05))
board = board.union(slab(u14[0], u14[1], u14[2], u14[3], 0, 14.3))
dd = box_of("DISP1"); board = board.union(slab(dd[0], dd[1], dd[2], dd[3], 0, DISP_TOP))
for bx, by in btn: board = board.union(slab(bx - 2.5, by - 2.5, bx + 2.5, by + 2.5, 0, SW_TOP))
if BAY >= 30:  # the RJ45 plug with its boot
    board = board.union(slab(j9[0] + 4, j9[3] - 1, j9[2] - 4, j9[3] + 24, 0.5, 8.5))

out = os.path.join(here, "out_clam"); os.makedirs(out, exist_ok=True)
for name, s in (("clam_back", back), ("clam_cover", cover), ("clam_button", cap), ("clam_buttons", caps), ("board_standin", board), ("use_card", use_card), ("paint", paint)):
    if name not in ("clam_buttons", "use_card", "paint"): cq.exporters.export(s, os.path.join(out, name + ".step"))
    cq.exporters.export(s, os.path.join(out, name + ".stl"), tolerance=0.05)
bb = back.union(cover).val().BoundingBox()
print("outside: %.1f x %.1f x %.1f mm" % (bb.xlen, bb.ylen, bb.zlen))
for name, a, b in (("cover/board", cover, board), ("cover/back", cover, back), ("back/board", back, board), ("caps/cover", caps, cover), ("caps/board", caps, board)):
    v = a.intersect(b).val().Volume() if a.intersect(b).vals() else 0.0
    print("overlap %-12s %.2f mm3" % (name, v))

# ---- preview pictures ------------------------------------------------------------------------------------
import vtk
def actor(name, color, dz=0.0):
    r = vtk.vtkSTLReader(); r.SetFileName(os.path.join(out, name + ".stl"))
    n = vtk.vtkPolyDataNormals(); n.SetInputConnection(r.GetOutputPort()); n.SetFeatureAngle(40)
    m = vtk.vtkPolyDataMapper(); m.SetInputConnection(n.GetOutputPort())
    a = vtk.vtkActor(); a.SetMapper(m); a.GetProperty().SetColor(*color); a.SetPosition(0, 0, dz)
    return a
def shot(fname, actors, pos, up=(0, 1, 0)):
    ren = vtk.vtkRenderer(); ren.SetBackground(1, 1, 1)
    for a in actors: ren.AddActor(a)
    w = vtk.vtkRenderWindow(); w.SetOffScreenRendering(1); w.SetSize(1200, 900); w.AddRenderer(ren)
    c = ren.GetActiveCamera(); c.SetFocalPoint(50, -70, 0); c.SetPosition(50 + pos[0], -70 + pos[1], pos[2]); c.SetViewUp(*up)
    ren.ResetCamera(); c.Zoom(1.2); w.Render()
    f = vtk.vtkWindowToImageFilter(); f.SetInput(w); f.Update()
    p = vtk.vtkPNGWriter(); p.SetFileName(os.path.join(out, fname)); p.SetInputConnection(f.GetOutputPort()); p.Write()
grey, blue, green, red = (0.82, 0.82, 0.80), (0.55, 0.66, 0.80), (0.20, 0.50, 0.25), (0.80, 0.25, 0.20)
beige, white = (0.87, 0.80, 0.66), (0.97, 0.97, 0.95)
black = (0.05, 0.05, 0.05)
def card_face():  # the card's printed front (make_card.py draws it) laid on the card
    png = os.path.join(here, "card", "use_card_front.png")
    if not os.path.exists(png): return actor("use_card", white)
    r = vtk.vtkPNGReader(); r.SetFileName(png)
    t = vtk.vtkTexture(); t.SetInputConnection(r.GetOutputPort()); t.InterpolateOn()
    x0_, x1_, yb_, yt_, z_ = cx0 + 0.3, cx1 - 0.3, Y(cy1 - 0.3), Y(cy1 - 0.3 - CARD_H), Z_TOP + 0.62
    pl = vtk.vtkPlaneSource(); pl.SetOrigin(x0_, yb_, z_); pl.SetPoint1(x1_, yb_, z_); pl.SetPoint2(x0_, yt_, z_)
    m = vtk.vtkPolyDataMapper(); m.SetInputConnection(pl.GetOutputPort())
    a = vtk.vtkActor(); a.SetMapper(m); a.SetTexture(t); a.GetProperty().LightingOff()
    return a
def black_paint():  # flat black, whatever the light
    a = actor("paint", black); a.GetProperty().LightingOff(); return a
BOARD_COLOR = (0.08, 0.08, 0.09)   # black solder mask; green is (0.20, 0.50, 0.25)
front = lambda: [actor("clam_back", grey), actor("board_standin", BOARD_COLOR), actor("clam_cover", beige), actor("clam_buttons", red), actor("use_card", white), card_face(), black_paint()]
shot("preview_front.png", front(), (60, -120, 240))
shot("preview_face.png", front(), (0, -30, 300))
shot("preview_step2.png", [actor("clam_back", grey), actor("board_standin", green)], (90, -200, 200), (0, 0, 1))
shot("preview_exploded.png", [actor("clam_back", grey), actor("board_standin", green, 30), actor("clam_buttons", red, 70), actor("clam_cover", blue, 80)], (190, -260, 170), (0, 0, 1))
shot("preview_inside.png", [actor("clam_cover", blue), actor("clam_buttons", red)], (-60, -160, -220))
def shot_at(fname, actors, focal, pos, up=(0, 0, 1)):
    ren = vtk.vtkRenderer(); ren.SetBackground(1, 1, 1)
    for a in actors: ren.AddActor(a)
    w = vtk.vtkRenderWindow(); w.SetOffScreenRendering(1); w.SetSize(1200, 900); w.AddRenderer(ren)
    c = ren.GetActiveCamera(); c.SetFocalPoint(*focal); c.SetPosition(focal[0] + pos[0], focal[1] + pos[1], focal[2] + pos[2]); c.SetViewUp(*up)
    ren.ResetCameraClippingRange(); w.Render()
    f = vtk.vtkWindowToImageFilter(); f.SetInput(w); f.Update()
    p = vtk.vtkPNGWriter(); p.SetFileName(os.path.join(out, fname)); p.SetInputConnection(f.GetOutputPort()); p.Write()
shot_at("preview_buttons.png", [actor("board_standin", green), actor("clam_buttons", red)], (28, -26, 6), (10, -60, 22))
shot_at("preview_labels.png", [actor("clam_back", grey), actor("clam_cover", blue), actor("clam_buttons", red)], (50, -30, Z_TOP), (0, -40, 150), (0, 1, 0))
shot_at("preview_card.png", [actor("clam_cover", beige), actor("use_card", white)], (50, -122, Z_TOP), (30, -70, 90), (0, 1, 0))
shot("preview_bottom.png", [actor("clam_back", grey), actor("clam_cover", blue)], (40, -260, 60), (0, 0, 1))
