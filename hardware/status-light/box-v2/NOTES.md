# box-v2 — design notes & decisions (for future sessions)

Why the v2 case is shaped the way it is. The *numbers* live in `case_params.py` (provenance-
commented) and the *build* in `README.md`; this file is the **reasoning** — the non-obvious
trade-offs, the decisions locked with the user, and the things a future session would otherwise
have to re-derive. Read alongside the repo `CLAUDE.md` and `../board_spec.md`.

## What it is (one line)
A one-piece slip shell that press-fits over a **Nekmit Ultra-Thin flat wall charger**
(43.18 × 50.80 × 20.32 mm). The charger is the mechanical **and** electrical anchor: it plugs
into the wall, AC prongs exit the **open back**, an inward snap lip retains it. LED stick shows
out the front; XIAO + pigtail live in a lower skirt; skirt back is a separate screw-on cover.

## Decisions locked with the user (don't silently revisit)
- **8-px WS2812 stick, re-used from v1** — *not* the "16-LED array" the original blueprint text
  mentioned. The firmware pixel map is 8 (plan §7).
- **Stick mounted VERTICALLY** on the front (charger face is portrait 50.8 tall; the 51.5 mm
  stick fits the height, not the 43.18 width).
- **XIAO rests flat on the skirt floor** — no tray/standoffs. Gravity + the wires to the stick
  hold it; the cover stops it falling out the back.
- **Skirt back = a SEPARATE screw-on cover** (not an integral wall) — so the body prints open-
  backed with no supports in a blind pocket.
- **Stick retention = hidden M3 screws from the open back** (user: "hidden back, but use M3").
- **Cover top edge = tongue-under-corner-tab** retention (added on request).

## The depth story (why it protrudes ~29 mm, not the blueprint's ~20.7)
The blueprint assumed the charger touches the front wall. It doesn't, for two stacked reasons —
**this is the #1 thing to understand before touching the Z stack:**
1. The vertical LED stick (3.0 mm: 1.6 PCB + 1.4 domes) sits **in front of** the charger.
2. The stick is **back-screwed**, so there's a **head-relief gap** (`STRIP_HEAD_RELIEF`, 2.6 mm)
   between the charger face and the stick's PCB back for the hidden screw heads.

So internal depth = charger (20.72 slip) + relief (2.6) + stick (3.0) → front wall interior at
~26.9, outer face ~29.1 mm. `D_IN`/`OUT_D` in `case_params.py` derive from exactly this chain.

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

## XIAO in the skirt
- Lies **flat on the floor** (21 × 17.8 footprint on the X-Z plane, only ~5.5 mm tall in Y).
- **Skirt height (`SKIRT_H`, 17 mm) is floored by the USB-A pigtail plug's vertical clearance
  (~15 mm, per the blueprint), NOT the XIAO.** Don't shrink it below ~15 or the plug hanging off
  the charger's bottom port collides. (It was 24 mm when the XIAO had a front-wall tray.)
- The stick pocket, charger cavity, and skirt share **one open interior** — wiring routes freely
  (no dedicated slot needed).
- **Not retained sideways** (user's call) — add foam if it rattles in transit. A molded snap tab
  is the clean follow-up if ever wanted.

## Skirt back cover + top lip
- Flush rebate + **2× M3×8** into **gusseted** side-wall bosses (posts alone can't root over the
  open cavity — they must merge with / rib to the perimeter walls).
- **Top edge:** the cover's top corners step to a **half-thickness tongue** that tucks under two
  **body tabs** filling the outer slice → flush outside, can't pull out. Assembly: tilt tongue in
  under the tabs, then screw the bottom.
- **Corner tabs, NOT a full-width lip:** printing front-face-down, a full-width top lip would be
  a ~44 mm **bridge** at the top of the print. Short corner tabs are brief overhangs instead.

## Print orientation
**Front-face-down** on the bed: window = flat bottom layer; walls rise; the **whole back (charger
region + skirt) is open at the top** → no bridge over the big front face, no supports in a blind
pocket (that's the whole reason the skirt back is a separate cover). The snap lip becomes a small
inward overhang at the top; chamfer or a few support lines clean it up. The `cover` prints flat.

## Charger caveat (verify before a final print)
The physical Nekmit is **dual USB-A output**; the XIAO is fed by a **USB-A→USB-C pigtail**. The
BOM lists it as the "USB-C wall charger" (the power the board sees is USB-C) but the cavity is
sized to the **Nekmit's body** (43.18 × 50.80 × 20.32). **If you swap to a charger with a native
USB-C port or different dimensions, re-measure and update `CHG_W/H/D`** — the whole slip fit,
snap lip, and skirt derive from those three numbers.

## Before committing a full print
1. **Print a test coupon first** — the slip fit (`FIT_W/H/D`), the snap lip (`LIP_*`), the stick
   boss self-tap, and the cover tongue slide-fit (`COVER_LIP_SLIDE`) all want a cheap coupon
   before a ~multi-hour full print. FDM holes/fits print tight.
2. Diffuser film over the window (outer rebate) or print in translucent filament.
3. First XIAO flash is done **out of the case** (then OTA over WiFi); set
   `USB_SERVICE_SLOT = True` if you want a cabled-access slot in the skirt bottom.

## Toolchain (same as `../box`)
`img23d` conda env, trimesh CSG. `case_params.py` (single source of truth, clearances ASSERTED
in `build_case.check_clearances()`) → `build_case.py` + `build_cover.py` → `case.stl` +
`cover.stl`. `render_preview.py` (2×2 overview) + `render_lip_detail.py` (cover lip section).
`conda run -n img23d python build_all.py`.
