# Asset provenance

Every asset here was generated for this example only — none are found,
downloaded, or hand-drawn assets.

- `photo.png` — a 64x64 blue circle with a yellow center, via
  Python/Pillow:
  ```python
  from PIL import Image, ImageDraw
  img = Image.new("RGB", (64, 64), (255, 255, 255))
  draw = ImageDraw.Draw(img)
  draw.ellipse([4, 4, 60, 60], fill=(60, 140, 230))
  draw.ellipse([20, 20, 44, 44], fill=(255, 210, 40))
  img.save("photo.png")
  ```
- `bounce.gif` — the same bouncing-ball GIF as
  `examples/graphics/animated_gif`; see that example's own `CREDITS.md`
  for its generation script.
- `clip.mp4` — the same ffmpeg `testsrc2` synthetic clip as
  `examples/graphics/video_playback`; see that example's own
  `CREDITS.md` for its generation command.
