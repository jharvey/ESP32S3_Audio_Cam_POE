# Schematic: front-panel buttons move from the TM1637 key scan to a second TCA9555 (U22, I2C 0x21).
#   * U22 (copy of U21's symbol) in a new block at the sheet's free area (x 20-135, y 135-195);
#     A0 = +3V3, A1/A2 = COM -> address 0x21; INT shares /EXP.INT with U21; 100 nF decoupling cap.
#   * PB1, PB2, PB4, PB5, PB6-PB12 move into the block (same references and IDs, so the board
#     footprints stay where they are): pin 2 -> COM, pin 4 -> its own U22 input (internal pull-up).
#   * R109-R115 (1k anti-ghosting resistors for the key scan) are deleted; old BTNn labels removed.
#   The old key-scan wires near the display stay as harmless leftovers on the SEG / K nets.
import re, sys, uuid

P = "ESP32S3_Audio_Cam_POE.kicad_sch"
s = open(P, encoding="utf-8", newline="").read().replace("\r\n", "\n")  # git may check it out with CRLF
assert '"U22"' not in s, "already done"

def U():
    return str(uuid.uuid4())

# An earlier edit glued some symbols onto the previous one's closing ")" without a line break.
s = s.replace("\n\t)\t(symbol\n\t\t(lib_id", "\n\t)\n\t(symbol\n\t\t(lib_id")

def span(ref):
    i = s.find('(property "Reference" "%s"' % ref)
    assert i > 0, ref
    st = s.rfind('\n\t(symbol\n\t\t(lib_id', 0, i)
    en = s.find('\n\t)', i) + 3
    # the block must be this symbol alone: exactly one Reference property in it
    assert s[st:en].count('(property "Reference"') == 1, ref
    return st, en

def block(ref):
    st, en = span(ref)
    return s[st:en]

def delete(ref):
    global s
    st, en = span(ref)
    s = s[:st] + s[en:]

def newuuids(b):
    for u in set(re.findall(r'\(uuid "([0-9a-f-]+)"\)', b)):
        b = b.replace(u, U())
    return b

def place(b, x, y):
    """Move a symbol block so its origin is (x, y); its property texts move with it."""
    m = re.search(r'\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)', b)
    dx, dy = x - float(m.group(1)), y - float(m.group(2))
    return re.sub(r'\(at ([-\d.]+) ([-\d.]+)( [-\d.]+)?\)',
                  lambda k: "(at %.2f %.2f%s)" % (float(k.group(1)) + dx, float(k.group(2)) + dy, k.group(3) or ""), b)

def setref(b, old, new):
    b = b.replace('(property "Reference" "%s"' % old, '(property "Reference" "%s"' % new)
    return b.replace('(reference "%s")' % old, '(reference "%s")' % new)

PWR = [max(int(v) for v in re.findall(r'"#PWR0*(\d+)"', s)) + 1]
def first_ref(lib_id, prefix):
    # complete symbol blocks with this lib_id; the reference must come from inside the block
    for m in re.finditer(r'\n\t\(symbol\n\t\t\(lib_id "%s"\)[\s\S]*?\n\t\)' % re.escape(lib_id), s):
        refs = re.findall(r'\(property "Reference" "([^"]+)"', m.group(0))
        if len(refs) == 1 and refs[0].startswith(prefix):
            return refs[0]
    raise SystemExit("no template for " + lib_id)
def first_block(lib_id):
    return block(first_ref(lib_id, "#PWR"))
VCC_T = first_block("power:+3V3")
COM_T = block("#PWR0130")
assert '"COM"' in COM_T and '"+3V3"' in VCC_T

def power(tpl, x, y, rot="0"):
    ref = re.search(r'\(property "Reference" "(#PWR\d+)"', tpl).group(1)
    b = newuuids(setref(place(tpl, x, y), ref, "#PWR%04d" % PWR[0]))
    b = re.sub(r'^(\n\t\(symbol\s*\(lib_id "[^"]+"\)\s*\(at [-\d.]+ [-\d.]+) [-\d.]+\)', lambda k: k.group(1) + " %s)" % rot, b)
    PWR[0] += 1
    return b

LABEL = ('\n\t(label "%s"\n\t\t(at %.2f %.2f %d)\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n'
         '\t\t\t(justify %s bottom)\n\t\t)\n\t\t(uuid "%s")\n\t)')
def label(name, x, y, left=True):
    return LABEL % (name, x, y, 0 if left else 180, "left" if left else "right", U())

def wire(x0, y0, x1, y1):
    return ('\n\t(wire\n\t\t(pts\n\t\t\t(xy %.2f %.2f) (xy %.2f %.2f)\n\t\t)\n\t\t(stroke\n\t\t\t(width 0)\n\t\t\t(type default)\n'
            '\t\t)\n\t\t(uuid "%s")\n\t)' % (x0, y0, x1, y1, U()))

new = ""
# ---- U22 ------------------------------------------------------------------------------------
UX, UY = 45.72, 167.64
u22 = newuuids(setref(place(block("U21"), UX, UY), "U21", "U22"))
new += u22
def upin(lx, ly):  # rot 0, no mirror: sheet y grows downward
    return UX + lx, UY - ly
# inputs: P00.. for the buttons, the rest spare
BUTTONS = [("PB1", "BTN.TALK1"), ("PB2", "BTN.MODE"), ("PB4", "BTN.UP"), ("PB5", "BTN.DOWN"), ("PB6", "BTN.SETUP"),
           ("PB7", "BTN.TALK7"), ("PB8", "BTN.TALK8"), ("PB9", "BTN.TALK9"), ("PB10", "BTN.TALK10"),
           ("PB11", "BTN.TALK11"), ("PB12", "BTN.TALK12")]
PORT = [(17.78, 20.32 - 2.54 * i) for i in range(8)] + [(17.78, -2.54 - 2.54 * i) for i in range(8)]
for i, (pb, name) in enumerate(BUTTONS):
    x, y = upin(*PORT[i])
    new += wire(x, y, x + 5.08, y) + label(name, x + 5.08, y)
x, y = upin(-17.78, 20.32); new += wire(x, y, x - 5.08, y) + label("I2C.SDA", x - 5.08, y, False)
x, y = upin(-17.78, 17.78); new += wire(x, y, x - 5.08, y) + label("I2C.SCL", x - 5.08, y, False)
x, y = upin(-17.78, 15.24); new += wire(x, y, x - 5.08, y) + label("EXP.INT", x - 5.08, y, False)
x, y = upin(-17.78, -20.32); new += wire(x, y, x - 5.08, y) + power(VCC_T, x - 5.08, y)        # A0 = 1 -> 0x21
x, y = upin(-17.78, -17.78); new += wire(x, y, x - 7.62, y) + power(COM_T, x - 7.62, y)        # A1 = 0
x, y = upin(-17.78, -15.24); new += wire(x, y, x - 10.16, y) + power(COM_T, x - 10.16, y)      # A2 = 0
x, y = upin(0, 27.94); new += power(VCC_T, x, y)                                                # VCC
x, y = upin(0, -27.94); new += power(COM_T, x, y)                                               # GND
# decoupling: copy a 0603 100 nF instance, beside U22's VCC
cref = first_ref("JLCPCB-Capacitors:0603,100nF", "C")
cnum = max(int(v) for v in re.findall(r'\(property "Reference" "C(\d+)"', s)) + 1
cb = newuuids(setref(place(block(cref), UX + 12.7, UY - 33.02), cref, "C%d" % cnum))
new += cb
cpins = re.search(r'\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)', cb)
cx, cy = float(cpins.group(1)), float(cpins.group(2))
new += power(VCC_T, cx, cy - 3.81) + power(COM_T, cx, cy + 3.81, "0")

# ---- buttons ----------------------------------------------------------------------------------
# Tactile button pins (local, y up): 1 (-5.08, 2.54), 2 (5.08, 2.54), 3 (-5.08, -5.08), 4 (5.08, -5.08)
GRID = [(80.01 + 22.86 * (i % 3), 144.78 + 12.7 * (i // 3)) for i in range(len(BUTTONS))]
for (pb, name), (bx, by) in zip(BUTTONS, GRID):
    b = place(block(pb), bx, by)
    b = re.sub(r'(\n\t\(symbol\s*\(lib_id "[^"]+"\)\s*\(at [-\d.]+ [-\d.]+) [-\d.]+\)(\s*\(mirror \w\))?',
               lambda k: k.group(1) + " 0)", b, count=1)  # rot 0, no mirror
    delete(pb)
    new += b
    p2 = (bx + 5.08, by - 2.54)
    p4 = (bx + 5.08, by + 5.08)
    new += wire(p2[0], p2[1], p2[0] + 2.54, p2[1]) + power(COM_T, p2[0] + 2.54, p2[1])
    new += wire(p4[0], p4[1], p4[0] + 5.08, p4[1]) + label(name, p4[0] + 5.08, p4[1])

for r in ("R109", "R110", "R111", "R112", "R113", "R114", "R115"):
    delete(r)
s = re.sub(r'\n\t\(label "BTN\d+"\s*\(at [-\d.]+ [-\d.]+ \d+\).*?\n\t\)', "", s, flags=re.S)

i = s.find('\n\t(symbol\n\t\t(lib_id')
s = s[:i] + new + s[i:]
open(P, "w", encoding="utf-8", newline="").write(s)
print("ok: U22, C%d, %d buttons moved" % (cnum, len(BUTTONS)))
