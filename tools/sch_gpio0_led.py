# Schematic: GPIO0 (CHIP_BOOT) carries the BOOT button and the WS2812B data (D9 DIN).
#   * C71 (100 nF debounce on GPIO0) -> DNP: it rounded the LED's 800 kHz data, and with only the chip's
#     ~45k internal pull-up it held GPIO0 low for ~4.5 ms at power-up (EN releases after ~1 ms: random
#     download mode). The footprint stays, unfitted.
#   * R109 10k pull-up GPIO0 -> +3V3 (boot pin firmly high at reset).
#   * R110 33R in series to D9 DIN (new net LED.DIN): damps ringing on the LED data line.
import re, sys, uuid

P = "ESP32S3_Audio_Cam_POE.kicad_sch"
raw = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in raw
s = raw.replace("\r\n", "\n")
assert '"R110"' not in s and '"R109"' not in s, "already done (R109/R110 exist)"

def U():
    return str(uuid.uuid4())

def span(ref):
    i = s.find('(property "Reference" "%s"' % ref)
    assert i > 0, ref
    st = s.rfind('\n\t(symbol\n\t\t(lib_id', 0, i)
    en = s.find('\n\t)', i) + 3
    assert s[st:en].count('(property "Reference"') == 1, ref
    return st, en

def block(ref):
    st, en = span(ref)
    return s[st:en]

def newuuids(b):
    for u in set(re.findall(r'\(uuid "([0-9a-f-]+)"\)', b)):
        b = b.replace(u, U())
    return b

def place(b, x, y):
    m = re.search(r'\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)', b)
    dx, dy = x - float(m.group(1)), y - float(m.group(2))
    return re.sub(r'\(at ([-\d.]+) ([-\d.]+)( [-\d.]+)?\)',
                  lambda k: "(at %.2f %.2f%s)" % (float(k.group(1)) + dx, float(k.group(2)) + dy, k.group(3) or ""), b)

def setref(b, old, new):
    b = b.replace('(property "Reference" "%s"' % old, '(property "Reference" "%s"' % new)
    return b.replace('(reference "%s")' % old, '(reference "%s")' % new)

def first_ref(lib_id, prefix):
    for m in re.finditer(r'\n\t\(symbol\n\t\t\(lib_id "%s"\)[\s\S]*?\n\t\)' % re.escape(lib_id), s):
        refs = re.findall(r'\(property "Reference" "([^"]+)"', m.group(0))
        if len(refs) == 1 and refs[0].startswith(prefix):
            return refs[0]
    raise SystemExit("no template for " + lib_id)

PWR = [max(int(v) for v in re.findall(r'"#PWR0*(\d+)"', s)) + 1]
def power(tpl, x, y):
    ref = re.search(r'\(property "Reference" "(#PWR\d+)"', tpl).group(1)
    b = newuuids(setref(place(tpl, x, y), ref, "#PWR%04d" % PWR[0]))
    PWR[0] += 1
    return b

LABEL = ('\n\t(label "%s"\n\t\t(at %.2f %.2f %d)\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n'
         '\t\t\t(justify %s bottom)\n\t\t)\n\t\t(uuid "%s")\n\t)')
def label(name, x, y, left=True):
    return LABEL % (name, x, y, 0 if left else 180, "left" if left else "right", U())

# 1. C71 -> DNP (not fitted, not in the BOM)
st, en = span("C71")
c71 = s[st:en].replace("(in_bom yes)", "(in_bom no)", 1).replace("(dnp no)", "(dnp yes)", 1)
assert "(dnp yes)" in c71
s = s[:st] + c71 + s[en:]

# 2. D9's data label becomes LED.DIN (D9 pin 4 at 411.48, 289.56; label at 408.94, 289.56)
old = '(label "CHIP_BOOT"\n\t\t(at 408.94 289.56 180)'
assert s.count(old) == 1
s = s.replace(old, '(label "LED.DIN"\n\t\t(at 408.94 289.56 180)')

new = ""
# 3. R109 10k pull-up: copy of R51 (0402 10k, rotation 180 -> pins at y -/+ 3.81)
RX, RY = 548.64, 236.22
new += newuuids(setref(place(block("R51"), RX, RY), "R51", "R109"))
new += power(block(first_ref("power:+3V3", "#PWR")), RX, RY - 3.81)
new += label("CHIP_BOOT", RX, RY + 3.81)

# 4. R110 33R in series to D9: copy of a 0402 33R (rotation 90 -> pins at x -/+ 3.81)
SX, SY = 396.24, 281.94
tpl = first_ref("JLCPCB-Resistors:0402,33Ω", "R")
new += newuuids(setref(place(block(tpl), SX, SY), tpl, "R110"))
new += label("CHIP_BOOT", SX - 3.81, SY, left=False)
new += label("LED.DIN", SX + 3.81, SY)

# insert before the sheet_instances / end of file
k = s.rfind("\n\t(sheet_instances")
if k < 0:
    k = s.rstrip().rfind("\n)")
s = s[:k] + new + s[k:]
if crlf:
    s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("ok: C71 DNP, R109 10k pull-up, R110 33R to D9 (template %s)" % tpl)
