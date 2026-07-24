#!/usr/bin/env python3
"""
Build cover.stl -- the SEPARATE skirt back cover.  A flat plate that drops into the flush
rebate across the main body's skirt back and is held by two M3 self-tappers (countersunk on
the outer/wall face) into the body's gusseted bosses.  It closes the XIAO bay and stops the
board falling out the back; the main body prints open-backed so nothing bridges a blind pocket.

    z = 0        outer (wall-facing) face, flush with the back rim
    z = COVER_T  inner face (toward the XIAO)

    python3 build_cover.py   # -> cover.stl
"""
import warnings
warnings.filterwarnings('ignore')
from trimesh.boolean import difference
import case_params as P
from build_case import rrect, prism, cyl_z, box_at


def build_cover():
    plate = prism(rrect(P.COVER_W, P.COVER_H, max(0.5, P.OUT_R - P.COVER_BORDER)),
                  0, P.COVER_T, P.COVER_CX, P.COVER_CY)
    subs = []
    for (cx, cy) in P.COVER_SCREW_XY:
        subs.append(cyl_z(P.COVER_SCREW_CLR / 2, -0.1, P.COVER_T + 0.1, cx, cy))   # clearance hole
        subs.append(cyl_z(P.COVER_CSK_D / 2, -0.1, 1.2, cx, cy))                    # csk on the wall face

    # top-corner tongue: remove the OUTER slice (z 0..COVER_LIP_T) so the top edge tucks UNDER
    # the body's retaining tabs (which fill that slice).  Extra length in Y lets it slide in.
    step_y = P.COVER_LIP_Y + P.COVER_LIP_SLIDE
    for sx in (-1, 1):
        x_out = sx * (P.COVER_W / 2 + 0.2)
        x_in = sx * (P.COVER_W / 2 - P.COVER_LIP_REACH - 1.0)
        subs.append(box_at(abs(x_out - x_in), step_y + 0.2, P.COVER_LIP_T + 0.1,
                           (x_out + x_in) / 2, P.COVER_TOP_Y - step_y / 2 + 0.1, P.COVER_LIP_T / 2 - 0.05))
    return difference([plate] + subs)


if __name__ == "__main__":
    m = build_cover()
    m.export("cover.stl")
    print("cover.stl written")
    print("  plate  %.1f x %.1f x %.1f mm" % (P.COVER_W, P.COVER_H, P.COVER_T))
    print("  watertight:", m.is_watertight)
