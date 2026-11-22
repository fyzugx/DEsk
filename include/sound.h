#ifndef DESK_SOUND_H
#define DESK_SOUND_H

/* What the desk sounds like.
 *
 * Every sound is made here, not recorded: paper is a shower of tiny
 * crackles behind a breath of moving air, wood and metal are the few
 * resonances they ring at, a drawer is friction along its runners (see
 * sfx.c) -- or, the ones chosen by ear, made the same way in Faust and
 * baked (art/sound). They are made once, a few of each so that nothing sounds the
 * same twice, and mixed on a thread of their own -- pitched a little, and
 * placed left or right where the thing is on the screen -- into the sound
 * server (PulseAudio, or PipeWire speaking its language), ALSA if there is
 * no server, or nowhere. Nothing here blocks the caller. */

#include <stdbool.h>

enum desk_sound {
	/* paper */
	DESK_SND_PAPER_SWING = 0,  /* a flap going over, through the air */
	DESK_SND_PAPER_PRESS,      /* and pressed down: the crease */
	DESK_SND_PAPER_UNPRESS,    /* a crease let go, opening out */
	DESK_SND_PAPER_SETTLE,     /* a folded packet laid down */
	DESK_SND_PAPER_UNFOLD,     /* a sheet opening out to its size */
	DESK_SND_PAPER_CRUMPLE,    /* screwed up */
	DESK_SND_PAPER_TOSS,       /* thrown */
	DESK_SND_PAPER_LAND,       /* landing, off the desk, some way off */
	DESK_SND_CORNER_LIFT,      /* a corner turning up */
	DESK_SND_CORNER_TAP,       /* what is printed on its back, pressed */
	/* the drawer */
	DESK_SND_DRAWER_OUT,       /* sliding out on its runners */
	DESK_SND_DRAWER_IN,
	DESK_SND_DRAWER_STOP_OUT,  /* at its stop, the things in it shifting */
	DESK_SND_DRAWER_STOP_IN,   /* home */
	DESK_SND_FELT_PICK,        /* something lifted off the felt */
	DESK_SND_FELT_DROP,        /* and laid on it */
	/* the bookcase */
	DESK_SND_BINDER_LIFT,      /* a binder drawn out of the row */
	DESK_SND_BINDER_OPEN,      /* laid open on the desk, the ring rattling */
	DESK_SND_BINDER_CLOSE,
	DESK_SND_PAGE_TURN,        /* a sheet going over the ring */
	DESK_SND_SHEET_TAKE,       /* a sheet taken off the ring */
	DESK_SND_SHEET_DOWN,       /* a sheet put down on the desk */
	DESK_SND_SHEET_DOWN_SOFT,  /* and on a mat: leather, linoleum, felt (baked) */
	/* a sheet slid under the hand, going round for as long as it moves
	 * (desk_sound_loop): slowly and quickly, faded between by how fast it
	 * goes, on the wood, a soft mat and the clear one (baked) */
	DESK_SND_SLIDE_WOOD_SLOW,
	DESK_SND_SLIDE_WOOD_QUICK,
	DESK_SND_SLIDE_SOFT_SLOW,
	DESK_SND_SLIDE_SOFT_QUICK,
	DESK_SND_SLIDE_CLEAR_SLOW,
	DESK_SND_SLIDE_CLEAR_QUICK,
	/* the drawer on a finger, going round for as long as it moves (desk_sound_loop): out and in,
	 * slowly and quickly, faded between by how fast it goes (baked) */
	DESK_SND_DRAWER_RUN_OUT_SLOW,
	DESK_SND_DRAWER_RUN_OUT_QUICK,
	DESK_SND_DRAWER_RUN_IN_SLOW,
	DESK_SND_DRAWER_RUN_IN_QUICK,
	/* the post box */
	DESK_SND_POST_OPEN,        /* its door, on its hinge */
	DESK_SND_POST_CLOSE,
	DESK_SND_LETTER_ARRIVE,    /* a letter through the slot */
	DESK_SND_LETTER_TAKE,      /* a letter drawn out */
	DESK_SND_LETTER_FLICK,     /* the letters turned over */
	/* switches, keys and the set */
	DESK_SND_KEY_PRESS,        /* a key of the clock, a button */
	DESK_SND_KEY_RELEASE,
	DESK_SND_TOGGLE,           /* a lever on the console */
	DESK_SND_KNOB_TICK,        /* a knob's detent */
	DESK_SND_KEY_IN,           /* the key going into the lock */
	DESK_SND_LOCK_CLUNK,       /* and turned: the bolt */
	DESK_SND_GUARD_LIFT,       /* a lid of glass or plastic, on its hinge */
	DESK_SND_GUARD_STOP,       /* and at its stop */
	DESK_SND_LID_LIFT,         /* the console's walnut lid, on its brass hinge */
	DESK_SND_LID_STOP,         /* and at either end of its swing */
	DESK_SND_POWER_PRESS,      /* the big button, and the relay behind it */
	DESK_SND_POWER_DOWN,       /* the set going out: hum, whine, ticks */
	DESK_SND_CARD_SLIDE,       /* a card out from under the console */
	DESK_SND_SET_WARM,         /* the set switched on: a relay, the hum swelling */
	DESK_SND_SET_OFF,          /* and off */
	/* writing */
	DESK_SND_PENCIL_TICK,      /* a box ticked */
	DESK_SND_PENCIL_RING,      /* a word ringed */
	DESK_SND_AERIAL_UP,        /* the wireless aerial's motor, putting it up */
	DESK_SND_AERIAL_DOWN,      /* and taking it down */
	/* the Control Panel's own (baked) */
	DESK_SND_CC_CAP_ON,        /* a lit cap that puts something on */
	DESK_SND_CC_CAP_OFF,       /* and off */
	DESK_SND_CC_CAP,           /* one that does something once (DISPLAY) */
	DESK_SND_CC_CAP_SLEEP,     /* SLEEP's and SUSPEND's, smaller */
	DESK_SND_CC_AIR_ON,        /* AIRPLANE: its cap and both levers dropping */
	DESK_SND_CC_AIR_OFF,       /* and all of it back */
	DESK_SND_CC_LEVER_UP,      /* a lever thrown on */
	DESK_SND_CC_LEVER_DOWN,    /* and off */
	DESK_SND_CC_KNOB,          /* a knob's detent */
	DESK_SND_CC_KEY_IN,        /* the key going into the lock */
	DESK_SND_CC_KEY_TURN,      /* and turned */
	DESK_SND_CC_KEY_BACK,      /* turned back */
	DESK_SND_CC_KEY_OUT,       /* and drawn out */
	DESK_SND_CC_LID_UP,        /* the walnut lid lifted */
	DESK_SND_CC_LID_UP_STOP,   /* and at the top of its swing */
	DESK_SND_CC_LID_DOWN,      /* let down */
	DESK_SND_CC_LID_DOWN_STOP, /* and shut */
	DESK_SND_CC_POWER,         /* SHUT DOWN: the button, the relay, the set going out */
	/* the days kept: pitched by rate, from the note given */
	DESK_SND_BEEP,             /* the clock's buzzer, a tune's note: C5 */
	DESK_SND_TICK,             /* a metronome's beat */
	DESK_SND_TOCK,             /* and the first of the bar */
	DESK_SND_BELL,             /* a glockenspiel's bar: C6 */
	DESK_SND_JINGLE,           /* sleigh bells, shaken */
	DESK_SND_FW_WHISTLE,       /* a firework going up */
	DESK_SND_FW_BOOM,          /* and bursting, the sound coming back off the houses */
	DESK_SND_FW_CRACKLE,       /* its stars crackling as they fall */
	DESK_SND_POPPER,           /* a party popper going off, its paper showering */
	/* the camera Print looks through */
	DESK_SND_SHUTTER,          /* the mirror up, the shutter, the mirror down, the wind */
	DESK_SND_TAPE_IN,          /* a tape pushed into its slot, taken home, taken up */
	DESK_SND_TAPE_OUT,         /* and thrown out of it */
	DESK_SND_REC_START,        /* the camera recording: its beep, its motor */
	DESK_SND_REC_STOP,         /* and stopped */
	DESK_SND_FLASH,            /* a flash going off, and charging again */
	/* the typewriter */
	DESK_SND_TYPE_STRIKE,      /* a key going down, the type on the platen */
	DESK_SND_TYPE_RELEASE,     /* and back up, the carriage going on a space */
	DESK_SND_TYPE_SPACE,       /* the bar: no type, only the carriage */
	DESK_SND_TYPE_SHIFT,       /* the whole basket lifting */
	DESK_SND_TYPE_BELL,        /* the margin's bell */
	DESK_SND_TYPE_RETURN,      /* the lever, the platen's ratchet, the carriage going home */
	DESK_SND_COUNT
};

/* Start: the sounds are made and the server is found on a thread of their
 * own; until both are done, nothing plays. app names the client to the
 * server ("DEsk"). */
void desk_sound_init(const char *app);
void desk_sound_shutdown(void);
/* How loud, against as designed: 0 is silent (and nothing is mixed). */
void desk_sound_set_level(double level);
/* Play s, at pan -1 (left) .. 1 (right), gain against as designed; rate
 * against its own (a faster flap is a higher swish), 0 for its own. A
 * little different every time. Returns a handle for stopping it, or 0. */
int desk_sound_play(enum desk_sound s, double pan, double gain, double rate);
/* The same, to be heard `in` seconds from now: for what is seen to happen
 * then (a drawer reaching its stop), asked for while it is on its way, so
 * that it is put out early by as long as the way to the speakers takes.
 * Asked for too late, it is played as soon as it can be. */
int desk_sound_play_in(enum desk_sound s, double pan, double gain, double rate,
	double in);
/* How long that way is just now, in seconds: what is played now is heard
 * this much later. 0 with nowhere to play to. */
double desk_sound_latency(void);
/* Somebody is at the desk (a key, the pointer). If asked to
 * (DESK_SOUND_AWAKE_S), the way to the speakers is kept open for that
 * long, so that the next sound is not kept waiting for it to open; by
 * default nothing, as some cards cannot be kept running (see sound.c).
 * Cheap; call it on any input. */
void desk_sound_awake(void);
/* The same, as written: exactly at rate (a tune's notes keep their pitch)
 * and gain, nothing varied but which of its variants it is. */
int desk_sound_play_exact(enum desk_sound s, double pan, double gain, double rate,
	double in);
/* s going round until stopped -- a sound for as long as something goes
 * on (a sheet slid), its loudness and place changed with desk_sound_set
 * as it does. Nothing varied. It stops by itself a second after it was
 * last set, as anything let go of does. A handle, or 0. */
int desk_sound_loop(enum desk_sound s, double pan, double gain);
void desk_sound_set(int handle, double pan, double gain);
/* Stop one, fading over fade seconds; one not yet begun is never heard; a
 * handle that has finished is fine. */
void desk_sound_stop(int handle, double fade);
/* Stop it as heard `after` seconds from now (as the thing it goes with
 * stops): a sound that is heard late is cut to end on time. */
void desk_sound_stop_after(int handle, double after, double fade);
/* Never mind one asked for with play_in, if it has not begun. */
void desk_sound_cancel(int handle);

/* For listening to them: sound s, made with variation v, as a 48 kHz
 * mono WAV; and its name. */
const char *desk_sound_name(enum desk_sound s);
int desk_sound_variants(enum desk_sound s);
bool desk_sound_write_wav(enum desk_sound s, int variant, const char *path);

/* What sfx.c makes (those baked in art/sound are read by sound.c, and
 * have nothing here): sound s, variant v, as mono float at 48 kHz (the
 * caller frees it). */
float *desk_sfx_make(enum desk_sound s, int variant, int *frames);
#define DESK_SFX_RATE 48000

#endif
