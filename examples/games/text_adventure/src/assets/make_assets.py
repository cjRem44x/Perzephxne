#!/usr/bin/env python3
"""Regenerate every asset in this folder (all original, made by this script).

    cd examples/games/text_adventure/src/assets && python3 make_assets.py

Needs Pillow (pip install pillow) and the ffmpeg command line tool.
The fonts in fonts/ come from tools/make_font_atlas.py instead.
"""
import math
import random
import struct
import subprocess
import wave

from PIL import Image, ImageDraw

W, H = 320, 240
random.seed(7)


def sky(draw, top, bottom, height=H):
    for y in range(height):
        t = y / max(1, height - 1)
        c = tuple(int(a + (b - a) * t) for a, b in zip(top, bottom))
        draw.line([(0, y), (W, y)], fill=c)


def stars(draw, n, max_y):
    for _ in range(n):
        x, y = random.randrange(W), random.randrange(max_y)
        v = random.randrange(150, 255)
        draw.point((x, y), fill=(v, v, v))


def gate():
    img = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(img)
    sky(d, (40, 20, 70), (230, 120, 60), 170)
    stars(d, 40, 90)
    d.rectangle([0, 170, W, H], fill=(30, 25, 30))
    d.rectangle([90, 60, 230, 175], fill=(90, 85, 95))          # gatehouse
    d.pieslice([120, 80, 200, 160], 180, 360, fill=(15, 10, 20))  # arch
    d.rectangle([120, 120, 200, 175], fill=(15, 10, 20))
    for x in range(126, 200, 12):                                 # portcullis bars
        d.line([(x, 82), (x, 175)], fill=(60, 55, 50), width=3)
    for x in range(90, 231, 20):                                  # battlements
        d.rectangle([x, 48, x + 10, 60], fill=(90, 85, 95))
    img.save("gate.png")


def courtyard():
    img = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(img)
    sky(d, (10, 12, 35), (35, 40, 80), 120)
    stars(d, 60, 110)
    d.rectangle([0, 60, 40, H], fill=(70, 70, 80))
    d.rectangle([280, 60, W, H], fill=(70, 70, 80))
    d.rectangle([0, 150, W, H], fill=(55, 50, 45))
    d.ellipse([125, 150, 195, 185], fill=(40, 40, 45))            # well
    d.rectangle([125, 168, 195, 205], fill=(100, 95, 100))
    d.ellipse([125, 195, 195, 215], fill=(100, 95, 100))
    d.line([(130, 168), (130, 120), (190, 120), (190, 168)], fill=(110, 80, 50), width=4)
    for r in range(30, 0, -3):                                    # lantern glow
        a = int(255 * (1 - r / 30))
        d.ellipse([240 - r, 140 - r, 240 + r, 140 + r], fill=(a, int(a * 0.8), int(a * 0.3)))
    img.save("courtyard.png")


def hall():
    img = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, W, H], fill=(20, 18, 22))
    d.polygon([(0, 0), (110, 70), (110, 190), (0, H)], fill=(45, 40, 45))
    d.polygon([(W, 0), (210, 70), (210, 190), (W, H)], fill=(45, 40, 45))
    d.polygon([(0, H), (110, 190), (210, 190), (W, H)], fill=(35, 30, 30))
    d.rectangle([135, 95, 185, 190], fill=(90, 55, 30))           # door
    d.ellipse([175, 140, 181, 146], fill=(220, 190, 60))          # lock
    for x in (60, 260):                                           # torches
        d.rectangle([x - 2, 90, x + 2, 120], fill=(80, 60, 40))
        d.ellipse([x - 7, 72, x + 7, 92], fill=(250, 160, 40))
    img.save("hall.png")


def library_gif():
    frames = []
    for f in range(8):
        img = Image.new("RGB", (W, H), (25, 18, 14))
        d = ImageDraw.Draw(img)
        rnd = random.Random(1)
        for shelf in range(4):                                    # bookshelves
            y = 30 + shelf * 45
            d.rectangle([10, y + 38, W - 10, y + 42], fill=(80, 50, 30))
            x = 14
            while x < W - 20:
                bw = rnd.randrange(6, 14)
                bh = rnd.randrange(24, 37)
                col = (rnd.randrange(60, 180), rnd.randrange(30, 120), rnd.randrange(30, 120))
                d.rectangle([x, y + 38 - bh, x + bw, y + 38], fill=col)
                x += bw + 2
        flick = math.sin(f / 8 * 2 * math.pi) * 3                 # candle flame
        d.rectangle([155, 205, 165, 235], fill=(230, 225, 200))
        d.ellipse([153, 182 - flick, 167, 206], fill=(255, 170, 40))
        d.ellipse([157, 190 - flick, 163, 204], fill=(255, 240, 160))
        frames.append(img)
    frames[0].save("library.gif", save_all=True, append_images=frames[1:], duration=90, loop=0)


def tower_video():
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "lavfi",
                    "-i", "mandelbrot=size=320x240:rate=25:end_scale=0.002",
                    "-t", "6", "-pix_fmt", "yuv420p", "-c:v", "libx264", "tower.mp4"],
                   check=True)


RATE = 44100


def write_mp3(name, samples):
    with wave.open("tmp.wav", "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", max(-32767, min(32767, int(s * 32767))))
                               for s in samples))
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", "tmp.wav",
                    "-c:a", "libmp3lame", "-b:a", "96k", name], check=True)
    subprocess.run(["rm", "-f", "tmp.wav"])


def note(freq, secs, vol=0.3, attack=0.01, decay=None):
    n = int(RATE * secs)
    decay = secs if decay is None else decay
    out = []
    for i in range(n):
        t = i / RATE
        env = min(1.0, t / attack) * math.exp(-3 * t / decay)
        out.append(vol * env * (math.sin(2 * math.pi * freq * t)
                                + 0.3 * math.sin(4 * math.pi * freq * t)))
    return out


def mix(dst, src, at):
    start = int(at * RATE)
    for i, s in enumerate(src):
        if start + i < len(dst):
            dst[start + i] += s


def music():
    # an 8-second A-minor arpeggio that ends in silence, so it loops cleanly
    secs = 8.0
    out = [0.0] * int(RATE * secs)
    chords = [(220.0, 261.63, 329.63), (174.61, 220.0, 261.63),
              (196.0, 246.94, 293.66), (164.81, 207.65, 246.94)]
    for c, chord in enumerate(chords):
        base = c * 2.0
        mix(out, note(chord[0] / 2, 1.9, 0.18, 0.2, 1.5), base)       # bass
        for k in range(8):
            f = chord[k % 3] * (2 if k >= 4 else 1)
            mix(out, note(f, 0.5, 0.12, 0.01, 0.35), base + k * 0.25)
    write_mp3("music.mp3", out)


def sfx():
    pick = [0.0] * int(RATE * 0.4)
    mix(pick, note(880.0, 0.25, 0.4, 0.005, 0.2), 0.0)
    mix(pick, note(1318.5, 0.3, 0.4, 0.005, 0.25), 0.08)
    write_mp3("pickup.mp3", pick)

    step = [0.0] * int(RATE * 0.3)
    for i in range(len(step)):
        t = i / RATE
        step[i] = 0.5 * math.exp(-t * 25) * (random.random() * 2 - 1) * 0.6 \
            + 0.5 * math.exp(-t * 18) * math.sin(2 * math.pi * 70 * t)
    write_mp3("step.mp3", step)

    err = [0.0] * int(RATE * 0.25)
    for i in range(len(err)):
        t = i / RATE
        err[i] = 0.25 * (1 if math.sin(2 * math.pi * 110 * t) > 0 else -1) * math.exp(-t * 6)
    write_mp3("error.mp3", err)

    win = [0.0] * int(RATE * 1.6)
    for k, f in enumerate((523.25, 659.25, 783.99, 1046.5)):
        mix(win, note(f, 1.4, 0.2, 0.01, 1.0), k * 0.12)
    write_mp3("win.mp3", win)


if __name__ == "__main__":
    gate()
    courtyard()
    hall()
    library_gif()
    tower_video()
    music()
    sfx()
    print("assets written")
