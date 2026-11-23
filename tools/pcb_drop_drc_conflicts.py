# Delete the unlocked tracks / vias that a DRC report flags as shorting or too close to other copper,
# limited to the given nets. Pads and locked (hand-routed) copper are never touched; they are listed.
# usage: python pcb_drop_drc_conflicts.py board.kicad_pcb drc.json NET [NET ...]
import json, sys
import pcbnew

path, report, nets = sys.argv[1], sys.argv[2], set(sys.argv[3:])
b = pcbnew.LoadBoard(path)
d = json.load(open(report, encoding="utf-8"))
uu = set()
for v in d["violations"]:
    if v["type"] in ("shorting_items", "clearance", "hole_clearance", "solder_mask_bridge"):
        for it in v["items"]:
            uu.add(it["uuid"])
dele, kept = [], []
for t in b.GetTracks():
    if t.m_Uuid.AsString() in uu and t.GetNetname() in nets:
        (kept if t.IsLocked() else dele).append(t)
for t in dele:
    b.Delete(t)
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
b.Save(path)
print("deleted %d, locked kept %d" % (len(dele), len(kept)))
for t in kept:
    print("  locked:", t.GetNetname(), pcbnew.ToMM(t.GetStart().x), pcbnew.ToMM(t.GetStart().y))
