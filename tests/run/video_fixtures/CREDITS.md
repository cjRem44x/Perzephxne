# Test fixture provenance

`testsrc.mp4` — a 32x24, 10fps, ~1.5s H.264 clip of ffmpeg's own
`testsrc` lavfi source filter (a synthetic test pattern, not a real
video), generated for this test only:

```
ffmpeg -f lavfi -i "testsrc=size=32x24:rate=10:duration=1.5" \
    -pix_fmt yuv420p -c:v libx264 -movflags +faststart testsrc.mp4
```
