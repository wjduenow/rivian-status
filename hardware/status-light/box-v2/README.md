# box-v2 — wall-charger slip case

A **v2 enclosure** for the rivian-status light that ditches the desk box and instead
**press-fits over a Nekmit Ultra-Thin flat USB wall charger**. The charger is the mechanical
*and* electrical anchor: it plugs into the wall, its AC prongs protrude out the **open back**,
and this shell slides on from behind and snaps in place. **Two printed parts** (`case` +
skirt-back `cover`) + a stick-on diffuser film.

```
conda run -n img23d python build_all.py      # -> case.stl + cover.stl
conda run -n img23d python render_preview.py # -> render_preview.png
```

Same toolchain as `../box` (trimesh CSG, mm, every number provenance-commented in
`case_params.py`, clearances **ASSERTED** in `build_case.check_clearances()` so an illegal
edit fails loudly instead of writing a broken STL).

## Frame
`+X` width (horizontal on the wall) · `+Y` up (vertical) · `+Z` out (wall → room).
Origin: X centred on the charger; `y=0` at the charger's bottom face; `z=0` at the open back rim.

## Layout (locked with the user)
- **8-px WS2812 stick, re-used from v1**, mounted **vertically** on the front interior face.
  It drops into a front **pocket** (domes → `+Z` out the window) and is **screwed** through its
  two Ø3.75 holes to bosses molded off the front wall. The **M3 self-tappers go in from the
  OPEN BACK** — heads hidden from the front, sitting in a relief gap behind the PCB — and their
  pilots thread up into the **solid 2.2 mm front wall** (not thin necked walls). Recommended
  screw: **M3×5** (~3.4 mm thread engagement). The window ledge is a second, backup stop.
- **XIAO ESP32-S3 rests flat on the skirt floor** (no tray) — gravity + the wires to the stick
  hold it; the USB-A→USB-C pigtail from the charger's bottom port loops down to it. The stick
  pocket, charger cavity and skirt share one open interior, so wiring routes freely between them.
- **Skirt back = a separate screw-on `cover`.** The main body prints **fully open-backed** (no
  supports in a blind pocket); the cover drops into a flush rebate and is held by **2 M3×8**
  self-tappers, countersunk on the wall-facing face, into gusseted side-wall bosses. Its **top
  edge is captured by two corner tabs** — the cover's top corners step to a half-thickness
  tongue that tucks under body tabs, so the top can't pull out (tilt the tongue in, then screw
  the bottom). It closes the XIAO bay and stops the board falling out the back.
- **Retention:** an inward **snap lip** around the back rim catches behind the charger's rear
  edge (blueprint §3).

## Key numbers (all in `case_params.py`)
| | mm | source |
|---|---|---|
| Charger (W×H×D) | 43.18 × 50.80 × 20.32 | blueprint |
| Slip-fit cavity | 43.78 × 51.40 × 20.72 | +0.6/+0.6/+0.4 |
| **Outer envelope (W×H×D)** | **48.2 × 72.8 × 29.1** | derived |
| Protrusion from wall | **29.1** | derived |
| Wall / front | 2.2 / 2.2 | choice |
| Skirt (interior height) | 17.0 | floored by the USB-A pigtail (~15 mm) |
| Window (X×Y) | 5.95 × 50.95 | over the emitter run |
| Stick screws | 2 × **M3×5** from the back, hidden | Ø5.0 bosses, ~3.4 mm grip |
| Head-relief gap / dome recess | 2.6 / 0.6 | behind the PCB / below the window |
| Skirt-back cover | separate plate, 2 × **M3×8** | flush rebate, gusseted bosses |
| Snap lip (inward × tall) | 1.2 × 2.0 | blueprint 1.0–1.5 |

## Deliberate deviations from the blueprint (documented, not accidents)
1. **Depth grows to ~29.1 mm** (blueprint said ~20.7 + wall). The blueprint assumed the
   charger touches the front wall, but the vertical LED stick sits **in front of** the charger
   (3.0 mm) **and is back-screwed**, needing a **2.6 mm relief gap** behind it for the hidden
   screw heads. So internal depth = charger + head gap + stick.
2. **Skirt is 17 mm** (blueprint 15–18). It's floored by the **USB-A pigtail plug's vertical
   clearance** hanging off the charger's bottom port — *not* the XIAO, which now lies flat on
   the floor (~5.5 mm tall). Down from the earlier 24 mm.

## Print orientation
Print **front-face-down** on the bed: the window is a flat bottom layer, the walls rise, and
the **whole back (charger region *and* skirt) ends up open on top** — no bridging over the big
front face, and **no supports in a blind skirt pocket** (that's why the skirt back is a separate
cover). The rear snap lip becomes a small inward overhang at the top; a chamfer or a few support
lines clean it up. The `cover` prints flat, csk-face up.

## Open items / next iteration
- **Stick thread engagement is modest (~3.4 mm).** Fine for a ~3 g static part, but if you
  want more grip, raise `DOME_RECESS` (lets the boss get taller, at a little more depth) or
  step to a longer screw. Print a boss test coupon — FDM self-tap holes want tuning.
- **XIAO is unretained sideways** — it just rests on the floor (per your call). Gravity + the
  stick wires + the back cover hold it; add a dab of foam if it rattles in transit.
- **Cover top edge** is now held by **two corner retaining tabs** (tongue-under-tab) in addition
  to the 2 side screws + bottom/side rebate. The tabs are short print overhangs (corner tabs,
  not a full-width lip → no 44 mm bridge); verify they clear on a test print and tune
  `COVER_LIP_*` if the tongue is tight.
- **First flash.** No external USB-C port by default — first flash the XIAO out of the case,
  then it's OTA over WiFi. Set `USB_SERVICE_SLOT = True` to cut a cabled-access slot in the
  skirt bottom.
- **Verify against the real charger + snap lip fit** with a print (FDM slip-fits and snap lips
  want a test coupon before committing the whole part).
- The **snap lip is a simple rectangular step** — tune `LIP_IN`/`LIP_H`/`LIP_CHAMFER` after a
  test fit.
