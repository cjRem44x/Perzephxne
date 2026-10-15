# The Lantern Keep — a text adventure starter

A small, complete text adventure in a terminal-style window, meant as a base
to build a bigger game on. Five rooms, two items, one locked door, one ending.

## Run it

From this directory:

```sh
przp run                       # play (needs an X display)
przp run --no-audio            # without sound
przp run --demo                # plays itself and prints the transcript
przp run --audio-device=null   # run the audio code with output discarded
```

Build dependencies (Debian/Ubuntu package names): `libx11-dev libgl-dev
zlib1g-dev libavformat-dev libavcodec-dev libavutil-dev libswscale-dev
libasound2-dev libmpg123-dev`. They're listed for the linker in `przp.toml`.

## Controls

| Key | Does |
|---|---|
| typing | edits the input line |
| Enter | runs the command |
| Backspace / Esc | delete a character / clear the line |
| Up / Down | previous / next command from history |
| PageUp / PageDown, mouse wheel | scroll the output |

Type `help` in the game for its commands (`look`, `go north` or `n`, `take`,
`drop`, `inventory`, `examine`, `music`, `volume`, `clear`, `quit`).

## How it's put together

| File | What it does |
|---|---|
| `src/world.json` | The story: rooms, text, exits, items, locks, the ending. Edit this to change the game. |
| `src/world.przp` | Loads `world.json` into `Room`s (`std/json`), with exits in a `HashMap` and items in a `List`. |
| `src/game.przp` | Game state and the command table: a `HashMap<str, Cmd>` of handlers, aliases via `put_all`. |
| `src/terminal.przp` | The console: word-wrapped, colored scrollback, input line, history, scrolling. |
| `src/ui.przp` | Fonts, text styles (face / size / color), word wrap, `TextField`s and panel frames. |
| `src/main.przp` | Window layout, the room picture (PNG, GIF or video), music and sound effects, the frame loop. |

### Common changes

- **Add a room:** add it to `rooms` in `world.json` and link it from another
  room's `exits`. `media` can be a `.png`, `.gif` or `.mp4`.
- **Add a command:** write `fn cmd_name(g: *Game, arg: str)` in `game.przp` and
  register it in `new_game` with `add(&g, ["name", "alias"], "usage", "description", cmd_name)`.
- **Change how text looks:** every line is printed with a `ui.Style`
  (`ui.style(bold, scale, color)`). The palette is at the top of `ui.przp`.
- **Use another font:** make an atlas from any monospace TTF with
  `python3 tools/make_font_atlas.py font.ttf out.png --px 20` (repo root) and
  load it in `ui.load_fonts` with the glyph size it prints.
- **Change the music or sounds:** replace the MP3s in `src/assets/`. Any sample
  rate and mono/stereo work; the mixer converts them.

## Assets

Everything in `src/assets/` was generated for this example — see
`src/assets/CREDITS.md`.
