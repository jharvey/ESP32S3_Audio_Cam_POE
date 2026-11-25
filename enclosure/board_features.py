# Dump what the case needs from the board: outline, mounting holes and the courtyard box of every part the case
# has an opening or a clearance for. Run with KiCad's python after the layout changes:
#   "C:\Program Files\KiCad\10.0\bin\python.exe" enclosure/board_features.py
# Writes enclosure/board_features.json (mm, origin at the board's top-left corner, y down as in KiCad).
import json, os, pcbnew

here = os.path.dirname(os.path.abspath(__file__))
b = pcbnew.LoadBoard(os.path.join(here, "..", "ESP32S3_Audio_Cam_POE.kicad_pcb"))
M = pcbnew.ToMM
e = b.GetBoardEdgesBoundingBox()
x0, y0 = round(M(e.GetX()), 1), round(M(e.GetY()), 1)
out = {"w": round(M(e.GetWidth()), 1), "h": round(M(e.GetHeight()), 1), "holes": [], "parts": {}, "keepouts": {}}
WANT = {"DISP1", "PB1", "PB7", "U16", "U17", "J9", "J1", "CARD1", "CN1", "D9", "BT1", "U14", "FPC1", "J10"}
for f in b.GetFootprints():
    r = f.GetReference()
    p = f.GetPosition()
    if r[0] == "H" and r[1:].isdigit():
        out["holes"].append([round(M(p.x) - x0, 2), round(M(p.y) - y0, 2)])
    if r in WANT:
        c = f.GetCourtyard(pcbnew.B_CrtYd if f.IsFlipped() else pcbnew.F_CrtYd)
        q = c.BBox() if c.OutlineCount() else f.GetBoundingBox(False)
        out["parts"][r] = {"x": round(M(p.x) - x0, 2), "y": round(M(p.y) - y0, 2), "back": f.IsFlipped(),
                           "box": [round(M(v) - o, 2) for v, o in ((q.GetX(), x0), (q.GetY(), y0), (q.GetRight(), x0), (q.GetBottom(), y0))]}
for z in b.Zones():
    if z.GetIsRuleArea():
        q = z.GetBoundingBox()
        out["keepouts"][z.GetZoneName()] = [round(M(v) - o, 2) for v, o in ((q.GetX(), x0), (q.GetY(), y0), (q.GetRight(), x0), (q.GetBottom(), y0))]
json.dump(out, open(os.path.join(here, "board_features.json"), "w"), indent=1)
print("holes", out["holes"], "parts", sorted(out["parts"]))
