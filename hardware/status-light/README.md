# status-light — the shipped `rivian-status` unit

The headless Rivian charge/range light: a **Seeed XIAO ESP32-S3** driving an **8-pixel
WS2812 stick**, in a desk-standing box that shows the pixels out the front.

| | |
|---|---|
| **[board_spec.md](board_spec.md)** | every XIAO dimension the box uses + where it came from (the "no mounting holes" finding lives here) |
| **[ref/](ref/)** | vendor CAD: `XIAO-ESP32S3 v2.step` + the measured proof renders |
| **[box/](box/)** | the two-part enclosure (`shell.stl` + `lid.stl`) and its generators |

## Conventions (shared with `../../../sonos-nest/hardware`)
- Python CSG toolchain (trimesh + manifold3d), the `img23d` conda env — **not** OpenSCAD.
- Every dimension lives in a `*_params.py`; builders `assert` their own clearances.
- Board numbers come from vendor CAD; anything estimated is flagged **⚠️VERIFY** and
  caliper-checked before a final print.
- Keep `hardware/` commits separate from firmware — this is user-owned mechanical work.
