#!/usr/bin/env python3
"""
build template.stl -- a small flat ALIGNMENT JIG for the v2 LED-stick mount.

It reproduces ONLY the front-wall region around the stick -- the diffuser window, the seating
pocket, and the two hidden M3 screw posts (bosses + blind pilots) -- at EXACTLY the case's
positions (all read from case_params: STICK_CX / LED_HOLE_XY, driven by LED_MOUNT_OFFS_X, and the
window on LED_CX).  It is the cheap way to verify the mount before committing a multi-hour case
reprint.

Use it:
  1. Print it front-face-DOWN (window on the bed), same as the case.
  2. From the OPEN BACK, drop the real 8-px stick into the pocket and drive 2x M3 self-tappers
     through its holes into the two posts (heads on the back, exactly like the case).
  3. Look through the window from the front: the lit emitters should sit CENTRED in the slot.
  4. If they're still off, measure the left/right error (+ up/down) and adjust LED_MOUNT_OFFS_X
     (and add a window Y offset if needed) in case_params.py, rebuild, reprint the coupon.

Because it shares case_params, a template that lines up guarantees the full case will too.

    conda run -n img23d python build_template.py   # -> template.stl
"""
import warnings; warnings.filterwarnings('ignore')
from trimesh.boolean import union, difference
import case_params as P
from build_case import rrect, prism, cyl_z, box_at

RIM    = 3.0                      # solid wall kept around the pocket + window
ENDPAD = 3.0                     # plate margin past the stick ends (Y)
BACK   = P.LED_BACK_Z - 1.0      # open-back plane: 1 mm behind the stick's PCB back face


def build_template():
    hx = [h[0] for h in P.LED_HOLE_XY]
    # footprint must contain the window, the shifted pocket, and both bosses, + RIM
    x_lo = min(P.WIN_CX - P.WIN_W / 2, P.STICK_CX - P.POCKET_W / 2,
               min(hx) - P.STRIP_BOSS_OD / 2) - RIM
    x_hi = max(P.WIN_CX + P.WIN_W / 2, P.STICK_CX + P.POCKET_W / 2,
               max(hx) + P.STRIP_BOSS_OD / 2) + RIM
    y_lo = P.LED_CY - P.LED_L / 2 - ENDPAD
    y_hi = P.LED_CY + P.LED_L / 2 + ENDPAD
    TW, TH = x_hi - x_lo, y_hi - y_lo
    cx, cy = (x_lo + x_hi) / 2, (y_lo + y_hi) / 2

    body = prism(rrect(TW, TH, 3.0), BACK, P.OUT_D, cx, cy)
    subs = []
    # seating pocket (open back up to the front-wall interior) -- locates the stick in X/Y
    subs.append(box_at(P.POCKET_W, P.POCKET_H, P.D_IN - BACK + 1.0,
                       P.STICK_CX, P.LED_CY, (BACK - 1.0 + P.D_IN) / 2))
    # diffuser window through the front wall (on the emitter row, LED_CX)
    subs.append(box_at(P.WIN_W, P.WIN_H, P.FRONT_T + 2.0,
                       P.WIN_CX, P.WIN_CY, P.D_IN + P.FRONT_T / 2))
    body = difference([body] + subs)

    # the two screw posts (boss + blind pilot), identical to the case's stick bosses
    adds, pilots = [], []
    for (bx, by) in P.LED_HOLE_XY:
        adds.append(cyl_z(P.STRIP_BOSS_OD / 2, P.LED_PCB_TOP_Z, P.D_IN, bx, by))
        pilots.append(cyl_z(P.SCREW_PILOT / 2, P.LED_PCB_TOP_Z - 0.2, P.PILOT_TOP_Z, bx, by))
    # posts sit outboard of the window now -> complete round bosses (no window-edge clipping)
    t = union([body] + adds)
    t = difference([t] + pilots)
    return t


if __name__ == "__main__":
    m = build_template()
    m.export("template.stl")
    b = m.bounds
    print("template.stl written")
    print("  plate            %.1f (X) x %.1f (Y) x %.1f (Z) mm" %
          (b[1][0] - b[0][0], b[1][1] - b[0][1], b[1][2] - b[0][2]))
    print("  screw posts X    %.2f  (Y = %.1f, %.1f); groove shifted +%.2f, window on its -X edge" %
          (P.LED_HOLE_XY[0][0], P.LED_HOLE_XY[0][1], P.LED_HOLE_XY[1][1], P.STICK_CX))
    print("  window centre X  %.2f  (W x H = %.2f x %.2f)" % (P.WIN_CX, P.WIN_W, P.WIN_H))
    print("  watertight:", m.is_watertight)
