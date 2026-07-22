# XIAO ESP32-S3 — mechanical spec (as used by the enclosure)

Every number the box relies on, and exactly where it came from. Same discipline as
`../../../sonos-nest/hardware`: CAD-verified numbers are plain; anything estimated is
flagged **⚠️VERIFY**. All values are already baked into `box/case_params.py` — this file
is the paper trail.

## Sources (Seeed official)

Pulled from the Seeed wiki resource list for the XIAO ESP32-S3
(`wiki.seeedstudio.com/xiao_esp32s3_getting_started`):

| File | What | Kept where |
|---|---|---|
| `seeed-studio-xiao-esp32s3-3d_model.zip` → **`XIAO-ESP32S3 v2.step`** | vendor 3D CAD — the authority for outline, thickness, connector positions | committed to **`ref/`** |
| `XIAO_ESP32S3_v1.1_Dimensioning.dxf`, `TOP.dxf` | 2D fab drawings — cross-check + the bottom-copper view | render in **`ref/xiao_footprint_bottom.png`** |

The STEP was converted to a mesh with `cascadio` (STEP→glTF) and measured in `trimesh`,
in **mm**. In the STEP's own frame: X = board length, Z = board width, Y = thickness.
`ref/xiao_ortho_measured.png` is the orthographic proof render those numbers came off.

## The board (measured off the STEP)

| Dimension | Value | Provenance |
|---|---|---|
| PCB length (USB-C ↔ antenna) | **21.0 mm** | STEP `SOLID` X-span 20.955 |
| PCB width | **17.8 mm** | STEP `SOLID` Z-span 17.780 |
| PCB thickness | **1.25 mm** | STEP `SOLID` Y-span −0.25…1.00 (Seeed spec: 1.2) |
| Bottom side | **flat** — no tall components | STEP: overall Y-min = PCB back face |
| Tallest top component | **4.21 mm** above PCB back (the USB-C shell) | STEP `CHAMFER9` |
| RF shield height | 3.00 mm above PCB back | STEP `Shield` |

### USB-C (on the +X short end)
| | Value | Provenance |
|---|---|---|
| Overhang past the PCB edge | **1.53 mm** | STEP: connector X-max 13.809 vs PCB 12.282 |
| Shell width | **8.94 mm** | STEP Z-span |
| Aperture height | **3.26 mm** | STEP Y-span 4.20 minus the lip |
| Aperture centre above PCB **back** face | **2.36 mm** | STEP Y-centre 2.11, PCB back −0.25 |
| Centred on the board width? | **yes** | STEP USB Z-centre −6.12 ≈ PCB Z-centre −6.11 |

### U.FL external-antenna jack (on **top**, at the −X / antenna end)
CLAUDE.md is explicit: **this antenna must stay plugged in or WiFi dies**, so the box has
to leave room for the mated plug and let the pigtail escape.
| | Value | Provenance |
|---|---|---|
| Along the board length, from centre | **−6.95 mm** (toward −X) | STEP `U.FL-R-SMT` X-centre |
| Height above the PCB **top** face | **1.25 mm** (bare jack; a mated plug adds ~3) | STEP Y-top 2.25 − PCB top 1.00 |
| Offset from the width centreline | **4.73 mm** | STEP Z-centre −10.84 vs −6.11 |

## ⚠️ The XIAO has **no screw mounting holes**

Confirmed two ways: the STEP has no mounting bores (only the connectors + a shield can),
and the bottom-copper DXF (`ref/xiao_footprint_bottom.png`) shows only the two 7-pin
castellated rows plus **two Ø0.6 tooling holes** — no M2/M3 pads. XIAO boards are meant to
be retained by their pins or by the host enclosure.

**Consequence for the box:** the board is **not screwed down**. It drops onto four corner
standoffs inside a rib pocket, is pinned in X by the USB-C sitting in its port, and is
clamped from above by two pads on the lid. Wires are **hand-soldered directly to the pads**
(user-confirmed — no header, so the board back stays flat and lies flush on the standoffs).

## LED stick — measured by the user (generic bare 8-bit stick)

Not in any vendor CAD, so these are **caliper values** the user measured off the real part:

| Dimension | Value |
|---|---|
| Length | **51.5 mm** |
| Width (short edge) | **10 mm** |
| Total thickness incl. LEDs | **3.0 mm** → PCB **1.6** + domes **1.4** |
| Lit span (first→last LED centre) | **45 mm** (emitter extent ≈ 50 mm) |
| Mounting holes | **2**, Ø **3.75 mm**, at **±13.0 mm** along the length (26 mm apart) |
| Hole offset from the LED row | **1.5 mm** to one long edge (dialled in vs the board) |

From the board photo the holes are off to one long edge (by the Cx caps / pad column),
~¼ and ¾ along the length, **1.5 mm** off the LED row (`LED_HOLE_DY`). The box **screws
the stick down** onto two posts through those Ø3.75 holes (2× M3). The lid window spans the
full **50 mm emitter extent** (lit span + one emitter), and the LEDs sit **2 mm below the
case top** (`LED_BELOW_TOP`) so they read near the surface of the window slot.

## Note — USB-C exits the back wall

Because the stick is stacked over the board and the lid is screwed on, the board lies
front-to-back under the stick and **USB-C exits the back (+Y) wall, centred and proud**
(see `box/README.md` "Layout note"). An end-exit variant is a one-param change but needs
the board recessed ~5 mm.

The **U.FL antenna stays inside** the box by default (`ANT_EXIT = False`) — the jack keeps
its plug clearance and the pigtail coils in the open floor. Flip `ANT_EXIT = True` for a
front-wall exit slot if the in-box antenna underperforms.
