# Asset provenance

`clip.mp4` — a 160x120, 15fps, 3-second H.264 clip of ffmpeg's own
`testsrc2` lavfi source filter (a synthetic test pattern, not a real
video), generated for this example only:

```
ffmpeg -f lavfi -i "testsrc2=size=160x120:rate=15:duration=3" \
    -pix_fmt yuv420p -c:v libx264 -movflags +faststart clip.mp4
```
