#!/usr/bin/env python3
"""
Build case.stl -- the v2 wall-charger SLIP CASE.  One printed part (plus a stick-on diffuser
film): a full-cover shell with an OPEN BACK that press-fits over a Nekmit flat wall charger.

    z = 0        open back rim (wall plane); charger inserts here, AC prongs protrude out
    z = OUT_D    front face (room side), with the LED window

Features (all dimensions in case_params.py; clearances ASSERTED in check_clearances()):
  * charger cavity (slip-fit) open to the back
  * rear inward SNAP LIP that catches behind the charger's back edge
  * a front pocket that captures the vertical 8-px LED stick (domes out through the window)
  * the LED diffuser window + an outer rebate for a stick-on film
  * a lower SKIRT bay below the charger with a XIAO tray (standoffs + locating ribs)

    python3 build_case.py   # -> case.stl
"""
import warnings
warnings.filterwarnings('ignore')
import trimesh
from trimesh.creation import extrude_polygon
from trimesh.boolean import union, difference
from shapely.geometry import box as sbox
import case_params as P


def rrect(w, h, r):
    r = max(0.01, min(r, w / 2 - 0.01, h / 2 - 0.01))
    return sbox(-(w / 2 - r), -(h / 2 - r), (w / 2 - r), (h / 2 - r)).buffer(r, resolution=16)


def prism(poly, z0, z1, cx=0.0, cy=0.0):
    m = extrude_polygon(poly, z1 - z0)
    m.apply_translation([cx, cy, z0])
    return m


def box_at(dx, dy, dz, cx, cy, cz):
    m = trimesh.creation.box(extents=(dx, dy, dz))
    m.apply_translation([cx, cy, cz])
    return m


def cyl_z(r, z0, z1, x, y):
    m = trimesh.creation.cylinder(radius=r, height=z1 - z0, sections=P.SEG)
    m.apply_translation([x, y, (z0 + z1) / 2])
    return m


def build_case():
    check_clearances()
    CY_MID = (P.YMAX + P.YMIN) / 2

    # -- outer body --------------------------------------------------------------------
    body = prism(rrect(P.OUT_W, P.OUT_H, P.OUT_R), 0, P.OUT_D, 0, CY_MID)

    adds, subs = [], []

    # -- charger cavity (open back: z from below 0 up to the charger's front face) ------
    subs.append(prism(rrect(P.CAV_W, P.CAV_H, P.IN_R), -1.0, P.CAV_D, 0, P.CAV_H / 2))

    # -- LED-stick front pocket (charger front -> front wall; opens back into the cavity)
    pk0 = P.CAV_D - 0.3                       # slight overlap into the charger cavity
    subs.append(box_at(P.POCKET_W, P.POCKET_H, P.D_IN - pk0, 0, P.LED_CY, (pk0 + P.D_IN) / 2))

    # -- skirt bay (below the charger; OPEN at the back, shares the front wall) ----------
    subs.append(prism(rrect(P.CAV_W, P.SKIRT_H, P.IN_R), -1.0, P.D_IN + 0.01, 0, -P.SKIRT_H / 2))
    # flush rebate across the skirt back for the screw-on cover (open at the top, y=0)
    subs.append(box_at(P.REB_X, P.REB_H, P.COVER_T + 0.2, 0, P.REB_CY, P.COVER_T / 2 - 0.1))

    # -- LED window + outer diffuser rebate (through the front wall) ---------------------
    subs.append(box_at(P.WIN_W, P.WIN_H, P.FRONT_T + 2, P.WIN_CX, P.WIN_CY, P.D_IN + P.FRONT_T / 2))
    subs.append(box_at(P.WIN_W + 2 * P.DIFFUSER_REBATE_W, P.WIN_H + 2 * P.DIFFUSER_REBATE_W,
                       2 * P.DIFFUSER_REBATE_T, P.WIN_CX, P.WIN_CY, P.OUT_D))

    # -- optional USB-C service slot in the skirt bottom wall ---------------------------
    if P.USB_SERVICE_SLOT:
        subs.append(box_at(P.USB_SLOT_W, P.WALL + 2, P.USB_SLOT_H,
                           P.XIAO_CX, P.YMIN + P.WALL / 2, P.COVER_T + P.USB_SLOT_H / 2))

    body = difference([body] + subs)

    # -- rear snap lip (inward flange around the charger opening at the back rim) --------
    lip = difference([
        prism(rrect(P.CAV_W, P.CAV_H, P.IN_R), 0, P.LIP_H, 0, P.CAV_H / 2),
        prism(rrect(P.CAV_W - 2 * P.LIP_IN, P.CAV_H - 2 * P.LIP_IN, max(0.5, P.IN_R - P.LIP_IN)),
              -0.5, P.LIP_H + 0.5, 0, P.CAV_H / 2),
    ])
    adds.append(lip)

    # -- LED-stick screw bosses: a nub on the front-wall interior over each hole, with a
    #    blind pilot bored UP into the solid front wall.  M3 self-tappers go in from the open
    #    back (through the stick's Ø3.75 holes); the heads hide in the relief gap behind it.
    strip_pilots = []
    for (hx, hy) in P.LED_HOLE_XY:
        adds.append(cyl_z(P.STRIP_BOSS_OD / 2, P.LED_PCB_TOP_Z, P.D_IN, hx, hy))
        strip_pilots.append(cyl_z(P.SCREW_PILOT / 2, P.LED_PCB_TOP_Z - 0.2, P.PILOT_TOP_Z, hx, hy))

    # -- top retaining lip: two tabs at the top corners of the skirt opening.  Each fills the
    #    OUTER slice (z 0..COVER_LIP_T) over a top corner, overlapping the cover's half-thickness
    #    tongue so the cover top can't pull out.  Rooted along the side border wall.
    for sx in (-1, 1):
        x_root = sx * (P.REB_X / 2 + 0.6)                       # into the side border wall
        x_tip = sx * (P.REB_X / 2 - P.COVER_LIP_REACH)          # inward reach
        subs2_w = abs(x_root - x_tip)
        adds.append(box_at(subs2_w, P.COVER_LIP_Y, P.COVER_LIP_T,
                           (x_root + x_tip) / 2, -P.COVER_LIP_Y / 2, P.COVER_LIP_T / 2))

    # -- skirt-cover screw bosses: a post gusseted out to each side wall, with a blind pilot
    #    the cover's M3 screws bite into (driven from the back, countersunk flush in the cover)
    for (cx, cy) in P.COVER_SCREW_XY:
        wall_in = (1 if cx > 0 else -1) * P.CAV_W2
        adds.append(cyl_z(P.COVER_BOSS_OD / 2, P.COVER_T, P.COVER_T + P.COVER_BOSS_H, cx, cy))
        adds.append(box_at(abs(wall_in - cx) + 2.0, P.COVER_BOSS_OD, P.COVER_BOSS_H,   # gusset
                           (cx + wall_in) / 2, cy, P.COVER_T + P.COVER_BOSS_H / 2))
        strip_pilots.append(cyl_z(P.SCREW_PILOT / 2, P.COVER_T - 0.1, P.COVER_PILOT_TOP_Z, cx, cy))

    case = union([body] + adds)
    case = difference([case] + strip_pilots)
    return case


def check_clearances():
    # slip fit is positive on every axis
    assert P.CAV_W > P.CHG_W and P.CAV_H > P.CHG_H and P.CAV_D > P.CHG_D, "charger cavity too small"
    # LED stick fits the front pocket, which may reach a touch into the top wall / skirt
    assert P.POCKET_H <= P.CAV_H + P.WALL, "LED pocket taller than charger region + top wall"
    assert P.POCKET_W < P.CAV_W, "LED pocket wider than the charger face"
    # the stick's domes sit DOME_RECESS below the window's inner face
    assert abs((P.D_IN - P.LED_DOME_TOP_Z) - P.DOME_RECESS) < 1e-6, "dome recess wrong"
    # screw heads fit behind the stick and clear the charger; pilot doesn't breach the face
    assert P.STRIP_HEAD_RELIEF > P.STRIP_SCREW_HEAD_H, "no room for the screw heads"
    assert P.LED_BACK_Z - P.STRIP_SCREW_HEAD_H > P.CAV_D, "screw heads foul the charger face"
    assert P.PILOT_TOP_Z < P.OUT_D, "boss pilot breaches the front face"
    for (hx, hy) in P.LED_HOLE_XY:      # each hole lands on the PCB, clear of the window
        assert abs(hx - P.LED_CX) <= P.LED_W / 2 and abs(hy - P.LED_CY) <= P.LED_L / 2, "hole off the stick"
        assert abs(hx) > P.WIN_W / 2, "screw boss overlaps the window"
    # window shows every emitter but stays inside the stick body
    assert P.WIN_H >= P.LED_LIT_SPAN + P.LED_5050, "window too short for all emitters"
    assert P.WIN_H < P.LED_L and P.WIN_W < P.LED_W, "window bigger than the stick"
    assert P.OUT_D > P.D_IN, "no front wall"
    # XIAO lies flat on the floor: length along X, width along Z (depth), thickness up in Y
    assert P.PCB_L + 2 * P.PCB_FIT < P.CAV_W, "XIAO too wide for the skirt"
    assert P.PCB_W + 2 * P.PCB_FIT < P.D_IN - P.COVER_T, "XIAO too deep for the skirt bay"
    assert P.PCB_T + P.COMP_Z_ABOVE_PCB < P.SKIRT_H, "XIAO (on the floor) too tall for the skirt"
    # skirt must clear the USB-A pigtail plug hanging off the charger's bottom port (~15 mm)
    assert P.SKIRT_H >= 15.0, "skirt too short for the USB-A pigtail plug"
    # cover: screws (with countersinks) fit inside the plate; bosses clear the XIAO on the floor
    for (cx, cy) in P.COVER_SCREW_XY:
        assert abs(cx) + P.COVER_CSK_D / 2 < P.COVER_W / 2, "cover screw/csk runs off the plate edge"
        assert cy - P.COVER_BOSS_OD / 2 > P.XIAO_FLOOR_Y + P.PCB_T + P.COMP_Z_ABOVE_PCB, \
            "cover boss fouls the XIAO on the floor"
    assert P.REB_H > 0, "skirt cover rebate has no height"
    assert 0 < P.COVER_LIP_T < P.COVER_T, "cover lip thicker than the cover"
    assert P.COVER_LIP_REACH < P.COVER_W / 2, "cover lip reaches past the plate centre"
    # snap lip is sane
    assert P.LIP_IN < P.WALL and P.LIP_H < P.CAV_D, "snap lip out of range"


if __name__ == "__main__":
    m = build_case()
    m.export("case.stl")
    b = m.bounds
    print("case.stl written")
    print("  outer envelope  %.1f (W) x %.1f (H) x %.1f (D) mm" % (P.OUT_W, P.OUT_H, P.OUT_D))
    print("  protrusion from wall  %.1f mm" % P.OUT_D)
    print("  charger cavity  %.2f x %.2f x %.2f mm" % (P.CAV_W, P.CAV_H, P.CAV_D))
    _eng = P.PILOT_TOP_Z - P.LED_PCB_TOP_Z
    print("  stick screws    2x M3 self-tap from the back; ~%.1f mm thread engagement "
          "-> use M3x%.0f" % (_eng, round(P.LED_PCB_T + _eng)))
    print("  cover screws    2x M3 self-tap through the cover -> use M3x%.0f"
          % round(P.COVER_T + P.COVER_PILOT_DEPTH))
    print("  bounds  X[%.2f,%.2f] Y[%.2f,%.2f] Z[%.2f,%.2f]" %
          (b[0][0], b[1][0], b[0][1], b[1][1], b[0][2], b[1][2]))
    print("  watertight:", m.is_watertight)
