# Clam shell case for the intercom board (CadQuery). Draft to look at, not yet a tested print.
#   1. The back plate is flat and is screwed to the wall (or to a single-gang wall box).
#   2. The board is screwed to four posts on the back plate and the Ethernet cable is plugged in.
#   3. The front cover hangs on two hooks at the top, swings shut and is held by one screw from below.
# Two loose button caps sit in the cover over TALK and OFFICE, and a red LOCKDOWN cap under a clear flip-up lid.
#   python enclosure/case_clam.py     -> enclosure/out_clam/*.step, *.stl, preview_*.png
# Positions come from board_features.json (run board_features.py after the layout changes).
#
# Axes: x right, y up (KiCad's y is flipped here), z towards the room. z = 0 is the board's front face.
import json, os
import cadquery as cq

here = os.path.dirname(os.path.abspath(__file__))
# Three versions of the case, chosen with the environment variable CASE_VARIANT:
#   room    (default) a classroom unit on the wall                                        -> out_clam/
#   office  the office unit: one more small button, QUIET, above the lockdown lid         -> out_clam_office/
#   mobile  a unit that is carried about: the network cable plugs in from outside, into a
#           coupler in the bottom wall, so it is charged and unplugged without opening it -> out_clam_mobile/
VARIANT = os.environ.get("CASE_VARIANT", "room")
assert VARIANT in ("room", "office", "mobile"), VARIANT
B = json.load(open(os.path.join(here, "board_features.json")))
W, H = B["w"], B["h"]

# ---- numbers to tune -------------------------------------------------------------------------------------
PCB_T = 1.6
GAP = 2.5        # board edge to the inside of the cover wall (room for the back plate's lip)
WALL = 2.8
FLOOR = 2.4      # back plate thickness
FACE = 2.0       # front panel thickness
BATTERY = True   # an 18650 in its holder BT1 on the back of the board (False: a thinner case without it)
BT_H = 20.5      # holder with a cell in it, above the board. The holder alone is 14.96 mm (its 3D model); where the top
                 # of the cell ends up is AN ESTIMATE, measure one
POE_H = 13.85    # PoE module U14, on the back of the board: 13.35 +/- 0.5 mm (SDAPO DP9900M drawing, page 7)
BACK_H = (BT_H if BATTERY else POE_H) + 0.55    # room behind the board
FRONT_H = 6.55 + 0.55   # room in front of the board: the Ethernet transformer T1 (6.55 mm, its 3D model) is the tallest
                 # part there, apart from the RJ45 J9 (8.85 mm), which gets a raised bump in the face (JACK_BUMP).
                 # The 3-pin headers J5-J8 must be left off or fitted lying down (upright they are about 8.5 mm).
BAY = 48.0       # room below the board for the RJ45 plug and the cable's bend (0 = no bay)
LIP_T, LIP_H = 1.6, 4.0   # lip on the back plate that the cover fits around
FIT = 0.2        # play between lip and cover wall
POST_D = 8.0     # posts under the board
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
CAP_DY = 0.0     # the switches now sit under the caps (the row was moved 2.5 mm down on the board, clear of the display)
BTN_PROUD = 1.5  # how far the cap stands out of the face
SW_TOP = 1.5     # top of the push button above the board (its 3D model; the switch's drawing is not checked)
SW_TRAVEL = 0.25 # how far the switch moves to click: A GUESS, check the part
STOP_EXTRA = 0.15  # the cap's two legs land on the board this much after the click, so a hard press goes into the FR4
LEG_X0, LEG_HALF_W = 2.95, 0.9   # legs stand left and right of the switch, between its two pads on each side
LABEL_H, LABEL_DEPTH = 3.2, 0.6   # engraved text height and depth
CORNER_R = 7.0   # the case's four corners
FACE_R = 2.5     # rounding of the front edge
CARD_W, CARD_H = 80.0, 42.0   # the use card (paper or laminated, up to 0.8 mm thick), in the area below the board
CARD_TOP = 103.5 # its top edge, below the jack's bump (KiCad y); it slides down into its holder from above
CARD_RAIL, CARD_LIP, CARD_SLOT = 2.4, 2.0, 0.9   # rail width, how far the rail grips over the card, room for the card
# The red LOCKDOWN button: a round red cap over PB11 (wired to U22 P11), which was moved to x 9.5, y 14.0 on the board
# (U22 has to move out of its way). The lid has a notch at the left microphone port, so it never covers the port.
# inside a raised rim, under a clear flip-up lid. The lid is laser cut from 2 mm clear acrylic (out_clam/lid.dxf):
# it hangs on two ears in the hinge posts, falls shut by its own weight and opens to about 135 degrees.
RED_D, RED_FL = 14.0, 15.6   # red cap and its flange behind the face
LID_T = 2.0      # acrylic sheet
EAR = 2.4        # each ear reaches this far past the lid's neck
HGAP = 0.6       # play between the lid's neck and each post, so the lid swings and falls freely
POST_W = 3.4
# Camera shutter: a 9 g servo (SG90 size) stands beside the board on the left, its shaft towards the room. The whole
# left side of the case is moved out to make room for it, so that side stays one straight wall; the board, the face
# and everything on it stay where they were, which leaves them a little off centre. A paddle on the shaft turns just behind the face:
#   closed   the disc covers the camera lens
#   open     a quarter turn down: lens free, paddle still inside the case (silent help call)
#   flag     half a turn: the disc sticks out of a slot in the side of the case, so everyone sees the camera is on (lockdown)
# ALL SERVO SIZES ARE THE USUAL SG90 ONES, NOT MEASURED: check yours. The camera's lens must end SHUT_GAP below the face.
SERVO_W, SERVO_L, SERVO_BODY_H = 12.2, 22.7, 22.7   # body: across, along, tall (shaft end up)
SERVO_EAR, SERVO_EAR_Z, SERVO_EAR_T = 4.7, 15.9, 2.5  # each ear sticks out this far; its underside is this high above the bottom
SERVO_SHAFT_IN = 5.9   # shaft axis from the nearer end of the body
SERVO_TOP = 30.0       # bottom of the body to the tip of the shaft
PIVOT = (-6.9, 51.5)   # shaft axis (KiCad x, y): beside the board, as high as the audio jack's opening allows
PAD_D, PAD_T, ARM_W = 15.0, 1.2, 6.0   # the disc over the lens, paddle thickness, arm width
SHUT_GAP = 2.0         # room between the lens and the face that the paddle turns in
POD_X0 = -15.5         # inside of the case's left wall (it was -GAP before the servo)
POD_Y = (40.5, 80.5)   # where the servo and the paddle are (KiCad y)
SLOT_Y0 = 47.0         # the slot in the left wall runs from here to POD_Y[1]
CARD_DX = 6.0    # the card holder sits this far right of the board's middle, to leave the lower left of the face for the speaker
# Box speaker (35 x 25 mm, 4 ohm 3 W, 1.25 mm plug; LCSC C49247004 or the like): in the lower left of the case, beside
# the Ethernet plug, firing through its own grille. It is as far from the two microphones as the case allows, and the
# speaker's own sealed box keeps its sound out of the inside of the case: both help the echo canceller.
SPK2_W, SPK2_H, SPK2_T = 25.0, 35.0, 6.8   # across, tall, thick (6.2 to 6.8 mm, by maker)
SPK2_AT = (-1.9, 124.0)                    # its middle (KiCad x, y)
# People sensor (Hi-Link HLK-LD2410C, 22 x 16 mm, about 3 mm thick): it stands behind the lower face, right of the
# Ethernet plug, looking into the room through the face and the use card (plastic and paper do not stop it; no metal
# or foil there). It slides down into two grooves from above and two posts close over it; its wires go to J22 on the board.
# THE MODULE'S SIZE IS FROM THE MAKER'S MANUAL, NOT MEASURED. The grooves take 0.8 mm of its two short edges, so the
# soldered wires are free if its pins are on a long edge (NOT CHECKED on a real module).
RADAR = True
RADAR_W, RADAR_H, RADAR_PCB = 22.0, 16.0, 1.0   # across, tall, board thickness
RADAR_AT = (82.0, 120.0)                   # its middle (KiCad x, y)
RADAR_GAP = 6.0                            # room between the module's front and the inside of the face: about half a wavelength at 24 GHz (6.2 mm)
RADAR_THIN = 1.2  # the face is thinned to this in front of the sensor, from the inside. 24 GHz passes a thin wall best:
                  # 2.0 mm of ASA is close to a quarter wave in the plastic (about 1.8 mm), which reflects most
# office version: a small round QUIET cap over PB10, the top switch of the left column (wired to U22 P10). It sits above
# the lockdown lid's hinge, between the two hinge posts and left of the microphone port. The switches between TALK and
# OFFICE are only 9.5 mm apart, too close for a cap of their own.
QUIET_D, QUIET_FL, QUIET_PROUD = 5.5, 7.1, 0.8   # cap, its flange behind the face, how far it stands out
# mobile version: a keystone RJ45 coupler (the snap-in kind for wall plates, both ends sockets) clicks into the bottom
# wall from inside; a short flat patch lead (about 15 cm) goes from its inner end to J9. THE OPENING IS THE USUAL
# KEYSTONE SIZE FROM PUBLISHED DRAWINGS, NOT TEST FITTED: print the cover's bottom edge alone first and try your coupler.
KEY_X = 75.0                # middle of the coupler (KiCad x): right of the plug, clear of the board's post and the snaps
KEY_W, KEY_H = 14.9, 16.4   # the opening in the outer skin of the wall
KEY_H_IN = 19.6             # ... and behind that skin, where the coupler's two catches spread
KEY_SKIN = 1.8              # thickness the catches grip
KEY_ZC = -10.7              # middle of the coupler above the board's front face (it lies behind the board's plane)
KEY_L = 35.0                # coupler length, for the fit check only (30 to 36 mm, by maker)
MIC_D = 1.5      # microphone ports
CAM_D = 12.0     # camera lens opening

def Y(y): return -y  # KiCad y -> model y
def part(r): return B["parts"].get(r)
# Buttons, spread out, each cap centred over a switch that is already on the board. The row under the display is
# PB7 (x 23.50), PB1 (33.05), PB2 (42.50), PB4 (51.95), PB5 (61.45), all at y 25.49.
# TALK on the left is PB1 (wired as TALK). OFFICE on the right is over PB5, which is wired as DOWN today:
# the firmware (or the board) has to make that switch the OFFICE button.
TALK_AT = (part("PB1")["x"], part("PB1")["y"])
OFFICE_AT = (61.45, TALK_AT[1])
RED_AT = (part("PB12")["x"], part("PB12")["y"])   # the third switch of the left column: low enough that the hinge is clear of the microphone
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
def rrect(w, h, r, z0, z1, cx=0.0, cy=0.0):  # rounded rectangle block, centre at KiCad (cx, cy)
    return cq.Workplane("XY").box(w, h, z1 - z0, centered=(True, True, False)).edges("|Z").fillet(r).translate((cx, Y(cy), z0))

XL_IN, XR_IN = POD_X0, W + GAP                  # inside of the cover walls (the left one stands off for the servo)
YT_IN, YB_IN = -GAP, H + max(BAY, GAP)
XL, XR, YT, YB = XL_IN - WALL, XR_IN + WALL, YT_IN - WALL, YB_IN + WALL   # outside
Z_FLOOR = -PCB_T - BACK_H                       # front face of the back plate
Z_BACK = Z_FLOOR - FLOOR                        # against the wall
Z_FACE = FRONT_H                                # inside face of the front
Z_TOP = Z_FACE + FACE
GUIDE_H = min(6.0, Z_FACE - max(SW_TOP + 0.9, 4.16 + 0.3))   # button guides stop above the switches and the 4.16 mm display
BODY_Z0 = max(Z_FACE - 5.0, 4.5)                # the cap is solid above this height, clear of the 4.16 mm display
j9 = box_of("J9", 1.0); u14 = box_of("U14")
J9_BACK = part("J9")["back"]
JACK_H = 8.85    # RJ45 J9 above the board (HCTL drawing); 2.70 mm below it
JACK_BUMP = 0 if J9_BACK else max(0.0, JACK_H + 0.55 - FRONT_H)   # how far the face rises over the jack

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
for x, y, dx, dy in ((45, 20, 2, 0), (80, 20, 2, 0), (20, yb, 0, 2), (80, yb, 0, 2)):
    back = back.cut(slot(x, y, WALL_SCREW_D, dx, dy))
for y in (H / 2 - BOX_PITCH / 2, H / 2 + BOX_PITCH / 2):
    back = back.cut(slot(W / 2, y, 4.0, 0, 1.5))
# the cable comes through a hole in the bay, right behind the plug, and goes straight into the wall
if BAY >= 30 and VARIANT != "mobile":       # (a mobile unit has no wall behind it: its back is closed)
    back = back.cut(slab(40, H + 6, 62, H + 32, Z_BACK - 1, Z_FLOOR + 1).edges("|Z").fillet(4.0))
if VARIANT == "mobile":                     # gap in the lip where the coupler sits in the cover's bottom wall
    back = back.cut(slab(KEY_X - KEY_W / 2 - 1.5, YB_IN - FIT - LIP_T - 0.1, KEY_X + KEY_W / 2 + 1.5, YB_IN, Z_FLOOR, Z_FLOOR + LIP_H + 1))

# ---- camera shutter: a pocket in the back plate and two posts for the servo ----
k_ = B["keepouts"]["camera heatsink"]; CAM_AT = ((k_[0] + k_[2]) / 2, (k_[1] + k_[3]) / 2)
SV_Z0 = (FRONT_H - 0.4) - SERVO_TOP              # bottom of the servo: the shaft's tip ends at the paddle's front face
SV_Y0 = PIVOT[1] - SERVO_SHAFT_IN                # the servo's upper end; its body runs down from there
SV_X0, SV_X1 = PIVOT[0] - SERVO_W / 2, PIVOT[0] + SERVO_W / 2
if SV_Z0 < Z_FLOOR: back = back.cut(slab(SV_X0 - 0.3, SV_Y0 - 0.3, SV_X1 + 0.3, SV_Y0 + SERVO_L + 0.3, SV_Z0 - 0.2, Z_FLOOR + 1))
for ey in (SV_Y0 - SERVO_EAR / 2, SV_Y0 + SERVO_L + SERVO_EAR / 2):   # a post under each ear, for the servo's own two screws
    back = back.union(slab(SV_X0, ey - 2.2, SV_X1, ey + 2.2, Z_FLOOR, SV_Z0 + SERVO_EAR_Z)).cut(cyl(PIVOT[0], ey, 1.7, SV_Z0 + SERVO_EAR_Z - 8, SV_Z0 + SERVO_EAR_Z + 1))

# ---- front cover -----------------------------------------------------------------------------------------
cover = slab(XL, YT, XR, YB, Z_FLOOR, Z_TOP).edges("|Z").fillet(CORNER_R).faces(">Z").edges().fillet(FACE_R)
cover = cover.cut(slab(XL_IN, YT_IN, XR_IN, YB_IN, Z_FLOOR - 1, Z_FACE).edges("|Z").fillet(CORNER_R - WALL))
for hx in HOOK_X:                               # pockets for the hooks (closed to the outside)
    cover = cover.cut(slab(hx - HOOK_W / 2 - 0.4, YT_IN - 1.9, hx + HOOK_W / 2 + 0.4, YT_IN + 0.1, Z_FLOOR + 1.2, Z_FLOOR + LIP_H + 0.8))
cover = cover.cut(cyl_y(LOCK_X, LOCK_Z, SCREW_D, YB_IN - 1, YB + 1)).cut(cyl_y(LOCK_X, LOCK_Z, HEAD_D, YB - 1.4, YB + 1))
for sx_ in SNAP_X:                              # grooves for the snap ridges
    cover = cover.cut(cyl_x(YB_IN, Z_FLOOR + SNAP_Z, 2 * SNAP_R + 0.2, sx_ - SNAP_W / 2, sx_ + SNAP_W / 2))
# the lower part of the face (over the RJ45 jack, its plug and the cable bay) is raised by JACK_BUMP, so the rest of
# the face can sit low over the board. The use card sits on the raised part.
STEP_Y = j9[1] - 1.6 - FACE                     # where the face steps up (KiCad y)
if JACK_BUMP > 0:
    outline = slab(XL, YT, XR, YB, Z_TOP - 1, Z_TOP + JACK_BUMP).edges("|Z").fillet(CORNER_R)
    step = outline.intersect(slab(XL - 1, STEP_Y, XR + 1, YB + 1, Z_TOP - 1, Z_TOP + JACK_BUMP))
    try: step = step.faces(">Z").edges().fillet(min(FACE_R, JACK_BUMP * 0.8))
    except Exception: pass
    cover = cover.union(step).cut(slab(XL_IN, STEP_Y + FACE, XR_IN, YB_IN, Z_FACE - 1, Z_FACE + JACK_BUMP).edges("|Z").fillet(CORNER_R - WALL))
Z_LOW = Z_TOP + JACK_BUMP                       # top of the raised lower part
# knock-out for a surface cable: the bottom wall is left 0.6 mm thick here
cover = cover.cut(slab(KNOCK_X[0], YB_IN - 0.1, KNOCK_X[1], YB - 0.6, Z_FLOOR - 0.1, Z_FLOOR + 7))
# display window
d = box_of("DISP1", -2.0)
cover = cover.cut(slab(d[0], d[1], d[2], d[3], Z_FACE - 1, Z_TOP + 1))
# a box from the face down to the display, so you see the digits and not the inside of the case
DISP_TOP = 4.16  # height of the display above the board (its 3D model; the parts list says 3.85)
cover = cover.union(slab(d[0] - 1.2, d[1] - 1.2, d[2] + 1.2, d[3] + 1.2, DISP_TOP + 0.3, Z_FACE).cut(slab(d[0], d[1], d[2], d[3], DISP_TOP, Z_FACE + 1)))
# TALK (PB1) and OFFICE (PB7): a guide under the face, bored for the caps
btn = [TALK_AT, OFFICE_AT]
FW, FH = BTN_W + 2 * BTN_FLANGE, BTN_H + 2 * BTN_FLANGE
for bx, by in btn: cover = cover.union(rrect(FW + 2.8, FH + 2.8, BTN_R + 2.2, Z_FACE - GUIDE_H, Z_FACE, bx, by + CAP_DY))
for bx, by in btn:
    cover = cover.cut(rrect(FW + 0.4, FH + 0.4, BTN_R + 1.0, Z_FACE - GUIDE_H - 1, Z_FACE - 0.01, bx, by + CAP_DY))
    cover = cover.cut(rrect(BTN_W + 0.4, BTN_H + 0.4, BTN_R + 0.2, Z_FACE - 1, Z_TOP + 1, bx, by + CAP_DY))
# office version: the QUIET button, a small round cap over PB10
QUIET_AT = (part("PB10")["x"], part("PB10")["y"]) if part("PB10") else (5.35, 6.35)   # from the board file (PB10 at 55.35, 56.35)
if VARIANT == "office":
    qbx, qby = QUIET_AT
    cover = cover.union(cyl(qbx, qby, QUIET_FL + 3.0, Z_FACE - GUIDE_H, Z_FACE))
    cover = cover.cut(cyl(qbx, qby, QUIET_FL + 0.4, Z_FACE - GUIDE_H - 1, Z_FACE - 0.01)).cut(cyl(qbx, qby, QUIET_D + 0.5, Z_FACE - 1, Z_TOP + 1))
# mobile version: the opening for the keystone coupler in the bottom wall
if VARIANT == "mobile":
    cover = cover.cut(slab(KEY_X - KEY_W / 2, YB_IN - 0.1, KEY_X + KEY_W / 2, YB + 0.1, KEY_ZC - KEY_H / 2, KEY_ZC + KEY_H / 2))
    cover = cover.cut(slab(KEY_X - KEY_W / 2, YB_IN - 0.1, KEY_X + KEY_W / 2, YB - KEY_SKIN, KEY_ZC - KEY_H_IN / 2, KEY_ZC + KEY_H_IN / 2))
# red LOCKDOWN button. The clear lid lies flat ON the face (nothing between them), hanging from two hinge posts. The
# red cap sits in a round hole under it, its top 0.8 mm below the face, so the lid covers it and a flat hand cannot
# press it. Below the lid's lower edge the face has a shallow scoop, so a fingertip gets under the edge and flips it up.
# TO FIT THE LID: hold it sticking straight out from the face and drop its two ears into the notches in the FRONT of
# the posts, down into the round holes. Then press a CLIP onto each post from the front: its tongue fills the notch
# above the ear and its split pin clicks in behind the face (a barb, as on the push buttons of the FPGA camera case).
# After that the lid cannot be taken off without opening the case. The clips leave the upper half of each post free,
# which is where the lid's shoulders land when it is flipped up.
# Everything is placed from the switch (PB12), wherever it is moved.
rx, ry = RED_AT
LID_X = (rx - 10.0, rx + 10.0)                  # lid body, left and right edge
HINGE_Y = ry - 8.5                              # hinge axis (KiCad y)
LID_Y1 = ry + 8.5                               # lid's lower edge
NECK_X = (rx - 6.5, rx + 6.5)                   # the lid is narrower at the hinge
RED_DOWN = 0.8                                  # the cap's top is this far below the face
Z_AX = Z_TOP + LID_T / 2                        # hinge axis: the middle of the lid's thickness
cover = cover.union(cyl(rx, ry, RED_FL + 3.0, Z_FACE - GUIDE_H, Z_FACE))
cover = cover.cut(cyl(rx, ry, RED_FL + 0.4, Z_FACE - GUIDE_H - 1, Z_FACE - 0.01)).cut(cyl(rx, ry, RED_D + 0.5, Z_FACE - 1, Z_TOP + 1))
cover = cover.cut(cq.Workplane("XY").circle(RED_D / 2 + 0.25).workplane(offset=1.0).circle(RED_D / 2 + 1.25).loft().translate((rx, Y(ry), Z_TOP - 0.99)))   # a bevel round the hole, for the fingertip
cover = cover.cut(slab(NECK_X[0] - HGAP, HINGE_Y - 2.4, NECK_X[1] + HGAP, HINGE_Y + 0.8, Z_TOP - 1.4, Z_TOP + 1))    # a groove for the lid's tail, which dips as the lid opens
SCOOP_R, SCOOP_DEEP = 9.0, 1.5   # the finger scoop: a hollow rounded like the inside of a pipe, deepest right under the lid's lower edge and
cover = cover.cut(cyl_x(LID_Y1, Z_TOP + SCOOP_R - SCOOP_DEEP, 2 * SCOOP_R, rx - 8.0, rx + 8.0))   # curving up to the face below it, so a fingertip slides down the curve and under the edge
posts = None
for x0_, x1_ in ((NECK_X[0] - HGAP - POST_W, NECK_X[0] - HGAP), (NECK_X[1] + HGAP, NECK_X[1] + HGAP + POST_W)):
    # the post reaches 4 mm above the axis and 2.6 mm over it: the lid's shoulders land on that corner at about 135 degrees
    p_ = slab(x0_, HINGE_Y - 4.0, x1_, HINGE_Y + 2.4, Z_TOP - 0.2, Z_AX + 2.6)
    posts = p_ if posts is None else posts.union(p_)
posts = posts.edges("|X").fillet(0.6)
# the ear turns in a round hole; a notch in the front of each post lets it snap in while the lid sticks straight out
posts = posts.cut(cyl_x(HINGE_Y, Z_AX, 4.6, NECK_X[0] - EAR - 0.7, NECK_X[1] + EAR + 0.7))   # 4.6 mm hole for an ear 2.4 x 2 mm: it rattles, on purpose
posts = posts.cut(slab(NECK_X[0] - EAR - 0.7, HINGE_Y - 1.4, NECK_X[1] + EAR + 0.7, HINGE_Y + 1.4, Z_AX, Z_AX + 4))
cover = cover.union(posts)
# the two clips (print one of each hand): cap on the lower half of the post, tongue in the notch, barbed split pin
CLIP_PIN_D, CLIP_BARB_D, CLIP_SLOT, CLIP_PIN_OUT = 4.0, 5.0, 1.1, 3.0   # pin, its barb, the slit that lets it squeeze, pin centre outside the post
clips = None
clip_parts = []
for side_, (x0_, x1_) in ((-1, (NECK_X[0] - HGAP - POST_W, NECK_X[0] - HGAP)), (1, (NECK_X[1] + HGAP, NECK_X[1] + HGAP + POST_W))):
    pxc = (x0_ if side_ < 0 else x1_) + side_ * CLIP_PIN_OUT; pyc = HINGE_Y + 0.5
    cover = cover.cut(cyl(pxc, pyc, CLIP_PIN_D + 0.25, Z_FACE - 1, Z_TOP + 1))                       # the hole the pin clicks into
    xa, xb = min(x0_, pxc - 3.0), max(x1_, pxc + 3.0)
    c_ = slab(xa, HINGE_Y - 1.5, xb, HINGE_Y + 3.2, Z_AX + 2.6, Z_AX + 4.2).edges("|Z").fillet(1.0)                          # cap
    c_ = c_.union(slab(max(x0_ + 0.1, NECK_X[0] - EAR - 0.6), HINGE_Y - 1.25, min(x1_ - 0.1, NECK_X[1] + EAR + 0.6), HINGE_Y + 1.25, Z_AX + 1.9, Z_AX + 2.7))   # tongue in the notch, just clear of the ear
    pin = cyl(pxc, pyc, CLIP_PIN_D, Z_FACE - 0.15, Z_AX + 2.7)
    pin = pin.union(cq.Workplane("XY").circle(1.7).workplane(offset=1.5).circle(CLIP_BARB_D / 2).loft().translate((pxc, Y(pyc), Z_FACE - 1.65)))
    pin = pin.cut(slab(pxc - 3, pyc - CLIP_SLOT / 2, pxc + 3, pyc + CLIP_SLOT / 2, Z_FACE - 2, Z_TOP + 1.5))
    c_ = c_.union(pin)
    clip_parts.append(c_); clips = c_ if clips is None else clips.union(c_)
# the lid, shut (for the pictures and the fit check) and as a flat outline for the laser
lid2d = (cq.Workplane("XY").rect(LID_X[1] - LID_X[0], LID_Y1 - (HINGE_Y + 3.0)).extrude(LID_T).edges("|Z").fillet(1.5)
         .translate(((LID_X[0] + LID_X[1]) / 2, Y((LID_Y1 + HINGE_Y + 3.0) / 2), 0)))
lid2d = lid2d.union(slab(NECK_X[0], HINGE_Y - 1.5, NECK_X[1], HINGE_Y + 3.5, 0, LID_T))
lid2d = lid2d.union(slab(NECK_X[0] - EAR, HINGE_Y - 1.2, NECK_X[1] + EAR, HINGE_Y + 1.2, 0, LID_T))
micx, micy = part("U16")["x"], part("U16")["y"]
if HINGE_Y - 3 < micy + 3.0 and NECK_X[0] < micx + 2.4 and NECK_X[1] > micx - 2.4:   # a notch, only if the lid would reach the microphone port
    lid2d = lid2d.cut(slab(micx - 2.4, HINGE_Y - 3, micx + 2.4, micy + 3.0, -1, LID_T + 1).edges("|Z").fillet(1.0))   # the notch at the microphone port
lid = lid2d.translate((0, 0, Z_TOP))
lid_open = lid.rotate((0, Y(HINGE_Y), Z_AX), (1, Y(HINGE_Y), Z_AX), -135)
lid_90 = lid.rotate((0, Y(HINGE_Y), Z_AX), (1, Y(HINGE_Y), Z_AX), -90)      # sticking straight out: how it goes in
lid_pre = lid_90.translate((0, 0, 7.0))                                       # ... held in front of the posts, before it is pressed in
# engraved labels under the buttons
def label(txt, x, y, halign):
    return cq.Workplane("XY").text(txt, LABEL_H, LABEL_DEPTH + 0.5, halign=halign, valign="center", kind="bold").translate((x, Y(y), Z_TOP - LABEL_DEPTH))
LABEL_Y = TALK_AT[1] + CAP_DY + BTN_H / 2 + 3.0   # every button's name is written under it, on one line
def small(txt, x, y): return cq.Workplane("XY").text(txt, LABEL_H, LABEL_DEPTH + 0.5, halign="center", valign="center", kind="bold").translate((x, Y(y), Z_TOP - LABEL_DEPTH))
engraved = [small("LOCKDOWN", rx + 1.5, LABEL_Y), label("TALK", TALK_AT[0], LABEL_Y, "center"), label("OFFICE", OFFICE_AT[0], LABEL_Y, "center")]
if VARIANT == "office": engraved.append(small("QUIET", QUIET_AT[0] - 10.8, QUIET_AT[1] - 4.6))   # above and left of its cap, clear of the hinge post
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
# vents for the PoE module, which is on the back of the board: slots in the bottom wall behind the board to let
# air in, slots in the top wall to let the warm air out. The face has none.
if BACK_H > 9:
    for i in range(5):
        vx = u14[0] + 1 + i * 4.0
        cover = cover.cut(slab(vx, YB_IN - 1, vx + 2.0, YB + 1, Z_FLOOR + 5, -PCB_T - 3))
        cover = cover.cut(slab(41 + i * 4.0, YT - 1, 43 + i * 4.0, YT_IN + 1, Z_FLOOR + 5, -PCB_T - 3))
if BAY < 30:  # no bay: the cable leaves through the bottom wall
    cover = cover.cut(slab(j9[0], YB_IN - 1, j9[2], YB + 1, -1, 14.5))
# side openings: USB-C (right, wide enough for the plug's body), microSD and audio jack (left)
USB_OPEN = False  # USB-C is reached only with the cover off
if USB_OPEN and part("J1"):
    b1 = box_of("J1", 2.5); cover = cover.cut(slab(XR_IN - 1, b1[1], XR + 1, b1[3], -2.5, 6.0))
if SD_OPEN and part("CARD1"):
    b1 = box_of("CARD1", 0.5); cover = cover.cut(slab(XL - 1, b1[1], XL_IN + 1, b1[3], -0.5, 3.0))
if part("CN1"):
    # the audio jack now sits 15 mm inside the left wall, so the opening is big enough for the plug's body to go in
    b1 = box_of("CN1", 1.0); cover = cover.cut(slab(XL - 1, b1[1], XL_IN + 1, b1[3], -2.5, 6.9).edges("|X").fillet(2.0))

# ---- camera shutter: the slot in the left wall that the flag comes out of ----
cover = cover.cut(slab(POD_X0 - WALL - 1, SLOT_Y0, POD_X0 + 0.1, POD_Y[1], Z_FACE - SHUT_GAP, Z_FACE))
# finger recess under the lid's lower edge
# (the finger scoop is cut with the lid's recess, above)
# the paddle (print one): hub on the servo's shaft, arm, disc
import math
ARM_L = math.hypot(CAM_AT[0] - PIVOT[0], CAM_AT[1] - PIVOT[1])
PZ1 = Z_FACE - 0.4; PZ0 = PZ1 - PAD_T
paddle = cq.Workplane("XY").circle(PAD_D / 2).extrude(PAD_T).translate((ARM_L, 0, PZ0))
paddle = paddle.union(cq.Workplane("XY").box(ARM_L, ARM_W, PAD_T, centered=(False, True, False)).translate((0, 0, PZ0)))
paddle = paddle.union(cq.Workplane("XY").circle(4.5).extrude(PZ1 - (SV_Z0 + SERVO_TOP - 3.1)).translate((0, 0, SV_Z0 + SERVO_TOP - 3.1)))
paddle = paddle.cut(cq.Workplane("XY").circle(2.4).extrude(5.0).translate((0, 0, SV_Z0 + SERVO_TOP - 3.5)))   # socket for the shaft's spline (fit it to yours)
A0 = math.degrees(math.atan2(Y(CAM_AT[1]) - Y(PIVOT[1]), CAM_AT[0] - PIVOT[0]))
def paddle_at(turn):   # 0 = closed, 90 = open, 180 = flag out; it turns downwards, away from the audio jack
    return paddle.rotate((0, 0, 0), (0, 0, 1), A0 - turn).translate((PIVOT[0], Y(PIVOT[1]), 0))
pad_closed, pad_open, pad_flag = paddle_at(0), paddle_at(90), paddle_at(180)
servo = slab(SV_X0, SV_Y0, SV_X1, SV_Y0 + SERVO_L, SV_Z0, SV_Z0 + SERVO_BODY_H)
servo = servo.union(slab(SV_X0, SV_Y0 - SERVO_EAR, SV_X1, SV_Y0 + SERVO_L + SERVO_EAR, SV_Z0 + SERVO_EAR_Z, SV_Z0 + SERVO_EAR_Z + SERVO_EAR_T))
servo = servo.union(cyl(PIVOT[0], PIVOT[1], 11.8, SV_Z0 + SERVO_BODY_H, SV_Z0 + SERVO_BODY_H + 4.0)).union(cyl(PIVOT[0], PIVOT[1], 4.6, SV_Z0, SV_Z0 + SERVO_TOP))

# ---- use card holder on the face: two side rails and a bottom rail that grip the card's edges. The card slides
# down from the top; a gap in the bottom rail lets a finger push it back up to read its other side. -------------
cx0, cx1 = W / 2 + CARD_DX - CARD_W / 2 - 0.3, W / 2 + CARD_DX + CARD_W / 2 + 0.3
cy0, cy1 = CARD_TOP, CARD_TOP + CARD_H + 0.3
zt = Z_LOW + CARD_SLOT + 0.9
holder = slab(cx0 - CARD_RAIL, cy0, cx0, cy1 + CARD_RAIL, Z_LOW - 0.2, zt)
holder = holder.union(slab(cx1, cy0, cx1 + CARD_RAIL, cy1 + CARD_RAIL, Z_LOW - 0.2, zt))
holder = holder.union(slab(cx0 - CARD_RAIL, cy1, cx1 + CARD_RAIL, cy1 + CARD_RAIL, Z_LOW - 0.2, zt))
holder = holder.union(slab(cx0, cy0, cx0 + CARD_LIP, cy1, Z_LOW + CARD_SLOT, zt))
holder = holder.union(slab(cx1 - CARD_LIP, cy0, cx1, cy1, Z_LOW + CARD_SLOT, zt))
holder = holder.union(slab(cx0, cy1 - CARD_LIP, cx1, cy1, Z_LOW + CARD_SLOT, zt))
holder = holder.edges("|Z").fillet(0.9)
holder = holder.cut(cyl(W / 2 + CARD_DX, cy1 + CARD_RAIL, 18.0, Z_LOW, zt + 1))   # finger gap in the bottom rail
cover = cover.union(holder)
use_card = slab(cx0 + 0.3, cy1 - 0.3 - CARD_H, cx1 - 0.3, cy1 - 0.3, Z_LOW + 0.1, Z_LOW + 0.6)   # the card itself, for the pictures only

# ---- pockets that a part slides into, flat along the inside of the face. Rails hold the part against the face; a
# stop wall ends the pocket; at the open end two thin posts with a small bump spring apart as the part passes and
# close behind it. Everything prints without support with the face on the bed: walls and posts stand straight up, and
# the rails' lips and the bumps' undersides are cut at 45 degrees. To take a part out, bend one post aside with a
# fingernail and slide it back. THE POSTS BEND 0.3 TO 0.4 mm: PRINT IN PETG (PLA may crack them).
def prism_x(pts, x0, x1):  # a profile of (KiCad y, z) points, from x0 to x1
    return cq.Workplane("YZ").workplane(offset=x0).polyline([(Y(y_), z_) for y_, z_ in pts]).close().extrude(x1 - x0)
def prism_y(pts, y0, y1):  # a profile of (x, z) points, from KiCad y0 to y1
    return cq.Workplane("XZ").workplane(offset=y0).polyline(pts).close().extrude(y1 - y0)
def prism_z(pts, z0, z1):  # a profile of (x, KiCad y) points, from z0 to z1
    return cq.Workplane("XY").workplane(offset=z0).polyline([(x_, Y(y_)) for x_, y_ in pts]).close().extrude(z1 - z0)

# ---- box speaker in the lower left: it slides in from the right, along the face, until it meets the left wall (foam
# tape or a gasket between speaker and face seals it: use a kind that slides), and a grille of slots in front of it ----
sx0, sx1 = SPK2_AT[0] - SPK2_W / 2 - 0.2, SPK2_AT[0] + SPK2_W / 2 + 0.2
sy0, sy1 = SPK2_AT[1] - SPK2_H / 2 - 0.2, SPK2_AT[1] + SPK2_H / 2 + 0.2
Z_IN = Z_FACE + JACK_BUMP                       # inside of the raised lower face
SPK_LIP, SPK_RAIL, SPK_POST_T, SPK_BUMP = 1.5, 1.6, 0.9, 0.6   # lip over the speaker's back, rail and post thickness, bump height
zs = Z_IN - SPK2_T - 0.3                        # the speaker's back, with a little play
pocket = slab(max(sx0 - 1.5, XL_IN), sy0 - SPK_RAIL, sx0, sy1 + SPK_RAIL, Z_IN - 5.0, Z_IN + 0.1)      # the stop, on the left
bumps = []
x_rail = sx1 - 4.6                              # the rails end here; the posts stand beyond
for ye, sg in ((sy0, 1), (sy1, -1)):            # sg +1: the speaker lies below this rail (larger KiCad y)
    pocket = pocket.union(slab(max(sx0 - 1.5, XL_IN), min(ye, ye - sg * SPK_RAIL), x_rail, max(ye, ye - sg * SPK_RAIL), zs - SPK_LIP, Z_IN + 0.1))
    pocket = pocket.union(prism_x([(ye, zs), (ye + sg * SPK_LIP, zs - SPK_LIP), (ye, zs - SPK_LIP)], sx0, x_rail))
    # the post, and its bump just past the speaker's right edge: a long slope on the way in, a steep face on the way out
    pocket = pocket.union(slab(sx1 - 4.0, min(ye, ye - sg * SPK_POST_T), sx1 + 3.0, max(ye, ye - sg * SPK_POST_T), zs - 0.6, Z_IN + 0.1))
    bump = prism_z([(sx1 + 0.1, ye), (sx1 + 0.3, ye + sg * SPK_BUMP), (sx1 + 2.8, ye)], zs - 0.6, zs + 1.6)
    bump = bump.intersect(prism_x([(ye, zs + 1.6), (ye + sg * SPK_BUMP, zs + 1.6 - SPK_BUMP), (ye + sg * SPK_BUMP, zs - 0.6), (ye, zs - 0.6)], sx1, sx1 + 3.0))
    pocket = pocket.union(bump); bumps.append(bump)
cover = cover.union(pocket)
g2 = None
for i in range(-3, 4):
    s_ = slab(SPK2_AT[0] - 9.5, SPK2_AT[1] + i * 4.0 - 1.0, SPK2_AT[0] + 9.5, SPK2_AT[1] + i * 4.0 + 1.0, Z_IN - 1, Z_LOW + 1).edges("|Z").fillet(0.9)
    g2 = s_ if g2 is None else g2.union(s_)
cover = cover.cut(g2)
speaker2 = slab(sx0 + 0.2, sy0 + 0.2, sx1 - 0.2, sy1 - 0.2, Z_IN - SPK2_T, Z_IN)
speaker_path = speaker2.union(speaker2.translate((SPK2_W - 1.0, 0, 0)))    # the room it needs on its way in

# ---- people sensor: it slides down from above, its two short edges in grooves, until it stands on the bottom wall.
# The grooves hold its board RADAR_GAP off the face; the two posts at the top close over its upper corners. ----
radar = radar_path = None
if RADAR:
    qx0, qx1 = RADAR_AT[0] - RADAR_W / 2 - 0.2, RADAR_AT[0] + RADAR_W / 2 + 0.2
    qy0, qy1 = RADAR_AT[1] - RADAR_H / 2 - 0.2, RADAR_AT[1] + RADAR_H / 2 + 0.2
    zq1 = Z_IN - RADAR_GAP                  # the module's front
    zq0 = zq1 - RADAR_PCB - 0.3             # its back, with a little play
    RD_LIP, RD_POST_T, RD_BUMP = 0.8, 0.8, 0.55   # the groove's depth, post thickness, bump height (0.2 of it is the play)
    y_rail = qy0 + 3.5                      # the grooves start here; the posts stand above
    ch = slab(qx0 - 1.5, qy1, qx1 + 1.5, qy1 + 1.5, zq0 - RD_LIP, Z_IN + 0.1)             # the stop, at the bottom
    for xe, sg in ((qx0, 1), (qx1, -1)):    # sg +1: the module lies right of this rail
        ch = ch.union(slab(min(xe, xe - sg * 1.5), y_rail, max(xe, xe - sg * 1.5), qy1 + 1.5, zq0 - RD_LIP, Z_IN + 0.1))
        ch = ch.union(slab(min(xe, xe + sg * RD_LIP), y_rail, max(xe, xe + sg * RD_LIP), qy1, zq1, Z_IN + 0.1))      # the ledge its front rests on
        ch = ch.union(prism_y([(xe, zq0), (xe + sg * RD_LIP, zq0 - RD_LIP), (xe, zq0 - RD_LIP)], y_rail, qy1))       # the lip over its back
        ch = ch.union(slab(min(xe, xe - sg * RD_POST_T), qy0 - 3.0, max(xe, xe - sg * RD_POST_T), qy0 + 3.0, zq0 - RD_LIP, Z_IN + 0.1))   # the post
        bump = prism_z([(xe, qy0 - 0.1), (xe + sg * RD_BUMP, qy0 - 0.3), (xe, qy0 - 2.8)], zq0 - 0.4, zq1 + 1.2)
        bump = bump.intersect(prism_y([(xe, zq1 + 1.2), (xe + sg * RD_BUMP, zq1 + 1.2 - RD_BUMP), (xe + sg * RD_BUMP, zq0 - 0.4), (xe, zq0 - 0.4)], qy0 - 3.0, qy0))
        ch = ch.union(bump); bumps.append(bump)
    cover = cover.union(ch)
    if RADAR_THIN < FACE:               # the thin patch in front of the sensor, cut from the inside (nothing shows outside)
        cover = cover.cut(slab(qx0 + RD_LIP + 0.6, qy0 + 1.0, qx1 - RD_LIP - 0.6, qy1 - 1.0, Z_IN - 0.01, Z_IN + FACE - RADAR_THIN))
    radar = slab(qx0 + 0.2, qy0 + 0.2, qx1 - 0.2, qy1 - 0.2, zq1 - RADAR_PCB, zq1)
    radar_path = radar.union(radar.translate((0, RADAR_H - 1.0, 0)))                     # the room it needs on its way in
    radar = radar.union(slab(RADAR_AT[0] - 2.5, RADAR_AT[1] - 2.5, RADAR_AT[0] + 2.5, RADAR_AT[1] + 2.5, zq1, zq1 + 0.9))   # its chip
# ---- looks: an engraved ring round the speaker and round the status light, a bevel on the camera opening ------
def ring(x, y, d_out, width):
    return cyl(x, y, d_out, Z_TOP - LABEL_DEPTH, Z_TOP + 1).cut(cyl(x, y, d_out - 2 * width, Z_TOP - LABEL_DEPTH - 1, Z_TOP + 2))
rings = ([ring(part("D9")["x"], part("D9")["y"], 7.0, 0.8)] if part("D9") else [])
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
zb = min(SW_TOP + 1.5, BODY_Z0 - 0.01)
legs = slab(-R, -LEG_HALF_W, R, LEG_HALF_W, zb, BODY_Z0)                  # bridge over the switch
for s in (-1, 1):                                                                   # two legs down to just above the board
    legs = legs.union(slab(min(s * LEG_X0, s * R), -LEG_HALF_W, max(s * LEG_X0, s * R), LEG_HALF_W, SW_TRAVEL + STOP_EXTRA, zb))
cap = cap.union(legs)
# the red cap (print one, in red): round, with the same stem and legs
red = cyl(0, 0, RED_D, Z_FACE - 0.3, Z_TOP - RED_DOWN).faces(">Z").edges().fillet(0.6)
red = red.union(cyl(0, 0, RED_FL, BODY_Z0, Z_FACE - 0.3)).union(cyl(0, 0, 4.0, SW_TOP, BODY_Z0)).union(legs)
red_at = red.translate((rx, Y(ry), 0))
caps = None
for bx, by in btn:
    c = cap.translate((bx, Y(by), 0)); caps = c if caps is None else caps.union(c)
quiet = quiet_at = keystone = None
if VARIANT == "office":   # the QUIET cap (print one): small and round, with the same stem and legs
    quiet = cyl(0, 0, QUIET_D, Z_FACE - 0.3, Z_TOP + QUIET_PROUD).faces(">Z").edges().fillet(0.5)
    quiet = quiet.union(cyl(0, 0, QUIET_FL, BODY_Z0, Z_FACE - 0.3)).union(cyl(0, 0, 4.0, SW_TOP, BODY_Z0)).union(legs)
    quiet_at = quiet.translate((QUIET_AT[0], Y(QUIET_AT[1]), 0))
    caps = caps.union(quiet_at)
if VARIANT == "mobile":   # the coupler with the inner lead's plug and boot above it, for the fit check and the pictures
    keystone = slab(KEY_X - 7.3, YB - KEY_L, KEY_X + 7.3, YB, KEY_ZC - 8.0, KEY_ZC + 8.0)
    keystone = keystone.union(slab(KEY_X - 6.0, YB - KEY_L - 26.0, KEY_X + 6.0, YB - KEY_L, KEY_ZC - 5.0, KEY_ZC + 5.0))

# ---- a stand-in for the board, to check the fit ----------------------------------------------------------
board = slab(0, 0, W, H, -PCB_T, 0)
for hx, hy in B["holes"]: board = board.cut(cyl(hx, hy, 3.2, -PCB_T - 1, 1))
jz = (-PCB_T - JACK_H, 2.7) if J9_BACK else (-PCB_T - 2.7, JACK_H)
board = board.union(slab(j9[0] + 1, j9[1] + 1, j9[2] - 1, j9[3] - 1, jz[0], jz[1]))
board = board.union(slab(u14[0], u14[1], u14[2], u14[3], -PCB_T - POE_H, -PCB_T))
if BATTERY:
    bt = box_of("BT1"); board = board.union(slab(bt[0], bt[1], bt[2], bt[3], -PCB_T - BT_H, -PCB_T))
dd = box_of("DISP1"); board = board.union(slab(dd[0], dd[1], dd[2], dd[3], 0, DISP_TOP))
for bx, by in btn + [RED_AT] + ([QUIET_AT] if VARIANT == "office" else []): board = board.union(slab(bx - 2.5, by - 2.5, bx + 2.5, by + 2.5, 0, SW_TOP))
if BAY >= 30:  # the RJ45 plug with its boot
    board = board.union(slab(j9[0] + 4, j9[3] - 1, j9[2] - 4, j9[3] + 24, *((-PCB_T - 8.4, -PCB_T - 0.4) if J9_BACK else (0.5, 8.4))))

out = os.path.join(here, "out_clam" if VARIANT == "room" else "out_clam_" + VARIANT); os.makedirs(out, exist_ok=True)
for name, s in (("clam_back", back), ("clam_cover", cover), ("clam_button", cap), ("clam_buttons", caps), ("board_standin", board), ("use_card", use_card), ("paint", paint),
                ("clam_red_button", red), ("red_at", red_at), ("lid", lid), ("lid_open", lid_open), ("lid_90", lid_90), ("lid_pre", lid_pre),
                ("lid_clip_left", clip_parts[0]), ("lid_clip_right", clip_parts[1]), ("clips_at", clips), ("clips_pre", clips.translate((0, 0, 9.0))),
                ("speaker_standin", speaker2), ("radar_standin", radar), ("clam_quiet_button", quiet), ("keystone_standin", keystone), ("shutter_paddle", paddle), ("pad_closed", pad_closed), ("pad_open", pad_open), ("pad_flag", pad_flag), ("servo_standin", servo)):
    if s is None: continue
    if name not in ("clam_buttons", "use_card", "paint", "red_at", "lid_open", "lid_90", "lid_pre", "clips_at", "clips_pre", "pad_closed", "pad_open", "pad_flag"): cq.exporters.export(s, os.path.join(out, name + ".step"))
    cq.exporters.export(s, os.path.join(out, name + ".stl"), tolerance=0.05)
cq.exporters.export(lid2d.faces("<Z"), os.path.join(out, "lid.dxf"))   # the lid's outline, for the laser cutter (mm)
cover_nb = cover
for b_ in bumps: cover_nb = cover_nb.cut(b_)   # the cover without the bumps, to check that nothing else is in a part's way
bb = back.union(cover).val().BoundingBox()
print("outside: %.1f x %.1f x %.1f mm" % (bb.xlen, bb.ylen, bb.zlen))
for name, a, b in (("cover/board", cover, board), ("cover/back", cover, back), ("back/board", back, board), ("caps/cover", caps, cover), ("caps/board", caps, board),
                   ("red/cover", red_at, cover), ("red/board", red_at, board), ("quiet/lid", quiet_at, lid), ("quiet/lid open", quiet_at, lid_open), ("quiet/clips", quiet_at, clips),
                   ("coupler/cover", keystone, cover), ("coupler/back", keystone, back), ("coupler/board", keystone, board), ("coupler/radar", keystone, radar), ("coupler/speaker", keystone, speaker2), ("lid/cover", lid, cover), ("lid open/cover", lid_open, cover), ("lid/red", lid, red_at), ("clips/cover", clips, cover), ("clips/lid", clips, lid), ("clips/lid open", clips, lid_open),
                   ("speaker/cover", speaker2, cover), ("speaker/back", speaker2, back), ("speaker/board", speaker2, board), ("speaker way in/cover", speaker_path, cover_nb), ("radar/cover", radar, cover), ("radar/back", radar, back), ("radar/board", radar, board), ("radar way in/cover", radar_path, cover_nb), ("servo/cover", servo, cover), ("servo/back", servo, back), ("servo/board", servo, board),
                   ("paddle closed/cover", pad_closed, cover), ("paddle open/cover", pad_open, cover), ("paddle flag/cover", pad_flag, cover),
                   ("paddle closed/board", pad_closed, board), ("paddle open/board", pad_open, board), ("paddle/servo", pad_closed, servo)):
    if a is None or b is None: continue
    v = a.intersect(b).val().Volume() if a.intersect(b).vals() else 0.0
    print("overlap %-22s %.2f mm3" % (name, v))

# ---- preview pictures ------------------------------------------------------------------------------------
import vtk
def clear(name, dz=0.0):  # the acrylic lid
    a = actor(name, (0.75, 0.88, 0.95), dz); a.GetProperty().SetOpacity(0.38); return a
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
grey, blue, green, red_c = (0.82, 0.82, 0.80), (0.55, 0.66, 0.80), (0.20, 0.50, 0.25), (0.80, 0.25, 0.20)
beige, white = (0.87, 0.80, 0.66), (0.97, 0.97, 0.95)
black, dark = (0.05, 0.05, 0.05), (0.30, 0.31, 0.34)
def card_face():  # the card's printed front (make_card.py draws it) laid on the card
    png = os.path.join(here, "card", "use_card_front.png")
    if not os.path.exists(png): return actor("use_card", white)
    r = vtk.vtkPNGReader(); r.SetFileName(png)
    t = vtk.vtkTexture(); t.SetInputConnection(r.GetOutputPort()); t.InterpolateOn()
    x0_, x1_, yb_, yt_, z_ = cx0 + 0.3, cx1 - 0.3, Y(cy1 - 0.3), Y(cy1 - 0.3 - CARD_H), Z_LOW + 0.62
    pl = vtk.vtkPlaneSource(); pl.SetOrigin(x0_, yb_, z_); pl.SetPoint1(x1_, yb_, z_); pl.SetPoint2(x0_, yt_, z_)
    m = vtk.vtkPolyDataMapper(); m.SetInputConnection(pl.GetOutputPort())
    a = vtk.vtkActor(); a.SetMapper(m); a.SetTexture(t); a.GetProperty().LightingOff()
    return a
def black_paint():  # flat black, whatever the light
    a = actor("paint", black); a.GetProperty().LightingOff(); return a
BOARD_COLOR = (0.08, 0.08, 0.09)   # black solder mask; green is (0.20, 0.50, 0.25)
front = lambda: [clear("lid"), actor("clam_back", grey), actor("board_standin", BOARD_COLOR), actor("clam_cover", beige), actor("clam_buttons", dark), actor("red_at", red_c), actor("use_card", white), card_face(), black_paint()]
shot("preview_front.png", front(), (60, -120, 240))
shot("preview_face.png", front(), (0, -30, 300))
shot("preview_step2.png", [actor("clam_back", grey), actor("board_standin", green)], (90, -200, 200), (0, 0, 1))
shot("preview_exploded.png", [actor("clam_back", grey), actor("board_standin", green, 30), actor("clam_buttons", dark, 70), actor("red_at", red_c, 70), clear("lid", 95), actor("clam_cover", blue, 80)], (190, -260, 170), (0, 0, 1))
shot("preview_inside.png", [actor("clam_cover", blue), actor("clam_buttons", dark), actor("red_at", red_c)], (-60, -160, -220))
def shot_at(fname, actors, focal, pos, up=(0, 0, 1)):
    ren = vtk.vtkRenderer(); ren.SetBackground(1, 1, 1)
    for a in actors: ren.AddActor(a)
    w = vtk.vtkRenderWindow(); w.SetOffScreenRendering(1); w.SetSize(1200, 900); w.AddRenderer(ren)
    c = ren.GetActiveCamera(); c.SetFocalPoint(*focal); c.SetPosition(focal[0] + pos[0], focal[1] + pos[1], focal[2] + pos[2]); c.SetViewUp(*up)
    ren.ResetCameraClippingRange(); w.Render()
    f = vtk.vtkWindowToImageFilter(); f.SetInput(w); f.Update()
    p = vtk.vtkPNGWriter(); p.SetFileName(os.path.join(out, fname)); p.SetInputConnection(f.GetOutputPort()); p.Write()
shot_at("preview_buttons.png", [actor("board_standin", green), actor("clam_buttons", dark), actor("red_at", red_c)], (28, -26, 6), (10, -60, 22))
shot_at("preview_labels.png", [actor("clam_back", grey), actor("clam_cover", blue), actor("clam_buttons", dark), actor("red_at", red_c)], (50, -30, Z_TOP), (0, -40, 150), (0, 1, 0))
shot_at("preview_card.png", [actor("clam_cover", beige), actor("use_card", white)], (50, -122, Z_LOW), (30, -70, 90), (0, 1, 0))
shot_at("preview_red.png", [actor("clam_cover", beige), actor("clam_buttons", dark), actor("red_at", red_c), black_paint(), clear("lid")], (rx + 4, Y(ry + 2), Z_TOP), (-45, -60, 70), (0, 1, 0))
for n_, l_, c_ in ((1, "lid_pre", None), (2, "lid_90", "clips_pre"), (3, "lid_90", "clips_at"), (4, "lid", "clips_at")):   # fitting the lid, in four steps
    shot_at("preview_lid_fit%d.png" % n_, [actor("clam_cover", beige), actor("clam_buttons", dark), actor("red_at", red_c), black_paint(), clear(l_)] + ([actor(c_, beige)] if c_ else []),
            (rx + 2, Y(ry - 2), Z_TOP + 4), (-50, -45, 55), (0, 1, 0))
shot_at("preview_red_open.png", [actor("clam_cover", beige), actor("clips_at", beige), actor("clam_buttons", dark), actor("red_at", red_c), black_paint(), clear("lid_open")], (rx + 4, Y(ry + 2), Z_TOP), (-45, -60, 70), (0, 1, 0))
# the shutter: the cover seen through, with the paddle closed, open and out as a flag
def glass(name):
    a = actor(name, beige); a.GetProperty().SetOpacity(0.35); return a
orange = (0.95, 0.55, 0.10)
for tag in ("closed", "open", "flag"):
    shot_at("preview_shutter_%s.png" % tag, [actor("clam_back", grey), actor("board_standin", BOARD_COLOR), actor("servo_standin", blue), actor("pad_" + tag, orange), glass("clam_cover"), actor("clam_buttons", dark), actor("red_at", red_c)],
            (8, Y(62), 0), (-30, -25, 190), (0, 1, 0))
    shot_at("preview_shutter_side_%s.png" % tag, [actor("clam_back", grey), actor("clam_cover", beige), actor("pad_" + tag, orange), actor("clam_buttons", dark), actor("red_at", red_c), black_paint(), clear("lid")],
            (20, Y(62), 0), (-150, -40, 110), (0, 0, 1))
# the whole case with a see-through front, to look at the inside and the outside together
def seen(pad="pad_closed"):
    return [actor("clam_back", grey), actor("board_standin", BOARD_COLOR), actor("servo_standin", blue), actor("speaker_standin", (0.15, 0.15, 0.17)), actor(pad, orange), actor("clam_buttons", dark),
            actor("red_at", red_c), actor("use_card", white), glass("clam_cover"), clear("lid")]
shot("preview_glass_face.png", seen(), (0, -30, 300))
shot("preview_glass_front.png", seen(), (60, -120, 240))
shot("preview_glass_left.png", seen("pad_flag"), (-230, -40, 150), (0, 0, 1))
shot("preview_glass_right.png", seen(), (230, -60, 150), (0, 0, 1))
shot("preview_bottom.png", [actor("clam_back", grey), actor("clam_cover", blue)], (40, -260, 60), (0, 0, 1))
