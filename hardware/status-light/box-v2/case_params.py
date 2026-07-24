"""
V2 geometry -- a wall-charger *slip case*.  Instead of the v1 desk box, the v2 enclosure is
a single full-cover shell that press-fits OVER a Nekmit Ultra-Thin flat USB wall charger.
The charger is the mechanical + electrical anchor: it plugs into the wall socket, its AC
prongs protrude out the OPEN BACK, and the shell slides on from behind and snaps in place.

    +X  width   (horizontal on the wall)      -- charger 43.18 wide
    +Y  up      (vertical on the wall)         -- charger 50.8 tall; skirt hangs BELOW it
    +Z  out     (depth, wall -> room)          -- charger 20.32 deep; LED board + window in front

World origin: X centred on the charger; y = 0 at the charger's BOTTOM face (the USB-A ports,
which point down into the skirt); z = 0 at the OPEN BACK rim (the wall plane).

LAYOUT (locked with the user):
  * The 8-pixel WS2812 stick (the v1 board, re-used) mounts VERTICALLY on the front interior
    face, domes facing +Z out through a window.  It is SCREWED to the case through its two
    Ø3.75 holes with M3 self-tappers driven from the OPEN BACK (heads hidden from the front);
    the bosses' pilots thread up into the solid front wall.  A window ledge is a second stop.
  * The XIAO ESP32-S3 rests FLAT on the FLOOR of the lower SKIRT bay -- gravity plus the wires
    to the stick hold it, no tray (user's call).  A USB-A->USB-C pigtail from the charger's
    bottom port loops down to it.  The skirt's wall-facing back is a SEPARATE screw-on COVER,
    so the main body prints with a fully OPEN back (no supports to dig out of a blind pocket).
  * Retention: an inward SNAP LIP around the back rim catches behind the charger's rear edge.

DEVIATION FROM THE BLUEPRINT (documented on purpose):
  The blueprint's 20.7 mm internal depth assumes the charger touches the front wall.  The
  vertical LED stick sits IN FRONT of the charger AND is back-screwed, so the depth grows to
  charger + head-relief gap + stick = ~26.9 mm and the device protrudes ~29.1 mm from the
  wall.  The skirt is ~17 mm (down from 24) -- now floored by the USB-A pigtail plug's vertical
  clearance (~15 mm, BP), not the XIAO (which lies flat on the floor).

Same toolchain + conventions as ../box (trimesh CSG, mm, provenance-commented, clearances
ASSERTED in build_case.check_clearances()).  Provenance tags: BP=blueprint, MEAS=calipered,
CAD=Seeed STEP, CHOICE=design value, DERIVED=computed below.
"""

# ======================================================================================
#  Nekmit Ultra-Thin Flat Wall Charger  (dual USB-A, 12W/15W)  -- source: BP (blueprint)
# ======================================================================================
CHG_W = 43.18    # BP  width  (world X)
CHG_H = 50.80    # BP  height (world Y)
CHG_D = 20.32    # BP  depth  (world Z, wall->room)

# Slip-fit slack so the FDM shell slides over the charger without splitting (BP: +0.6 on the
# two flat dims, +0.4 on depth -> +0.3/+0.3/+0.2 per side).
FIT_W = 0.6      # BP  total added to width  -> per-side 0.3
FIT_H = 0.6      # BP  total added to height
FIT_D = 0.4      # BP  total added to depth

# ======================================================================================
#  8-pixel WS2812 stick  -- RE-USED from v1, MEASURED (see ../box/case_params.py, MEAS)
#  Mounted VERTICALLY here: its length runs along world Y, its width along world X.
# ======================================================================================
LED_L        = 51.5    # MEAS  stick length  -> world Y
LED_W        = 10.0    # MEAS  stick width   -> world X
LED_PCB_T    = 1.6     # MEAS  bare PCB thickness
LED_BODY_H   = 1.4     # MEAS  how far the 5050 domes stand off the PCB face
LED_TOTAL_T  = 3.0     # MEAS  PCB + domes  (= LED_PCB_T + LED_BODY_H)
LED_COUNT    = 8       # firmware pixel map (plan §7)
LED_LIT_SPAN = 45.0    # MEAS  first->last LED centre  (runs along Y here)
LED_5050     = 4.75    # MEAS  emitter square

# Mounting holes (MEAS, from ../box).  Two holes ~1/4 and ~3/4 along the stick length (world
# Y here) and offset off the LED row toward ONE long edge (world X here).
LED_HOLE_D       = 3.75          # MEAS  hole Ø
LED_HOLE_OFFS_Y  = [-13.0, 13.0] # MEAS  along the length, from stick centre (26 mm apart)
LED_HOLE_DX      = 4.5           # MEAS  offset off the LED row, across the width
LED_HOLE_SIDE    = 1             # CHOICE which X side the holes sit (cosmetic; board is centred)

# ======================================================================================
#  Fasteners  -- M3 self-tapping, driven from the OPEN BACK into front-wall bosses (CHOICE)
# ======================================================================================
SCREW_PILOT        = 2.6   # M3 thread-forming pilot Ø
STRIP_SCREW_HEAD_D = 6.0   # M3 head Ø (lives in the relief gap behind the stick)
STRIP_SCREW_HEAD_H = 2.2   # M3 head height
STRIP_HEAD_RELIEF  = 2.6   # air kept behind the stick's PCB for the screw heads (> head H)
STRIP_BOSS_OD      = 5.0   # boss nub Ø on the front-wall interior over each hole
DOME_RECESS        = 0.6   # domes sit this far below the window inner face (also a light baffle)
PILOT_WALL_MIN     = 0.8   # min front-wall left over the blind pilot (don't breach the face)

# ======================================================================================
#  XIAO ESP32-S3  -- lives in the skirt bay.  numbers from Seeed's STEP (../box, CAD)
#  Laid FLAT against the skirt's front interior wall: length(21) along X, width(17.8) along Y.
# ======================================================================================
PCB_L        = 21.0    # CAD  USB<->antenna axis  -> world X here
PCB_W        = 17.8    # CAD  -> world Y here
PCB_T        = 1.25    # CAD
PCB_FIT      = 0.4     # per-side slack in the skirt board pocket
USB_SHELL_H  = 3.26    # CAD  USB-C aperture height
COMP_Z_ABOVE_PCB = 4.21  # CAD  tallest part (USB-C shell) off the PCB back

# ======================================================================================
#  Shell  -- CHOICE (wall thickness per BP: 2.0-2.4 mm = 5-6 perimeters @ 0.4 nozzle)
# ======================================================================================
WALL      = 2.2    # CHOICE  side/top walls (BP 2.0-2.4)
FRONT_T   = 2.2    # CHOICE  front face (holds the window)
OUT_R     = 3.0    # CHOICE  outer corner radius (matches v1)
SEG       = 96     # CHOICE  cylinder facets
# (the charger region AND the skirt are OPEN at the back; the skirt is closed by a separate
#  screw-on COVER -- see the COVER section.  No integral back wall to print over a pocket.)

# Rear snap-lip (BP: 1.0-1.5 mm inward lip along the back rim, catches behind the charger).
LIP_IN    = 1.2    # CHOICE  how far the lip reaches inward over the charger's rear edge (BP 1.0-1.5)
LIP_H     = 2.0    # CHOICE  lip thickness along Z at the back rim
LIP_CHAMFER = 1.2  # CHOICE  ramp so the inward overhang self-supports (front-face-down print)

# Lower cable/electronics skirt.  Floored by the USB-A pigtail plug's vertical clearance
# (~15 mm, BP), NOT by the XIAO -- which now lies FLAT on the skirt floor (21x17.8 footprint
# on the X-Z plane, only ~5.5 mm tall in Y), held by gravity + its wires to the stick.
SKIRT_H   = 17.0   # CHOICE  interior skirt height below the charger (BP 15-18)

# ======================================================================================
#  LED stick mount (front pocket) + window  -- CHOICE
# ======================================================================================
# The stick sits in a pocket in the front wall interior: domes toward +Z (window), PCB back
# toward -Z.  It is screwed to two bosses (SCREW section) and the pocket walls locate it in
# X/Y; the window ledge is a forward stop.
POCKET_FIT     = 0.35   # CHOICE  per-side slack around the stick in its pocket
WIN_MARGIN     = 0.6    # CHOICE  window overhang past the emitter extent, per edge
DIFFUSER_REBATE_T = 0.8 # CHOICE  outer rebate depth for a stick-on diffusion film
DIFFUSER_REBATE_W = 1.2 # CHOICE  outer rebate lip width

# ======================================================================================
#  XIAO skirt bay  -- the XIAO just RESTS on the floor (no standoffs/ribs) -- CHOICE
# ======================================================================================
# Nothing to mount: the board lies flat on the skirt floor, gravity + the stick wires keep it
# put, and the back COVER stops it falling out the back.  (The stick pocket, charger cavity and
# skirt share one open interior, so the stick tail + charger pigtail route freely between them.)

# ======================================================================================
#  Skirt back COVER  -- a SEPARATE screw-on plate (main body prints open-backed) -- CHOICE
# ======================================================================================
COVER_T        = 2.0   # CHOICE  cover plate thickness (sits in a flush rebate at the back rim)
COVER_BORDER   = 1.5   # CHOICE  solid outer-wall border kept around the cover rebate
COVER_FIT      = 0.3   # CHOICE  per-side clearance of the cover in its rebate
COVER_BOSS_OD  = 5.0   # CHOICE  screw-boss Ø (gusseted to the skirt side walls)
COVER_BOSS_H   = 7.0   # CHOICE  boss height (into the skirt, off the rebate floor)
COVER_BOSS_X   = 17.0  # CHOICE  boss centre X (clear of the ~10.5 mm-half XIAO, ribbed to wall)
COVER_SCREW_CLR = 3.4  # CHOICE  M3 clearance hole through the cover
COVER_CSK_D    = 6.0   # CHOICE  countersink on the cover's outer (wall) face -> flush head
COVER_PILOT_DEPTH = 6.0  # CHOICE  blind M3 pilot depth in each boss

# Top retaining lip: two corner TABS on the body that overlap a half-thickness tongue on the
# cover's top edge -> the cover top can't pull out (the 2 side screws hold the rest).  Corner
# tabs (not a full-width lip) keep it to short print overhangs instead of a 44 mm bridge.
COVER_LIP_REACH  = 6.0   # CHOICE  how far each tab reaches inward from the side wall
COVER_LIP_Y      = 4.0   # CHOICE  tab depth down from the top (y=0)
COVER_LIP_FRAC   = 0.5   # CHOICE  outer fraction of the cover thickness the tab/tongue splits at
COVER_LIP_SLIDE  = 0.4   # CHOICE  extra clearance so the tongue slides under the tab

# Optional USB-C service slot in the skirt bottom wall (default OFF -- first flash is done with
# the XIAO out of the case; afterwards it's OTA over WiFi).  Turn on if you want cabled access.
USB_SERVICE_SLOT = False
USB_SLOT_W = 12.0
USB_SLOT_H = 7.0

# ======================================================================================
#  DERIVED  (nothing below is a free choice)
# ======================================================================================
# --- charger cavity (with slip fit) ---
CAV_W = CHG_W + FIT_W          # 43.78  interior width  (BP 43.8)
CAV_H = CHG_H + FIT_H          # 51.40  interior height (BP 51.4)
CAV_D = CHG_D + FIT_D          # 20.72  charger depth   (BP 20.7)

# --- depth stack (Z): back rim -> charger -> head-relief gap -> LED stick -> front wall ---
LED_BACK_Z     = CAV_D + STRIP_HEAD_RELIEF   # PCB back; screw heads live in the gap below it
LED_PCB_TOP_Z  = LED_BACK_Z + LED_PCB_T
LED_DOME_TOP_Z = LED_PCB_TOP_Z + LED_BODY_H
D_IN  = LED_DOME_TOP_Z + DOME_RECESS         # front wall interior z (domes recess DOME_RECESS)
OUT_D = D_IN + FRONT_T                        # outer front face z; = protrusion from the wall
PILOT_TOP_Z = OUT_D - PILOT_WALL_MIN          # blind boss pilot stops here (face stays intact)

# --- outer envelope ---
OUT_W = CAV_W + 2 * WALL
YMAX  = CAV_H + WALL           # top wall outer face (+Y)
YMIN  = -(SKIRT_H + WALL)      # skirt bottom outer face (-Y)
OUT_H = YMAX - YMIN            # total height
OUT_W2 = OUT_W / 2
CAV_W2 = CAV_W / 2
IN_R   = OUT_R - WALL

# --- LED stick pose (world): vertical, centred on the charger face ---
LED_CX = 0.0
LED_CY = CAV_H / 2             # centred over the charger height
# the two mounting holes (world): both on one X side, ~1/4 and ~3/4 up the stick
LED_HOLE_XY = [(LED_CX + LED_HOLE_SIDE * LED_HOLE_DX, LED_CY + oy) for oy in LED_HOLE_OFFS_Y]

# window over the emitter run (vertical): tall in Y, narrow in X
WIN_W = LED_5050 + 2 * WIN_MARGIN                       # along X
WIN_H = LED_LIT_SPAN + LED_5050 + 2 * WIN_MARGIN        # along Y
WIN_CX, WIN_CY = LED_CX, LED_CY

# stick pocket footprint (locates the stick in X/Y)
POCKET_W = LED_W + 2 * POCKET_FIT     # X
POCKET_H = LED_L + 2 * POCKET_FIT     # Y

# --- XIAO pose in the skirt (world): lying FLAT on the floor, PCB in the X-Z plane ---
XIAO_CX = 0.0
XIAO_FLOOR_Y = -SKIRT_H                 # skirt floor (interior)
XIAO_CY = XIAO_FLOOR_Y + PCB_T / 2      # PCB centre, resting on the floor
XIAO_CZ = (COVER_T + D_IN) / 2          # centred in the skirt depth (front wall <-> cover)

# --- skirt back cover + its rebate (world) ---
# rebate: a flush pocket across the skirt back, inset COVER_BORDER from the outer edge, open at
# the top (y=0) into the charger region.
REB_X   = OUT_W - 2 * COVER_BORDER               # rebate width
REB_Y0  = YMIN + COVER_BORDER                     # rebate bottom (keeps a bottom-wall border)
REB_Y1  = 0.0                                     # rebate top (charger junction)
REB_H   = REB_Y1 - REB_Y0
REB_CY  = (REB_Y0 + REB_Y1) / 2
COVER_W = REB_X - 2 * COVER_FIT
COVER_H = REB_H - 2 * COVER_FIT
COVER_CX = 0.0
COVER_CY = REB_CY
# two cover screws, mid-skirt on each side; bosses are gusseted out to the side walls
COVER_SCREW_XY = [(sx * COVER_BOSS_X, -SKIRT_H / 2) for sx in (-1, 1)]
COVER_PILOT_TOP_Z = COVER_T + COVER_PILOT_DEPTH   # blind pilot top (inside the skirt)

# top retaining lip (derived): tab/tongue split plane + the cover's top edge
COVER_LIP_T   = COVER_T * COVER_LIP_FRAC          # tab thickness = outer slice of the cover
COVER_TOP_Y   = COVER_CY + COVER_H / 2            # cover top edge (world Y)
