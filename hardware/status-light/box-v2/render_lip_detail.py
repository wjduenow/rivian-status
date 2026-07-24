#!/usr/bin/env python3
"""
Render lip_detail.png -- a focused look at the cover's TOP RETAINING LIP:
  (left)  the cover from its inner face, showing the two top-corner tongues (half-thickness)
  (right) a Y-Z section through a tab, showing the body tab (outer slice) capturing the cover
          tongue (inner slice) -> flush outer face, cover top can't pull out.

    python3 render_lip_detail.py   # -> lip_detail.png
"""
import numpy as np, warnings
warnings.filterwarnings('ignore'); import matplotlib; matplotlib.use('Agg')
import matplotlib.colors as mcolors
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import case_params as P
import build_cover as CV

cover = CV.build_cover()
C_COVER, C_CASE = '#c58a3d', '#93b4dd'

fig = plt.figure(figsize=(13, 5.5))
fig.suptitle("Cover top retaining lip — corner tongue tucks under a body tab", fontsize=13, weight='bold')


def shade(ax, mesh, color):
    light = np.array([-0.3, 0.4, 0.85]); light /= np.linalg.norm(light)
    b = 0.4 + 0.6 * np.clip(mesh.face_normals @ light, 0, 1)
    fc = np.clip(b[:, None] * np.array(mcolors.to_rgb(color)), 0, 1)
    ax.add_collection3d(Poly3DCollection(mesh.triangles, facecolors=fc, edgecolors='#7a531f', lw=0.1))


# ---- (left) cover, inner face toward viewer ----------------------------------------
a = fig.add_subplot(1, 2, 1, projection='3d')
a.set_title("Cover (inner face) — top-corner tongues")
shade(a, cover, C_COVER)
a.set_box_aspect((P.COVER_W, P.COVER_H, P.COVER_T * 6))
a.set_xlim(-P.COVER_W / 2, P.COVER_W / 2)
a.set_ylim(P.COVER_CY - P.COVER_H / 2, P.COVER_CY + P.COVER_H / 2)
a.set_zlim(0, P.COVER_T)
a.view_init(elev=52, azim=-80); a.set_xlabel('X'); a.set_ylabel('Y (up)'); a.set_zlabel('Z')

# ---- (right) Y-Z section through a tab ---------------------------------------------
a = fig.add_subplot(1, 2, 2)
a.set_title("Section at a tab (X ≈ %.0f) — tab over tongue" % (P.REB_X / 2 - P.COVER_LIP_REACH / 2))
# body tab: outer slice z[0, COVER_LIP_T], y[-COVER_LIP_Y, 0]
a.add_patch(Rectangle((-P.COVER_LIP_Y, 0), P.COVER_LIP_Y, P.COVER_LIP_T, fc=C_CASE, ec='#3a567f', label='body tab (outer)'))
# body above the opening (the charger-region wall, y>0) + front wall context
a.add_patch(Rectangle((0, 0), 6, P.COVER_T, fc=C_CASE, ec='#3a567f'))
# cover: full thickness below the tongue region, tongue = inner slice under the tab
cov_top = P.COVER_TOP_Y
a.add_patch(Rectangle((cov_top - 8, 0), 8 - P.COVER_LIP_Y, P.COVER_T, fc=C_COVER, ec='#7a531f'))       # full-thickness body of cover
a.add_patch(Rectangle((cov_top - P.COVER_LIP_Y, P.COVER_LIP_T), P.COVER_LIP_Y - 0.3, P.COVER_T - P.COVER_LIP_T,
                      fc=C_COVER, ec='#7a531f', label='cover tongue (inner)'))                          # tongue
a.annotate("flush outer face →", (cov_top - 6, -0.6), ha='center', va='top', fontsize=8)
a.plot([cov_top - 10, 4], [0, 0], 'k--', lw=0.8)
a.annotate("pull-out (−Z) blocked\nby the tab", (-P.COVER_LIP_Y / 2, P.COVER_T + 0.8),
           ha='center', fontsize=8, color='#3a567f')
a.set_xlim(cov_top - 10, 6); a.set_ylim(-2, P.COVER_T + 3)
a.set_aspect('equal'); a.set_xlabel('Y (up)  →  top at y=0'); a.set_ylabel('Z (out)')
a.legend(loc='lower left', fontsize=7); a.grid(alpha=0.3)

fig.tight_layout(rect=(0, 0, 1, 0.94))
fig.savefig("lip_detail.png", dpi=120, facecolor='white')
print("wrote lip_detail.png")
