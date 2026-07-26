#!/usr/bin/env python3
"""
Render a 1:1 BLACK-AND-WHITE paper template of the full v2 case front face: the outer outline,
the LED window, and the two screw-post centres (with crosshairs).  For printing at 100% scale
and laying over the part / marking positions.

    conda run -n img23d python render_paper_template.py   # -> case_template.pdf + .png

PRINT AT 100% (no "fit to page" / "shrink to fit").  Then check the 50 mm scale bar with a
ruler before trusting any position.  Everything is driven by case_params, so the window and
posts match the case exactly (post X follows LED_MOUNT_OFFS_X).
"""
import warnings; warnings.filterwarnings('ignore')
import numpy as np
import case_params as P
from build_case import rrect              # pulls in trimesh before matplotlib (env import-order)
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Circle

MARGIN = 16.0                                   # mm of paper around the case outline
CYM = (P.YMAX + P.YMIN) / 2                      # case is not centred on y=0
x0, x1 = -P.OUT_W2 - MARGIN, P.OUT_W2 + MARGIN
y0, y1 = P.YMIN - MARGIN, P.YMAX + MARGIN
W, H = x1 - x0, y1 - y0
BK = 'black'

# Put the 1:1 drawing on a real US Letter page.  A small custom-size page gets "fit to page"
# scaled up by most viewers/printers -- a standard page prints true at "Actual size" / 100%.
PAGE_W, PAGE_H = 215.9, 279.4                    # US Letter, portrait, mm
fig = plt.figure(figsize=(PAGE_W / 25.4, PAGE_H / 25.4))
axw, axh = W / PAGE_W, H / PAGE_H                 # content occupies its TRUE mm fraction of the page
ax = fig.add_axes([(1 - axw) / 2, (1 - axh) / 2, axw, axh])   # centred, 1 data mm == 1 page mm
ax.set_xlim(x0, x1); ax.set_ylim(y0, y1); ax.axis('off')

# --- case outer outline (true rounded corners) ------------------------------------------
poly = rrect(P.OUT_W, P.OUT_H, P.OUT_R)
xs, ys = poly.exterior.xy
ax.plot(np.asarray(xs), np.asarray(ys) + CYM, color=BK, lw=1.6)

# --- charger cavity + skirt reference (dashed, light) -----------------------------------
ax.plot([-P.CAV_W / 2, P.CAV_W / 2, P.CAV_W / 2, -P.CAV_W / 2, -P.CAV_W / 2],
        [0, 0, P.CAV_H, P.CAV_H, 0], color=BK, lw=0.6, ls=(0, (4, 3)))
ax.axhline(0, xmin=0.12, xmax=0.88, color=BK, lw=0.5, ls=(0, (1, 3)))   # charger / skirt seam
ax.axvline(0, color=BK, lw=0.6, ls=(0, (7, 5)))                          # face centreline

# --- LED window -------------------------------------------------------------------------
ax.add_patch(Rectangle((P.WIN_CX - P.WIN_W / 2, P.WIN_CY - P.WIN_H / 2), P.WIN_W, P.WIN_H,
                       fill=False, ec=BK, lw=1.6))
ax.annotate("WINDOW  %.1f x %.1f" % (P.WIN_W, P.WIN_H),
            (P.WIN_CX + P.WIN_W / 2 + 2.5, P.WIN_CY + 8), fontsize=6, va='center')

# --- screw posts (boss + pilot + crosshair) ---------------------------------------------
for (hx, hy) in P.LED_HOLE_XY:
    ax.add_patch(Circle((hx, hy), P.STRIP_BOSS_OD / 2, fill=False, ec=BK, lw=1.2))
    ax.add_patch(Circle((hx, hy), P.SCREW_PILOT / 2, fill=False, ec=BK, lw=0.8))
    r = P.STRIP_BOSS_OD / 2 + 2.5
    ax.plot([hx - r, hx + r], [hy, hy], color=BK, lw=0.6)
    ax.plot([hx, hx], [hy - r, hy + r], color=BK, lw=0.6)
ax.annotate("POST M3  x=%.2f" % P.LED_HOLE_XY[0][0],
            (P.LED_HOLE_XY[0][0] + P.STRIP_BOSS_OD / 2 + 3, P.LED_HOLE_XY[0][1]),
            fontsize=6, va='center')

# --- labels / orientation ---------------------------------------------------------------
ax.annotate("rivian-status v2 — case front, 1:1 template", (0, y1 - 4), ha='center',
            fontsize=7, weight='bold')
ax.annotate("TOP — into wall outlet", (0, P.YMAX + 3.5), ha='center', fontsize=7, weight='bold')
ax.annotate("+X →  (right, facing unit)", (P.OUT_W2 - 1, P.YMIN + 4),
            ha='right', fontsize=6)

# --- 50 mm scale-check bar (bottom, centred) --------------------------------------------
bx, by = -25.0, y0 + 8
ax.plot([bx, bx + 50], [by, by], color=BK, lw=1.3)
for t in range(0, 51, 10):
    ax.plot([bx + t, bx + t], [by, by + (3 if t % 50 == 0 else 2)], color=BK, lw=1.0)
ax.annotate("50 mm — print at 100% (no 'fit to page'); verify with a ruler",
            (bx, by + 4.5), fontsize=6)

# --- corner crop marks ------------------------------------------------------------------
for cx in (x0 + 3, x1 - 3):
    for cy in (y0 + 3, y1 - 3):
        ax.plot([cx - 2.5, cx + 2.5], [cy, cy], color=BK, lw=0.5)
        ax.plot([cx, cx], [cy - 2.5, cy + 2.5], color=BK, lw=0.5)

fig.savefig("case_template.pdf")                                 # keep full Letter page (no bbox crop)
fig.savefig("case_template.png", dpi=200, facecolor='white')
print("wrote case_template.pdf + case_template.png")
print("  page US Letter %.1f x %.1f mm ; case drawn 1:1 at %.1f x %.1f mm" %
      (PAGE_W, PAGE_H, P.OUT_W, P.OUT_H))
print("  print 'Actual size' / 100%% ; window X=%.2f ; posts X=%.2f at Y=%.1f/%.1f" %
      (P.WIN_CX, P.LED_HOLE_XY[0][0], P.LED_HOLE_XY[0][1], P.LED_HOLE_XY[1][1]))
