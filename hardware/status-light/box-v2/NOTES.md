# box-v2 — design notes & decisions (for future sessions)

Why the v2 case is shaped the way it is. The *numbers* live in `case_params.py` (provenance-
commented) and the *build* in `README.md`; this file is the **reasoning** — the non-obvious
trade-offs, the decisions locked with the user, and the things a future session would otherwise
have to re-derive. Read alongside the repo `CLAUDE.md` and `../board_spec.md`.

## What it is (one line)
A one-piece slip shell that press-fits over a **Nekmit Ultra-Thin flat wall charger**
(45.1 × 51.6 × 20.32 mm; W×H calipered 2026-07-24, D from blueprint). The charger is the
mechanical **and** electrical anchor: it plugs
into the wall, AC prongs exit the **open back**, an inward snap lip retains it. LED stick shows
out the front; XIAO + pigtail live in a lower skirt; skirt back is a separate screw-on cover.

## Decisions locked with the user (don't silently revisit)
- **8-px WS2812 stick, re-used from v1** — *not* the "16-LED array" the original blueprint text
  mentioned. The firmware pixel map is 8 (plan §7).
- **Stick mounted VERTICALLY** on the front (charger face is portrait 51.6 tall; the 51.5 mm
  stick fits the height, not the 45.1 width).
- **XIAO rests flat on the skirt floor** — no tray/standoffs. Gravity + the wires to the stick
  hold it; the cover stops it falling out the back.
- **Skirt back = a SEPARATE screw-on cover** (not an integral wall) — so the body prints open-
  backed with no supports in a blind pocket.
- **Stick retention = hidden M3 screws from the open back** (user: "hidden back, but use M3").
  The front wall is **3.2 mm** and each pilot is capped by **1.6 mm** of solid wall so the posts
  don't show through the front (user asked to hide them; front thickened to suit).
- **Cover = plain screw-held plate.** An earlier tongue-under-corner-tab top retention was
  **removed** (user: "not helpful"); the 2 side screws + flush rebate hold the cover.

## The depth story (why it protrudes ~30 mm, not the blueprint's ~20.7)
The blueprint assumed the charger touches the front wall. It doesn't, for two stacked reasons —
**this is the #1 thing to understand before touching the Z stack:**
1. The vertical LED stick (3.0 mm: 1.6 PCB + 1.4 domes) sits **in front of** the charger.
2. The stick is **back-screwed**, so there's a **head-relief gap** (`STRIP_HEAD_RELIEF`, 2.6 mm)
   between the charger face and the stick's PCB back for the hidden screw heads.

So internal depth = charger (20.72 slip) + relief (2.6) + stick (3.0) → front wall interior at
~26.9, and with a **3.2 mm front wall** the outer face is ~30.1 mm. (`FRONT_T` was thickened
2.2 → 3.2 to keep the screw-post pilots hidden — see below.) `D_IN`/`OUT_D` in `case_params.py`
derive from exactly this chain.

### Why hidden-M3 forces the depth (the front-sandwich problem)
The stick is sandwiched between the charger (behind) and the window (front), so a screw boss
must go on **one** side and that dictates depth:
- **Hidden-back screw** ⟹ the boss it threads into must be on the **front-wall** side, and the
  screw head lands on the **charger** side → needs the relief gap (adds depth). We root the boss
  by boring its **pilot up into the solid 2.2 mm front wall** (not a thin necked pin through the
  Ø3.75 hole — that would leave ~0.5 mm walls and strip). Engagement ~3.4 mm → **M3×5**.
- A robust Ø5.5 boss *behind* the stick would instead need **front-face screws** (visible) and
  a pocket floor — even more depth. Rejected.
- Net: any positive stick screw here costs depth; hidden-back is the least-visible option and
  what the user chose. Thread grip is modest but fine for a ~3 g static part.

`DOME_RECESS` (0.6 mm) lets the LEDs sit a hair below the window (also a light baffle) and buys
a common screw length; raise it if you want more thread engagement (at a little more depth).

**Hiding the screw posts:** the blind pilots are bored up into the front wall and stop
`PILOT_WALL_MIN` short of the outer face. At the old 0.8 mm cap the posts showed through the
front, so `PILOT_WALL_MIN` is now **1.6 mm** and `FRONT_T` was raised 2.2 → 3.2 to keep ~3.6 mm
of thread engagement (cap = `PILOT_WALL_MIN` regardless of `FRONT_T`, so both had to move). Net
cost is ~1 mm more protrusion.

## XIAO in the skirt
- Lies **flat on the floor** (21 × 17.8 footprint on the X-Z plane, only ~5.5 mm tall in Y).
- **Skirt height (`SKIRT_H`) is 41 mm** — grown in two +12 bumps to make room for the
  **USB cable/plug** hanging off the charger's bottom port (user's call). It is NOT floored by
  the XIAO, which lies flat (~5.5 mm tall); don't shrink it below ~15 or the plug collides.
  (History: 24 mm when the XIAO had a front-wall tray → 17 once it moved to the floor → 29 → 41
  for cable room.)
- The stick pocket, charger cavity, and skirt share **one open interior** — wiring routes freely
  (no dedicated slot needed).
- **Not retained sideways** (user's call) — add foam if it rattles in transit. A molded snap tab
  is the clean follow-up if ever wanted.

## Skirt back cover
- Flush rebate + **2× M3×8** into **gusseted** side-wall bosses (posts alone can't root over the
  open cavity — they must merge with / rib to the perimeter walls).
- **Plain plate, no top retention.** An earlier version stepped the cover's top corners to a
  half-thickness tongue that tucked under two body tabs. The user found the tabs unhelpful, so
  they're **removed** — the 2 side screws + the flush rebate hold the cover. If the top ever
  bows out, add a third screw or a small central boss rather than bringing the tabs back.

## Print orientation
**Front-face-down** on the bed: window = flat bottom layer; walls rise; the **whole back (charger
region + skirt) is open at the top** → no bridge over the big front face, no supports in a blind
pocket (that's the whole reason the skirt back is a separate cover). The snap lip becomes a small
inward overhang at the top; chamfer or a few support lines clean it up. The `cover` prints flat.
(Removing the top tabs also removed the last small top-edge overhangs — nothing bridges now.)

## Charger caveat (verify before a final print)
The physical Nekmit is **dual USB-A output**; the XIAO is fed by a **USB-A→USB-C pigtail**. The
BOM lists it as the "USB-C wall charger" (the power the board sees is USB-C) but the cavity is
sized to the **Nekmit's body** (45.1 × 51.6 × 20.32). **If you swap to a charger with a native
USB-C port or different dimensions, re-measure and update `CHG_W/H/D`** — the whole slip fit,
snap lip, and skirt derive from those three numbers.

## Before committing a full print
1. **Print a test coupon first** — the slip fit (`FIT_W/H/D`), the snap lip (`LIP_*`), and the
   stick boss self-tap all want a cheap coupon before a ~multi-hour full print. FDM holes/fits
   print tight. Confirm the 1.6 mm pilot cap fully hides the posts in your filament while here.
2. Diffuser film over the window (outer rebate) or print in translucent filament.
3. First XIAO flash is done **out of the case** (then OTA over WiFi); set
   `USB_SERVICE_SLOT = True` if you want a cabled-access slot in the skirt bottom.

## Toolchain (same as `../box`)
`img23d` conda env, trimesh CSG. `case_params.py` (single source of truth, clearances ASSERTED
in `build_case.check_clearances()`) → `build_case.py` + `build_cover.py` → `case.stl` +
`cover.stl`. `render_preview.py` (2×2 overview).
`conda run -n img23d python build_all.py`.
