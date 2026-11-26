# Draws the 80 x 42 mm use card, front and back (needs Pillow): python enclosure/make_card.py
# -> enclosure/card/use_card_front.png, use_card_back.png. Print them with enclosure/use_card.html.
# The case previews (case_clam.py) lay the front on the card in the holder.
import os
from PIL import Image, ImageDraw, ImageFont

here = os.path.dirname(os.path.abspath(__file__)); out = os.path.join(here, "card"); os.makedirs(out, exist_ok=True)
S = 20; W, H = 80 * S, 42 * S   # 20 pixels per mm
def font(size, bold=False, mono=False):
    names = (["DejaVuSansMono-Bold.ttf", "consolab.ttf"] if mono else ["DejaVuSans-Bold.ttf", "arialbd.ttf"] if bold else ["DejaVuSans.ttf", "arial.ttf"])
    for d in ("/usr/share/fonts/truetype/dejavu/", "C:/Windows/Fonts/", ""):
        for n in names:
            try: return ImageFont.truetype(d + n, size)
            except OSError: pass
    return ImageFont.load_default()
BTN, BLACK, GREY = (77, 79, 87), (17, 17, 17), (90, 90, 90)
def chip(d, x, y, w, h, text, fill, fg, f):
    d.rounded_rectangle((x, y, x + w, y + h), radius=h // 5, fill=fill)
    tw = d.textlength(text, font=f); d.text((x + (w - tw) / 2, y + h / 2), text, font=f, fill=fg, anchor="lm")
def card():
    im = Image.new("RGB", (W, H), (255, 255, 255)); d = ImageDraw.Draw(im)
    d.rounded_rectangle((8, 8, W - 9, H - 9), radius=40, outline=BLACK, width=6)
    return im, d

# ---- front: how to use it
im, d = card()
d.text((60, 40), "INTERCOM", font=font(74, True), fill=BLACK)
d.text((W - 60, 62), "Display codes on the back", font=font(36), fill=GREY, anchor="ra")
rows = (("TALK", BTN, "Hold and speak to the whole school.", "Let go when you have finished."),
        ("OFFICE", BTN, "Press to call the office. oFC shows", "until the office answers."),
        ("CAL", BLACK, "The office is calling. Hold OFFICE", "and speak. Only the office hears you."))
for i, (name, col, a, b) in enumerate(rows):
    y = 190 + i * 210
    chip(d, 60, y, 290, 130, name, col, (255, 255, 255) if col == BTN else (127, 184, 255), font(62, True, mono=(col == BLACK)))
    d.text((400, y + 2), a, font=font(54), fill=BLACK); d.text((400, y + 70), b, font=font(54), fill=BLACK)
im.save(os.path.join(out, "use_card_front.png"))

# ---- back: what the display shows
im, d = card()
d.text((60, 40), "Display codes", font=font(64, True), fill=BLACK)
codes = (("214", "Working (room number)"), ("CAL", "Office is calling: hold OFFICE"), ("oFC", "Calling the office: wait"),
         ("LoC", "Lockdown"), ("EUA", "Evacuate"), ("SHE", "Shelter in place"),
         ("E1", "Network problem: tell office"), ("E2", "No network: tell office"), ("oL", "Cable shorted: tell office"),
         ("dUP", "Two units, one number"), ("---", "No room number: tell office"))
for i, (c, t) in enumerate(codes):
    col, row = divmod(i, 6)
    x, y = 60 + col * 770, 170 + row * 108
    chip(d, x, y, 150, 84, c, BLACK, (127, 184, 255), font(52, True, mono=True))
    d.text((x + 175, y + 42), t, font=font(37), fill=BLACK, anchor="lm")
im.save(os.path.join(out, "use_card_back.png"))
print("wrote use_card_front.png, use_card_back.png")
