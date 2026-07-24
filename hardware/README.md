# hardware/ — rivian-status physical build

3D-printed enclosure for the headless Rivian status light (XIAO ESP32-S3 + 8-pixel WS2812
stick). Modelled the same way as the sibling **[sonos-nest](../../sonos-nest/hardware)**
project: a Python CSG toolchain (**trimesh + manifold3d**, not OpenSCAD), one shared
`*_params.py` per part, `build_*.py` generators → `.stl`, and a `render_preview.py`. The
toolchain lives in the `img23d` conda env (`conda run -n img23d python build_all.py`).

**→ [MEASUREMENTS.md](MEASUREMENTS.md) — every dimension the enclosure is built from, in one table.**

| unit | board | enclosures |
|------|-------|-----------|
| **[status-light/](status-light/)** | Seeed XIAO ESP32-S3 + generic 8-bit WS2812 stick | **[box/](status-light/box/)** — v1 top-bar desk enclosure (shell + lid) · **[box-v2/](status-light/box-v2/)** — v2 slip case over a flat wall charger (case + cover) |

## Conventions
- Board outlines/holes come from **verified vendor CAD** (Seeed's STEP); connector
  positions not in the CAD are estimated from photos/datasheets and flagged **⚠️VERIFY**
  in the part's `board_spec.md` — caliper-check before a final print.
- Keep `hardware/` commits **separate from firmware**; this is user-owned work.
- The `img23d` env also has `ezdxf` + `cascadio` (added here) for reading vendor DXF/STEP.
