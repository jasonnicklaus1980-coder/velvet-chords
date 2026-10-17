#!/usr/bin/env python3
"""Render the skin pages to PNG the way MPC would lay them out, using live parameter text from the plugin.
Checks every image exists and every binding points at a real parameter. Live text uses DejaVu Sans as a stand-in for
the MPC's Titillium Web, so text widths are approximate.

usage: preview_skin.py <skin dir> <plugin.so> <list_params> <out prefix> [tab=index=value ...]
"""
import json, os, subprocess, sys
from PIL import Image, ImageDraw, ImageFont

skin, so, lister, prefix = sys.argv[1:5]
settings = {}
for a in sys.argv[5:]:
    tab, rest = a.split("=", 1)
    settings.setdefault(tab, []).append(rest)
tui = json.load(open(os.path.join(skin, "TUI.json")))["pageData"]
defs = {d["key"]: d["value"] for d in tui["componentDefinitions"]["localComponentDefinitions"]}
problems = []

def params_for(tab):
    out = subprocess.run([lister, so] + settings.get(tab, []), capture_output=True, text=True, check=True).stdout
    p = {}
    for line in out.splitlines():
        i, name, disp, val = line.split("\t")
        p[int(i)] = (name, disp, float(val))
    return p

def font(h):
    return ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", max(8, int(h * 0.72)))

def img(name):
    path = os.path.join(skin, name)
    if not os.path.exists(path):
        problems.append("missing image " + name)
        return None
    return Image.open(path).convert("RGBA")

def bounds(b):
    return [int(float(x)) for x in b["bounds"].split()]

for tab in tui["tabs"]:
    P = params_for(tab["tabName"])
    canvas = Image.new("RGBA", (1280, 628), (0, 0, 0, 255))
    d = ImageDraw.Draw(canvas)
    page = defs[tab["componentName"]]
    for comp in page["componentsData"]:
        cd = comp["componentData"]
        bx, by, bw, bh = bounds(comp["bounds"])
        remap = {m["key"]: int(m["value"].split()[1]) for m in comp["handle remapping"]["map"]}
        for pidx in remap.values():
            if pidx not in P:
                problems.append("%s: %s bound to missing parameter %d" % (tab["tabName"], cd["name"], pidx))
        subs = [comp] if cd["type"] in ("Image",) else defs.get(cd["type"], {}).get("componentsData")
        if subs is None:
            problems.append("unknown component type " + cd["type"]); continue
        for sc in subs:
            sd = sc["componentData"]
            sx, sy, sw, sh = bounds(sc["bounds"])
            if cd["type"] != "Image":
                sx += bx; sy += by
            t = sd["type"]
            if t == "Image":
                im = img(sd["data"]["image"])
                if im: canvas.alpha_composite(im, (sx, sy))
            elif t == "Button":
                data = sd["data"]
                v = P.get(remap.get(data["handleName"], -1), ("", "", 0.0))[2]
                n = data["numButtonsInGroup"]
                on = (v >= 0.5) if n == 1 else (round(v * (n - 1)) == data["buttonId"])
                im = img(data["onImage"] if on else data["offImage"])
                if im: canvas.alpha_composite(im, (sx, sy))
            elif t == "Label":
                data = sd["data"]
                pidx = remap.get(data["handleName"])
                txt = P[pidx][1] if pidx in P else cd["name"]
                ts = data["textStyle"]
                f = font(ts["font"]["height"])
                col = "#" + ts["colour"][2:]
                while txt and d.textlength(txt, font=f) > sw:
                    txt = txt[:-2] + "\u2026" if len(txt) > 2 else ""
                tw = d.textlength(txt, font=f)
                l, tp, r, b = d.textbbox((0, 0), "Ag", font=f)
                ty = sy + (sh - (b - tp)) / 2 - tp
                tx = sx + (sw - tw) / 2 if "horizontallyCentred" in ts["justification"] else sx
                d.text((tx, ty), txt, font=f, fill=col)
    out = "%s_%s.png" % (prefix, tab["tabName"].lower())
    canvas.convert("RGB").save(out)
    print("preview", out)
print("problems:", problems if problems else "none")
sys.exit(1 if problems else 0)
