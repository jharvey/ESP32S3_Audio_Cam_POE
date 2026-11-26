# Schematic: a second speaker connector J20 in parallel with J10, for speakers with a 1.25 mm 2-pin plug
# (Molex PicoBlade 53261-0271, lying down). Drawn just above J10, joined to the amplifier by the labels
# SPK.OutPS and SPK.OutN. Run once, with KiCad closed:  python tools/sch_speaker_j20.py
import re, uuid

P = "ESP32S3_Audio_Cam_POE.kicad_sch"
s = open(P, encoding="utf-8", newline="").read()
crlf = "\r\n" in s
s = s.replace("\r\n", "\n")
assert '"J20"' not in s, "already done"
X, Y = 529.59, 180.34                       # J10 is at 529.59, 190.5
for m in re.finditer(r"\((?:at|xy|start|end) ([\d.]+) ([\d.]+)", s):   # the place must be empty
    x, y = float(m.group(1)), float(m.group(2))
    assert not (518 <= x <= 546 and 176 <= y <= 185.5), ("something is already drawn there", x, y)

i = s.index('(property "Reference" "J10"')
a = s.rfind("\n\t(symbol\n", 0, i); b = s.index("\n\t)", i) + 3
blk = s[a:b]
assert blk.count('(property "Reference"') == 1
dy = Y - 190.5
def shift(m): return "(at %s %s %s)" % (m.group(1), ("%.4f" % (float(m.group(2)) + dy)).rstrip("0").rstrip("."), m.group(3))
new = re.sub(r"\(at ([\d.]+) ([\d.]+) (\d+)\)", shift, blk)
new = re.sub(r'\(uuid "[0-9a-f-]+"\)', lambda m: '(uuid "%s")' % uuid.uuid4(), new)
new = new.replace('"J10"', '"J20"').replace('(reference "J10")', '(reference "J20")')
new = new.replace('(property "Value" "Conn_01x02"', '(property "Value" "53261-0271"')
new = new.replace("Connector_PinHeader_1.00mm:PinHeader_1x02_P1.00mm_Vertical", "Connector_Molex:Molex_PicoBlade_53261-0271_1x02-1MP_P1.25mm_Horizontal")
new = new.replace("(in_bom no)", "(in_bom yes)").replace("(dnp yes)", "(dnp no)")
sym_uuid = re.search(r'\(uuid "([0-9a-f-]+)"\)', new).group(1)

j = s.index('(label "SPK.OutN"'); la = s.rfind("\n\t(label", 0, j + 1); lb = s.index("\n\t)", j) + 3
lab = s[la:lb]
def label(name, y):
    t = re.sub(r'\(label "[^"]+"', '(label "%s"' % name, lab)
    t = re.sub(r"\(at [\d.]+ [\d.]+ \d+\)", "(at %s %s 180)" % (X - 5.08, y), t, count=1)
    t = re.sub(r"\(justify [a-z ]+\)", "(justify right bottom)", t)
    return re.sub(r'\(uuid "[0-9a-f-]+"\)', lambda m: '(uuid "%s")' % uuid.uuid4(), t)
s = s[:b] + new + label("SPK.OutPS", Y) + label("SPK.OutN", Y + 2.54) + s[b:]
open(P, "w", encoding="utf-8", newline="").write(s.replace("\n", "\r\n") if crlf else s)
print("J20 added, symbol uuid", sym_uuid)
