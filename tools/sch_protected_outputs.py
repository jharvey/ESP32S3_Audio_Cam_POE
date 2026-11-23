# Schematic: lamp / MISC / servo-power P-FETs -> SY6288 current-limited power switches; rotary
# switches SW1/SW2 removed, their TCA9555 port-1 inputs become the switches' fault (OCB) lines.
#
# Per channel (all FETs share one orientation: rot 270, mirror x):
#   * the FET symbol becomes "Project:SY6288" with EN and OUT where gate and drain were, so the
#     existing gate / drain wiring lands on EN / OUT;
#   * IN, GND and OCB are new pins: a Vdrive symbol, a COM symbol and a fault label are added there;
#   * the old source node (0-ohm link + gate-source resistor) is re-tied from Vdrive to COM: the
#     gate-source resistor becomes the EN pull-down (100k), the gate series resistor 1k.
import re, sys, uuid

P = "ESP32S3_Audio_Cam_POE.kicad_sch"
s = open(P, encoding="utf-8", newline="").read().replace("\r\n", "\n")  # git may check it out with CRLF
assert 'lib_id "Project:SY6288"' not in s, "already done"

# channel: (FET, series R, gate-source R, 0-ohm link, fault label, U21 label it replaces, part, LCSC)
CH = [
    ("Q3", "R57", "R54", "TST-R22", "FLT.WH",  "ROOM.1",  "SY6288AAAC", "C111829"),
    ("Q4", "R58", "R55", "TST-R23", "FLT.IR",  "ROOM.4",  "SY6288AAAC", "C111829"),
    ("Q5", "R63", "R56", "TST-R24", "FLT.OTH", "FLOOR.1", "SY6288AAAC", "C111829"),
    ("Q6", "R77", "R8",  "TST-R30", "FLT.M1",  "FLOOR.4", "SY6288AAAC", "C111829"),
    ("Q7", "R80", "R79", "TST-R31", "FLT.M2",  "FLOOR.2", "SY6288AAAC", "C111829"),
    ("Q8", "R83", "R82", "TST-R32", "FLT.M3",  "FLOOR.8", "SY6288AAAC", "C111829"),
    ("Q2", "R47", "R42", "TST-R17", "FLT.RC",  "ROOM.2",  "SY6288AAAC", "C111829"),  # PoE module: 1.8 A total, 0.6 A parts everywhere
]
SPARE = ("ROOM.8", "EXP.P17")  # TCA9555 P17: free input

def U():
    return str(uuid.uuid4())

def span(ref):
    i = s.find('(property "Reference" "%s"' % ref)
    assert i > 0, ref
    st = s.rfind('\n\t(symbol', 0, i)
    en = s.find('\n\t)', i) + 3
    return st, en

def at_of(ref):
    st, en = span(ref)
    m = re.search(r'\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)', s[st:en])
    return float(m.group(1)), float(m.group(2)), m.group(3)

def edit(ref, fn):
    global s
    st, en = span(ref)
    s = s[:st] + fn(s[st:en]) + s[en:]

def setprop(blk, name, val):
    new, n = re.subn(r'\(property "%s" "[^"]*"' % re.escape(name), lambda _: '(property "%s" "%s"' % (name, val), blk, count=1)
    assert n == 1, name
    return new

# ---- library symbol ----------------------------------------------------------------------
def fx(sz="1.27"):
    return "(effects (font (size %s %s)))" % (sz, sz)
LIB = """
		(symbol "Project:SY6288"
			(pin_names (offset 0.254))
			(exclude_from_sim no)
			(in_bom yes)
			(on_board yes)
			(property "Reference" "U" (at 1.27 6.985 0) %s)
			(property "Value" "SY6288" (at 1.27 -6.985 0) %s)
			(property "Footprint" "" (at 0 0 0) (hide yes) %s)
			(property "Datasheet" "" (at 0 0 0) (hide yes) %s)
			(property "Description" "Current-limited power distribution switch, EN, open-drain fault flag OCB" (at 0 0 0) (hide yes) %s)
			(symbol "SY6288_0_1"
				(rectangle (start -2.54 3.81) (end 5.08 -3.81) (stroke (width 0.254) (type default)) (fill (type background)))
			)
			(symbol "SY6288_1_1"
				(pin input line (at -5.08 0 0) (length 2.54) (name "EN" %s) (number "4" %s))
				(pin open_collector line (at -5.08 2.54 0) (length 2.54) (name "OCB" %s) (number "3" %s))
				(pin power_out line (at 2.54 6.35 270) (length 2.54) (name "OUT" %s) (number "1" %s))
				(pin power_in line (at 7.62 2.54 180) (length 2.54) (name "IN" %s) (number "5" %s))
				(pin power_in line (at 7.62 -2.54 180) (length 2.54) (name "GND" %s) (number "2" %s))
			)
		)""" % tuple([fx()] * 5 + [fx("0.762")] * 10)
# OUT has to end where the FET drain ended: (2.54, 5.08). Body top is 3.81, so pin length 1.27.
LIB = LIB.replace('(pin power_out line (at 2.54 6.35 270) (length 2.54)', '(pin power_out line (at 2.54 5.08 270) (length 1.27)')
i = s.find("(lib_symbols")
j = s.find("\n", i)
s = s[:j] + LIB + s[j:]

# Symbol local (x, y up) -> sheet offset for rot 270 + mirror x (checked against Q3's wiring).
def world(ref, lx, ly):
    x, y, rot = at_of(ref)
    assert rot == "270", (ref, rot)
    return round(x + ly, 2), round(y - lx, 2)

TEMPLATES = {}
PWR_NEXT = [max(int(v) for v in re.findall(r'"#PWR0*(\d+)"', s)) + 1]

def copy_power(template_ref, value_ok, lib, value, x, y, rot="0"):
    if template_ref not in TEMPLATES:  # snapshot before the loop changes it
        st, en = span(template_ref)
        TEMPLATES[template_ref] = s[st:en]
    blk = TEMPLATES[template_ref]
    assert value_ok in blk
    ref = "#PWR%04d" % PWR_NEXT[0]
    PWR_NEXT[0] += 1
    blk = re.sub(r'\(lib_id "[^"]+"\)', '(lib_id "%s")' % lib, blk, count=1)
    blk = re.sub(r'\(at [-\d.]+ [-\d.]+ [-\d.]+\)', lambda m: "(at %.2f %.2f %s)" % (x, y, rot), blk)
    blk = blk.replace(template_ref, ref)
    blk = setprop(blk, "Value", value)
    for u in set(re.findall(r'\(uuid "([0-9a-f-]+)"\)', blk)):
        blk = blk.replace(u, U())
    return blk

LABEL = '\n\t(label "%s"\n\t\t(at %.2f %.2f 0)\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n\t\t\t(justify left bottom)\n\t\t)\n\t\t(uuid "%s")\n\t)'

# Templates: a Vdrive (power:VBUS, value Vdrive) and a COM (power:Earth) symbol.
VD_T, COM_T = "#PWR0118", "#PWR0130"
copy_power(VD_T, '"Vdrive"', "power:VBUS", "Vdrive", 0, 0)   # take the snapshots now
copy_power(COM_T, '"COM"', "power:Earth", "COM", 0, 0)
new_items = ""
r100k = re.search(r'\(lib_id "JLCPCB-Resistors:0402,100kΩ"\)', s)
for fet, rser, rgs, link, flt, old, part, lcsc in CH:
    en_ = world(fet, -5.08, 0)
    ocb = world(fet, -5.08, 2.54)
    vin = world(fet, 7.62, 2.54)
    gnd = world(fet, 7.62, -2.54)
    # FET -> SY6288
    def fet_fn(b, part=part, lcsc=lcsc):
        b = re.sub(r'\(lib_id "[^"]+"\)', '(lib_id "Project:SY6288")', b, count=1)
        b = setprop(b, "Value", part)
        b = setprop(b, "Footprint", "JLCPCB:SOT-23-5_L3.0-W1.7-P0.95-LS2.8-BL")
        b = setprop(b, "LCSC", lcsc)
        b = setprop(b, "Description", "%s current-limited power switch (%s), EN active high, OCB fault flag" %
                    (part, "2 A, limit 2.1-3.7 A" if part.startswith("SY6288C") else "0.6 A, limit 0.8-1.6 A"))
        b = setprop(b, "Datasheet", "https://www.lcsc.com/product-detail/%s.html" % lcsc)
        pins = "".join('\n\t\t(pin "%d"\n\t\t\t(uuid "%s")\n\t\t)' % (n, U()) for n in (1, 2, 3, 4, 5))
        b = re.sub(r'(\n\t\t\(pin "\d+"\n\t\t\t\(uuid "[^"]+"\)\n\t\t\))+', lambda _: pins, b, count=1)
        return b
    edit(fet, fet_fn)
    # the 0-ohm link's supply end: Vdrive -> COM (the old source node becomes ground)
    lx, ly, _ = at_of(link)
    st0 = s.find('(lib_id "power:VBUS")')
    vd = None
    for m in re.finditer(r'\n\t\(symbol\s*\(lib_id "power:VBUS"\)\s*\(at ([-\d.]+) ([-\d.]+) ([-\d.]+)\)', s):
        x, y = float(m.group(1)), float(m.group(2))
        if (abs(x - lx) < 0.05 and abs(abs(y - ly) - 3.81) < 0.05) or (abs(y - ly) < 0.05 and abs(abs(x - lx) - 3.81) < 0.05):
            j = s.find('(property "Reference" "', m.end())
            vd = s[j + 23:j + 40].split('"')[0]
    assert vd, link
    def vd_fn(b):
        b = re.sub(r'\(lib_id "power:VBUS"\)', '(lib_id "power:Earth")', b, count=1)
        return setprop(b, "Value", "COM")
    edit(vd, vd_fn)
    # gate-source resistor -> EN pull-down 100k; series resistor -> 1k
    def to_r(b, val, lcsc):
        b = re.sub(r'\(lib_id "JLCPCB-Resistors:0402,[^"]+"\)', '(lib_id "JLCPCB-Resistors:0402,%s")' % val, b, count=1)
        b = setprop(b, "Value", val)
        b = setprop(b, "LCSC", lcsc)
        b = re.sub(r'±1% [\d.]+[kM]?Ω 0402', '±1% ' + val + ' 0402', b)
        return b
    edit(rgs, lambda b: to_r(b, "100kΩ", "C25741"))
    edit(rser, lambda b: to_r(b, "1kΩ", "C11702"))
    # new pins: IN -> Vdrive, GND -> COM, OCB -> fault label
    new_items += copy_power(VD_T, '"Vdrive"', "power:VBUS", "Vdrive", vin[0], vin[1], "0")
    new_items += copy_power(COM_T, '"COM"', "power:Earth", "COM", gnd[0], gnd[1], "0")
    new_items += LABEL % (flt, ocb[0], ocb[1], U())
    # the U21 port-1 label for the rotary switch becomes the fault line
    s = re.sub(r'\(label "%s"(\s*\(at 86\.36 )' % re.escape(old), lambda m: '(label "%s"%s' % (flt, m.group(1)), s)

s = re.sub(r'\(label "%s"(\s*\(at 86\.36 )' % re.escape(SPARE[0]), lambda m: '(label "%s"%s' % (SPARE[1], m.group(1)), s)

# rotary switches, their labels and ground symbols go
for ref in ("SW1", "SW2", "#PWR0340", "#PWR0341"):
    st, en = span(ref)
    s = s[:st] + s[en:]
s = re.sub(r'\n\t\(label "(?:ROOM|FLOOR)\.\d"\s*\(at 116\.84 [-\d.]+ \d+\).*?\n\t\)', "", s, flags=re.S)
assert not re.search(r'\(label "(?:ROOM|FLOOR)\.', s), "leftover rotary labels"

i = s.find('\n\t(symbol\n\t\t(lib_id')
s = s[:i] + new_items + s[i:]
open(P, "w", encoding="utf-8", newline="").write(s)
print("ok")
