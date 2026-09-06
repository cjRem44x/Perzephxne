# Asset Credits

All assets below are free (CC0 / public domain) — no attribution is
legally required, but it's recorded here for provenance.

## Images

- `asteroid_big.png`, `asteroid_med.png`, `asteroid_small.png` — from
  Kenney's ["Space Shooter Redux"](https://kenney.nl/assets/space-shooter-redux)
  pack (originally `meteorBrown_big1.png`, `meteorBrown_med1.png`,
  `meteorBrown_small1.png`), CC0 / public domain. Fetched via
  [tasdikrahman/spaceShooter](https://github.com/tasdikrahman/spaceShooter),
  whose own README confirms these images are from Kenney's pack under
  Public Domain.

## Sounds

- `fire.mp3` — converted from `laserLarge_000.ogg`, Kenney's
  ["Sci-fi Sounds"](https://kenney.nl/assets/sci-fi-sounds) pack, CC0 /
  public domain.
- `explosion.mp3` — converted from `explosionCrunch_000.ogg`, same pack.

Both fetched (still under their original Kenney filenames) via
[frederickjjoubert/bevy-ball-game](https://github.com/frederickjjoubert/bevy-ball-game),
whose README explicitly lists `sci-fi-sounds/Audio/explosionCrunch_000.ogg`
and `sci-fi-sounds/Audio/laserLarge_000.ogg` as sourced from that pack.
Converted to MP3 via `ffmpeg` since `std/audio`'s decoder is MP3-only
(`libmpg123`).
