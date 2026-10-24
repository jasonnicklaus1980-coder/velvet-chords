#!/usr/bin/env python3
"""Generate the Velvet Chords MPC screen skin.

Output: <out>/Velvet Chords - VST - Velvet Chords/
  TUI.json                 pages, component definitions, parameter bindings
  Q-Links.json             Q-Link assignments per page
  Q-Links - 8by1.json      same, for 8-Q-Link hardware (identical, as in the JV-880 reference)
  *.png                    every image the pages use

The file format mirrors a working community skin (JV-880 by sd88me) supplied as reference; it is not documented by
Akai. Parameters are bound by NAME: indices come from the built plugin via tools/list_params, so the skin can't drift
from the code.

Visual language: flat studio hardware. Dark surfaces, 1px hairlines, small tracked uppercase labels, one amber
accent reserved for state (selected, playing). No gloss, no gradients on controls, no drop shadows.
Every image is drawn at 3x and downsampled for clean edges.

usage: make_skin.py <plugin.so> <list_params binary> <out dir>
"""
import json, os, subprocess, sys
from PIL import Image, ImageDraw, ImageFont

W, H = 1280, 628
SS = 3
# Text is drawn in DejaVu Sans (installed with the usual Linux fonts); Bold for the weights marked bold.
FONT_FILES = {"Bold": "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
              "SemiBold": "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
              "Regular": "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
              "Medium": "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"}
LIVE_FONT = "Titillium Web"  # font the MPC uses for live text in the reference skin

# ---- palette: graphite surfaces, violet accent
BG = "#0c0c10"
HEADER = "#121218"
SURFACE = "#16161e"       # panels
HAIRLINE = "#262632"
CTRL = "#1a1a23"          # unselected control fill
CTRL_EDGE = "#2e2e3c"
CTRL_SEL = "#25223a"      # selected control fill
FIELD = "#0a0a0f"
FIELD_EDGE = "#2b2a3a"
ACCENT = "#a78bfa"        # violet: selected, playing
ACCENT_DIM = "#4c3f7c"
INK_ON_ACCENT = "#120e1f"
TEXT = "#ececf3"
TEXT_2 = "#a7a7b8"
LABEL = "#70707f"
LCD_BG = "#07110f"        # read-only displays: dark glass
LCD_EDGE = "#1d3a35"
LCD_TEXT = "#8fe6d8"      # teal-green display text
KEY_WHITE = "#e4e3ec"; KEY_WHITE_EDGE = "#a6a4b6"; KEY_BLACK = "#0b0b10"; KEY_BLACK_EDGE = "#3a3a48"
GOLD = ACCENT  # names used by older helpers
ROSE = ACCENT
BRIGHT = TEXT
MUTED = TEXT_2
INK = TEXT


def argb(hexrgb, a="ff"):
    return a + hexrgb.lstrip("#")


def font(weight, size):
    return ImageFont.truetype(FONT_FILES.get(weight, FONT_FILES["Regular"]), int(round(size)))


# ---------------------------------------------------------------- parameters
def load_params(so, lister):
    out = subprocess.run([lister, so], capture_output=True, text=True, check=True).stdout
    p = {}
    for line in out.splitlines():
        i, name = line.split("\t")[:2]
        p[name] = int(i)
    return p


# ---------------------------------------------------------------- drawing
class Art:
    """Draw at SS x in final-pixel coordinates; downsample on save."""

    def __init__(self, w, h, bg=None):
        self.w, self.h = w, h
        self.im = Image.new("RGBA", (w * SS, h * SS), bg or (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    def s(self, *v):
        return [x * SS for x in v]

    def rect(self, x0, y0, x1, y1, r=0, fill=None, edge=None, width=1, corners=(True, True, True, True)):
        self.d.rounded_rectangle(self.s(x0, y0, x1 - 1, y1 - 1), r * SS, fill=fill, outline=edge, width=int(width * SS), corners=corners)

    def line(self, x0, y0, x1, y1, fill, width=1):
        self.d.line(self.s(x0, y0, x1, y1), fill=fill, width=int(width * SS))

    def text(self, x, y, txt, size, fill, weight="Medium", anchor="la", tracking=0.0):
        f = font(weight, size * SS)
        if not tracking:
            self.d.text((x * SS, y * SS), txt, font=f, fill=fill, anchor=anchor)
            return
        widths = [self.d.textlength(c, font=f) for c in txt]
        total = sum(widths) + tracking * SS * (len(txt) - 1)
        cx = x * SS - (total / 2 if anchor[0] == "m" else total if anchor[0] == "r" else 0)
        for c, w in zip(txt, widths):
            self.d.text((cx, y * SS), c, font=f, fill=fill, anchor="l" + anchor[1])
            cx += w + tracking * SS

    def text_center(self, x0, y0, x1, y1, txt, size, fill, weight="Medium", tracking=0.0):
        f = font(weight, size * SS)
        _, t, _, b = self.d.textbbox((0, 0), "HX", font=f)
        y = (y0 + y1) / 2 * SS - (b - t) / 2 - t
        self.text((x0 + x1) / 2, y / SS, txt, size, fill, weight, anchor="ma", tracking=tracking)

    def label(self, x, y, txt):
        """section/control label: small, uppercase, tracked"""
        self.text(x, y, txt.upper(), 10.5, LABEL, "Medium", tracking=1.3)

    def done(self):
        return self.im.resize((self.w, self.h), Image.LANCZOS)


class Images:
    def __init__(self, outdir):
        self.dir = outdir
        self.made = set()

    def save(self, name, img):
        if name not in self.made:
            img.save(os.path.join(self.dir, name), optimize=True)
            self.made.add(name)
        return name

    def pad(self, n, on, w=148, h=120):
        """MPC-style pad tile: dark, active = amber outline + underline"""
        a = Art(w, h)
        if on:
            a.rect(1, 1, w - 1, h - 1, 6, fill=CTRL_SEL, edge=ACCENT, width=1.5)
            a.rect(10, h - 7, w - 10, h - 4, 1.5, fill=ACCENT)
        else:
            a.rect(1, 1, w - 1, h - 1, 6, fill=CTRL, edge=CTRL_EDGE, width=1)
        a.text(12, 10, "%02d" % n, 10.5, ACCENT if on else "#6b6f7a", "Medium", tracking=0.8)
        return self.save("vc_pad_%d_%dx%d_%s.png" % (n, w, h, "on" if on else "off"), a.done())

    def key(self, pc, on, w, h):
        """piano key of any size; white keys carry their note name above the scale dot"""
        return self.save("vc_key_%d_%dx%d_%s.png" % (pc, w, h, "on" if on else "off"), self.key_image(pc, on, w, h))

    def key_image(self, pc, on, w, h):
        black = pc in (1, 3, 6, 8, 10)
        names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
        a = Art(w, h)
        if black:
            a.rect(0, 0, w, h, 3, fill=ACCENT if on else KEY_BLACK, edge=KEY_BLACK_EDGE, corners=(False, False, True, True))
        else:
            a.rect(0, 0, w, h, 3, fill=ACCENT if on else KEY_WHITE, edge=KEY_WHITE_EDGE if not on else ACCENT_DIM,
                   corners=(False, False, True, True))
            yy = h - 38
            a.text_center(0, yy, w, yy + 14, names[pc], 11 if w >= 52 else 10, INK_ON_ACCENT if on else "#55575c", "Medium")
        return a.done()

    def dot(self, on_black):
        a = Art(14, 14)
        a.d.ellipse(a.s(3, 3, 11, 11), fill="#c9ccd2" if on_black else "#26282c")
        return self.save("vc_dot_%s.png" % ("b" if on_black else "w"), a.done())

    def blank(self, w, h):
        return self.save("vc_blank_%dx%d.png" % (w, h), Image.new("RGBA", (w, h), (0, 0, 0, 0)))

    def seg(self, group, i, n, text, w, h, on):
        """joined segmented control: shared 1px dividers, selected = lifted cell, amber text, amber underline"""
        a = Art(w, h)
        first, last = i == 0, i == n - 1
        corners = (first, last, last, first)
        a.rect(0, 0, w, h, 5, fill=CTRL_SEL if on else CTRL, corners=corners)
        a.rect(0, 0, w, h, 5, edge=CTRL_EDGE, corners=corners)
        if not first:
            a.line(0, 0, 0, h, CTRL_EDGE)
        if on:
            a.rect(8, h - 4, w - 8, h - 2, 1, fill=ACCENT)
        size = 13 if w >= 60 else 12
        a.text_center(0, 0, w, h - 1, text, size, ACCENT if on else TEXT_2, "Medium" if on else "Regular")
        return self.save("vc_seg_%s_%d_%s.png" % (group, i, "on" if on else "off"), a.done())

    def chevron(self, nxt):
        a = Art(36, 36)
        pts = [(16, 12), (21, 18), (16, 24)] if nxt else [(20, 12), (15, 18), (20, 24)]
        a.d.line([(x * SS, y * SS) for x, y in pts], fill=TEXT_2, width=int(1.8 * SS), joint="curve")
        return self.save("vc_chev_%s.png" % ("next" if nxt else "prev"), a.done())

    def chevron_pressed(self, nxt):
        a = Art(36, 36)
        a.rect(2, 2, 34, 34, 4, fill=CTRL_SEL)
        pts = [(16, 12), (21, 18), (16, 24)] if nxt else [(20, 12), (15, 18), (20, 24)]
        a.d.line([(x * SS, y * SS) for x, y in pts], fill=ACCENT, width=int(1.8 * SS), joint="curve")
        return self.save("vc_chev_%s_on.png" % ("next" if nxt else "prev"), a.done())

    def switch(self, on):
        w, h, top = 44, 36, 6   # 36 px tall touch area, the pill is 24 px of it
        a = Art(w, h)
        a.rect(0, top, w, top + 24, 12, fill=ACCENT if on else CTRL, edge=None if on else CTRL_EDGE)
        cx = w - 12 if on else 12
        a.d.ellipse(a.s(cx - 8, top + 4, cx + 8, top + 20), fill=INK_ON_ACCENT if on else "#6c7079")
        return self.save("vc_switch_%s.png" % ("on" if on else "off"), a.done())

    def button(self, name, text, w, h, on):
        a = Art(w, h)
        a.rect(0, 0, w, h, 5, fill=ACCENT if on else CTRL, edge=ACCENT if on else CTRL_EDGE)
        a.text_center(0, 0, w, h, text, 13, INK_ON_ACCENT if on else TEXT, "Medium")
        return self.save("vc_btn_%s_%s.png" % (name, "on" if on else "off"), a.done())

    def cell(self, row, on, w, h, colour):
        a = Art(w, h)
        if on:
            a.rect(0, 0, w, h, 3, fill=colour)
        else:
            a.rect(0, 0, w, h, 3, fill=CTRL, edge=CTRL_EDGE)
        return self.save("vc_cell_%d_%s.png" % (row, "on" if on else "off"), a.done())


class Background:
    """Full-page art: header, panels, field frames and labels; live text sits on top."""

    def __init__(self, page_title, info=True):
        self.a = Art(W, H, bg=BG)
        a = self.a
        a.rect(0, 0, W, 56, 0, fill=HEADER)
        a.line(0, 56, W, 56, HAIRLINE)
        for k, wbar in enumerate((18, 13, 8)):  # logo mark: a chord stack
            a.rect(24, 19 + k * 7, 24 + wbar, 23 + k * 7, 1.5, fill=ACCENT)
        a.text(52, 28, "VELVET CHORDS", 14, TEXT, "Medium", anchor="lm", tracking=2.2)
        a.line(212, 18, 212, 38, HAIRLINE)
        a.text(226, 28, page_title.upper(), 11, ACCENT, "Medium", anchor="lm", tracking=1.6)
        if info:
            self.lcd(452, 14, 472, 28)   # live headline: the key and scale in use (see header_info)

    @property
    def d(self):
        return self.a.d

    def panel(self, box, title=None):
        x0, y0, x1, y1 = box
        self.a.rect(x0, y0, x1, y1, 6, fill=SURFACE, edge=HAIRLINE)
        if title:
            self.a.label(x0 + 16, y0 + 12, title)

    def field(self, x, y, w, h):
        self.a.rect(x, y, x + w, y + h, 5, fill=FIELD, edge=FIELD_EDGE)

    def lcd(self, x, y, w, h):
        self.a.rect(x, y, x + w, y + h, 5, fill=LCD_BG, edge=LCD_EDGE)

    def caption(self, x, y, txt):
        self.a.label(x, y, txt)

    def text(self, *args, **kw):
        self.a.text(*args, **kw)

    @property
    def im(self):
        return self.a.done().convert("RGB")


# ---------------------------------------------------------------- TUI.json building blocks
def remap(pairs):
    return {"version": 1, "map": [{"key": k, "value": "Parameter %d" % v} for k, v in pairs]}


def bounds(x, y, w, h, focus="No", visible="Always", invalid="Show", inval=None):
    return {"version": 2, "acceptsHWFocus": focus, "showWhenDataModelInvalid": invalid, "whenVisible": visible,
            "boundsType": "Absolute", "bounds": "%d %d %d %d" % (x, y, w, h), "additionalInvalidatingHandles": inval or []}


def action(on, handler, overlay=""):
    return {"version": 2, "onAction": on, "handler": handler, "handleName": "" if handler == "Show Overlay" else "Data",
            "additionalData": overlay, "handle remapping": {"version": 1, "map": []}}


A_QLINK = action("Mouse Down", "Q-Link")
A_OVERLAY_DBL = action("Double Click", "Show Overlay", "knob overlay")
A_OVERLAY_ENTER = action("Enter Pressed", "Show Overlay", "knob overlay")
A_TOGGLE_ENTER = action("Enter Pressed", "Toggle Switch")
A_TOGGLE_DOWN = action("Mouse Down", "Toggle Switch")


def sub(name, ctype, data, b):
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": ctype, "data": data},
            "handle remapping": {"version": 1, "map": []}, "bounds": b}


def focus_sub(w, h):
    return sub("Focus", "Focus", {"version": 1, "backgroundColour": "00000000", "outlineColour": argb(ROSE),
                                  "backgroundInset": 2.0, "outlineThickness": 2.0}, bounds(0, 0, w, h, visible="WhenFocussed"))


def label_sub(name, handle, x, y, w, h, size, colour, just="horizontallyCentred verticallyCentred"):
    return sub(name, "Label", {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": LIVE_FONT, "style": "SemiBold",
               "height": float(size)}, "colour": argb(colour), "justification": just, "case": "Original"},
               "type": "Value", "handleName": handle}, bounds(x, y, w, h))


def button_sub(on, off, bid, n, w, h, x=0, y=0):
    return sub("Button", "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": bid, "numButtonsInGroup": n,
                                    "handleName": "Data", "gestureBehaviour": "Instant"}, bounds(x, y, w, h))


def definition(actions, comps, bgcolour="0", ignore=False):
    return {"version": 4, "actions": actions,
            "backgroundData": {"version": 1, "focussed": {"version": 1, "colour": bgcolour, "image": ""},
                               "unfocussed": {"version": 1, "colour": bgcolour, "image": ""}},
            "ignoreMousePresses": ignore, "disableCoarseDataWheel": False, "repeats": 1, "hideQLinkBounds": True,
            "componentsData": comps}


STEP_ROWS = [  # (step type index in the plugin, label, colour when set)
    (2, "Accent", ACCENT),
    (1, "Chord", "#7f6fc2"),
    (5, "Upper acc", "#6fb1db"),
    (4, "Upper", "#3f7fa8"),
    (3, "Bass", "#c4b5fd"),
    (6, "Tie", "#5a5f69"),
    (0, "Rest", "#34373e"),
]

CH = 36  # standard control height


class Skin:
    def __init__(self, params, outdir):
        self.P = params
        self.img = Images(outdir)
        self.defs = {}
        self.pages = []

    def p(self, name):
        if name not in self.P:
            raise KeyError("plugin has no parameter %r" % name)
        return self.P[name]

    def define(self, key, value):
        if key not in self.defs:
            self.defs[key] = value
        return key

    @staticmethod
    def place(deftype, name, x, y, w, h, pairs, focus="No", inval=None):
        return {"version": 2, "componentData": {"version": 1, "name": name, "type": deftype, "data": {"version": 1, "handleName": "Data"}},
                "handle remapping": remap(pairs), "bounds": bounds(x, y, w, h, focus=focus, invalid="Hide", inval=inval)}

    # ---- widgets
    def value_field(self, comps, bg, param, x, y, w, h=CH, size=18, colour=TEXT, editable=True, caption=None, box=True, lcd=False):
        if box:
            bg.lcd(x, y, w, h) if lcd else bg.field(x, y, w, h)
        if caption:
            bg.caption(x + 1, y - 17, caption)
        key = "vcField_%dx%d_%d_%s_%s" % (w, h, size, colour.lstrip("#"), "e" if editable else "r")
        comps_def = ([focus_sub(w, h)] if editable else []) + [label_sub("Value", "Data", 10, 0, w - 20, h, size, colour)]
        self.define(key, definition([A_QLINK, A_OVERLAY_DBL, A_OVERLAY_ENTER] if editable else [], comps_def, ignore=not editable))
        comps.append(self.place(key, caption or param, x, y, w, h, [("Data", self.p(param))], focus="Yes" if editable else "No"))

    def readout(self, comps, bg, param, x, y, w, h=CH, size=16, colour=LCD_TEXT, caption=None):
        """a read-only display: dark glass with teal text, as on a hardware screen"""
        self.value_field(comps, bg, param, x, y, w, h, size=size, colour=colour, editable=False, caption=caption, lcd=True)

    def stepper(self, comps, bg, param, prev, nxt, x, y, w, caption=None, h=CH):
        """one framed field with chevrons at both ends"""
        bg.field(x, y, w, h)
        bg.a.line(x + h, y + 6, x + h, y + h - 6, FIELD_EDGE)
        bg.a.line(x + w - h, y + 6, x + w - h, y + h - 6, FIELD_EDGE)
        for is_next, px, trig in ((False, x, prev), (True, x + w - h, nxt)):
            off, on = self.img.chevron(is_next), self.img.chevron_pressed(is_next)
            key = self.define("vcChev_%s" % ("next" if is_next else "prev"), definition([A_TOGGLE_ENTER], [button_sub(on, off, 1, 1, h, h)]))
            comps.append(self.place(key, "next" if is_next else "prev", px, y, h, h, [("Data", self.p(trig))]))
        self.value_field(comps, bg, param, x + h, y, w - 2 * h, h, size=18, box=False)
        if caption:
            bg.caption(x + 1, y - 17, caption)

    def segments(self, comps, bg, group, param, labels, x, y, w, h=CH, caption=None, ids=None, ntotal=None):
        """joined segmented control; ids/ntotal let one parameter's options span several rows"""
        if caption:
            bg.caption(x + 1, y - 17, caption)
        n = len(labels)
        for i, t in enumerate(labels):
            bid = ids[i] if ids else i
            on = self.img.seg(group, i, n, t, w, h, True)
            off = self.img.seg(group, i, n, t, w, h, False)
            key = self.define("vcSeg_%s_%d" % (group, i), definition([A_QLINK], [button_sub(on, off, bid, ntotal or n, w, h)]))
            comps.append(self.place(key, "%s %s" % (caption or group, t), x + i * w, y, w, h,
                                    [("Data", self.p(param))], focus="Yes" if i == 0 else "No"))

    def switch(self, comps, bg, param, x, y, caption=None):
        on, off = self.img.switch(True), self.img.switch(False)
        key = self.define("vcSwitch", definition([A_QLINK, A_TOGGLE_ENTER], [focus_sub(44, 36), button_sub(on, off, 1, 1, 44, 36)]))
        if caption:
            bg.caption(x + 1, y - 17, caption)
        comps.append(self.place(key, caption or param, x, y, 44, 36, [("Data", self.p(param))], focus="Yes"))

    def action_button(self, comps, bg, name, text, param, x, y, w=188, h=CH):
        on, off = self.img.button(name, text, w, h, True), self.img.button(name, text, w, h, False)
        key = self.define("vcBtn_%s" % name, definition([A_TOGGLE_ENTER], [button_sub(on, off, 1, 1, w, h)]))
        comps.append(self.place(key, text, x, y, w, h, [("Data", self.p(param))]))

    def pad_grid(self, comps, x0, y0, w=146, h=105, gap=8):
        """16 chord tiles laid out like the MPC's pads: 4x4, pad 1 bottom-left, pad 16 top-right"""
        for k in range(16):
            on, off = self.img.pad(k + 1, True, w, h), self.img.pad(k + 1, False, w, h)
            key = self.define("vcPad4_%d" % (k + 1), definition([A_QLINK], [
                button_sub(on, off, k + 1, 17, w, h),
                label_sub("Chord", "Chord", 6, 24, w - 12, 38, 24, TEXT),
                label_sub("Function", "Function", 6, 62, w - 12, 22, 14, ACCENT),  # the Roman numeral, as on Chord Prism's keys
            ]))
            col, row = k % 4, 3 - k // 4
            x = x0 + col * (w + gap)
            y = y0 + row * (h + gap)
            comps.append(self.place(key, "Pad %d" % (k + 1), x, y, w, h,
                                    [("Data", self.p("Screen pad")), ("Chord", self.p("Pad %d chord" % (k + 1))),
                                     ("Function", self.p("Pad %d function" % (k + 1)))]))

    def keyboard(self, comps, param, x0, y0, ww=56, wh=100, bw=34, bh=60, dots=False):
        """one-octave keyboard; each key is a button in a 12-way group bound to param.
        The white keys stop where the black keys sit, so a tap on a black key lands on the black key only."""
        whites = [0, 2, 4, 5, 7, 9, 11]
        blacks = [1, 3, 6, 8, 10]
        left_white = {1: 0, 3: 2, 6: 5, 8: 7, 10: 9}
        pos = {}
        for i, pc in enumerate(whites):
            pos[pc] = (x0 + i * ww, y0, ww, wh)
        for pc in blacks:
            pos[pc] = (pos[left_white[pc]][0] + ww - bw // 2, y0, bw, bh)
        black_spans = [(pos[pc][0], pos[pc][0] + bw) for pc in blacks]
        for pc in whites:
            x = pos[pc][0]
            on_full, off_full = self.img.key_image(pc, True, ww, wh), self.img.key_image(pc, False, ww, wh)
            # the part of the key above the black keys: the spans not covered by a black key
            spans = [(x, x + ww)]
            for b0, b1 in black_spans:
                nxt = []
                for a0, a1 in spans:
                    if b1 <= a0 or b0 >= a1:
                        nxt.append((a0, a1))
                    else:
                        if a0 < b0: nxt.append((a0, b0))
                        if b1 < a1: nxt.append((b1, a1))
                spans = nxt
            pieces = [(a0, y0, a1 - a0, bh, a0 - x, 0) for a0, a1 in spans if a1 > a0]
            pieces.append((x, y0 + bh, ww, wh - bh, 0, bh))   # the lower part, under the black keys
            for px, py, pw, ph, ox, oy in pieces:
                box = (ox, oy, ox + pw, oy + ph)
                on = self.img.save("vc_keypc_%d_%d_%d_%dx%d_on.png" % (pc, ox, oy, pw, ph), on_full.crop(box))
                off = self.img.save("vc_keypc_%d_%d_%d_%dx%d_off.png" % (pc, ox, oy, pw, ph), off_full.crop(box))
                key = self.define("vcKeyPc_%d_%dx%d_%d_%d" % (pc, pw, ph, ox, oy), definition([A_QLINK], [button_sub(on, off, pc, 12, pw, ph)]))
                comps.append(self.place(key, "Key %d" % pc, px, py, pw, ph, [("Data", self.p(param))]))
        for pc in blacks:
            x, y, w, h = pos[pc]
            on, off = self.img.key(pc, True, w, h), self.img.key(pc, False, w, h)
            key = self.define("vcKey_%d_%dx%d" % (pc, w, h), definition([A_QLINK], [button_sub(on, off, pc, 12, w, h)]))
            comps.append(self.place(key, "Key %d" % pc, x, y, w, h, [("Data", self.p(param))]))
        if dots:
            names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
            blank = self.img.blank(14, 14)
            for pc in whites + blacks:
                black = pc in blacks
                im = self.img.dot(black)
                key = self.define("vcDot_%s" % ("b" if black else "w"), definition([], [button_sub(im, blank, 1, 1, 14, 14)], ignore=True))
                x, y, w, h = pos[pc]
                comps.append(self.place(key, "in scale %s" % names[pc], x + (w - 14) // 2, y + h - 20, 14, 14,

                                        [("Data", self.p("Scale has %s" % names[pc]))]))

    def step_matrix(self, comps, bg, x0, y0, cw=65, ch=24, gapx=5, gapy=4):
        for g in range(4):  # quiet shading behind every beat (4 steps)
            gx = x0 + g * 4 * (cw + gapx) - 4
            bg.a.rect(gx, y0 - 30, gx + 4 * cw + 3 * gapx + 8, y0 + 7 * (ch + gapy) + 2, 5, fill="#1a1b1f" if g % 2 == 0 else SURFACE)
        for r, (_, label, colour) in enumerate(STEP_ROWS):
            y = y0 + r * (ch + gapy)
            bg.a.rect(x0 - 104, y + 9, x0 - 98, y + 15, 1.5, fill=colour)
            bg.a.text(x0 - 90, y + ch / 2, label.upper(), 10, LABEL, "Medium", anchor="lm", tracking=1.0)
        for k in range(16):
            x = x0 + k * (cw + gapx)
            bg.a.text(x + cw / 2, y0 - 16, str(k + 1), 11, TEXT if k % 4 == 0 else LABEL, "Medium", anchor="mm")
            for r, (state, label, colour) in enumerate(STEP_ROWS):
                on, off = self.img.cell(r, True, cw, ch, colour), self.img.cell(r, False, cw, ch, colour)
                key = self.define("vcCell_%d" % r, definition([A_QLINK], [button_sub(on, off, state, 7, cw, ch)]))
                comps.append(self.place(key, "Step %d %s" % (k + 1, label), x, y0 + r * (ch + gapy), cw, ch,
                                        [("Data", self.p("Step %d" % (k + 1)))], focus="Yes" if r == 1 else "No"))

    # ---- pages
    def page(self, tab_name, fn, comps, bg_name, bg):
        img = self.img.save(bg_name, bg.im)
        background = sub("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": img}, bounds(0, 0, W, H))
        key = "%s|%s" % (tab_name.upper(), tab_name)
        self.define(key, definition([], [background] + comps, bgcolour=argb(BG)))
        self.pages.append({"version": 3, "tabName": tab_name, "fnKeyIndex": fn, "fnKeySubIndex": 0, "qlinkBoundsData": ["0 0 0 0"],
                           "componentName": key, "initialSize": "0 0 %d %d" % (W, H), "scale": 1.0})

    def header_banks(self, c, bg, group):
        """four independent buttons, each on its own parameter: a segmented group put A and D out of reach on the MPC"""
        bg.a.text(1066, 28, "BANK", 10.5, LABEL, "Medium", anchor="rm", tracking=1.3)
        for i, t in enumerate(["A", "B", "C", "D"]):
            on, off = self.img.seg(group, i, 4, t, 45, 36, True), self.img.seg(group, i, 4, t, 45, 36, False)
            key = self.define("vcBank_%s_%s" % (group, t), definition([A_QLINK, A_TOGGLE_ENTER], [button_sub(on, off, 1, 1, 45, 36)]))
            c.append(self.place(key, "Bank %s" % t, 1076 + i * 45, 10, 45, 36, [("Data", self.p("Bank %s" % t))],
                                focus="Yes" if i == 0 else "No"))

    def header_help(self, c, bg):
        """the Help switch, on every page: when on, the Info line says what the last control you touched does"""
        self.switch(c, bg, "Help", 940, 10)
        bg.a.text(992, 28, "HELP", 10.5, LABEL, "Medium", anchor="lm", tracking=1.3)

    def header_info(self, c, bg):
        """the headline in the header: the key, scale and notes in use, on every page"""
        self.value_field(c, bg, "Info", 452, 14, 472, 28, size=14, colour=LCD_TEXT, editable=False, box=False)

    def build(self):
        voicings = ["Bass+upper", "Close", "Drop 2", "Spread", "Upper"]
        notes = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

        # ---------- Play
        bg = Background("Play", info=False)   # the mode buttons take the header; the Info bar below shows the scale
        c = []
        self.header_help(c, bg)
        self.segments(c, bg, "mode", "Mode", ["Scale", "Types", "Custom"], 456, 12, 104, 32, ids=[1, 2, 3], ntotal=4)
        self.header_banks(c, bg, "bank")
        self.stepper(c, bg, "Preset", "Preset prev", "Preset next", 12, 10, 428, h=36)  # the factory preset in use, top left
        bg.panel((12, 64, 1268, 144))
        self.readout(c, bg, "Info", 28, 88, 1224, 36, size=16, colour=TEXT_2)  # the scale's notes in the key, across the top
        bg.panel((12, 152, 632, 620))
        self.pad_grid(c, 20, 162)
        bg.panel((640, 152, 1268, 620))
        bg.caption(656, 166, "Key")
        bg.a.text(1252, 166, "DOTS MARK THE SCALE", 9.5, "#4d5159", "Medium", anchor="ra", tracking=1.0)
        self.keyboard(c, "Key", 656, 186, 85, 100, 50, 60, dots=True)   # seven white keys across the panel
        self.segments(c, bg, "bassl", "Bassline", ["Off", "Root", "5th", "Octave", "Walk"], 656, 294, 80, 36)
        # progression play: the bassline only plays while the progression runs with the host playing
        self.switch(c, bg, "Progression play", 1070, 296)
        bg.a.text(1124, 310, "PROGRESSION", 9.5, LABEL, "Medium", anchor="lm", tracking=1.0)
        self.stepper(c, bg, "Scale", "Scale prev", "Scale next", 656, 352, 596, "Scale")
        self.segments(c, bg, "smart", "Smart pads", ["Off", "Pads 9-16", "Follow"], 656, 420, 104, caption="Smart Scale pads")
        self.segments(c, bg, "smartn", "Smart notes", ["Chord", "Scale", "Bass"], 976, 420, 92, caption="Smart notes")
        self.segments(c, bg, "voicing1", "Voicing", voicings, 656, 488, 119, caption="Voicing")
        self.readout(c, bg, "Now playing", 656, 556, 452, 44, size=17, colour=ACCENT, caption="Now playing")
        self.action_button(c, bg, "direct", "Direct", "Direct", 1120, 556, w=132, h=44)  # on: silent built-in sound, chords to Velvet Chords Out
        self.page("Play", 0, c, "vc_bg_play.png", bg)

        # ---------- Perform
        bg = Background("Perform")
        c = []
        self.header_help(c, bg)
        self.header_info(c, bg)
        L, R = 28, 664
        rows = [100, 184, 268, 352, 436, 520]
        bg.panel((12, 64, 632, 620))
        snd = ["E. piano", "Rhodes", "Wurli", "Bright Rh", "Clav", "Piano", "Pad", "Poly", "Strings", "Supersaw",
               "Choir", "Brass", "Harp", "Organ", "Marimba", "Vibes", "Nylon gtr", "Pluck", "Music box", "Silent"]
        for r in range(4):  # 4 rows x 5: one 20-way choice
            self.segments(c, bg, "sound%d" % r, "Sound", snd[r * 5:r * 5 + 5], L, 96 + r * 32, 117, h=30,
                          caption="Instrument" if r == 0 else None, ids=list(range(r * 5, r * 5 + 5)), ntotal=20)
        self.segments(c, bg, "soundx", "Sound extra", ["FM bell", "Synth bass", "Glass", "Saw lead"], L, 586, 147,
                      h=30, caption="Sound extra (picks a sound from the grid's line-up above)", ids=[1, 2, 3, 4], ntotal=5)
        self.value_field(c, bg, "Tone", L, 252, 188, caption="Tone")
        self.value_field(c, bg, "Release", L + 200, 252, 188, caption="Release")
        self.value_field(c, bg, "Volume", L + 400, 252, 188, caption="Volume")
        self.segments(c, bg, "vel", "Velocity", ["Linear", "Soft", "Hard", "Fixed"], L, 320, 147, caption="Velocity")
        self.value_field(c, bg, "Strum", L, 388, 188, caption="Strum")
        self.segments(c, bg, "strumdir", "Strum dir", ["Up", "Down", "Alternate"], L + 204, 388, 128, caption="Strum direction")
        self.segments(c, bg, "arp", "Arp", ["Off", "Up", "Down", "Up-down", "Random", "Converge", "Diverge"], L, 456, 83, caption="Arpeggiator")
        self.segments(c, bg, "rate", "Arp rate", ["1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"], L, 524, 98, caption="Arp rate")
        bg.panel((648, 64, 1268, 620))
        self.segments(c, bg, "voicing2", "Voicing", voicings, R, rows[0], 117, caption="Voicing")
        self.segments(c, bg, "inv", "Inversion", ["Root", "1st", "2nd", "3rd"], R, rows[1], 147, caption="Inversion")
        self.segments(c, bg, "oct", "Octave", ["-2", "-1", "0", "+1", "+2"], R, rows[2], 117, caption="Octave")
        self.value_field(c, bg, "Transpose", R, rows[3], 188, caption="Transpose (song)")
        self.switch(c, bg, "Voice lead", R + 204, rows[3] + 6, "Voice leading")
        self.value_field(c, bg, "MIDI out", R + 336, rows[3], 252, caption="MIDI out")
        self.value_field(c, bg, "Pad 1 note", R, rows[4], 188, caption="Pad 1 note")
        self.action_button(c, bg, "learn", "Learn pad 1", "Learn pad 1", R + 200, rows[4], w=186)
        self.action_button(c, bg, "learnall", "Learn all 16 pads", "Learn all pads", R + 396, rows[4], w=192)
        self.readout(c, bg, "Now playing", R, rows[4] + 44, 588, 30, size=14, colour=ACCENT)
        self.action_button(c, bg, "export", "Export MIDI", "Export MIDI", R, rows[5])
        self.value_field(c, bg, "Reverb", R + 204, rows[5] + 60, 188, h=36, caption="Reverb")
        self.value_field(c, bg, "Humanize", R, rows[5] + 60, 188, h=36, caption="Humanize (velocity)")
        self.readout(c, bg, "Export MIDI", R + 204, rows[5], 180, size=14)
        self.action_button(c, bg, "uprog_save", "Save progression", "Save progression", R + 396, rows[5], w=192)
        self.page("Perform", 1, c, "vc_bg_perform.png", bg)

        # ---------- Edit
        bg = Background("Edit pads", info=False)   # the header holds the pad picker and shift keys
        c = []
        self.header_help(c, bg)
        bg.a.text(466, 28, "PAD", 10.5, LABEL, "Medium", anchor="rm", tracking=1.3)
        self.stepper(c, bg, "Edit pad", "Edit pad prev", "Edit pad next", 476, 10, 180)
        bg.a.text(866, 28, "TAP SELECTS", 10.5, LABEL, "Medium", anchor="rm", tracking=1.3)
        self.switch(c, bg, "Edit follows pad", 876, 16)
        self.header_banks(c, bg, "bank3")
        bg.panel((12, 64, 632, 620))
        self.pad_grid(c, 20, 110)
        bg.a.text(28, 84, "TAP A PAD TO EDIT IT", 10.5, LABEL, "Medium", tracking=1.3)
        bg.panel((640, 64, 1268, 620))
        bg.a.label(656, 78, "Chord")   # section heading: root, bass, type, inversion and octave belong to the chord
        self.keyboard(c, "Edit root", 656, 98, 48, 86, 30, 52)
        self.segments(c, bg, "bass", "Edit bass", ["Root"] + notes, 656, 222, 46, caption="Bass note")
        self.stepper(c, bg, "Edit type", "Edit type prev", "Edit type next", 656, 296, 596, "Chord type")
        self.segments(c, bg, "einv", "Edit inversion", ["Root", "1st", "2nd", "3rd"], 656, 370, 84, caption="Inversion")
        self.segments(c, bg, "eoct", "Edit octave", ["-2", "-1", "0", "+1", "+2"], 1012, 370, 48, caption="Octave")
        bg.a.line(656, 430, 1252, 430, HAIRLINE)   # divider: the chord above, what it plays below
        self.readout(c, bg, "Now playing", 656, 444, 596, 40, size=15, colour=LCD_TEXT, caption="Now playing")
        self.action_button(c, bg, "copy", "Copy layout to Custom", "Copy to Custom", 656, 496, w=292)
        self.action_button(c, bg, "relative", "Relative chord", "Relative chord", 960, 496, w=292)
        self.action_button(c, bg, "analyze", "Analyze sample", "Analyze sample", 656, 548, w=292)
        self.action_button(c, bg, "clear_prog", "Clear progression", "Clear progression", 960, 548, w=292)
        self.readout(c, bg, "Analysis status", 656, 596, 596, 24, size=12, caption=None)
        self.page("Edit", 2, c, "vc_bg_edit.png", bg)

        # ---------- Pattern
        bg = Background("Pattern")
        c = []
        self.header_help(c, bg)
        self.header_info(c, bg)
        bg.panel((12, 64, 1268, 144))
        self.stepper(c, bg, "Pattern", "Pattern prev", "Pattern next", 28, 96, 420, "Pattern")
        self.segments(c, bg, "prate", "Pattern rate", ["1/8", "1/8T", "1/16", "1/16T", "1/32"], 492, 96, 78, caption="Step length")
        self.segments(c, bg, "psync", "Pattern sync", ["Free", "Follow MPC"], 912, 96, 170, caption="Timing")
        bg.panel((12, 152, 1268, 408))
        self.step_matrix(c, bg, 132, 178, ch=30, gapy=2)
        bg.panel((12, 416, 1268, 620))
        r1, r2 = 450, 542
        self.value_field(c, bg, "Swing", 28, r1, 150, caption="Swing")
        self.value_field(c, bg, "Gate", 190, r1, 150, caption="Note length")
        self.value_field(c, bg, "Sound", 360, r1, 464, caption="Built-in sound (Silent = MIDI only)")
        self.value_field(c, bg, "MIDI channel", 844, r1, 120, caption="Out channel")
        self.readout(c, bg, "Now playing", 980, r1, 272, size=14, colour=ACCENT, caption="Now playing")
        self.segments(c, bg, "mout", "MIDI out", ["Off", "MPC", "Port", "Both"], 28, r2, 84, caption="Send chords as MIDI")
        self.segments(c, bg, "plen", "Pattern length", ["4", "8", "12", "16"], 380, r2, 40, caption="Length")
        self.segments(c, bg, "pchance", "Pattern chance", ["25%", "50%", "75%", "100%"], 556, r2, 50, caption="Chance")
        self.segments(c, bg, "pseed", "Pattern seed", ["1", "2", "3", "4"], 772, r2, 40, caption="Seed")
        self.readout(c, bg, "MIDI port", 948, r2, 304, size=14, caption="Virtual port: Velvet Chords Out")
        self.page("Pattern", 3, c, "vc_bg_pattern.png", bg)

        # ---------- Tools: hold, repeat, melody, key sync, MIDI import
        bg = Background("Tools")
        c = []
        self.header_help(c, bg)
        self.header_info(c, bg)
        L, R = 28, 664
        bg.panel((12, 64, 632, 620))
        bg.a.label(L, 76, "Playing")
        self.switch(c, bg, "Hold", L, 110, "Hold: chords keep playing")
        bg.a.text(L + 60, 128, "PRESS ANOTHER PAD TO CHANGE, SAME PAD TO STOP", 9.5, "#4d5159", "Medium", anchor="lm", tracking=1.0)
        self.segments(c, bg, "rep", "Repeat", ["Off", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"], L, 176, 84, caption="Repeat (in time with the MPC)")
        self.segments(c, bg, "mel", "Melody", ["Off", "Simple", "Busy"], L, 256, 196, caption="Melody generator")
        self.segments(c, bg, "melr", "Melody rate", ["1/8", "1/8T", "1/16"], L, 336, 196, caption="Melody rate")
        self.switch(c, bg, "Key sync", L, 416, "Key and scale: follow another copy")
        self.readout(c, bg, "Now playing", L, 534, 300, 40, size=16, colour=ACCENT, caption="Now playing")
        # Detect: the scale that fits the notes played; Use scale sets Key and Scale to it
        self.readout(c, bg, "Detected", L + 312, 534, 276, 40, size=13, caption="Detected")
        self.action_button(c, bg, "det_use", "Use scale", "Use detected scale", L, 464, w=120, h=36)
        self.action_button(c, bg, "det_clear", "Clear notes", "Clear detection", L + 128, 464, w=120, h=36)
        bg.panel((648, 64, 1268, 620))
        bg.a.label(R, 76, "MIDI import")
        self.stepper(c, bg, "Import file", "Import prev", "Import next", R, 116, 588, "File (Velvet Chords/Import)")
        self.action_button(c, bg, "imp", "Load chords", "Import load", R, 184, w=280)
        self.segments(c, bg, "imppads", "Pads play", ["Layout", "Imported"], R + 296, 184, 146, caption="")
        bg.caption(R + 297, 167, "Pads play")
        self.readout(c, bg, "Import status", R, 268, 588, 44, size=15, caption="Status")
        for i, line in enumerate([
                "1. Copy .mid files into the Velvet Chords/Import folder",
                "   on the MPC's internal drive.",
                "2. Pick a file and press Load chords.",
                "3. Its chords appear on the pads, in order, with the",
                "   file's exact notes. Copy to Custom (Edit page) to edit."]):
            bg.a.text(R, 344 + i * 26, line, 12.5, TEXT_2, "Regular")
        bg.a.label(R, 500, "Your presets")
        self.segments(c, bg, "upre", "User preset slot", ["1", "2", "3", "4"], R, 536, 56, caption="Slot")
        self.action_button(c, bg, "upre_save", "Save", "Save user preset", R + 252, 536, w=164)
        self.action_button(c, bg, "upre_load", "Load", "Load user preset", R + 428, 536, w=164)
        self.action_button(c, bg, "upre_export", "Export file", "Export user preset", R, 580, w=140)
        self.action_button(c, bg, "upre_import", "Import file", "Import user preset", R + 148, 580, w=140)
        self.action_button(c, bg, "upre_clear", "Clear", "Clear user preset", R + 296, 580, w=110)
        self.readout(c, bg, "User preset status", R + 414, 580, 180, size=12)
        self.switch(c, bg, "MIDI log", L + 340, 416, "MIDI log (diagnostic)")
        self.stepper(c, bg, "Bass phrase", "Previous phrase", "Next phrase", L + 392, 116, 196, caption="Bass phrase")
        bg.a.text(L + 400, 434, "WRITES MIDI TO MIDI LOG.TXT", 9.5, "#4d5159", "Medium", anchor="lm", tracking=1.0)
        self.action_button(c, bg, "fav_star", "Star preset", "Star preset", L, 580, w=120)
        self.action_button(c, bg, "fav_prev", "Prev star", "Previous starred preset", L + 128, 580, w=120)
        self.action_button(c, bg, "fav_next", "Next star", "Next starred preset", L + 256, 580, w=120)
        self.readout(c, bg, "Starred preset status", L + 384, 580, 200, size=12)
        self.page("Tools", 4, c, "vc_bg_tools.png", bg)

        # ---------- Lanes: presets, multi arp, melody riffs, note editor
        bg = Background("Lanes")
        c = []
        self.header_help(c, bg)
        self.header_info(c, bg)
        bg.panel((12, 64, 1268, 144))
        self.stepper(c, bg, "Preset", "Preset prev", "Preset next", 28, 96, 560, "Preset")
        self.switch(c, bg, "Shift keys", 620, 96, "Shift keys")
        self.segments(c, bg, "riff", "Riff", ["Off", "1", "2", "3", "4", "5", "6"], 720, 96, 60, caption="Riff")
        bg.panel((12, 152, 1268, 472))
        orders = ["Off", "Up", "Down", "Up-down", "Random", "Converge", "Diverge"]
        rates = ["1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"]
        lanes = [("lane1", "Arp", "Arp rate", "Arp octaves", "Arp transpose"),
                 ("lane2", "Arp 2", "Arp 2 rate", "Arp 2 octaves", "Arp 2 transpose"),
                 ("lane3", "Arp 3", "Arp 3 rate", "Arp 3 octaves", "Arp 3 transpose"),
                 ("lane4", "Arp 4", "Arp 4 rate", "Arp 4 octaves", "Arp 4 transpose")]
        for k, (grp, order, rate, octv, tr) in enumerate(lanes):
            y0 = 162 + k * 76
            bg.a.text(28, y0 + 10, "LANE %d" % (k + 1), 10.5, LABEL, "Medium", tracking=1.3)
            self.segments(c, bg, grp, order, orders, 120, y0, 110)
            self.segments(c, bg, grp + "rate", rate, rates, 910, y0, 55)
            self.segments(c, bg, grp + "oct", octv, ["1", "2", "3", "4"], 120, y0 + 38, 110)
            self.segments(c, bg, grp + "tr", tr, ["-12", "-6", "0", "+6", "+12"], 580, y0 + 38, 88)
        bg.panel((12, 480, 1268, 620))
        self.segments(c, bg, "rslot", "Riff slot", ["1", "2", "3", "4", "5", "6"], 28, 498, 52, caption="Riff slot")
        self.segments(c, bg, "rnote", "Edit note", ["Whole", "2", "3", "4", "5", "6", "7", "8"], 360, 498, 66, caption="Edit note")
        self.value_field(c, bg, "Edit note interval", 904, 498, 200, caption="Note interval")
        for i in range(16):
            x = 28 + i * 77
            bg.a.text(x + 36, 544, "%d" % (i + 1), 9.5, LABEL, "Medium", anchor="ma")
            self.value_field(c, bg, "Riff step %d" % (i + 1), x, 558, 72, size=14)
        self.page("Lanes", 5, c, "vc_bg_lanes.png", bg)

    def tui(self):
        return {"pageData": {"version": 1, "componentDefinitions": {"version": 2, "importFiles": [
            "/usr/share/Akai/Content/Synths/Generic/Generic Knob Overlay.json",
            "/usr/share/Akai/Content/Synths/Generic/Generic Menu Overlay.json"],
            "localComponentDefinitions": [{"key": k, "value": v} for k, v in self.defs.items()]},
            "info": {"version": 1, "type": "CompleteDescription"}, "tabs": self.pages}}

    def qlinks(self):
        def m(names):
            names = (names + [None] * 16)[:16]
            return {"Q-Link %d" % (i + 1): (self.p(n) if n else -1) for i, n in enumerate(names)}
        play = ["Mode", "Transpose", "Screen bank", "Progression", "Key", "Scale", "Preset genre", "Voicing",
                "Inversion", "Octave", "Progression play", "Sound", "Smart pads", "Smart notes", "Preset variation", "Volume"]
        perform = ["Sound", "Tone", "Release", "Volume", "Velocity", "Strum", "Strum dir", "Arp",
                   "Arp rate", "Voicing", "Inversion", "Octave", "Transpose", "Voice lead", "MIDI out", "Pad 1 note"]
        edit = ["Edit pad", "Edit root", "Edit type", "Edit bass", "Edit inversion", "Edit octave", "Edit follows pad",
                "Screen bank", "Mode", "Voicing", "Sound", "Volume"]
        pattern = ["Step %d" % (k + 1) for k in range(16)]
        tools = ["Hold", "Repeat", "Melody", "Melody rate", "Key sync", "Import file", "Pads play", "Smart pads",
                 "Smart notes", "Smart octave", "Pattern", "Progression slot", "Sound", "Bassline", "Harmony", "Volume"]
        lanes = ["Riff step %d" % (k + 1) for k in range(16)]
        tabs = [{"Tab": i + 1, "SubTab": 1, "Bank Direction": "Column", "Q-Links": m(x)} for i, x in enumerate([play, perform, edit, pattern, tools, lanes])]
        return {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
                "Screen Mode Q-Links": {"version": 4, "map": tabs}, "Program Mode Q-Links": m(play)}


def main():
    so, lister, out = sys.argv[1], sys.argv[2], sys.argv[3]
    params = load_params(so, lister)
    skin_dir = os.path.join(out, "Velvet Chords - VST - Velvet Chords")
    os.makedirs(skin_dir, exist_ok=True)
    s = Skin(params, skin_dir)
    s.build()
    json.dump(s.tui(), open(os.path.join(skin_dir, "TUI.json"), "w"), indent=4)
    q = s.qlinks()
    for name in ("Q-Links.json", "Q-Links - 8by1.json"):
        json.dump(q, open(os.path.join(skin_dir, name), "w"), indent=4)
    print("skin: %s (%d definitions, %d pages, %d images)" % (skin_dir, len(s.defs), len(s.pages), len(s.img.made)))


if __name__ == "__main__":
    main()
