# MEASUREMENTS — every number the enclosure is built from

One place for all the dimensions in `hardware/`. Everything here is the single source of
truth `status-light/box/case_params.py` reads from; this doc is the human-readable index.
Provenance tags:

- **CAD** — read off Seeed's official STEP (`status-light/ref/XIAO-ESP32S3 v2.step`), measured in mm.
- **MEAS** — calipered off the physical part by the user.
- **SPEC** — datasheet / standard part.
- **DERIVED** — computed from the above in `case_params.py` (not a free choice).
- **CHOICE** — a design value (wall thickness, clearances, etc.).

All values in **millimetres**. Frame: **+X** width (long axis) · **+Y** depth (front→back) ·
**+Z** up. Origin centred in X/Y, z = 0 at the floor's underside.

---

## 1. XIAO ESP32-S3 board  (source: CAD)

| Dimension | Value | Src |
|---|---|---|
| PCB length (USB-C ↔ antenna axis) | **21.0** | CAD |
| PCB width | **17.8** | CAD |
| PCB thickness | **1.25** | CAD |
| Bottom side | **flat** (no tall parts) | CAD |
| Tallest top part (USB-C shell) above PCB back | **4.21** | CAD |
| RF-shield height above PCB back | 3.00 | CAD |
| **Screw mounting holes** | **NONE** (retained by edges/box) | CAD |
| USB-C overhang past PCB edge | **1.53** | CAD |
| USB-C shell width | **8.94** | CAD |
| USB-C aperture height | **3.26** | CAD |
| USB-C aperture centre above PCB back face | **2.36** | CAD |
| U.FL jack — along length from board centre | **−6.95** | CAD |
| U.FL jack — offset across width | **4.73** | CAD |
| U.FL jack — height above PCB top face | **1.25** | CAD |

Wires solder **directly to the pads** (no header) → the board back is flat and lies on the
standoffs.

## 2. WS2812 LED stick — generic bare 8-bit  (source: MEAS)

| Dimension | Value | Src |
|---|---|---|
| Length | **51.5** | MEAS |
| Width (short edge) | **10.0** | MEAS |
| PCB thickness | **1.6** | MEAS (3.0 total − 1.4 domes) |
| Dome height above PCB face | **1.4** | MEAS |
| Total thickness incl. LEDs | **3.0** | MEAS |
| Pixel count | **8** | fixed (firmware, plan §7) |
| Lit span (first→last LED centre) | **45.0** | MEAS |
| 5050 emitter size | **5.0** sq | SPEC |
| Emitter extent (lit span + emitter) | **50.0** | DERIVED |
| Mounting holes | **2**, Ø **3.75** | MEAS |
| Hole positions along length (±X from centre) | **±13.0** (26 mm apart) | MEAS |
| Hole offset off the LED row (toward one edge) | **1.5** | MEAS (dialled vs board) |

## 3. Fasteners  (M3 self-tapping into printed bosses — source: CHOICE/SPEC)

| Item | Value |
|---|---|
| Thread-forming pilot Ø (M3) | 2.6 |
| Stick → post screws | 2× **M3 × 6** (through Ø3.75 holes) |
| Lid → side-wall bosses | 4× **M3 × 8**, countersunk (Ø3.4 clearance, Ø6 csk) |

## 4. Enclosure — shell + lid  (source: CHOICE, then DERIVED)

| Dimension | Value | Src |
|---|---|---|
| Wall thickness | 2.2 | CHOICE |
| Floor thickness | 2.0 | CHOICE |
| Lid thickness | 2.5 | CHOICE |
| Board standoff height | 2.0 | CHOICE |
| Stick-mount post Ø | 6.0 | CHOICE |
| Stick PCB underside height (`STRIP_BOTTOM_Z`) | 11.0 | CHOICE (clears board + antenna plug) |
| LED dome tops below the case top (`LED_BELOW_TOP`) | 2.0 | CHOICE |
| Lid window (X × Y) | **51.0 × 6.0** | DERIVED (covers all emitters) |
| Lid board-clamp pads | 2× Ø3.4, **13.1 mm apart**, straddling the 9 mm USB-C shell | DERIVED |
| USB-C port (back wall, X × Z) | 12.0 × 7.0 | CHOICE |
| Antenna pigtail exit slot | **none** (`ANT_EXIT = False`, antenna coils inside) | CHOICE |
| Room beyond each strip end (`STRIP_END_GAP`) | 9.7 (for the soldered end-wires) | CHOICE |
| Front working bay (`BOARD_FRONT_GAP`) | 8.4 (wires + antenna coil) | CHOICE |
| **Outer size (X × Y × Z)** | **75.3 × 34.3 × 16.0** | DERIVED |
| Shell rim height (`SHELL_H`) | 13.5 | DERIVED |
| Interior (X × Y) | 70.9 × 29.9 | DERIVED |

### Layout in one line
XIAO lies flat on the floor (centred), USB-C proud out the **back wall**; the LED stick is
screwed to two posts **above** it (LEDs up), showing through the lid window 2 mm under the
top; U.FL antenna coils inside; four M3 screws hold the lid.

## 5. Still worth a caliper pass before a final print
- **Hole offset** (`LED_HOLE_DY`, 1.5) direction + amount — dialled from the board photo/feel.
- **DIN end** orientation (`LED_DIN_AT_PLUS_X`) — cosmetic (shortest D0 wire run).
- **Peg/boss/hole fits** print tight in FDM — run a test coupon.

> To change any number: edit `status-light/box/case_params.py`, then
> `conda run -n img23d python build_all.py`. `build_shell.py` asserts its own clearances,
> so an illegal edit fails loudly instead of writing a broken STL.
