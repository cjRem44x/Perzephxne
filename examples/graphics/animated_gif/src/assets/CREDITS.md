# Asset provenance

`bounce.gif` — a simple 160x120, 8-frame animation of a solid-color ball
bouncing left-right, generated for this example only via Python/Pillow
(not a real/found asset, not hand-drawn). See the generation script
below if you want to regenerate or tweak it (frame count, colors, size):

```python
from PIL import Image, ImageDraw

W, H = 160, 120
BG = (30, 30, 60)
BALL = (255, 120, 40)

n_frames = 8
frames = []
for i in range(n_frames):
    img = Image.new("RGB", (W, H), BG)
    draw = ImageDraw.Draw(img)
    t = i / n_frames
    tri = t * 2 if t < 0.5 else 2 - t * 2   # triangle wave: 0 -> 1 -> 0
    cx = 20 + tri * (W - 40)
    cy = H / 2
    r = 16
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=BALL)
    frames.append(img.convert("P", palette=Image.ADAPTIVE, colors=8))

frames[0].save(
    "bounce.gif",
    save_all=True,
    append_images=frames[1:],
    duration=[120] * n_frames,
    loop=0,
    disposal=2,
)
```
