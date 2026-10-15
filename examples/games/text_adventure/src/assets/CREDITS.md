# Asset provenance

All pictures, the animation, the video, the music and the sound effects were
generated for this example by `make_assets.py` in this folder (Pillow and
ffmpeg); nothing was downloaded or drawn by hand. Run it to regenerate or tweak
them.

- `gate.png`, `courtyard.png`, `hall.png` — drawn with Pillow shapes.
- `library.gif` — 8-frame candle flicker, Pillow.
- `tower.mp4` — ffmpeg's built-in `mandelbrot` video source, H.264.
- `music.mp3` — an 8-second A-minor arpeggio synthesized in Python, ending in
  silence so it loops cleanly.
- `pickup.mp3`, `step.mp3`, `error.mp3`, `win.mp3` — short synthesized effects.

`fonts/dejavu_mono.png` and `fonts/dejavu_mono_bold.png` are DejaVu Sans Mono
(regular and bold) rendered by `tools/make_font_atlas.py`. DejaVu fonts are
free to use and redistribute; the licence is in `fonts/DEJAVU-LICENSE.txt`.
