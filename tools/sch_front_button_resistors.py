# Schematic: 1k in series with the two front buttons (PB1 TALK, PB7 OFFICE), next to U22, for ESD:
# people touch these two. The buttons behind the cover stay direct (U22's ~100k pull-ups; ~33 uA when pressed).
#   PB1 side label BTN.TALK1  -> BTN.TALK1_SW,  R111 1k: BTN.TALK1  - BTN.TALK1_SW
#   PB7 side label BTN.OFFICE -> BTN.OFFICE_SW, R112 1k: BTN.OFFICE - BTN.OFFICE_SW
import re, uuid

P = "ESP32S3_Audio_Cam_POE.kicad_sch"
raw = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in raw
s = raw.replace("\r\n", "\n")
assert '"R111"' not in s and '"R112"' not in s, "already done"

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

LABEL = ('\n\t(label "%s"\n\t\t(at %.2f %.2f %d)\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n'
         '\t\t\t(justify %s bottom)\n\t\t)\n\t\t(uuid "%s")\n\t)')
def label(name, x, y, left=True):
    return LABEL % (name, x, y, 0 if left else 180, "left" if left else "right", U())

# the button-side labels (PB1 at 229.87 248.92, PB7 at 275.59 261.62) get the _SW names
for name, xy in (("BTN.TALK1", "229.87 248.92"), ("BTN.OFFICE", "275.59 261.62")):
    old = '(label "%s"\n\t\t(at %s 0)' % (name, xy)
    assert s.count(old) == 1, old
    s = s.replace(old, '(label "%s_SW"\n\t\t(at %s 0)' % (name, xy))

tpl = "R63"  # an 0402 1k, rotation 0: pins at y -/+ 3.81
assert 'lib_id "JLCPCB-Resistors:0402,1kΩ"' in block(tpl)
new = ""
for ref, net, x, y in (("R111", "BTN.TALK1", 256.54, 295.91), ("R112", "BTN.OFFICE", 256.54, 306.07)):
    new += newuuids(setref(place(block(tpl), x, y), tpl, ref))
    new += label(net, x, y - 3.81)
    new += label(net + "_SW", x, y + 3.81)

k = s.rfind("\n\t(sheet_instances")
if k < 0:
    k = s.rstrip().rfind("\n)")
s = s[:k] + new + s[k:]
if crlf:
    s = s.replace("\n", "\r\n")
open(P, "w", encoding="utf-8", newline="").write(s)
print("ok: R111, R112 1k in series with PB1 and PB7")
