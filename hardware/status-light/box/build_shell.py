#!/usr/bin/env python3
"""
Build shell.stl -- the body of the `rivian-status` desk light: floor + four walls, the
XIAO board pocket (corner standoffs + locating ribs), the USB-C port in the back (+Y)
wall, the antenna-pigtail notch in the front (−Y) wall, two posts the LED stick screws
down onto, and four side-wall bosses the lid screws into.  lid.stl (with the LED window)
caps it.

    z = 0        outer underside of the floor
    z = SHELL_H  shell rim (lid underside)

Every dimension lives in case_params.py; clearances are ASSERTED in check_clearances().

    python3 build_shell.py   # -> shell.stl
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore')
from trimesh.creation import extrude_polygon
from trimesh.boolean import union, difference
from shapely.geometry import box as sbox
import case_params as P


def rrect(w, h, r):
    r = max(0.01, min(r, w / 2 - 0.01, h / 2 - 0.01))
    return sbox(-(w / 2 - r), -(h / 2 - r), (w / 2 - r), (h / 2 - r)).buffer(r, resolution=16)


def prism(poly, z0, z1):
    m = extrude_polygon(poly, z1 - z0); m.apply_translation([0, 0, z0]); return m


def box_at(dx, dy, dz, cx, cy, cz):
    m = trimesh.creation.box(extents=(dx, dy, dz)); m.apply_translation([cx, cy, cz]); return m


def cyl_z(r, z0, z1, x, y):
    m = trimesh.creation.cylinder(radius=r, height=z1 - z0, sections=P.SEG)
    m.apply_translation([x, y, (z0 + z1) / 2]); return m


def boss(od, z0, z1, x, y, pilot, pilot_depth):
    """a self-tapping boss: solid cylinder minus a blind pilot bore from the top."""
    b = cyl_z(od / 2, z0, z1, x, y)
    hole = cyl_z(pilot / 2, z1 - pilot_depth, z1 + 0.1, x, y)
    return difference([b, hole])


def build_shell():
    check_clearances()
    IN_R = P.OUT_R - P.WALL

    body = prism(rrect(P.OUT_X, P.OUT_Y, P.OUT_R), 0, P.SHELL_H)
    cav = prism(rrect(P.IN_X, P.IN_Y, IN_R), P.FLOOR_T, P.SHELL_H + 1)
    shell = difference([body, cav])

    adds, subs = [], []

    # -- board pocket ----------------------------------------------------------------
    hw, hl = P.PCB_W / 2, P.PCB_L / 2
    for sx in (-1, 1):
        for sy in (-1, 1):
            adds.append(cyl_z(2.6, P.FLOOR_T, P.PCB_BACK_Z,
                              P.BOARD_CX + sx * (hw - 2.5), P.BOARD_CY + sy * (hl - 2.5)))
    rt, rh, zc = P.BOARD_RIB_T, P.BOARD_RIB_H, P.FLOOR_T + P.BOARD_RIB_H / 2
    adds.append(box_at(P.PCB_W + 2 * P.PCB_FIT + 2 * rt, rt, rh,
                       P.BOARD_CX, P.PCB_FRONT_Y - P.PCB_FIT - rt / 2, zc))          # −Y front
    for sx in (-1, 1):                                                              # ±X sides
        adds.append(box_at(rt, P.PCB_L + 2 * P.PCB_FIT, rh,
                           P.BOARD_CX + sx * (hw + P.PCB_FIT + rt / 2), P.BOARD_CY, zc))
    # (+Y edge is located by the back wall + the USB-C sitting in its port)

    # -- LED-stick mount posts (the stick screws down onto these) ---------------------
    # posts sit under the stick's holes, which are offset off the LED row (POST_CY)
    for hx in P.LED_HOLE_X:
        adds.append(boss(P.POST_OD, P.FLOOR_T, P.STRIP_BOTTOM_Z, hx, P.POST_CY,
                         P.SCREW_PILOT, P.STRIP_SCREW_LEN))

    # -- lid screw bosses (side walls) -----------------------------------------------
    for (bx, by) in P.LID_BOSS_XY:
        adds.append(boss(P.LID_BOSS_OD, 0, P.SHELL_H, bx, by, P.SCREW_PILOT, P.LID_SCREW_LEN))

    shell = union([shell] + adds)

    # -- openings --------------------------------------------------------------------
    # USB-C port in the back (+Y) wall
    by0, by1 = P.IN_Y2 - 1, P.OUT_Y2 + 1
    subs.append(box_at(P.USB_PORT_W, by1 - by0, P.USB_PORT_H,
                       P.USB_PORT_CX, (by0 + by1) / 2, P.USB_PORT_CZ))
    # antenna pigtail notch in the front (−Y) wall (optional -- off by default; the antenna
    # stays coiled inside the box)
    if P.ANT_EXIT:
        fy0, fy1 = -P.OUT_Y2 - 1, -P.IN_Y2 + 1
        subs.append(box_at(P.ANT_NOTCH_W, fy1 - fy0, P.ANT_NOTCH_H + 1,
                           P.UFL_X, (fy0 + fy1) / 2, P.SHELL_H - P.ANT_NOTCH_H / 2 + 0.5))

    shell = difference([shell] + subs)
    return shell


def check_clearances():
    assert P.IN_X >= P.LED_L + 1.0, "interior too narrow for the LED stick"
    assert P.IN_Y >= P.PCB_L + 0.5, "interior too shallow for the board"
    # the stick sits above the tallest board part AND the mated antenna plug
    assert P.STRIP_BOTTOM_Z > P.PCB_BACK_Z + P.COMP_Z_ABOVE_PCB_BACK + 0.5, "stick fouls the board"
    assert P.STRIP_BOTTOM_Z > P.UFL_TOP_Z + P.UFL_PLUG_CLEARANCE, "stick fouls the antenna plug"
    # the shell rim lands between the stick's PCB top and its dome tops, so the domes poke
    # into the lid window while the lid still clears the PCB edges
    assert P.SHELL_H > P.STRIP_PCB_TOP_Z, "lid underside fouls the stick PCB"
    assert P.SHELL_H < P.STRIP_DOME_TOP_Z, "domes don't reach the window"
    # USB-C reaches through the back wall; the port clears the aperture
    assert P.USB_TIP_Y > P.IN_Y2, "USB-C doesn't reach the back wall"
    assert P.USB_PORT_W >= P.USB_SHELL_W and P.USB_PORT_H >= P.USB_SHELL_H, "USB port too small"
    # the LED window (in the lid) spans the emitters but stays inside the stick
    assert P.WIN_W >= P.LED_LIT_SPAN + P.LED_5050, "window doesn't cover all the emitters"
    assert P.WIN_W < P.LED_L, "window wider than the stick"
    # lid bosses + stick posts clear the (narrow) board footprint
    for (bx, by) in P.LID_BOSS_XY:
        assert abs(bx) - P.LID_BOSS_OD / 2 > P.PCB_W / 2, "a lid boss overlaps the board"
    for hx in P.LED_HOLE_X:
        assert abs(hx) - P.POST_OD / 2 > P.PCB_W / 2 - 0.5, "a stick post overlaps the board"


if __name__ == "__main__":
    m = build_shell()
    m.export("shell.stl")
    b = m.bounds
    print("shell.stl written")
    print("  outer box  %.1f x %.1f x %.1f mm" % (P.OUT_X, P.OUT_Y, P.OUT_Z))
    print("  bounds     X[%.2f,%.2f] Y[%.2f,%.2f] Z[%.2f,%.2f]" %
          (b[0][0], b[1][0], b[0][1], b[1][1], b[0][2], b[1][2]))
    print("  watertight:", m.is_watertight)
