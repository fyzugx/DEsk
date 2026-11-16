/* camera-map.h: made by camera.py with camera.png -- do not edit, make again.
 * Where each piece of the camera is in camera.png (ax, ay, w by h) and where
 * it goes on the camera, in its pictures' pixels (x, y): CAMERA_PX to the unit
 * of draw.c's camera (DESK_FINDER_CAMERA_W by _H). The base is the camera
 * whole -- the dial's knurling at its first turn, on, its button up, no knob in
 * the self-timer's slide, the counter's window bare, no tape; the rest go over
 * it. And where its parts are, in the camera's units. */

#define CAMERA_PX 3

struct camera_piece {
	int ax, ay, w, h;   /* in camera.png */
	int x, y;           /* on the camera */
};

static const struct camera_piece CAMERA_BASE = { 0, 0, 1800, 1363, 0, 77 };
/* the dial's knurling turned a quarter, a half, three quarters of a knurl on */
static const struct camera_piece CAMERA_KNURL[3] = { { 0, 1365, 432, 253, 150, 172 }, { 434, 1365, 432, 253, 150, 172 }, { 868, 1365, 432, 253, 150, 172 } };
/* the power lever at 0 (O), a quarter, a half and three quarters of the way to I */
static const struct camera_piece CAMERA_POWER[4] = { { 1302, 1365, 312, 189, 1248, 216 }, { 0, 1620, 312, 189, 1248, 216 }, { 314, 1620, 312, 189, 1248, 216 }, { 628, 1620, 312, 189, 1248, 216 } };
/* the shutter button a third, two thirds and all the way down */
static const struct camera_piece CAMERA_PRESS[3] = { { 942, 1620, 312, 189, 1248, 216 }, { 1256, 1620, 312, 189, 1248, 216 }, { 0, 1811, 312, 189, 1248, 216 } };
/* the self-timer's knob and its shadow, the knob at two seconds */
static const struct camera_piece CAMERA_KNOB = { 314, 1811, 96, 93, 1134, 421 };
/* the frame counter's glass: what it reflects, over its figures */
static const struct camera_piece CAMERA_DOME = { 412, 1811, 79, 29, 1154, 311 };
/* the eyepiece, its glass seen through */
static const struct camera_piece CAMERA_SEE = { 493, 1811, 288, 263, 756, 322 };
/* the tape's slot, its flap pushed in and nothing in it */
static const struct camera_piece CAMERA_SLOT = { 783, 1811, 494, 98, 215, 414 };
/* the tape in it, all but its spine's end in, its shadow under it */
static const struct camera_piece CAMERA_TAPE_IN = { 0, 2076, 548, 199, 221, 383 };
/* the tape alone, at the slot's mouth, its far end at it */
static const struct camera_piece CAMERA_TAPE = { 550, 2076, 430, 126, 247, 453 };

#define CAMERA_KNOB_STEP 22.0   /* the knob from one setting to the next, units */
#define CAMERA_KNOB_X 372.0      /* where it is off */
#define CAMERA_KNOB_Y 155.7
#define CAMERA_DIAL_X 122.0      /* the dial's top: its middle, its round */
#define CAMERA_DIAL_Y 89.8
#define CAMERA_DIAL_RX 55.6
#define CAMERA_DIAL_RY 18.3
#define CAMERA_DIAL_FOOT 111.0   /* where its foot is, at its middle */
#define CAMERA_DIAL_HUB 12.0    /* its hub, across */
#define CAMERA_DIAL_MARK_R 35.2 /* the pictures, from the middle */
#define CAMERA_DIAL_MARK 20.0   /* and across */
#define CAMERA_DIAL_STEP 0.95   /* a step of the dial, in radians */
#define CAMERA_KNURLS 60
#define CAMERA_COUNT_X 398.0     /* the frame counter's window: its middle, its round */
#define CAMERA_COUNT_Y 110.8
#define CAMERA_COUNT_RX 13.2
#define CAMERA_COUNT_RY 4.4
#define CAMERA_SHUT_X 468.0      /* the shutter button's top */
#define CAMERA_SHUT_Y 98.1
#define CAMERA_SHUT_R 15.0
#define CAMERA_COLLAR_Y 105.7    /* the power collar's, and its lever's reach */
#define CAMERA_COLLAR_R 40.0
#define CAMERA_EYE_X 273.0       /* the eyepiece's glass */
#define CAMERA_EYE_Y 126.0
#define CAMERA_EYE_W 54.0
#define CAMERA_EYE_H 43.8
#define CAMERA_PLATE_FAR 91.8   /* the top plate: its far edge, its near, its foot */
#define CAMERA_PLATE_NEAR 130.2
#define CAMERA_PLATE_FOOT 170.3
#define CAMERA_ROOF 44.8        /* the prism's roof */
#define CAMERA_TAPE_CLIP 149.5   /* the slot's top edge: of the tape going in, none
                              * of it over this is seen */
#define CAMERA_TAPE_PUSH 24.6   /* how far up it goes, pushed in from the mouth */
#define CAMERA_SLOT_X0 73.2     /* the slot, the tape in it and the eject mark */
#define CAMERA_SLOT_X1 248.4
#define CAMERA_SLOT_Y0 146.5
#define CAMERA_SLOT_Y1 170.7
#define CAMERA_LEFT 15.2        /* the camera's ends, lugs and all */
#define CAMERA_RIGHT 584.8
