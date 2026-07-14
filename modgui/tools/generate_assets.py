#!/usr/bin/env python3
"""Generates the NamStack modgui image assets (knob/switch film strips,
screenshot and thumbnail). The screenshot mirrors the layout defined in
stylesheet-namstack.css; re-run this script if the CSS layout changes.

Requires Pillow:  pip install pillow
"""

import math
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "resources")

# palette
BG_TOP = (38, 40, 46)
BG_BOTTOM = (24, 26, 30)
PANEL_EDGE = (12, 13, 15)
AMBER = (240, 160, 48)
AMBER_DIM = (150, 100, 40)
TEXT = (210, 210, 214)
TEXT_DIM = (140, 142, 148)
KNOB_BODY = (52, 54, 60)
KNOB_RING = (18, 19, 22)

SS = 4  # supersampling factor


def font(size, bold=False):
    names = (
        ["DejaVuSans-Bold.ttf", "LiberationSans-Bold.ttf"]
        if bold
        else ["DejaVuSans.ttf", "LiberationSans-Regular.ttf"]
    )
    for name in names:
        for base in ("/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/truetype/liberation"):
            path = os.path.join(base, name)
            if os.path.exists(path):
                return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def draw_knob_frame(draw, cx, cy, radius, value):
    """Draw one knob at (cx, cy); value in [0, 1]. Coordinates are in the
    supersampled space."""
    r = radius
    # outer ring
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=KNOB_RING)
    # body with a simple top-light gradient
    for i in range(int(r * 0.92), 0, -1):
        t = i / (r * 0.92)
        shade = tuple(int(c * (1.15 - 0.35 * t)) for c in KNOB_BODY)
        draw.ellipse([cx - i, cy - i - (r - i) * 0.08, cx + i, cy + i - (r - i) * 0.08], fill=shade)
    # value arc
    start, sweep = 135, 270
    draw.arc(
        [cx - r * 0.99, cy - r * 0.99, cx + r * 0.99, cy + r * 0.99],
        start=start,
        end=start + sweep * value,
        fill=AMBER,
        width=max(2, int(r * 0.10)),
    )
    # pointer
    angle = math.radians(start + sweep * value)
    x1 = cx + math.cos(angle) * r * 0.25
    y1 = cy + math.sin(angle) * r * 0.25
    x2 = cx + math.cos(angle) * r * 0.78
    y2 = cy + math.sin(angle) * r * 0.78
    draw.line([x1, y1, x2, y2], fill=AMBER, width=max(2, int(r * 0.10)))


def make_knob_strip(frames=49, size=64):
    img = Image.new("RGBA", (frames * size * SS, size * SS), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    for f in range(frames):
        value = f / (frames - 1)
        cx = (f * size + size / 2) * SS
        cy = (size / 2) * SS
        draw_knob_frame(draw, cx, cy, (size / 2 - 2) * SS, value)
    img = img.resize((frames * size, size), Image.LANCZOS)
    img.save(os.path.join(OUT, "knob.png"))


def draw_switch_frame(draw, x0, size, on):
    """Slide switch travelling left-to-right in a size x size frame starting at
    x0 (supersampled coordinates). Off sits left, on sits right: for the tone
    stack position that reads Pre on the left and Post on the right, i.e. in
    signal-chain order."""
    s = size * SS
    track_h = int(s * 0.42)
    pad_x = int(s * 0.08)
    top = (s - track_h) // 2
    track = [x0 + pad_x, top, x0 + s - pad_x, top + track_h]
    draw.rounded_rectangle(track, radius=track_h // 2, fill=KNOB_RING)

    inner = int(s * 0.05)
    knob_d = track_h - 2 * inner
    knob_x = track[2] - inner - knob_d if on else track[0] + inner
    color = AMBER if on else (150, 152, 158)
    draw.ellipse([knob_x, top + inner, knob_x + knob_d, top + inner + knob_d], fill=color)


def draw_footswitch_frame(draw, x0, size, pressed):
    """Round stomp switch in a size x size frame (supersampled coords)."""
    s = size * SS
    cx, cy = x0 + s // 2, s // 2
    r = int(s * 0.46)
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(60, 62, 68), outline=PANEL_EDGE, width=2 * SS)
    r2 = int(s * (0.30 if pressed else 0.33))
    draw.ellipse([cx - r2, cy - r2, cx + r2, cy + r2], fill=(84, 86, 94), outline=(40, 41, 45), width=SS)


def make_footswitch_strip(size=64):
    # frame 0 = active (bypass value 0), frame 1 = bypassed
    img = Image.new("RGBA", (2 * size * SS, size * SS), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw_footswitch_frame(draw, 0, size, pressed=False)
    draw_footswitch_frame(draw, size * SS, size, pressed=True)
    img = img.resize((2 * size, size), Image.LANCZOS)
    img.save(os.path.join(OUT, "footswitch.png"))


def make_switch_strip(size=64):
    img = Image.new("RGBA", (2 * size * SS, size * SS), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw_switch_frame(draw, 0, size, on=False)
    draw_switch_frame(draw, size * SS, size, on=True)
    img = img.resize((2 * size, size), Image.LANCZOS)
    img.save(os.path.join(OUT, "switch.png"))


# ------------------------------------------------------------------ fader
# Vertical slider for the 5-band graphic EQ. mod-ui has no slider widget -- the
# only thing it can render is a film strip -- so the travel is drawn frame by
# frame, the cap climbing from bottom (frame 0) to top (last frame).
FADER_W = 40
FADER_H = 96
FADER_FRAMES = 33  # -> 32 steps, an even number so the centre frame is 0 dB


def draw_fader_frame(draw, x0, value):
    """One fader frame, `value` in [0, 1] from bottom to top (supersampled)."""
    w, h = FADER_W * SS, FADER_H * SS

    # slot
    slot_w = int(w * 0.14)
    slot_x = x0 + (w - slot_w) // 2
    margin = int(h * 0.10)
    draw.rounded_rectangle(
        [slot_x, margin, slot_x + slot_w, h - margin],
        radius=slot_w // 2,
        fill=KNOB_RING,
    )

    # centre detent (0 dB)
    mid_y = h // 2
    draw.line([x0 + int(w * 0.18), mid_y, x0 + w - int(w * 0.18), mid_y],
              fill=(70, 72, 78), width=SS)

    # cap: travels between the slot ends
    cap_w, cap_h = int(w * 0.62), int(h * 0.11)
    travel_top = margin + cap_h // 2
    travel_bottom = h - margin - cap_h // 2
    cy = int(travel_bottom - value * (travel_bottom - travel_top))
    cx = x0 + w // 2
    draw.rounded_rectangle(
        [cx - cap_w // 2, cy - cap_h // 2, cx + cap_w // 2, cy + cap_h // 2],
        radius=int(cap_h * 0.3),
        fill=KNOB_BODY,
        outline=KNOB_RING,
        width=SS,
    )
    # amber indicator line across the cap
    draw.line([cx - cap_w // 2 + SS, cy, cx + cap_w // 2 - SS, cy], fill=AMBER, width=SS)


def make_fader_strip():
    img = Image.new("RGBA", (FADER_FRAMES * FADER_W * SS, FADER_H * SS), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    for i in range(FADER_FRAMES):
        draw_fader_frame(draw, i * FADER_W * SS, i / (FADER_FRAMES - 1))
    img = img.resize((FADER_FRAMES * FADER_W, FADER_H), Image.LANCZOS)
    img.save(os.path.join(OUT, "fader.png"))


# ------------------------------------------------------------------ layout
# Must match stylesheet-namstack.css
PEDAL_W, PEDAL_H = 960, 480
ROW_X = 22
BLOCK_W = 88
KNOB = 56
ROW1_TOP, ROW2_TOP = 74, 204  # top of each control block (label line)
EQ_ROW_TOP = 322
LABEL_H, VALUE_H = 16, 16

ROW1 = ["INPUT", "STACK", "STACK ON", "PRE/POST", "BASS", "MIDDLE", "TREBLE",
        "PARAM 1", "PARAM 2", "OUTPUT"]
ROW1_SWITCH = {2, 3}
ROW1_VALUES = {0: "0.0 dB", 4: "0.50", 5: "0.50", 6: "0.50", 7: "0.50", 8: "0.50",
               9: "0.0 dB"}
ROW2 = ["IR 1", "IR 2", "IR 3", "IR 4", "DOUBLER", "MIX", "TIME", "WIDTH"]
ROW2_SWITCH = {4}
ROW2_VALUES = {0: "0.0 dB", 1: "0.0 dB", 2: "0.0 dB", 3: "0.0 dB", 5: "0.50", 6: "18 ms", 7: "1.00"}

# The MOD variant adds the slimmable-model quality knob on the second row.
ROW2_MOD = ROW2 + ["QUALITY"]
ROW2_MOD_VALUES = {**ROW2_VALUES, 8: "1.00"}

# 5-band graphic EQ row: two switches then the five faders (CSS .ns-ctrl-fader).
EQ_SWITCHES = ["EQ ON", "EQ PRE/POST"]
EQ_BANDS = ["80", "240", "750", "2200", "6600"]
FADER_BLOCK_W = 72


def draw_eq_row(img, draw, top, label_font, value_font):
    """The graphic-EQ row: two switches, then five faders sitting at 0 dB."""
    for i, label in enumerate(EQ_SWITCHES):
        bx = ROW_X + i * BLOCK_W
        cx = (bx + BLOCK_W / 2) * SS
        draw.text((cx, (top + LABEL_H / 2) * SS), label, font=label_font, fill=TEXT, anchor="mm")
        ky = top + LABEL_H + 2
        sw = Image.new("RGBA", (KNOB * SS, KNOB * SS), (0, 0, 0, 0))
        draw_switch_frame(ImageDraw.Draw(sw), 0, KNOB, on=False)
        img.alpha_composite(sw, (int(cx - KNOB / 2 * SS), ky * SS))

    fader_x0 = ROW_X + len(EQ_SWITCHES) * BLOCK_W
    for i, label in enumerate(EQ_BANDS):
        bx = fader_x0 + i * FADER_BLOCK_W
        cx = (bx + FADER_BLOCK_W / 2) * SS
        draw.text((cx, (top + LABEL_H / 2) * SS), label, font=label_font, fill=TEXT, anchor="mm")

        ky = top + LABEL_H + 2
        fd = Image.new("RGBA", (FADER_W * SS, FADER_H * SS), (0, 0, 0, 0))
        draw_fader_frame(ImageDraw.Draw(fd), 0, 0.5)  # centre = 0 dB
        img.alpha_composite(fd, (int(cx - FADER_W / 2 * SS), ky * SS))

        draw.text((cx, (ky + FADER_H + 2 + VALUE_H / 2) * SS), "0.0 dB",
                  font=value_font, fill=TEXT_DIM, anchor="mm")


def draw_pedal(scale=1):
    w, h = PEDAL_W * scale, PEDAL_H * scale
    img = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # panel with vertical gradient
    radius = 12 * scale * SS
    for y in range(h * SS):
        t = y / (h * SS)
        color = tuple(int(BG_TOP[i] + (BG_BOTTOM[i] - BG_TOP[i]) * t) for i in range(3))
        draw.line([(0, y), (w * SS, y)], fill=color)
    # round the corners + edge
    mask = Image.new("L", (w * SS, h * SS), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, w * SS - 1, h * SS - 1], radius=radius, fill=255)
    img.putalpha(mask)
    draw.rounded_rectangle([0, 0, w * SS - 1, h * SS - 1], radius=radius, outline=PANEL_EDGE, width=2 * SS)

    return img, draw


def make_screenshot():
    img, draw = draw_pedal()

    title_font = font(26 * SS, bold=True)
    sub_font = font(11 * SS)
    label_font = font(10 * SS, bold=True)
    value_font = font(10 * SS)

    draw.text((24 * SS, 12 * SS), "NamStack", font=title_font, fill=AMBER)
    draw.text((190 * SS, 26 * SS), "NAM · AIDA-X · TONE STACK · 5-BAND EQ · IR MIXER · DOUBLER",
              font=sub_font, fill=TEXT_DIM)
    draw.text((PEDAL_W * SS - 24 * SS, 26 * SS), "Pilali", font=sub_font, fill=TEXT_DIM, anchor="ra")

    def draw_row(labels, switches, values, top):
        for i, label in enumerate(labels):
            bx = ROW_X + i * BLOCK_W
            cx = (bx + BLOCK_W / 2) * SS
            draw.text((cx, (top + LABEL_H / 2) * SS), label, font=label_font, fill=TEXT, anchor="mm")
            ky = top + LABEL_H + 2
            if i in switches:
                # draw switch frame (off)
                sw = Image.new("RGBA", (KNOB * SS, KNOB * SS), (0, 0, 0, 0))
                draw_switch_frame(ImageDraw.Draw(sw), 0, KNOB, on=False)
                img.alpha_composite(sw, (int(cx - KNOB / 2 * SS), ky * SS))
            else:
                draw_knob_frame(draw, cx, (ky + KNOB / 2) * SS, (KNOB / 2 - 2) * SS, 0.5)
            if i in values:
                draw.text((cx, (ky + KNOB + 2 + VALUE_H / 2) * SS), values[i],
                          font=value_font, fill=TEXT_DIM, anchor="mm")

    draw_row(ROW1, ROW1_SWITCH, ROW1_VALUES, ROW1_TOP)
    draw_row(ROW2, ROW2_SWITCH, ROW2_VALUES, ROW2_TOP)
    draw_eq_row(img, draw, EQ_ROW_TOP, label_font, value_font)

    # footswitch + led (CSS: fsw 880/396 48px, led 897/372 14px)
    fx, fy = 904, 420
    draw.ellipse([(fx - 24) * SS, (fy - 24) * SS, (fx + 24) * SS, (fy + 24) * SS],
                 fill=(60, 62, 68), outline=PANEL_EDGE, width=2 * SS)
    draw.ellipse([(fx - 16) * SS, (fy - 16) * SS, (fx + 16) * SS, (fy + 16) * SS],
                 fill=(84, 86, 94))
    draw.ellipse([(fx - 7) * SS, (379 - 7) * SS, (fx + 7) * SS, (379 + 7) * SS],
                 fill=AMBER, outline=AMBER_DIM, width=SS)

    img = img.resize((PEDAL_W, PEDAL_H), Image.LANCZOS)
    img.save(os.path.join(OUT, "screenshot-namstack.png"))


def make_screenshot_mod():
    """Screenshot of the MOD (plain LV2) variant, in signal-flow reading order
    on a 88px grid: model + quality + gain staging, all the EQ on one line
    (tone stack then graphic EQ), IR mixer (selector above each slot's
    on/level/pan), doubler. Mirrors mod/modgui/stylesheet-namstack-mod.css."""
    global PEDAL_W, PEDAL_H
    saved_w, saved_h = PEDAL_W, PEDAL_H
    PEDAL_W, PEDAL_H = 1120, 624
    img, draw = draw_pedal()

    title_font = font(26 * SS, bold=True)
    sub_font = font(11 * SS)
    label_font = font(10 * SS, bold=True)
    value_font = font(10 * SS)

    # header: title, subtitle and brand share one text baseline (CSS: flex
    # align-items baseline; title top 6 + half-leading 3 + ~24px ascent = 33)
    baseline = 33 * SS
    draw.text((24 * SS, baseline), "NamStack", font=title_font, fill=AMBER, anchor="ls")
    title_w = draw.textlength("NamStack", font=title_font)
    draw.text((24 * SS + title_w + 24 * SS, baseline),
              "NAM · AIDA-X · TONE STACK · 5-BAND EQ · IR MIXER · DOUBLER",
              font=sub_font, fill=TEXT_DIM, anchor="ls")
    draw.text((PEDAL_W * SS - 24 * SS, baseline), "Pilali", font=sub_font, fill=TEXT_DIM, anchor="rs")

    def draw_row(labels, switches, values, top, x0=ROW_X, block_w=BLOCK_W):
        for i, label in enumerate(labels):
            bx = x0 + i * block_w
            cx = (bx + block_w / 2) * SS
            draw.text((cx, (top + LABEL_H / 2) * SS), label, font=label_font, fill=TEXT, anchor="mm")
            ky = top + LABEL_H + 2
            if i in switches:
                sw = Image.new("RGBA", (KNOB * SS, KNOB * SS), (0, 0, 0, 0))
                draw_switch_frame(ImageDraw.Draw(sw), 0, KNOB, on=False)
                img.alpha_composite(sw, (int(cx - KNOB / 2 * SS), ky * SS))
            else:
                draw_knob_frame(draw, cx, (ky + KNOB / 2) * SS, (KNOB / 2 - 2) * SS, 0.5)
            if i in values:
                draw.text((cx, (ky + KNOB + 2 + VALUE_H / 2) * SS), values[i],
                          font=value_font, fill=TEXT_DIM, anchor="mm")

    def draw_file_select(bx, top, label, box_w=132):
        draw.text((bx * SS, (top + 7) * SS), label, font=label_font, fill=TEXT, anchor="lm")
        box = [bx * SS, (top + 16) * SS, (bx + box_w) * SS, (top + 16 + 22) * SS]
        draw.rounded_rectangle(box, radius=4 * SS, fill=(16, 17, 20), outline=(58, 60, 66), width=SS)
        draw.text(((bx + 6) * SS, (top + 16 + 11) * SS), "-- none --", font=value_font,
                  fill=AMBER, anchor="lm")

    def draw_section(top, label):
        # thin rule broken by the section name (CSS .ns-section: left 28,
        # 1064 wide, 472px segments around a 120px centre gap, line at top+7)
        y = (top + 7) * SS
        draw.line([28 * SS, y, (28 + 472) * SS, y], fill=(58, 60, 66), width=SS)
        draw.line([(28 + 472 + 120) * SS, y, (28 + 1064) * SS, y], fill=(58, 60, 66), width=SS)
        draw.text(((28 + 472 + 60) * SS, (top + 7) * SS), label,
                  font=label_font, fill=TEXT_DIM, anchor="mm")

    # row 1 (CSS .ns-row-model: top 70, left 460) after the model selector
    # (CSS .ns-file[#model]: left 220, top 94, 232px box); centred as a group
    draw_section(50, "A M P")
    draw_file_select(220, 94, "NEURAL MODEL", box_w=232)
    draw_row(["QUALITY", "INPUT", "PARAM 1", "PARAM 2", "OUTPUT"], set(),
             {0: "1.00", 1: "0.0 dB", 2: "0.50", 3: "0.50", 4: "0.0 dB"}, 70, x0=460)

    # row 2, all the EQ on one line (CSS .ns-row-eq: top 190, left 28): the
    # tone stack, then the graphic EQ switches and faders
    draw_section(170, "E Q")
    draw_row(["STACK", "STACK ON", "PRE/POST", "BASS", "MIDDLE", "TREBLE",
              "EQ ON", "EQ PRE/POST"], {1, 2, 6, 7},
             {3: "0.50", 4: "0.50", 5: "0.50"}, 190, x0=28)
    fader_x0 = 28 + 8 * BLOCK_W
    for i, label in enumerate(EQ_BANDS):
        cx = (fader_x0 + i * FADER_BLOCK_W + FADER_BLOCK_W / 2) * SS
        draw.text((cx, (190 + LABEL_H / 2) * SS), label, font=label_font, fill=TEXT, anchor="mm")
        fy = 190 + LABEL_H + 2
        fd = Image.new("RGBA", (FADER_W * SS, FADER_H * SS), (0, 0, 0, 0))
        draw_fader_frame(ImageDraw.Draw(fd), 0, 0.5)  # centre = 0 dB
        img.alpha_composite(fd, (int(cx - FADER_W / 2 * SS), fy * SS))
        draw.text((cx, (fy + FADER_H + 2 + VALUE_H / 2) * SS), "0.0 dB",
                  font=value_font, fill=TEXT_DIM, anchor="mm")

    # row 3, IR mixer: file selector above each slot's on / level / pan
    # (CSS .ns-file[#ir*]: 264px pitch from left 32, top 350; .ns-row-ir: top 392)
    draw_section(330, "C A B")
    for i in range(4):
        draw_file_select(32 + i * 264, 350, "IR %d" % (i + 1), box_w=232)
    draw_row(["ON", "LEVEL", "PAN"] * 4, {0, 3, 6, 9},
             {i: ("0.0 dB" if i % 3 == 1 else "0.00") for i in range(12) if i % 3}, 392, x0=32)

    # row 4, the whole doubler (CSS .ns-row-dbl: top 512, left 296)
    draw_section(492, "D O U B L E R")
    draw_row(["DOUBLER", "MIX", "TIME", "DETUNE", "HUMANIZE", "WIDTH"], {0},
             {1: "0.50", 2: "18 ms", 3: "9.0 ct", 4: "0.30", 5: "1.00"}, 512, x0=296)

    # footswitch + led (CSS: fsw 1040/568 48px, led 1057/544 14px)
    fx, fy = 1064, 592
    draw.ellipse([(fx - 24) * SS, (fy - 24) * SS, (fx + 24) * SS, (fy + 24) * SS],
                 fill=(60, 62, 68), outline=PANEL_EDGE, width=2 * SS)
    draw.ellipse([(fx - 16) * SS, (fy - 16) * SS, (fx + 16) * SS, (fy + 16) * SS],
                 fill=(84, 86, 94))
    draw.ellipse([(fx - 7) * SS, (551 - 7) * SS, (fx + 7) * SS, (551 + 7) * SS],
                 fill=AMBER, outline=AMBER_DIM, width=SS)

    img = img.resize((PEDAL_W, PEDAL_H), Image.LANCZOS)
    img.save(os.path.join(OUT, "screenshot-namstack-mod.png"))
    PEDAL_W, PEDAL_H = saved_w, saved_h


def make_thumbnail():
    w, h = 256, 64
    img = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    for y in range(h * SS):
        t = y / (h * SS)
        color = tuple(int(BG_TOP[i] + (BG_BOTTOM[i] - BG_TOP[i]) * t) for i in range(3))
        draw.line([(0, y), (w * SS, y)], fill=color)
    mask = Image.new("L", (w * SS, h * SS), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, w * SS - 1, h * SS - 1], radius=8 * SS, fill=255)
    img.putalpha(mask)
    draw.rounded_rectangle([0, 0, w * SS - 1, h * SS - 1], radius=8 * SS,
                           outline=PANEL_EDGE, width=2 * SS)

    draw.text((14 * SS, 18 * SS), "NamStack", font=font(24 * SS, bold=True), fill=AMBER)
    for i in range(3):
        cx = (176 + i * 26 + 10) * SS
        draw_knob_frame(draw, cx, 32 * SS, 10 * SS, 0.3 + 0.2 * i)

    img = img.resize((w, h), Image.LANCZOS)
    img.save(os.path.join(OUT, "thumbnail-namstack.png"))


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    make_knob_strip()
    make_switch_strip()
    make_footswitch_strip()
    make_fader_strip()
    make_screenshot()
    make_screenshot_mod()
    make_thumbnail()
    print("assets written to", os.path.abspath(OUT))
