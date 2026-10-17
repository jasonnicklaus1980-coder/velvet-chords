#!/bin/sh
# Checks the committed skin against the current plugin: every image exists, every binding names a real parameter,
# and (when the generator's font is available) regenerating gives the same TUI.json and Q-Links.
set -e
SKIN="package/skin/Velvet Chords - VST - Velvet Chords"
python3 - "$SKIN" build/native/velvetchords.so build/native/list_params <<'PY'
import json, os, subprocess, sys
skin, so, lister = sys.argv[1:4]
names = {}
for line in subprocess.run([lister, so], capture_output=True, text=True, check=True).stdout.splitlines():
    i, n = line.split("\t")[:2]; names[int(i)] = n
tui = json.load(open(os.path.join(skin, "TUI.json")))["pageData"]
txt = json.dumps(tui)
bad = []
import re
for p in set(int(x) for x in re.findall(r'"Parameter (\d+)"', txt)):
    if p not in names: bad.append("binding to missing parameter %d" % p)
for img in set(re.findall(r'"([^"]+\.png)"', txt)):
    if not os.path.exists(os.path.join(skin, img)): bad.append("missing image " + img)
for f in ("Q-Links.json", "Q-Links - 8by1.json"):
    q = json.load(open(os.path.join(skin, f)))
    vals = [v for t in q["Screen Mode Q-Links"]["map"] for v in t["Q-Links"].values()] + list(q["Program Mode Q-Links"].values())
    bad += ["%s: Q-Link to missing parameter %d" % (f, v) for v in vals if v != -1 and v not in names]
keys = {d["key"] for d in tui["componentDefinitions"]["localComponentDefinitions"]}
for t in tui["tabs"]:
    if t["componentName"] not in keys: bad.append("tab without page " + t["tabName"])
print("skin check: %d parameters bound, %d images, %d pages: %s" % (len(set(re.findall(r'"Parameter (\d+)"', txt))),
      len(set(re.findall(r'"([^"]+\.png)"', txt))), len(tui["tabs"]), "; ".join(bad) or "ok"))
sys.exit(1 if bad else 0)
PY
if python3 -c "import PIL" 2>/dev/null && [ -f /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf ]; then
  rm -rf build/skincheck
  python3 tools/make_skin.py build/native/velvetchords.so build/native/list_params build/skincheck >/dev/null
  for f in TUI.json Q-Links.json "Q-Links - 8by1.json"; do
    cmp -s "build/skincheck/Velvet Chords - VST - Velvet Chords/$f" "$SKIN/$f" || { echo "skin out of date ($f): run make skin"; exit 1; }
  done
  echo "skin matches the generator"
fi
