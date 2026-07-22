# box/ — the `rivian-status` desk light enclosure

A small **top-bar** box: the 8-pixel WS2812 stick is stacked **above** the XIAO ESP32-S3
and its LEDs face **up through a window in the lid**. Everything is screwed — the stick
onto two posts, the lid to four side-wall bosses. USB-C exits the **back wall** (centred,
fully proud); the U.FL antenna coils **inside** for now (a front-wall exit slot is a
one-flag toggle, `ANT_EXIT`).

**Outer size:** ~60.4 × 26.4 × 17.0 mm (recomputes from the params).

## Parts
| File | What |
|---|---|
| `shell.stl` | body: floor, walls, XIAO pocket (corner standoffs + ribs), USB-C port (back wall), **2 stick-mount posts**, **4 lid-screw bosses** (optional front-wall antenna notch via `ANT_EXIT`) |
| `lid.stl` | cap: roof + the **LED diffuser window** (with a film rebate), 4 countersunk screw holes, 2 board-clamp pads |

## Build
```bash
conda run -n img23d python build_all.py      # -> shell.stl + lid.stl
conda run -n img23d python render_preview.py # -> render_preview.png
```
Same toolchain as `../../../../sonos-nest/hardware` (trimesh CSG + manifold3d, `img23d`
env). `build_shell.py` runs `check_clearances()` on every build.

## Assembly / fasteners (all M3 self-tapping into printed bosses)
1. Drop the **XIAO** into its floor pocket (corner standoffs + ribs; USB-C pokes out the
   back wall). Wires solder directly to the pads.
2. Plug in the **U.FL antenna**; coil the pigtail + flex antenna in the open floor space
   around the board (or set `ANT_EXIT = True` to route it out a front-wall slot).
3. Set the **LED stick** on the two posts (LEDs up) and drive **2× M3×6** down through its
   Ø3.75 holes into the posts.
4. Wire the stick (DIN/5V/GND) down to the XIAO (D0 / 5V / GND).
5. Lower the **lid** (LEDs show through the window) and fasten **4× M3×8** into the
   side-wall bosses. The lid pads clamp the board onto its standoffs.

## Layout note — why USB-C is on the back wall
The stick sits over the board and the lid is screwed down, so a corner screw boss can't
share a short end with the board. Putting USB-C on the **back long wall** keeps it fully
proud with no recess. An **end-exit** USB-C is possible but needs the board recessed ~5 mm
from that wall (a screw boss then clears the board corner) — say the word and I'll switch.

## ⚠️ Before a final print
1. **Peg/hole & boss fits** print tight in FDM — test a coupon. `SCREW_PILOT` (2.6) is the
   thread-forming pilot for M3; `LED_HOLE_D` (3.75) is the stick's measured hole.
2. **Diffuser** — the window has a top rebate for a stick-on film / thin acrylic, or print
   the lid in translucent/natural filament.
3. **Antenna** — it coils inside by default; if WiFi is weak (CLAUDE.md warns this XIAO
   needs the external antenna), flip `ANT_EXIT = True` to poke it out the front wall.

## Frame
`+X` width (long axis, the LED bar) · `+Y` depth (front→back) · `+Z` up (LEDs face +Z).
Origin centred in X and Y; z = 0 is the floor's underside.
