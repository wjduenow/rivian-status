#!/usr/bin/env python3
"""Render template_preview.png -- front view (window + posts + strip) and a side section."""
import warnings
warnings.filterwarnings('ignore'); import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Circle
import case_params as P
import build_template as T

m = T.build_template()
b = m.bounds
TWx = (b[0][0], b[1][0]); TWy = (b[0][1], b[1][1])

fig, (a0, a1) = plt.subplots(1, 2, figsize=(11, 6))
fig.suptitle("v2 LED-mount alignment template  (%.0f x %.0f x %.1f mm)  "
             "posts %.2f mm off centre, window centred"
             % (b[1][0]-b[0][0], b[1][1]-b[0][1], b[1][2]-b[0][2], P.LED_HOLE_DX),
             weight='bold')

# ---- front view (looking at the window; +X = RIGHT facing the unit) ----
a0.set_title("Front — window (grey), posts (red), strip (dashed)")
a0.add_patch(Rectangle((TWx[0], TWy[0]), TWx[1]-TWx[0], TWy[1]-TWy[0], fc='#e8ecf2', ec='#888'))
# stick body outline (where the real strip sits: centred on the shifted mount)
a0.add_patch(Rectangle((P.STICK_CX-P.LED_W/2, P.LED_CY-P.LED_L/2), P.LED_W, P.LED_L,
                       fill=False, ec='#c0424a', ls='--'))
# window
a0.add_patch(Rectangle((P.WIN_CX-P.WIN_W/2, P.WIN_CY-P.WIN_H/2), P.WIN_W, P.WIN_H,
                       fc='#20242a', ec='#444'))
# emitters (on the row, should read centred in the window)
pitch = P.LED_LIT_SPAN / (P.LED_COUNT-1)
for i in range(P.LED_COUNT):
    a0.add_patch(Circle((P.LED_CX, P.LED_CY-P.LED_LIT_SPAN/2+i*pitch), 2.2,
                        color=['#c0424a', '#e0b878', '#2a6b32'][i % 3]))
# posts
for (bx, by) in P.LED_HOLE_XY:
    a0.add_patch(Circle((bx, by), P.STRIP_BOSS_OD/2, fc='#f3c1cb', ec='#b03050', lw=1.5))
    a0.add_patch(Circle((bx, by), P.SCREW_PILOT/2, fc='#b03050'))
a0.axvline(0, color='#4a78c8', lw=0.8, ls=':')                      # case-face centreline
a0.annotate("+X →\n(right, facing)", (TWx[1]-1, TWy[0]+3), ha='right', fontsize=8, color='#4a78c8')
a0.set_aspect('equal'); a0.set_xlim(TWx[0]-2, TWx[1]+2); a0.set_ylim(TWy[0]-2, TWy[1]+2)
a0.set_xlabel('X (mm)'); a0.set_ylabel('Y (mm)'); a0.grid(alpha=0.3)

# ---- side section (Y-Z at the post X): open back, boss, window, strip seat ----
a1.set_title("Side section — open back, post, strip seat, window")
a1.add_patch(Rectangle((TWy[0], T.BACK), TWy[1]-TWy[0], P.OUT_D-T.BACK, fc='#eef1f6', ec='none'))
a1.add_patch(Rectangle((TWy[0], P.D_IN), TWy[1]-TWy[0], P.FRONT_T, color='#93b4dd'))    # front wall
a1.add_patch(Rectangle((P.LED_CY-P.LED_L/2, P.LED_BACK_Z), P.LED_L, P.LED_PCB_T, color='#c0424a'))
a1.add_patch(Rectangle((P.LED_CY-13-P.STRIP_BOSS_OD/2, P.LED_PCB_TOP_Z), P.STRIP_BOSS_OD,
                       P.D_IN-P.LED_PCB_TOP_Z, color='#93b4dd'))                        # a boss
a1.plot([P.WIN_CY-P.WIN_H/2, P.WIN_CY+P.WIN_H/2], [P.OUT_D, P.OUT_D], 'w-', lw=3)
a1.annotate("strip", (P.LED_CY, P.LED_BACK_Z-0.5), ha='center', va='top', fontsize=8, color='#c0424a')
a1.annotate("open back →", (TWy[0]+1, T.BACK+1), fontsize=8)
a1.set_aspect('equal'); a1.set_xlim(TWy[0]-2, TWy[1]+2); a1.set_ylim(T.BACK-2, P.OUT_D+3)
a1.set_xlabel('Y (mm)'); a1.set_ylabel('Z (out, mm)'); a1.grid(alpha=0.3)

fig.tight_layout(rect=(0, 0, 1, 0.95))
fig.savefig("template_preview.png", dpi=120, facecolor='white')
print("wrote template_preview.png")
