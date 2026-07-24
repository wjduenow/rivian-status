#!/usr/bin/env python3
"""
Render render_preview.png (2x2) for the v2 wall-charger slip case:
  (1) 3D assembly -- case (translucent), charger, LED stick, XIAO flat on the floor, back cover
  (2) front view (X-Y) -- vertical window + 8 pixels, hidden stick bosses, XIAO on the floor
  (3) side section (Y-Z) -- open back, charger, stick in the front pocket, XIAO on floor, cover
  (4) depth section (X-Z) -- charger, stick pocket, window

    python3 render_preview.py   # -> render_preview.png
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore'); import matplotlib; matplotlib.use('Agg')
import matplotlib.colors as mcolors
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Circle
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import case_params as P
import build_case as B
import build_cover as CV

case = B.build_case()
cover = CV.build_cover()
CY_MID = (P.YMAX + P.YMIN) / 2

# stand-in parts
charger = trimesh.creation.box(extents=(P.CHG_W, P.CHG_H, P.CHG_D))
charger.apply_translation([0, P.CHG_H / 2, P.CHG_D / 2])
stick = trimesh.creation.box(extents=(P.LED_W, P.LED_L, P.LED_PCB_T))
stick.apply_translation([P.LED_CX, P.LED_CY, P.LED_BACK_Z + P.LED_PCB_T / 2])
xiao = trimesh.creation.box(extents=(P.PCB_L, P.PCB_T, P.PCB_W))   # lying FLAT on the floor
xiao.apply_translation([P.XIAO_CX, P.XIAO_CY, P.XIAO_CZ])

C_CASE, C_CHG, C_LED, C_XIAO, C_COVER = '#93b4dd', '#666a70', '#c0424a', '#2a6b32', '#c58a3d'

fig = plt.figure(figsize=(15, 12))
fig.suptitle(f"rivian-status v2 — wall-charger slip case   "
             f"({P.OUT_W:.1f} W × {P.OUT_H:.1f} H × {P.OUT_D:.1f} D mm, "
             f"protrudes {P.OUT_D:.1f} mm)", fontsize=14, weight='bold')


def shade(ax, mesh, color, alpha=1.0):
    light = np.array([-0.4, -0.6, 0.75]); light /= np.linalg.norm(light)
    b = 0.35 + 0.65 * np.clip(mesh.face_normals @ light, 0, 1)
    fc = np.clip(b[:, None] * np.array(mcolors.to_rgb(color)), 0, 1)
    ax.add_collection3d(Poly3DCollection(mesh.triangles, facecolors=fc, edgecolors='none', alpha=alpha))


# ---- (1) 3D assembly ----------------------------------------------------------------
a = fig.add_subplot(2, 2, 1, projection='3d')
a.set_title("Assembly — charger + stick + XIAO on floor + back cover")
shade(a, charger, C_CHG)
shade(a, stick, C_LED)
shade(a, xiao, C_XIAO)
shade(a, cover, C_COVER)
shade(a, case, C_CASE, alpha=0.28)
a.set_box_aspect((P.OUT_W, P.OUT_H, P.OUT_D))
a.set_xlim(-P.OUT_W2, P.OUT_W2); a.set_ylim(P.YMIN, P.YMAX); a.set_zlim(0, P.OUT_D)
a.view_init(elev=22, azim=-66); a.set_xlabel('X'); a.set_ylabel('Y (up)'); a.set_zlabel('Z (out)')

# ---- (2) front view (X-Y) -----------------------------------------------------------
a = fig.add_subplot(2, 2, 2)
a.set_title("Front view — vertical window, XIAO on floor")
a.add_patch(Rectangle((-P.OUT_W2, P.YMIN), P.OUT_W, P.OUT_H, fc='#e8ecf2', ec='#888'))
a.add_patch(Rectangle((-P.CAV_W / 2, 0), P.CAV_W, P.CAV_H, fill=False, ec=C_CHG, ls='--'))    # charger
a.add_patch(Rectangle((-P.CAV_W / 2, -P.SKIRT_H), P.CAV_W, P.SKIRT_H, fill=False, ec='#888', ls=':'))
xh = P.PCB_T + P.COMP_Z_ABOVE_PCB
a.add_patch(Rectangle((-P.PCB_L / 2, P.XIAO_FLOOR_Y), P.PCB_L, xh, fc=C_XIAO, alpha=0.5))
a.add_patch(Rectangle((P.WIN_CX - P.WIN_W / 2, P.WIN_CY - P.WIN_H / 2), P.WIN_W, P.WIN_H, fc='#20242a', ec='#444'))
pitch = P.LED_LIT_SPAN / (P.LED_COUNT - 1)
for i in range(P.LED_COUNT):
    y = P.LED_CY - P.LED_LIT_SPAN / 2 + i * pitch
    a.add_patch(Circle((P.LED_CX, y), 2.2, color=['#c0424a', '#e0b878', '#2a6b32'][i % 3]))
for (hx, hy) in P.LED_HOLE_XY:                                          # hidden stick bosses
    a.add_patch(Circle((hx, hy), P.STRIP_BOSS_OD / 2, fc='none', ec='#b03050', lw=1.5))
    a.add_patch(Circle((hx, hy), P.SCREW_PILOT / 2, fc='#b03050'))
for (cx, cy) in P.COVER_SCREW_XY:                                       # cover screws
    a.add_patch(Circle((cx, cy), P.COVER_SCREW_CLR / 2, fc='#c58a3d', ec='#7a531f'))
a.annotate("XIAO flat on floor", (0, P.XIAO_FLOOR_Y + xh / 2), ha='center', va='center', fontsize=7, color='#123')
a.set_xlim(-P.OUT_W2 - 2, P.OUT_W2 + 2); a.set_ylim(P.YMIN - 2, P.YMAX + 2)
a.set_aspect('equal'); a.set_xlabel('X'); a.set_ylabel('Y (up)'); a.grid(alpha=0.3)

# ---- (3) side section (Y-Z at X=0) --------------------------------------------------
a = fig.add_subplot(2, 2, 3)
a.set_title("Side section — open back, cover over the skirt")
a.add_patch(Rectangle((P.YMIN, 0), P.OUT_H, P.OUT_D, fc='#eef1f6', ec='none'))
a.add_patch(Rectangle((P.YMIN, P.D_IN), P.OUT_H, P.FRONT_T, color=C_CASE))          # front wall
a.add_patch(Rectangle((P.CAV_H, 0), P.WALL, P.OUT_D, color=C_CASE))                  # top wall
a.add_patch(Rectangle((P.YMIN, 0), P.WALL, P.OUT_D, color=C_CASE))                   # skirt bottom wall
a.add_patch(Rectangle((0, 0), P.LIP_H, P.LIP_H, color='#5f7fb0'))                    # snap lip
a.add_patch(Rectangle((P.CAV_H - P.LIP_H, 0), P.LIP_H, P.LIP_H, color='#5f7fb0'))
a.add_patch(Rectangle((-P.SKIRT_H, 0), P.SKIRT_H, P.COVER_T, color=C_COVER))         # back cover
a.add_patch(Rectangle((0, 0), P.CHG_H, P.CHG_D, fc=C_CHG, alpha=0.5, ec=C_CHG))      # charger
a.add_patch(Rectangle((P.LED_CY - P.LED_L / 2, P.LED_BACK_Z), P.LED_L, P.LED_PCB_T, color=C_LED))  # stick
a.add_patch(Rectangle((P.XIAO_FLOOR_Y, P.XIAO_CZ - P.PCB_W / 2), P.PCB_T, P.PCB_W, color=C_XIAO))  # XIAO
a.plot([P.WIN_CY - P.WIN_H / 2, P.WIN_CY + P.WIN_H / 2], [P.OUT_D, P.OUT_D], 'w-', lw=3)
a.annotate("cover", (-P.SKIRT_H / 2, P.COVER_T + 0.5), ha='center', va='bottom', fontsize=7, color='#7a531f')
a.annotate("wall\n(open back)", (P.CAV_H / 2, -1.5), ha='center', va='top', fontsize=8)
a.annotate("→ room", (P.CAV_H / 2, P.OUT_D + 0.5), ha='center', va='bottom', fontsize=8)
a.set_xlim(P.YMIN - 2, P.YMAX + 2); a.set_ylim(-4, P.OUT_D + 3)
a.set_aspect('equal'); a.set_xlabel('Y (up)'); a.set_ylabel('Z (out)'); a.grid(alpha=0.3)

# ---- (4) depth section (X-Z at charger mid-height) ----------------------------------
a = fig.add_subplot(2, 2, 4)
a.set_title("Depth section — charger, stick pocket, window")
a.add_patch(Rectangle((-P.OUT_W2, P.D_IN), P.OUT_W, P.FRONT_T, color=C_CASE))          # front wall
for sx in (-1, 1):
    a.add_patch(Rectangle((sx * P.CAV_W / 2 - (P.WALL if sx > 0 else 0), 0), P.WALL, P.OUT_D, color=C_CASE))
a.add_patch(Rectangle((-P.CHG_W / 2, 0), P.CHG_W, P.CHG_D, fc=C_CHG, alpha=0.5, ec=C_CHG))
a.add_patch(Rectangle((-P.LED_W / 2, P.LED_BACK_Z), P.LED_W, P.LED_PCB_T, color=C_LED))
a.add_patch(Rectangle((-P.LED_5050 / 2, P.LED_PCB_TOP_Z), P.LED_5050, P.LED_BODY_H, color='#e0b878'))
a.plot([-P.WIN_W / 2, P.WIN_W / 2], [P.OUT_D, P.OUT_D], 'w-', lw=3)
a.annotate("charger", (0, P.CHG_D / 2), ha='center', va='center', fontsize=8, color='w')
a.annotate("stick", (P.LED_W / 2 + 1, P.LED_BACK_Z + 1), ha='left', fontsize=7, color=C_LED)
a.set_xlim(-P.OUT_W2 - 2, P.OUT_W2 + 2); a.set_ylim(-2, P.OUT_D + 3)
a.set_aspect('equal'); a.set_xlabel('X'); a.set_ylabel('Z (out)'); a.grid(alpha=0.3)

fig.tight_layout(rect=(0, 0, 1, 0.97))
fig.savefig("render_preview.png", dpi=110, facecolor='white')
print("wrote render_preview.png")
