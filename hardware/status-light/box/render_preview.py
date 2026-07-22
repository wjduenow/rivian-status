#!/usr/bin/env python3
"""
Render render_preview.png (2x2):
  (1) 3D of the assembled box (shell + lid + stand-in XIAO + stand-in LED stick)
  (2) side section (Y–Z): the stick stacked above the board, USB-C out the back wall
  (3) top view (X–Y): the LED window + the 8 pixels below it
  (4) interior plan (X–Y): board pocket, stick posts, lid bosses, every keep-out

    python3 render_preview.py   # -> render_preview.png
"""
import numpy as np, trimesh, warnings
warnings.filterwarnings('ignore'); import matplotlib; matplotlib.use('Agg')
import matplotlib.colors as mcolors
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Circle
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import case_params as P
import build_shell as S, build_lid as L

shell = S.build_shell()
lid = L.build_lid()

board = trimesh.creation.box(extents=(P.PCB_W, P.PCB_L, P.PCB_T))
board.apply_translation([P.BOARD_CX, P.BOARD_CY, P.PCB_BACK_Z + P.PCB_T / 2])
stick = trimesh.creation.box(extents=(P.LED_L, P.LED_W, P.LED_PCB_T))
stick.apply_translation([P.LED_CX, P.STRIP_CY, P.STRIP_BOTTOM_Z + P.LED_PCB_T / 2])

C_SHELL, C_LID, C_PCB, C_LED = '#93b4dd', '#e0b878', '#2a6b32', '#c0424a'

fig = plt.figure(figsize=(15, 11))
fig.suptitle(f"rivian-status desk light — XIAO ESP32-S3 + 8px WS2812 top bar   "
             f"({P.OUT_X:.1f} × {P.OUT_Y:.1f} × {P.OUT_Z:.1f} mm)",
             fontsize=14, weight='bold')


def shade(ax, mesh, color, alpha=1.0):
    light = np.array([-0.4, -0.6, 0.75]); light /= np.linalg.norm(light)
    b = 0.35 + 0.65 * np.clip(mesh.face_normals @ light, 0, 1)
    fc = np.clip(b[:, None] * np.array(mcolors.to_rgb(color)), 0, 1)
    ax.add_collection3d(Poly3DCollection(mesh.triangles, facecolors=fc, edgecolors='none', alpha=alpha))


# ---- (1) 3D assembly ----------------------------------------------------------------
a = fig.add_subplot(2, 2, 1, projection='3d')
a.set_title("Assembly (LEDs face up, out the lid)")
shade(a, shell, C_SHELL)
shade(a, board, C_PCB)
shade(a, stick, C_LED)
shade(a, lid, C_LID, alpha=0.35)
a.set_box_aspect((P.OUT_X, P.OUT_Y, P.OUT_Z))
a.set_xlim(-P.OUT_X2, P.OUT_X2); a.set_ylim(-P.OUT_Y2, P.OUT_Y2); a.set_zlim(0, P.OUT_Z)
a.view_init(elev=24, azim=-72); a.set_xlabel('X'); a.set_ylabel('Y'); a.set_zlabel('Z')

# ---- (2) side section (Y–Z, at X≈0) -------------------------------------------------
a = fig.add_subplot(2, 2, 2)
a.set_title("Side section — stick stacked above the board")
a.add_patch(Rectangle((-P.OUT_Y2, 0), P.OUT_Y, P.FLOOR_T, color=C_SHELL))
a.add_patch(Rectangle((-P.OUT_Y2, 0), P.WALL, P.SHELL_H, color=C_SHELL))
a.add_patch(Rectangle((P.IN_Y2, 0), P.WALL, P.SHELL_H, color=C_SHELL))
a.add_patch(Rectangle((-P.OUT_Y2, P.SHELL_H), P.OUT_Y, P.LID_T, color=C_LID))
# stick (flat, LEDs up) + board (flat)
a.add_patch(Rectangle((P.STRIP_CY - P.LED_W / 2, P.STRIP_BOTTOM_Z), P.LED_W, P.LED_PCB_T, color=C_LED))
a.add_patch(Rectangle((P.PCB_FRONT_Y, P.PCB_BACK_Z), P.PCB_L, P.PCB_T, color=C_PCB))
# lid window gap
a.plot([-P.WIN_D / 2, P.WIN_D / 2], [P.OUT_Z, P.OUT_Z], 'w-', lw=4)
a.annotate("LED window\n(in lid)", (0, P.OUT_Z + 0.4), ha='center', va='bottom', fontsize=8)
a.annotate("USB-C →", (P.IN_Y2 - 1, P.USB_PORT_CZ), ha='right', va='center', fontsize=8)
a.annotate("antenna", (-P.IN_Y2 + 1, P.SHELL_H - P.ANT_NOTCH_H / 2), ha='left', va='center', fontsize=7)
a.set_xlim(-P.OUT_Y2 - 2, P.OUT_Y2 + 2); a.set_ylim(-1, P.OUT_Z + 3)
a.set_aspect('equal'); a.set_xlabel('Y (front → back)'); a.set_ylabel('Z'); a.grid(alpha=0.3)

# ---- (3) top view (X–Y) -------------------------------------------------------------
a = fig.add_subplot(2, 2, 3)
a.set_title("Top view — 8 pixels under the window")
a.add_patch(Rectangle((-P.OUT_X2, -P.OUT_Y2), P.OUT_X, P.OUT_Y, fc='#e8ecf2', ec='#888'))
a.add_patch(Rectangle((-P.LED_L / 2, P.STRIP_CY - P.LED_W / 2), P.LED_L, P.LED_W,
                      fill=False, ec=C_LED, ls=':'))                     # stick outline
a.add_patch(Rectangle((P.WIN_CX - P.WIN_W / 2, P.WIN_CY - P.WIN_D / 2), P.WIN_W, P.WIN_D,
                      fc='#20242a', ec='#444'))                          # lid window
pitch = P.LED_LIT_SPAN / (P.LED_COUNT - 1)
for i in range(P.LED_COUNT):
    x = -P.LED_LIT_SPAN / 2 + i * pitch
    a.add_patch(Circle((x, P.LED_CY), 2.2, color=['#c0424a', '#e0b878', '#2a6b32'][i % 3]))
for hx in P.LED_HOLE_X:                                                  # screw holes (offset)
    a.add_patch(Circle((hx, P.LED_HOLE_DY), P.LED_HOLE_D / 2, fc='#111', ec='#ccc'))
a.set_xlim(-P.OUT_X2 - 2, P.OUT_X2 + 2); a.set_ylim(-P.OUT_Y2 - 2, P.OUT_Y2 + 2)
a.set_aspect('equal'); a.set_xlabel('X'); a.set_ylabel('Y'); a.grid(alpha=0.3)

# ---- (4) interior plan (X–Y) --------------------------------------------------------
a = fig.add_subplot(2, 2, 4)
a.set_title("Interior plan (top-down)")
a.add_patch(Rectangle((-P.OUT_X2, -P.OUT_Y2), P.OUT_X, P.OUT_Y, fill=False, ec='#888'))
a.add_patch(Rectangle((-P.IN_X2, -P.IN_Y2), P.IN_X, P.IN_Y, fill=False, ec='#bbb', ls='--'))
a.add_patch(Rectangle((-P.PCB_W / 2, P.PCB_FRONT_Y), P.PCB_W, P.PCB_L, color=C_PCB, alpha=0.8))
a.add_patch(Rectangle((-P.LED_L / 2, P.STRIP_CY - P.LED_W / 2), P.LED_L, P.LED_W, fill=False, ec=C_LED, ls=':'))
# USB-C shell (back wall) + the lid clamp pads that straddle it
a.add_patch(Rectangle((P.USB_PORT_CX - P.USB_SHELL_W / 2, P.PCB_BACK_Y - 5.5),
                      P.USB_SHELL_W, 5.5 + P.USB_OVERHANG + P.WALL, color='#444'))
for (px, py) in P.BOARD_PAD_XY:
    a.add_patch(Circle((px, py), P.BOARD_PAD_D / 2, fc='none', ec='#e0b878', lw=1.5))
# stick posts, lid bosses, U.FL jack, antenna notch
for hx in P.LED_HOLE_X:
    a.add_patch(Circle((hx, P.POST_CY), P.POST_OD / 2, fc='#c0424a', ec='#600'))
for (bx, by) in P.LID_BOSS_XY:
    a.add_patch(Circle((bx, by), P.LID_BOSS_OD / 2, fc='none', ec='#333'))
a.add_patch(Circle((P.UFL_X, P.UFL_Y), 1.5, color='#888'))
a.annotate("U.FL\n(antenna\ninside)", (P.UFL_X, P.UFL_Y - 1.5), ha='center', va='top', fontsize=6, color='#555')
if P.ANT_EXIT:
    a.add_patch(Rectangle((P.UFL_X - P.ANT_NOTCH_W / 2, -P.OUT_Y2), P.ANT_NOTCH_W, P.WALL, fc='#fff', ec='#888'))
    a.annotate("antenna\nexit", (P.UFL_X, -P.OUT_Y2 - 0.3), ha='center', va='top', fontsize=7)
a.annotate("USB-C", (P.USB_PORT_CX, P.OUT_Y2 + 0.3), ha='center', va='bottom', fontsize=8)
a.annotate("posts = red · lid bosses = ○", (0, P.IN_Y2 - 1.5), ha='center', fontsize=7, color='#555')
a.set_xlim(-P.OUT_X2 - 3, P.OUT_X2 + 3); a.set_ylim(-P.OUT_Y2 - 4, P.OUT_Y2 + 4)
a.set_aspect('equal'); a.set_xlabel('X'); a.set_ylabel('Y'); a.grid(alpha=0.3)

fig.tight_layout(rect=(0, 0, 1, 0.97))
fig.savefig("render_preview.png", dpi=110, facecolor='white')
print("wrote render_preview.png")
