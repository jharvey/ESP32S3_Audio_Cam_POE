# Draws the office unit's 80 x 42 mm card, front and back (needs Pillow): python enclosure/make_office_card.py
# -> enclosure/card/office_card_front.png, office_card_back.png. Print them with enclosure/office_card.html.
# It goes in the same holder as a room's use card (make_card.py). The big sheet for the wall is office_wall_card.html.
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
BTN, BLACK, GREY, RED, BLUE = (77, 79, 87), (17, 17, 17), (90, 90, 90), (198, 40, 40), (127, 184, 255)
def chip(d, x, y, w, h, text, fill, fg, f):
    d.rounded_rectangle((x, y, x + w, y + h), radius=h // 5, fill=fill)
    tw = d.textlength(text, font=f); d.text((x + (w - tw) / 2, y + h / 2), text, font=f, fill=fg, anchor="lm")
def card():
    im = Image.new("RGB", (W, H), (255, 255, 255)); d = ImageDraw.Draw(im)
    d.rounded_rectangle((8, 8, W - 9, H - 9), radius=40, outline=BLACK, width=6)
    return im, d
def fits(d, text, f, width):
    assert d.textlength(text, font=f) <= width, (text, d.textlength(text, font=f), width)

# ---- front: the buttons, and what to do first
im, d = card()
d.text((60, 40), "OFFICE INTERCOM", font=font(74, True), fill=BLACK)
d.text((W - 60, 62), "Sounds: see the back", font=font(36), fill=GREY, anchor="ra")
rows = (("TALK", BTN, "Hold and speak to the whole school.", "Let go when you have finished."),
        ("214", BLACK, "Ringing, a room number blinks: that", "room is calling. Answer on the office page."),
        ("OFFICE", BTN, "Two beeps every 30 s: open the office page.", "No PC? Hold 3 seconds: quiet for 1 hour."),
        ("LoC", BLACK, "Alarm. Follow the steps on the office", "page. Only the page can end it."),
        ("RED", RED, "Emergency: lift the lid and press.", "The whole school goes to lockdown."))
for i, (name, col, a, b) in enumerate(rows):
    y = 172 + i * 128
    chip(d, 60, y, 250, 102, name, col, BLUE if col == BLACK else (255, 255, 255), font(54, True, mono=(col == BLACK)))
    f = font(45, col == RED)
    for k, t in enumerate((a, b)):
        fits(d, t, f, W - 350 - 50); d.text((350, y + 26 + 52 * k), t, font=f, fill=BLACK, anchor="lm")
im.save(os.path.join(out, "office_card_front.png"))

# ---- back: what each sound means, and how to quiet the beep when the PC cannot be used
im, d = card()
d.text((60, 40), "What the sounds mean", font=font(64, True), fill=BLACK)
sounds = (("Ringing every 2 seconds, a number blinks", "That room is calling the office. Answer it on the office page."),
          ("Ringing again every 30 seconds", "A call for help is still waiting. Answer it or send someone."),
          ("Two short beeps every 30 seconds", "The office page is not open on any PC. Open it: the beeps stop."))
y = 150
for head, text in sounds:
    fits(d, head, font(42, True), W - 120); fits(d, text, font(38), W - 120)
    d.text((60, y), head, font=font(42, True), fill=BLACK); d.text((60, y + 54), text, font=font(38), fill=BLACK)
    y += 122
d.rounded_rectangle((44, y + 4, W - 45, y + 206), radius=24, outline=BLACK, width=5)
box = (("PC not working? Hold OFFICE for 3 seconds, until it chimes.", True),
       ("The beeps stay quiet for 1 hour. Calls and alarms still sound.", False),
       ("Get the PC fixed soon: the page shows who is calling and why.", False))
for k, (t, bold) in enumerate(box):
    f = font(40 if bold else 37, bold); fits(d, t, f, W - 150); d.text((72, y + 26 + 58 * k), t, font=f, fill=BLACK)
y += 232
d.text((60, y + 28), "Office page:", font=font(40, True), fill=BLACK, anchor="lm")
d.line((330, y + 52, W - 250, y + 52), fill=BLACK, width=4); d.text((W - 60, y + 28), "/school", font=font(40, mono=True), fill=BLACK, anchor="rm")
assert y + 60 < H - 20, y
im.save(os.path.join(out, "office_card_back.png"))
print("wrote office_card_front.png, office_card_back.png")
