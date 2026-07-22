#!/usr/bin/env python3
"""
Build lid.stl -- the cap: a roof plate that sits on the shell rim, the LED diffuser
window (with an outer rebate for a stick-on film), four countersunk screw holes over the
side-wall bosses, and two pads that clamp the XIAO down onto its standoffs.  The LED stick
lives just below the window, screwed to the shell's posts.

    z = SHELL_H  lid underside (rests on the shell rim)
    z = OUT_Z    top of the box

    python3 build_lid.py   # -> lid.stl
"""
import warnings
warnings.filterwarnings('ignore')
from trimesh.boolean import union, difference
import case_params as P
from build_shell import rrect, prism, box_at, cyl_z


def build_lid():
    lid = prism(rrect(P.OUT_X, P.OUT_Y, P.OUT_R), P.SHELL_H, P.OUT_Z)

    # board clamp pads: hang from the underside down to just above the PCB top, straddling
    # the USB-C shell
    adds = []
    for (px, py) in P.BOARD_PAD_XY:
        adds.append(cyl_z(P.BOARD_PAD_D / 2, P.PCB_TOP_Z + 0.3, P.SHELL_H, px, py))
    lid = union([lid] + adds)

    subs = []
    # LED diffuser window (through the lid)
    subs.append(box_at(P.WIN_W, P.WIN_D, P.LID_T + 2, P.WIN_CX, P.WIN_CY, P.SHELL_H + P.LID_T / 2))
    # outer rebate on top for a stick-on diffusion film / thin acrylic
    subs.append(box_at(P.WIN_W + 2 * P.DIFFUSER_REBATE_W, P.WIN_D + 2 * P.DIFFUSER_REBATE_W,
                       2 * P.DIFFUSER_REBATE_T, P.WIN_CX, P.WIN_CY, P.OUT_Z))
    # four screw holes + countersinks over the side-wall bosses
    for (bx, by) in P.LID_BOSS_XY:
        subs.append(cyl_z(P.LID_SCREW_CLR / 2, P.SHELL_H - 1, P.OUT_Z + 1, bx, by))
        subs.append(cyl_z(P.LID_CSK_D / 2, P.OUT_Z - 1.4, P.OUT_Z + 0.1, bx, by))

    lid = difference([lid] + subs)
    return lid


if __name__ == "__main__":
    m = build_lid()
    m.export("lid.stl")
    print("lid.stl written;  watertight:", m.is_watertight)
