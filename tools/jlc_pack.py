"""JLCPCB order pack for the intercom board. Never writes the board: it works on a temporary copy.

    python tools/jlc_pack.py                 BOM, placement, hand-fit list, printed parts, cost report
    python tools/jlc_pack.py --gerbers       also gerbers + drill files, zipped
    python tools/jlc_pack.py --refresh       look the LCSC numbers up again (type, price)
    python tools/jlc_pack.py --qty 5,45      order sizes for the cost report

Output goes to jlc-order/. Assembly is top side only; everything else is listed for hand fitting.
"""
import argparse, collections, csv, glob, json, os, re, shutil, subprocess, sys, tempfile, time, urllib.request, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAME = "ESP32S3_Audio_Cam_POE"
OUT = os.path.join(ROOT, "jlc-order")
CACHE = os.path.join(ROOT, "tools", "jlc_parts_cache.json")
OVER = json.load(open(os.path.join(ROOT, "tools", "jlc_overrides.json"), encoding="utf-8"))
# Fees: check the live quote before ordering, JLCPCB changes them.
EXT_FEE = 3.0          # per different extended part, per order
SETUP = {"economic": 8.0, "standard": 25.0}
STENCIL = 1.5
NYLON_G = 0.275        # USD per gram, MJF PA12 (JLC3DP notice of August 2025)
NYLON_DENS = 1.01      # g per cm3
ASA_DENS = 1.07        # g per cm3, printed solid (FDM parts with infill weigh less)


def kicad_cli():
    c = sorted(glob.glob(r"C:\Program Files\KiCad\*\bin\kicad-cli.exe"), key=lambda p: float(p.split(os.sep)[3]))
    return c[-1] if c else (shutil.which("kicad-cli") or sys.exit("kicad-cli not found"))


def footprints(text):
    out = []
    for f in re.split(r"\n\t\(footprint ", text)[1:]:
        f = re.split(r"\n\t\((?:gr_|segment|via|zone|embedded)", f)[0]
        g = lambda k: (re.search(r'\(property "%s" "([^"]*)"' % re.escape(k), f) or [None, ""])[1].strip()
        a = re.search(r"\n\t\t\(attr ([^)]*)\)", f)
        out.append(dict(ref=g("Reference"), val=g("Value"), lcsc=g("LCSC"), fp=re.match(r'"([^"]*)"', f).group(1).split(":")[-1],
                        side="bottom" if re.search(r'\(layer "([^"]*)"\)', f).group(1) == "B.Cu" else "top",
                        attr=a.group(1) if a else "", off=float(g("JLCPCB Rotation Offset") or 0),
                        tht=len(re.findall(r'\(pad "[^"]*" thru_hole', f))))
    return out


def lookup(nums, refresh):
    cache = json.load(open(CACHE, encoding="utf-8")) if os.path.exists(CACHE) else {}
    for n in sorted(nums):
        if n in cache and not refresh:
            continue
        for u in ("https://jlcsearch.tscircuit.com/components/list.json?search=" + n, "https://jlcsearch.tscircuit.com/api/search?limit=3&q=" + n):
            try:
                d = json.load(urllib.request.urlopen(urllib.request.Request(u, headers={"User-Agent": "bom-check"}), timeout=25))
                c = [x for x in d.get("components", []) if "C%s" % x["lcsc"] == n]
                if c:
                    cache.setdefault(n, {}).update({k: v for k, v in c[0].items() if v is not None})
            except Exception as e:
                print("lookup failed", n, e)
            time.sleep(0.15)
    json.dump(cache, open(CACHE, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    return cache


def unit(price, q):
    if isinstance(price, (int, float)):
        return float(price)
    best = None
    for t in str(price or "").split(","):
        if ":" in t:
            lo = int(t.split(":")[0].split("-")[0])
            if best is None or q >= lo:
                best = float(t.split(":")[1])
    return best or 0.0


def kind(v):
    return "basic" if v.get("is_basic") else "preferred" if v.get("is_preferred") else "extended"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gerbers", action="store_true")
    ap.add_argument("--refresh", action="store_true")
    ap.add_argument("--qty", default="5,45")
    a = ap.parse_args()
    qtys = [int(x) for x in a.qty.split(",")]
    os.makedirs(OUT, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="jlcpack_")
    for ext in ("kicad_pcb", "kicad_pro", "kicad_dru"):
        if os.path.exists(os.path.join(ROOT, NAME + "." + ext)):
            shutil.copy(os.path.join(ROOT, NAME + "." + ext), tmp)
    pcb = os.path.join(tmp, NAME + ".kicad_pcb")
    cli = kicad_cli()
    fps = footprints(open(pcb, encoding="utf-8").read())
    for f in fps:
        f["lcsc"] = f["lcsc"] or OVER["lcsc"].get(f["ref"], "")
    placed = [f for f in fps if "exclude_from_bom" not in f["attr"] and "board_only" not in f["attr"] and f["side"] == "top"]
    hand = [f for f in fps if f not in placed and f["lcsc"] and "dnp" not in f["attr"]]
    info = lookup({f["lcsc"] for f in fps if f["lcsc"]} | {x["lcsc"] for x in OVER["hand_fit_extra"]}, a.refresh)

    # BOM
    grp = collections.defaultdict(list)
    for f in placed:
        grp[(f["lcsc"], f["val"], f["fp"])].append(f["ref"])
    with open(os.path.join(OUT, "BOM_jlc.csv"), "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC"])
        for (l, v, fp), refs in sorted(grp.items(), key=lambda kv: kv[1][0]):
            w.writerow([v, ",".join(sorted(refs)), fp, l])
    missing = sorted(f["ref"] for f in placed if not f["lcsc"])

    # placement
    pos = os.path.join(tmp, "pos.csv")
    subprocess.run([cli, "pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "front", "--exclude-dnp", "-o", pos, pcb], check=True, capture_output=True)
    off = {f["ref"]: f["off"] for f in placed}
    n = 0
    with open(os.path.join(OUT, "CPL_jlc.csv"), "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for r in csv.DictReader(open(pos, encoding="utf-8")):
            if r["Ref"] in off:
                w.writerow([r["Ref"], "%.4fmm" % float(r["PosX"]), "%.4fmm" % float(r["PosY"]), "Top", "%g" % ((float(r["Rot"]) + off[r["Ref"]]) % 360)])
                n += 1
    # parts whose turn JLC's preview must confirm: polarised, or more than two pins
    risk = sorted(f["ref"] for f in placed if re.match(r"(U|Q|D|T|Y|X|J|FPC|CARD|CN|DISP|ANT|PB|BOOT)\d", f["ref"]))

    # hand-fit list
    hrows = [(f["ref"] + " " + f["val"], f["side"] + " side", f["lcsc"], 1) for f in hand]
    hrows += [(x["item"], "in the case", x["lcsc"], x["qty"]) for x in OVER["hand_fit_extra"]]
    with open(os.path.join(OUT, "hand_fit_lcsc.csv"), "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["Item", "Where", "LCSC", "Qty per unit", "Unit price USD", "Part name"])
        for it, wh, l, q in hrows:
            w.writerow([it, wh, l, q, "%.4f" % unit(info.get(l, {}).get("price"), q * qtys[0]), info.get(l, {}).get("mfr", "")])
    with open(os.path.join(OUT, "printed_parts.csv"), "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["Part", "File", "Qty per unit", "Volume cm3 each", "Material"])
        for p in OVER["printed"]:
            w.writerow([p["part"], p["file"], p["qty"], p["cm3"], "clear acrylic 2 mm" if p["file"].startswith("lid.") else "ASA"])

    if a.gerbers:
        gd = os.path.join(tmp, "gerber")
        os.makedirs(gd)
        subprocess.run([cli, "pcb", "export", "gerbers", "--no-protel-ext", "-o", gd + os.sep, pcb], check=True, capture_output=True)
        subprocess.run([cli, "pcb", "export", "drill", "--format", "excellon", "--excellon-separate-th", "-o", gd + os.sep, pcb], check=True, capture_output=True)
        with zipfile.ZipFile(os.path.join(OUT, "GERBER_jlc.zip"), "w", zipfile.ZIP_DEFLATED) as z:
            for p in sorted(glob.glob(os.path.join(gd, "*"))):
                z.write(p, os.path.basename(p))

    # cost report
    uniq = collections.Counter(f["lcsc"] for f in placed if f["lcsc"])
    kinds = collections.Counter(kind(info.get(l, {})) for l in uniq)
    L = ["# JLCPCB order report", "",
         "Made by `tools/jlc_pack.py`. Prices are list prices from a public parts index; stock is not checked. Confirm in JLCPCB's quote.", "",
         "- Parts placed by JLCPCB (top side): **%d**, %d different part numbers: %d basic, %d preferred, %d extended." % (len(placed), len(uniq), kinds["basic"], kinds["preferred"], kinds["extended"]),
         "- Placed parts with no LCSC number: %s" % (", ".join(missing) or "none"),
         "- Fitted by hand: %d items (see `hand_fit_lcsc.csv`)." % len(hrows), "",
         "| Order size | Placed parts, each | Extended fees, each | Setup + stencil, each | Hand-fit parts, each | Electronics, each (no bare board, no shipping) |",
         "|---|---|---|---|---|---|"]
    for q in qtys:
        parts = sum(unit(info.get(l, {}).get("price"), k * q) * k for l, k in uniq.items())
        hp = sum(unit(info.get(l, {}).get("price"), k * q) * k for _, _, l, k in hrows)
        fee = kinds["extended"] * EXT_FEE / q
        su = (SETUP["economic"] + STENCIL) / q
        L.append("| %d | $%.2f | $%.2f | $%.2f | $%.2f | **$%.2f** |" % (q, parts, fee, su, hp, parts + fee + su + hp))
    vol = sum(p["cm3"] * p["qty"] for p in OVER["printed"])
    L += ["", "## Extended parts (each adds a one-time fee per order, about $%.0f)" % EXT_FEE, "", "| LCSC | Part | Qty | Used at |", "|---|---|---|---|"]
    for l, k in sorted(uniq.items(), key=lambda kv: -unit(info.get(kv[0], {}).get("price"), 5) * kv[1]):
        if kind(info.get(l, {})) == "extended":
            L.append("| %s | %s | %d | %s |" % (l, info.get(l, {}).get("mfr", "?"), k, " ".join(sorted(f["ref"] for f in placed if f["lcsc"] == l))[:60]))
    L += ["", "## Printed parts", "",
          "**Material: ASA** (FDM), chosen because it does not yellow or turn brittle with age as ABS does. JLC3DP lists ASA in black and white, "
          "250 x 250 x 250 mm largest part, +/-0.3 mm; the clear lockdown lid is laser-cut acrylic, not printed. Prototypes: PETG.", "",
          "Total %.0f cm3 per unit (at most %.0f g in ASA, printed solid); the two case halves are 97%% of it. "
          "**ASA price: not known, take it from the live JLC3DP quote.** For comparison, MJF nylon at $%.3f/g would be about $%.0f per unit." % (vol, vol * ASA_DENS, NYLON_G, vol * NYLON_DENS * NYLON_G), "",
          "## Check these turns in JLCPCB's placement preview", "", " ".join(risk), ""]
    open(os.path.join(OUT, "cost_report.md"), "w", encoding="utf-8").write("\n".join(L))
    shutil.rmtree(tmp, ignore_errors=True)
    print("\n".join(L[:13]))
    print("placement rows", n, "| wrote", OUT)


if __name__ == "__main__":
    main()
