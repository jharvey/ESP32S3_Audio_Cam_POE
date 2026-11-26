# Board: add J20 (Molex PicoBlade 53261-0271, 1.25 mm, lying down), the second speaker connector, on the nets of J10.
# It is put down just left of the board, level with J10, for placing and routing by hand.
# Run after tools/sch_speaker_j20.py, with KiCad closed, using KiCad's python:
#   "C:\Program Files\KiCad\10.0\bin\python.exe" tools/pcb_speaker_j20.py SYMBOL_UUID
import os, sys, pcbnew

P = "ESP32S3_Audio_Cam_POE.kicad_pcb"
b = pcbnew.LoadBoard(P)
assert b.FindFootprintByReference("J20") is None, "already done"
j10 = b.FindFootprintByReference("J10")
lib = os.path.join(os.path.dirname(pcbnew.__file__), "..", "..", "..", "share", "kicad", "footprints", "Connector_Molex.pretty")
if not os.path.isdir(lib): lib = r"C:\Program Files\KiCad\10.0\share\kicad\footprints\Connector_Molex.pretty"
fp = pcbnew.FootprintLoad(lib, "Molex_PicoBlade_53261-0271_1x02-1MP_P1.25mm_Horizontal")
assert fp, "footprint not found"
fp.SetReference("J20"); fp.SetValue("53261-0271")
fp.SetFPID(pcbnew.LIB_ID("Connector_Molex", "Molex_PicoBlade_53261-0271_1x02-1MP_P1.25mm_Horizontal"))
if len(sys.argv) > 1: fp.SetPath(pcbnew.KIID_PATH("/" + sys.argv[1]))
nets = {p.GetNumber(): p.GetNet() for p in j10.Pads()}
for p in fp.Pads():
    if p.GetNumber() in nets: p.SetNet(nets[p.GetNumber()])
e = b.GetBoardEdgesBoundingBox()
fp.SetPosition(pcbnew.VECTOR2I(e.GetX() - pcbnew.FromMM(8), j10.GetPosition().y))
b.Add(fp)
pcbnew.SaveBoard(P, b)
print("J20 at", pcbnew.ToMM(fp.GetPosition().x), pcbnew.ToMM(fp.GetPosition().y), {k: v.GetNetname() for k, v in nets.items()})
