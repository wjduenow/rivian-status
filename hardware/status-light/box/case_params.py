"""
Shared geometry for the `rivian-status` desk light -- a Seeed XIAO ESP32-S3 with an
8-pixel WS2812 stick stacked ABOVE it, in a small box whose LEDs face UP through a window
in the LID.  Everything is screwed: the stick screws down onto two posts, the lid screws
to four side-wall bosses.

    +X  width   (long axis -- the 54 mm LED stick runs along X)
    +Y  depth   (front -> back)
    +Z  up      (z = 0 is the OUTER underside of the floor; the LEDs shine toward +Z)

World origin: centred in X and Y, on the floor's underside.

LAYOUT NOTE.  The stick is stacked over the board and the lid is screwed down, so the
board cannot also sit against a short-end wall (a corner screw boss would hit it).  The
board therefore lies front-to-back under the stick and the **USB-C exits the back (+Y)
wall, centred** -- fully proud, no recess.  (An end-exit USB-C is possible but needs the
board recessed ~5 mm from that wall; not done here.)

Same toolchain + conventions as ../../../../sonos-nest/hardware (trimesh CSG, mm, every
number provenance-commented; clearances ASSERTED in build_shell.check_clearances()).
"""

# ======================================================================================
#  XIAO ESP32-S3  --  all numbers read out of Seeed's own STEP (../ref/XIAO-ESP32S3 v2.step,
#  measured in mm; see ../board_spec.md).  Board-local: length(USB<->antenna) x width x thick.
# ======================================================================================
PCB_L        = 21.0    # STEP SOLID X-span 20.955.  In THIS box, laid along world +Y.
PCB_W        = 17.8    # STEP SOLID Z-span 17.780.  Laid along world +X.
PCB_T        = 1.25    # STEP SOLID Y-span (Seeed spec 1.2)
PCB_FIT      = 0.4     # per-side slack in the board pocket

# No screw mounting holes (see board_spec.md) -> the board drops onto standoffs in a rib
# pocket, pinned by the USB-C in its port + clamped by two lid pads.  Wires solder DIRECTLY
# to the pads (user-confirmed -- no header), so the board back is flat.
HEADER_PINS_DOWN = False

# USB-C, on the board's +Y (length) end.  Exits the back wall, centred on X.
USB_OVERHANG = 1.53    # connector shell past the PCB edge (STEP)
USB_SHELL_W  = 8.94    # shell width  (board-width direction = world X)
USB_SHELL_H  = 3.26    # aperture height (world Z)
USB_CENTRE_ABOVE_PCB_BACK = 2.36   # aperture centre above the PCB back face (STEP)
USB_PORT_W   = 12.0    # port opening in X (for a cable overmold)
USB_PORT_H   = 7.0     # port opening in Z

# U.FL external-antenna jack, on TOP at the −Y (antenna) end.  MUST stay plugged in
# (CLAUDE.md) -> leave plug clearance + a pigtail exit in the front wall.
UFL_ALONG_LEN   = -6.95   # from board centre along the length (world −Y)
UFL_ACROSS_W    =  4.73   # offset across the width (world X)
UFL_ABOVE_PCB_TOP = 1.25  # bare jack; a mated plug adds ~3
UFL_PLUG_CLEARANCE = 4.0  # vertical air kept above the jack for the mated plug

COMP_Z_ABOVE_PCB_BACK = 4.21   # tallest top part (the USB-C shell), from the PCB back (STEP)

# ======================================================================================
#  8-pixel WS2812 stick  --  MEASURED by the user (generic bare 8-bit stick)
# ======================================================================================
LED_L        = 51.5    # stick length (measured)
LED_W        = 10.0    # stick width (the short edge; runs along world Y)
LED_PCB_T    = 1.6     # bare PCB thickness (user: 3.0 total incl. LEDs, minus 1.4 domes)
LED_BODY_H   = 1.4     # how far the 5050 domes stand off the PCB face
LED_COUNT    = 8       # locked by the firmware pixel map (plan §7)
LED_LIT_SPAN = 45.0    # centre-of-first-LED to centre-of-last

# Mounting holes -- the box screws the stick down onto two posts through these.  From the
# real board photo the holes are NOT under the LEDs: they sit off to ONE long edge (by the
# Cx caps / pad column), ~1/4 and ~3/4 along the length.
LED_HOLE_COUNT = 2
LED_HOLE_D     = 3.75   # measured hole Ø
LED_HOLE_X     = [-12.75, 12.75]   # each hole's X-offset from the stick centre (measured)
LED_5050       = 5.0    # the 5050 emitter is 5.0 mm square
# offset of the hole row from the LED optical row, toward one long edge (both holes on the
# same side).  Dialled in against the real board -- the posts read 3 mm too far back at 4.5:
LED_HOLE_DY    = 1.5
LED_DIN_AT_PLUS_X = True   # DIN toward +X (board is centred, so cosmetic)

# ======================================================================================
#  Fasteners  (M3 self-tapping into printed bosses -- same family as sonos-nest)
# ======================================================================================
SCREW_PILOT   = 2.6    # M3 thread-forming pilot Ø
STRIP_SCREW_LEN = 6.0  # M3 x 6 through the stick into a post
LID_SCREW_LEN = 8.0    # M3 x 8 through the lid into a side boss
LID_SCREW_CLR = 3.4    # M3 clearance hole through the lid
LID_CSK_D     = 6.0    # countersink Ø on the lid top (flat/pan head sits flush-ish)

# ======================================================================================
#  Shell / lid
# ======================================================================================
WALL        = 2.2
FLOOR_T     = 2.0
LID_T       = 2.5      # thicker: takes the countersinks + the window rebate
STANDOFF    = 2.0      # board off the floor (clears bottom pads / solder)

BOARD_RIB_T = 1.5
BOARD_RIB_H = STANDOFF + PCB_T + 0.6

# stick seating (stacked above the board)
STRIP_BOTTOM_Z = 11.0  # stick PCB underside height -- clears the tallest board part +
                       # the mated antenna plug below it (asserted in the builder)
POST_OD     = 6.0      # stick-mount post Ø (M3 self-tap core)
LED_BELOW_TOP = 2.0    # the LED dome tops sit this far below the case top, so they show
                       # up near the surface of the window slot (not sunk deep in it)

# lid window (diffuser)
WIN_MARGIN_X = 0.5     # window overhang past the LED EMITTER extent, per end
WIN_INSET_Y  = 0.5     # window margin past the 5050 emitter row, per edge
DIFFUSER_REBATE_T = 0.8
DIFFUSER_REBATE_W = 1.2

# antenna: keep the U.FL pigtail + flex antenna INSIDE the box for now.  The jack still
# gets its vertical plug clearance (asserted); the antenna just coils in the open floor
# space around the board.  A plastic box is RF-transparent enough for a short-range desk
# appliance -- set ANT_EXIT = True to cut a pigtail slot in the front wall if WiFi suffers.
ANT_EXIT    = False
ANT_NOTCH_W = 5.0
ANT_NOTCH_H = 6.0

OUT_R       = 3.0
SEG         = 96

# ======================================================================================
#  DERIVED  (nothing below is a free choice)
# ======================================================================================
IN_X   = LED_L + 2.0                       # interior clears the stick
OUT_X  = IN_X + 2 * WALL
IN_Y   = PCB_L + 1.0                        # board length + 0.5 gap each end
OUT_Y  = IN_Y + 2 * WALL
IN_X2, IN_Y2 = IN_X / 2, IN_Y / 2
OUT_X2, OUT_Y2 = OUT_X / 2, OUT_Y / 2

# stick heights.  The dome tops sit LED_BELOW_TOP under the case top; the shell rim lands
# just below the domes so they poke up into the lid window.
STRIP_PCB_TOP_Z = STRIP_BOTTOM_Z + LED_PCB_T
STRIP_DOME_TOP_Z = STRIP_PCB_TOP_Z + LED_BODY_H
LED_MID_Z = STRIP_PCB_TOP_Z            # LED emitter plane ≈ PCB top
OUT_Z  = STRIP_DOME_TOP_Z + LED_BELOW_TOP
SHELL_H = OUT_Z - LID_T

# board pose: centred in X and Y, USB-C at the +Y edge near the back wall
BOARD_CX = 0.0
BOARD_CY = 0.0
PCB_FRONT_Y = BOARD_CY - PCB_L / 2     # −Y (antenna) edge
PCB_BACK_Y  = BOARD_CY + PCB_L / 2     # +Y (USB-C) edge
PCB_BACK_Z  = FLOOR_T + STANDOFF
PCB_TOP_Z   = PCB_BACK_Z + PCB_T

# USB-C port (back +Y wall)
USB_PORT_CX = BOARD_CX
USB_PORT_CZ = PCB_BACK_Z + USB_CENTRE_ABOVE_PCB_BACK
USB_TIP_Y   = PCB_BACK_Y + USB_OVERHANG

# U.FL jack (world) + antenna notch
UFL_X = BOARD_CX + UFL_ACROSS_W
UFL_Y = BOARD_CY + UFL_ALONG_LEN
UFL_TOP_Z = PCB_TOP_Z + UFL_ABOVE_PCB_TOP

# stick pose (world): the LED optical row is centred in the box (under the window); the
# stick body + its screw holes sit offset toward +Y (the holes' edge).
LED_CX, LED_CY = 0.0, 0.0                   # LED emitter row (window centre)
POST_CY = LED_CY + LED_HOLE_DY             # the two stick-mount posts, offset off the LEDs
STRIP_CY = LED_CY                          # stick body ≈ centred on its LED row
# lid window -- spans the full LED EMITTER extent (lit span + one emitter), so every pixel
# shows through and it's long enough end-to-end
WIN_W = LED_LIT_SPAN + LED_5050 + 2 * WIN_MARGIN_X   # along X
WIN_D = LED_5050 + 2 * WIN_INSET_Y                   # along Y (over the 5050 row)
WIN_CX, WIN_CY = LED_CX, LED_CY

# lid screw bosses: on the side (±X) walls, clear of the narrow board and the stick
LID_BOSS_OD = 5.5
LID_BOSS_XY = [(sx * (IN_X2 - LID_BOSS_OD / 2 - 0.3), sy * (IN_Y2 - LID_BOSS_OD / 2 - 0.3))
               for sx in (-1, 1) for sy in (-1, 1)]

# board clamp pads (lid): straddle the ~9 mm USB-C shell, landing on the bare PCB either
# side of it (not on top of the connector).  The strip between the shell edge (±4.47) and
# the board edge (±8.9) is only ~4.4 mm, so the pad is slim and sits with ~0.4 mm clear of
# each.
BOARD_PAD_D = 3.4
BOARD_PAD_X = USB_SHELL_W / 2 + 0.4 + BOARD_PAD_D / 2     # inner edge 0.4 off the shell
BOARD_PAD_XY = [(-BOARD_PAD_X, PCB_BACK_Y - 3.0), (BOARD_PAD_X, PCB_BACK_Y - 3.0)]
