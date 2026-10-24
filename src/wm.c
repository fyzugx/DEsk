/* ================================================================
 * desk: the compositor.
 *
 * The whole desktop is this one program on wlroots 0.18: the outputs and
 * the input devices, the windows (xdg-shell and Xwayland) laid on paper,
 * and every part of the desk a hand can use -- the drawer, the bookcase and
 * its binders and book, the calendar and memo pad, the Control Panel and
 * the tray under it, the post box, the clock, the front door (the lock),
 * Ctrl+Alt+Del -- with shell.conf and displays.conf read and written here.
 * What any of it looks like is drawn by the drawing library (draw.c,
 * include/desk.h); this file decides when, where, and in answer to what.
 *
 * One struct desk_server holds the state. The scene is a stack of layers,
 * bottom to top in main(), and docs/ARCHITECTURE.md (4.1) says what goes on
 * which. A frame is output_frame: the animations moved on by real time
 * (output_frame_work), then the scene committed -- and nothing drawn at
 * all when nothing has changed. Pointer events go through
 * process_cursor_motion, server_cursor_button and server_cursor_axis in a
 * fixed order (the lock, what is carried, a window being moved, the
 * screens beside the desk's, the view turned to, the desk, the windows);
 * keys through handle_keybinding.
 *
 * The file is in sections, each starting with a comment of the form
 * ---- what it is ----: search for "---- " for a table of contents.
 * ================================================================ */

#include "compat.h"
#include "i18n.h"
#include "memo.h"
#include "session.h"
#include "lock.h"
#include "camera.h"
#include "occasion.h"
#include "fonts.h"
#include "desk.h"
#include "apps.h"
#include "panel.h"
#include "power.h"
#include "sound.h"
/* What a letter is, bus or no bus: without one the box is simply empty. */
#include "post.h"
#include "ime.h"
#include "agenda.h"
#ifdef DESK_HAVE_POST
#include "tray.h"
#endif
#ifdef DESK_HAVE_GPU
/* what the screen is drawn with: a GPU, or the processor in its place */
#include "gpu.h"
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#endif

#include <assert.h>
#include <drm_fourcc.h>
#include <getopt.h>
#include <linux/input-event-codes.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <xf86drm.h>
#include <xkbcommon/xkbcommon.h>

#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#ifdef DESK_HAVE_LIBINPUT
#include <wlr/backend/libinput.h>
#endif
#include <wlr/render/allocator.h>
#include <wlr/render/pixman.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/util/region.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/util/addon.h>
#include <wlr/util/box.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/xcursor.h>
#include <wlr/config.h>
#include <wlr/backend/wayland.h>
#if WLR_HAS_X11_BACKEND
#include <wlr/backend/x11.h>
#endif
#ifdef DESK_HAVE_XWAYLAND
#include <wlr/version.h>
#include <wlr/xwayland.h>
#include <wlr/xwayland/server.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#endif

enum desk_cursor_mode {
	DESK_CUR_PASSTHROUGH,
	DESK_CUR_MOVE,
	DESK_CUR_RESIZE,
};

enum desk_role {
	DESK_ROLE_VIEW = 1,
	DESK_ROLE_DOCK,
	DESK_ROLE_SHEET,
};

enum desk_sheet_phase {
	SHEET_IDLE = 0,
	SHEET_FOLDING,
	SHEET_PARKED,
	SHEET_UNFOLDING,
};

/* A window's shadow lives in its own scene node instead of being painted
 * into the chrome. Moving a window then costs one node position update
 * rather than a repaint of the whole frame, which is what lets the shadow
 * swing round as the window crosses the lamp. */
struct desk_shadow {
	struct wlr_scene_buffer *node;
	int w, h;
	double side, amount;    /* which corner of the sheet is up, and how far */
	/* Where the shadow has got to, which is not where the sheet is. A sheet
	 * dragged across a desk does not carry its shadow rigidly: it bends,
	 * and the shadow comes along behind and catches up afterwards. */
	double lx, ly, vx, vy;  /* how far behind, and how fast it is closing */
	double px, py;          /* where the sheet was last time we looked */
	double held;            /* 0 down on the desk .. 1 picked up */
	double at;
	int settled;
	bool stretch;
};

struct desk_sheet {
	enum desk_sheet_phase phase;
	cairo_surface_t *src;
	struct desk_fold *fold;   /* how it folds, and what is printed on its back */
	double start;
	double dur;
	double x0, y0, x1, y1;
	double hit_x, hit_y, hit_w, hit_h;
	int slot;
	unsigned seed;   /* which way this sheet's folds go wrong */
	/* put away in the drawer rather than lying on the desk (and then not
	 * holding a place along the front of it) */
	bool in_drawer;
	bool to_drawer;  /* being folded straight into it (Shell Settings) */
	int snd_k;       /* the fold last heard going over (asked for, ahead) */
	/* its sounds asked for ahead and not yet heard, to be forgotten if
	 * it turns back; and whether the one for its landing has been */
	int snd_due[6], snd_due_i;
	bool snd_end;
	char name[96];   /* what is printed on the outside of the packet */
	struct wlr_scene_tree *tree;
	struct wlr_scene_buffer *buf;
	struct wlr_scene_tree *live;
	void *hit;
};

/* The shell's own settings (shell.conf): see "the shell's own settings". */
struct shell_conf {
	bool paper_all;             /* programs with frames of their own on paper too */
	bool throw_closed;          /* a closed window screwed up and thrown away */
	bool corner_hover;          /* a corner turns up when the hand rests on it */
	double scale;               /* points to pixels; 0: the screen's own */
	double motion;              /* how long things take, against normal */
	double bounce;              /* how much the camera goes past and settles,
	                             * and the head bobs walking: 1 as made, 0 none */
	bool clock_12h;
	char language[16];          /* "auto", "en", "ja", "zh", ... */
	bool drawer_closes;         /* the drawer goes back after something is used */
	bool take_sheet;            /* ALL APPS: the sheet is carried down for the window */
	bool fold_drawer;           /* a folded window goes into the drawer */
	double sound;               /* how loud the desk is: 0 silent */
	bool letter_sound;          /* a letter arriving is heard */
	bool lights_at_login;       /* the room starts dark and the lamp comes up */
	int gfx;                    /* how much is drawn: DESK_GFX_* (graphics) */
	bool gfx_own;               /* and chosen in the user's own file, not the machine's */
	bool reading_own;           /* (the user's own file is being read) */
	int bday_month, bday_day;   /* the birthday; 0 for none */
	bool celebrations;          /* days kept: the door dressed, the clock's shows */
	bool weather;               /* the door out in the real weather (asked for) */
	int screen_off;             /* minutes untouched before the screen goes dark; 0 never */
	bool lock_screen_off;       /* and the door comes down as it does */
	/* the keyboard and the pointer (input_apply) */
	bool tap_click;             /* a touchpad: a tap is a click */
	bool natural_scroll;        /* a touchpad: the page follows the fingers */
	bool left_handed;           /* the buttons the other way round */
	double pointer_speed;       /* libinput's, -1 to 1 */
	char kb_layout[32];         /* an xkb layout; "" the system's */
	char kb_second[32];         /* a second one to change to; "" none */
	char kb_switch[40];         /* the xkb option that changes between them */
	int repeat_rate, repeat_delay;   /* keys a second, and ms before the first */
	int n_apps;
	struct {
		char id[96];
		bool paper;
	} apps[64];                 /* and one program the other way */
	bool loaded, there;
	time_t mtime;
	long mtime_ns;
	off_t size;
};

/* One thing in the drawer: a program, kept there; the screwdriver (Shell
 * Settings); or a sheet folded away into it, which is there only while it is. */
enum drawer_kind {
	DRAWER_APP,
	DRAWER_PANEL,
	DRAWER_SHEET,
	DRAWER_STD,      /* one of DEsk's standard programs, whichever it is now */
};

#define DRAWER_MAX 32

struct drawer_entry {
	enum drawer_kind kind;
	char id[96];                  /* a program: its desktop file id */
	int std;                      /* a standard program: which (DESK_STD_*) */
	struct desk_sheet *sheet;   /* a sheet folded away */
	cairo_surface_t *packet;      /* and the packet it makes, made once */
	bool present;                 /* a program that is installed */
	double x, fit;                /* where it lies along the row, and how big */
	double x_to, fit_to;          /* and where it is on its way to */
};

/* A sheet taken out of a binder and put down on the desk: the program's
 * window comes to it (see "a sheet taken out of a binder"). */
enum base_state {
	BASE_FREE = 0,
	BASE_CARRIED,   /* in the hand, on its way down to the desk */
	BASE_OPENING,   /* put down, the paper coming out from behind it */
	BASE_WAITING,   /* open: the window, blank, the program not here yet */
	BASE_GOING,     /* the window has come, and it goes from under it */
};

#define DESK_BASES 4
#define TAKE_LIFT 0.22      /* off the ring and into the hand */
#define TAKE_TURN 0.22      /* when you turn back to the desk */
#define TAKE_DOWN 0.30      /* put down, once you are there */
#define BASE_OPEN_S 0.48    /* the paper coming out from behind it */
#define BASE_GO_S 0.25      /* and going from under the window */
#define BASE_WAIT_S 45.0    /* no window by then: thrown away */
#define BASE_GONE_S 5.0     /* and sooner, once nothing of the program runs */

struct desk_base {
	enum base_state state;
	char app[96];                 /* the program's desktop file id */
	char name[128];
	int x, y, w, h;               /* the frame of the window it is for, on the desk */
	double lw, lh;                /* the sheet that was put down, in the middle of it */
	cairo_surface_t *face;        /* what the catalogue printed on that sheet */
	cairo_surface_t *blank;       /* the paper the window will be */
	double at;                    /* when it went into the state it is in */
	double taken_at;
	unsigned seed;
	pid_t sid;                    /* the session its program was started in, or 0 */
	struct desk_toplevel *view; /* the window that has come to it */
	struct wlr_scene_buffer *node;
};

struct desk_buf {
	struct wlr_buffer base;
	cairo_surface_t *surface;
	cairo_t *cr;
};

/* One thing lying over the desk, kept the way a game keeps a sprite: its
 * own small buffer, drawn again only when its key says what it shows has
 * changed, and otherwise only ever moved. */
struct desk_sprite {
	struct wlr_scene_buffer *node;
	uint64_t key;
	bool drawn;
	int x, y;        /* on screen, as drawn: with the drawer shut */
	int dw, dh;      /* how big it is shown there */
	int ox, oy;      /* the overlay's pixel its picture's first pixel is */
	int bw, bh;      /* and the picture's size */
};

/* The book of every program lying still, drawn ahead and kept (book_ahead). */
#define BOOK_AHEAD 6
struct book_kept {
	struct desk_buf *buf;   /* held by a lock of its own; NULL: an empty slot */
	uint64_t key;
	int x, y, dw, dh;       /* where it goes on the screen, as shown there */
	int ox, oy;             /* the overlay's pixel its first pixel is */
	double used;            /* when it was last wanted: the oldest goes first */
};

struct desk_server;

/* the shelf view's layers, bottom to top */
enum { SH_CASE, SH_HOVER, SH_GADGET0, SH_GADGET1, SH_GADGET2, SH_BINDER, SH_BOOKHOT,
	SH_BOOKPLATE, SH_LEAF, SH_LAYERS };

struct desk_output {
	struct wl_list link;
	struct desk_server *server;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
	/* Turned off from a display tool (off): that is how the screens are
	 * set up, and nothing but a display tool turns it on again. Gone dark
	 * (dark): nobody has touched anything for a while, and the next touch
	 * undoes it -- the mode it was in, to come back in; nested, where the
	 * window on the other desktop is not ours to turn off, the black laid
	 * over it instead and whether that has been shown yet. */
	bool off, dark, dark_shown;
	struct wlr_output_mode *dark_mode;
	int dark_w, dark_h, dark_refresh;
	struct wlr_scene_rect *dark_cover;
	/* A screen beside the desk's own: the bare desk under its windows
	 * (side_desks_paint), and what it was drawn for -- where the screen
	 * is, how big, and the room it is cut from. */
	struct wlr_scene_buffer *side;
	struct wlr_box side_box;
	unsigned side_room;
	int side_level;
};

/* A program asking for the screen to stay lit (idle-inhibit): a film being
 * played, a call. It holds only while its window is out to be seen. */
struct desk_idle_hold {
	struct wl_list link;
	struct desk_server *server;
	struct wlr_idle_inhibitor_v1 *inhibitor;
	struct wl_listener destroy, map, unmap;
};

/* A window: a Wayland one, or an X one come through Xwayland. Exactly one
 * of xdg_toplevel and xsurface is set, and nearly everything below is the
 * same for both -- the paper, its frame, its shadow, the fold. Where the
 * two protocols differ the view_* helpers do, and nothing else asks. */
struct desk_toplevel {
	struct wl_list link;
	struct desk_server *server;
	bool above;      /* kept in front of the other sheets: a picture-in-picture */
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_xwayland_surface *xsurface;
	struct wlr_scene_tree *scene_tree;
	struct wlr_scene_tree *surface_tree;
	struct wlr_scene_buffer *chrome;
	int outer_w, outer_h;
	int maximized;
	int saved_x, saved_y, saved_inner_w, saved_inner_h;
	/* No frame of ours: the client draws its own (or asks for none), or
	 * it has the whole screen. The client then starts at the sheet's
	 * corner, and the paper is only as big as the client. */
	bool bare;
	/* It draws its own frame and is laid on our paper anyway: a plain
	 * margin of paper round it, no heading (see "whose frame a window
	 * has"). */
	bool own_frame;
	bool fullscreen;
	int fs_x, fs_y, fs_inner_w, fs_inner_h;   /* where it was before */
	/* A Wayland client that draws its own frame -- a GTK one with a header
	 * bar, which says so -- also draws its own shadow round it, in a margin
	 * of its surface outside the window proper. */
	bool own_shadow;
	/* it asked for ours by xdg-decoration (GTK 3 asks by KDE's protocol:
	 * kde_server_frame) */
	struct wlr_xdg_toplevel_decoration_v1 *deco;
	struct wl_listener deco_destroy;
	struct wl_listener deco_request_mode;
	/* what the frame was last drawn as: a commit that changes none of it
	 * -- which is nearly all of them -- draws nothing */
	uint64_t chrome_key;
	/* The turned-down top right corner, with what can be done with the
	 * sheet printed on its back (desk_draw_curl): how far it is turned
	 * down and how fast it is moving there, whether the pointer is near
	 * enough to lift it, the instruction under the pointer, and what was
	 * pressed while its turn of the corner plays out. */
	struct wlr_scene_buffer *curl;
	double curl_s, curl_v, curl_at, curl_box;
	/* what lies under the corner, where the paper was: the desk and
	 * whatever sheets are beneath, put together once per lift */
	cairo_surface_t *curl_under;
	int curl_under_x, curl_under_y, curl_under_c;
	bool curl_under_full;
	bool curl_near;        /* turned up: its back can be read */
	double curl_hover_at;  /* when the pointer came to rest on the fold, or 0 */
	int curl_hot;
	int curl_act;
	double curl_act_at;
	bool curl_done;
	uint64_t curl_key;
	/* Being made bigger or smaller by an edge. The paper goes where the
	 * pointer takes it at once, and the program catches up when it can:
	 * the paper's inner rectangle on the screen, the edges being moved,
	 * how hard they are being pulled (the turn of the paper at the edge),
	 * and what is drawn over the sheet while it lasts. */
	bool sizing;
	int sz_x0, sz_y0, sz_x1, sz_y1;
	unsigned sz_edges;
	double sz_lip, sz_lip_to, sz_at, sz_released, sz_tick_at;
	struct wlr_scene_buffer *sz_over;
	uint64_t sz_key;
	unsigned sz_seed;
	/* held where it was while the program redraws, instead of against an
	 * edge: after the sheet has been opened out or folded back */
	bool sz_hold;
	int sz_hx, sz_hy;
	/* opened out, from sz_was (outer, on the screen): the creases are the
	 * ones it was opened out along, not a pulled-out sheet's */
	bool sz_opened;
	struct wlr_box sz_was;
	bool sz_dirty;   /* moved since the last frame: redrawn once, at the frame */
	/* Opening out to the whole desk, or folding back: the panels folded
	 * under the edges swinging out -- sides, then top and bottom -- or back
	 * under the other way round. */
	bool ua_on;
	int ua_dir;                      /* +1 opening out, -1 folding back */
	int ua_phase;                    /* 0 the first pair of edges, 1 the second */
	double ua_start;                 /* when this phase began */
	struct wlr_box ua_from, ua_to;   /* outer rectangles on the screen */
	/* The sheet as it is opened out -- or as it was, folding back -- which
	 * everything on the screen during the move is drawn from: the part
	 * lying flat, the edges turning up, and the panels. */
	cairo_surface_t *ua_img;
	struct wlr_box ua_img_box;
	struct wlr_scene_buffer *ua_base;       /* the part lying flat */
	struct wlr_scene_buffer *ua_edge[4];    /* each edge, in the air */
	struct desk_unfold_edge ua_e[4];
	bool ua_e_on[4];
	/* X only. An override-redirect window -- a menu, a tooltip, a combo
	 * box's list -- is not ours to manage: it goes exactly where it says,
	 * with no frame, over every window, and never into the list. */
	bool unmanaged;
	/* Wayland: an input method's own window (ime_window_is) -- the same,
	 * but put where the text being written is, which it cannot say */
	bool ime_popup;
	bool associated;
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener request_configure;
	struct wl_listener request_activate;
	struct wl_listener set_decorations;
	struct wl_listener set_geometry;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener request_minimize;
	struct wl_listener set_title;
	struct desk_sheet sheet;
	struct desk_shadow shadow;
};

/* A surface's word, by KDE's server-decoration protocol, on whose frame
 * it has: ours, its own, or none (see kde_server_frame). */
struct desk_kde_deco {
	struct wl_list link;
	struct desk_server *server;
	struct wlr_server_decoration *deco;
	struct wl_listener mode;
	struct wl_listener destroy;
};

struct desk_popup {
	struct wlr_xdg_popup *xdg_popup;
	struct wl_listener commit;
	struct wl_listener destroy;
};

struct desk_keyboard {
	struct wl_list link;
	struct desk_server *server;
	struct wlr_keyboard *wlr_keyboard;
	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

/* A mouse or a touchpad, kept so that a change in Shell Settings reaches
 * the ones already plugged in (input_apply). */
struct desk_pointer {
	struct wl_list link;
	struct desk_server *server;
	struct wlr_input_device *device;
	struct wl_listener destroy;
};

/* A note in the air between the binder and the calendar (agd_fly_*):
 * from pose a to pose b, each a place on the binder or on the calendar
 * close up (AGD_ON_*), so that it leaves from and lands on them where they
 * are as they move. */
struct agd_flight {
	int mode;                       /* AGD_FLY_* */
	struct desk_stick s;
	struct desk_note_pose a, b;
	int a_on, b_on;
	double start, dur;
	int snd;                        /* its landing, asked for (snd_end) */
	struct desk_sprite sp;
};
#define AGD_FLIGHTS 4
enum { AGD_FLY_NONE, AGD_FLY_TO_CAL, AGD_FLY_TO_BINDER, AGD_FLY_OFF };
enum { AGD_ON_BINDER, AGD_ON_CAL };

/* Keep destruction handlers for client-owned pointer constraints. */
struct desk_game_constraint {
	struct desk_server *server;
	struct wlr_pointer_constraint_v1 *constraint;
	struct wl_listener destroy;
};

struct desk_server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	/* under everything, and moved by neither the drawer nor the camera:
	 * the bare desk on each screen beside the desk's own (see "more than
	 * one screen") */
	struct wlr_scene_tree *layer_side;
	struct wlr_scene_tree *layer_bg;
	struct wlr_scene_tree *layer_desk;
	struct wlr_scene_tree *layer_win;
	/* a window with the whole screen, and over that the menus X programs
	 * pop up -- which must clear every window, a fullscreen one included */
	struct wlr_scene_tree *layer_full;
	struct wlr_scene_tree *layer_xover;
	/* the drawer, pulled out: in front of the windows, which it comes out
	 * from under -- a sheet hanging over the edge of the desk does not
	 * cover it */
	struct wlr_scene_tree *layer_drawer;
	struct shell_conf conf;
	/* what is sounding: the drawer's slide, the sheet of the binder and the
	 * letter that last went over, the knob's last detent, and the newest
	 * letter heard arriving */
	int snd_drawer;
	int snd_page, snd_letter, snd_knob;
	/* the sounds for where a motion ends, asked for ahead of it (snd_end):
	 * the drawer at its stop, the binder down, the key turned, the glass
	 * down, the console landed */
	int snd_drawer_stop, snd_binder_down, snd_key_end, snd_guard_end, snd_cc_land;
	double snd_page_at;
	unsigned post_seen;
	bool post_seen_set;
	/* sheets being folded away or opened out, over the windows; over the
	 * drawer too, for one on its way into it or out of it (lying on the
	 * desk they are clear of it: the desk slides up out of its way) -- and
	 * under the bookcase, the console and the post box, which you are not
	 * looking at the desk while you look at */
	struct wlr_scene_tree *layer_parked;
	/* and once folded, lying on the desk: under every window, as paper
	 * put down is, the last put down on top */
	struct wlr_scene_tree *layer_folded;
	/* how far what lies on the desk -- the windows, the folded sheets --
	 * has gone up with it, out of the drawer's way */
	int win_dy;
	/* sheets on their way to the bin: screwed up where they lay, and
	 * thrown off the side of the desk */
	struct wl_list throws;
	bool quitting;
	struct wlr_scene_tree *layer_shelf;
	struct wlr_scene_tree *layer_top;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_list toplevels;

	/* X11 programs: an X server of their own, started the first time one
	 * connects, whose windows come to us as surfaces like any other */
	struct wlr_xwayland *xwayland;
	struct wl_listener xwayland_ready;
	struct wl_listener new_xwayland_surface;
	struct wl_listener xwayland_start;
	struct wl_listener xwayland_client_gone;
	struct wl_event_source *xwayland_retry;
	int xwayland_rebuilds;
	double xwayland_rebuilt_at;
	uint32_t xatom[16];   /* the window types we look at, once X is up */

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener pointer_focus_change;
	/* Wayland games use relative motion and pointer constraints to look around. */
	struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;
	struct wlr_pointer_constraints_v1 *pointer_constraints;
	struct wlr_pointer_constraint_v1 *active_constraint;
	/* where a pointer that says where it is (a tablet, a virtual machine's)
	 * last was, to make its movement from while a game holds it still */
	double abs_x, abs_y;
	bool abs_seen;
	struct wl_listener new_pointer_constraint;
	/* Typing into programs' windows through the input method (text-input,
	 * ti_*): the windows' text inputs, the one the input method is typing
	 * into, and for each key whether it is waiting on the input method's
	 * answer, gone through to the window, or kept by the input method. */
	struct wlr_text_input_manager_v3 *ti_mgr;
	struct wl_listener new_text_input, kb_focus_change;
	struct wl_list text_inputs;
	struct desk_ti *ti_ime;
	/* where the cursor of the text being written in a program is, on the
	 * screen: what the input method was told (ti_cursor); 0 high, none */
	struct wlr_box ime_at;
	/* the input method's own windows, put there (ime_window_place) */
	struct desk_toplevel *ime_win[4];
	uint8_t ti_keys[768];
	/* and what goes to the window, in the order it happened: keys, and
	 * the modifiers between them -- held up behind a key the input method
	 * has not yet answered for, so Ctrl is still down when the key it was
	 * held for gets there */
	struct ti_ev {
		uint8_t mods_only, pressed, waiting, taken;
		uint32_t code;
		struct wlr_keyboard_modifiers mods;
	} ti_q[64];
	int ti_q_head, ti_q_n;
	struct wl_event_source *ti_timer;
	struct wl_listener request_set_selection;
	struct wl_listener request_set_primary_selection;
	/* dragging something from one program to another: the thing being
	 * dragged, drawn under the pointer over everything else */
	struct wl_listener request_start_drag;
	struct wl_listener start_drag;
	struct wl_listener drag_icon_gone;
	struct wlr_scene_tree *layer_drag;
	/* how far the desk and everything on it is moved with the camera, at
	 * the desk end of a move */
	int desk_dx, desk_dy;
	/* The clock on the back of the desk: which page is up, how lit its
	 * keys are as a hand comes near, the key held down, the MEDIA page's
	 * button under the hand and held, the timer for what the face does of
	 * itself (a title running along it), and a record of what the machine
	 * has drawn, a second at a time for the last hour, for the POWER
	 * page's averages. */
	struct {
		int mode;
		double wake, wake_to;
		double glow[DESK_CLOCK_KEYS];
		int hot, pressed;
		int media_hot, media_down;
		bool near;
		double away_at;
		double anim_at;
		bool animating;
		bool vol_drag;
		double vol_scroll;
		struct wl_event_source *timer;
		double supply_at;
		struct desk_supply supply;
		float hist[3600];
		int hist_n, hist_at;
		double sum_all;
		long n_all;
	} clk;
	/* The tray's slips under the Control Panel -- how far each one has
	 * slid out to the pointer, kept by tray item so that it survives the
	 * list changing, and the one under it -- and the card of the one
	 * pulled out: whose, how far out, the submenus it has been taken down
	 * through, the entry under the pointer, and how far it is scrolled. */
	struct {
		unsigned lift_id[DESK_SLIPS_MAX];
		double lift[DESK_SLIPS_MAX];
		int n_lift;
		unsigned hot;
		double anim_at;
		bool animating;
		unsigned card;
		double pull, pull_from, pull_to, pull_start;
		bool pulling;
		int32_t path[6];
		char path_label[6][128];
		int depth;
		int hot_entry, top;
		struct wlr_scene_buffer *node;
		uint64_t key;
		/* more slips than room: how far the row is slid along, where it
		 * is going, and the wheel's part-notches on the way */
		double scroll, scroll_to;
		int wheel;
		/* the card's menu: the wheel's part-notches, so a trackpad glides */
		double card_scroll;
	} slp;
	struct wlr_scene_tree *drag_icon;
	struct wl_list keyboards;
	struct wl_list pointers;            /* struct desk_pointer */
	char kb_applied[160];               /* the layouts the keyboards have now */
	enum desk_cursor_mode cursor_mode;
	struct desk_toplevel *grabbed;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;
	struct wl_listener new_decoration;
	/* GTK 3's word on whose frame a window has, by KDE's older protocol:
	 * one for each surface it has given it for */
	struct wlr_server_decoration_manager *kde_deco;
	struct wl_list kde_decos;
	struct wl_listener new_kde_deco;

	struct wlr_scene_buffer *wallpaper;
	struct wlr_scene_buffer *drawer_sheet;
	/* what lies over the desk: the sprites under the drawer's shade, the
	 * shade, and the sprites it does not reach */
	struct wlr_scene_tree *ovl_under, *ovl_over;
	struct wlr_scene_rect *shade, *shade_edge;
	struct desk_sprite ovl[DESK_OVL_COUNT];
	int ovl_ox, ovl_oy;   /* where the desk was when they were drawn */
	const void *ovl_room; /* and which room it was */
	bool ovl_placed;
	/* What is in the drawer, in the order it lies there: programs and the
	 * screwdriver (Shell Settings), which are kept (drawer_save), and sheets
	 * folded away into it. */
	struct drawer_entry drw[DRAWER_MAX];
	int drw_n;
	bool drw_default;          /* nobody has filled it yet: what it comes with */
	bool drw_migrate;          /* kept by a DEsk that had no standard programs */
	unsigned drw_gen;          /* the list of programs it was last matched to */
	bool drw_dirty;            /* what lies in it has to be drawn again */
	struct wlr_scene_buffer *drw_node;   /* what lies in it, over the sheet */
	double drw_box[4];         /* what of the sheet that covers, in its pixels */
	double drw_res;            /* the buffer's pixels to one of the sheet's */
	int drw_hover;             /* the one under the hand, stood up; -1 */
	bool drw_sliding;          /* moving along the row to where they go now */
	double drw_slid_at;
	/* A hand down on one of them, or on a sheet folded on the desk, not yet
	 * moved far enough to be carrying it; and then carrying it. */
	int drw_press;
	struct desk_sheet *drw_press_sheet;
	double drw_px, drw_py;
	/* the button it is held with: the left takes it and does what it is
	 * for, the right only ever moves a folded sheet about */
	uint32_t drw_button;
	bool drw_carrying;
	struct drawer_entry drw_carry;
	int drw_carry_from;        /* its place in the row, or -1: off the desk */
	int drw_gap;               /* the place held open for it, or -1 */
	bool drw_over;             /* over the drawer, not the desk */
	bool drw_sprung;           /* the drawer has come out for it */
	double drw_gx, drw_gy;     /* where on it the hand has hold of it */
	struct wlr_scene_buffer *drw_carry_node;
	double drw_carry_ax, drw_carry_ay;
	double drw_poof_at;        /* a program taken out of it, fading */
	int desk_w, desk_h;
	/* Looking up at the bookcase. shelf_t runs 0 at the desk to 1 at the
	 * shelf; while it is strictly between, the frame is one of the traced
	 * keyframes of the move. */
	double shelf_t;
	double shelf_from, shelf_to, shelf_start, shelf_dur;
	int shelf_moving;
	int shelf_hover;
	/* the binder that has been taken down, and where in it we are */
	int shelf_binder;
	/* where the block has been turned to, and where it is heading: a
	 * fractional place so a notch of the wheel moves the paper rather than
	 * jumping it */
	double binder_scroll;     /* unspent scroll, so a trackpad glides */
	double binder_axis_at;    /* when the last scroll event arrived */
	int binder_burst;         /* sheets already spent on this one gesture */
	/* where the block of sheets is turned to, on a spring: the notches
	 * move where it is going, and it follows with the way it was going */
	double binder_pos, binder_pos_to, binder_pos_vel;
	double binder_pos_at;
	int binder_pos_moving;
	int binder_sel;
	/* The book of every program, when it is the one taken down
	 * (shelf_binder == DESK_SHELF_BOOK; binder_pos is then the spread it
	 * is open at): what is written on its line and where the caret is, the
	 * input method's part and its candidates, the keys handed to it, the
	 * line chosen with the keys and the thumb tab under the pointer. */
	char book_q[256];
	int book_caret;
	char book_pre[256];
	int book_pre_caret;
	char book_cand[16][96];
	const char *book_cand_p[16];
	int book_ncand, book_cand_cursor, book_cand_page;
	bool book_ime;
	struct {
		char utf8[8];
	} book_keyq[32];
	int book_keyq_n;
	int book_sel, book_tab;
	int book_cursor[4];
	/* A program taken out of the book or a binder: it is left open as you
	 * turn to the desk, and put back on the shelf once the shelf is out of
	 * sight. */
	bool binder_putback;
	/* the binder under the pointer, on its way out of the row */
	int hover_from;
	double hover_t, hover_start;
	int hover_moving;
	/* The desk calendar, taken off the shelf to be looked at close up:
	 * cal_t 0 on the shelf to 1 close up. The page showing; the one going
	 * over the wire and how many more were asked for while it went; the
	 * edge of the page the pointer is at (cal_zone: 1 the top half, 2 the
	 * bottom) and the edge lifted for it, by cal_lift_t. */
	double cal_t, cal_from, cal_to, cal_start, cal_dur;
	int cal_moving;
	/* the section being read and the day its page is about; a section
	 * being gone to by its tab, the pages between going over together;
	 * the tab the pointer is on */
	int cal_view, cal_to_view;
	double cal_switch_start, cal_switch_dur;
	int cal_hover_tab;
	int cal_year, cal_month, cal_day;
	int cal_turn, cal_more;
	double cal_turn_start, cal_turn_dur;
	int cal_zone, cal_lift;
	double cal_lift_t, cal_lift_at;
	double cal_scroll;
	struct wlr_scene_tree *cal_tree;
	/* the bookcase as it was when the calendar was taken, sharp and soft:
	 * stepped in towards, the one going over to the other */
	struct wlr_scene_buffer *cal_sharp, *cal_soft;
	struct desk_sprite cal_sp;
	double cal_nat_w, cal_nat_h;  /* the picture close up, before it is scaled */
	/* The memo pad, taken down to be written on, as the calendar is taken
	 * (memo_t, its pictures of the bookcase, its sprite). The sheet it is
	 * open at and what is written on it, kept by memo.c a moment after the
	 * last change (memo_keep) and whenever it is turned or put back; the
	 * caret; the input method's part and its candidates, the keys handed
	 * to it not yet answered, the key held down. A sheet going over the
	 * wire: memo_turn +1 on, -1 back, to memo_turn_to, what is written on
	 * that one in memo_other. */
	double memo_t, memo_from, memo_to, memo_start, memo_dur;
	int memo_moving;
	struct wlr_scene_tree *memo_tree;
	struct wlr_scene_buffer *memo_sharp, *memo_soft;
	struct desk_sprite memo_sp;
	double memo_nat_w, memo_nat_h;
	unsigned memo_gen_open;
	int memo_sheet;
	char memo_text[DESK_MEMO_TEXT];
	int memo_caret;
	bool memo_dirty;
	struct wl_event_source *memo_keep;
	char memo_pre[256];
	int memo_pre_caret;
	char memo_cand[16][96];
	const char *memo_cand_p[16];
	int memo_ncand, memo_cand_cursor;
	bool memo_typing, memo_ime;
	struct {
		char utf8[8];
	} memo_keyq[32];
	int memo_keyq_n;
	struct wl_event_source *memo_repeat;
	uint32_t memo_repeat_sym;
	uint32_t memo_held_sym;     /* the key last pressed on the memo, while it is down */
	int memo_cursor[4];
	int memo_turn, memo_turn_to;
	double memo_turn_start;
	char memo_other[DESK_MEMO_TEXT];
	double memo_scroll;
	/* The lamp coming up at login (boot_*): while it is on, the clock
	 * counts the room being made -- boot_p, as far as it has counted, and
	 * what is being made -- and the dark over everything (boot_night) is
	 * as the lamp is for that, boot_light; the clock's lit segments shine
	 * over it (boot_glow). When it began, the last frame, when the desk
	 * was up, how far the lamp has been brought up regardless (a machine
	 * slow to make the rest), when the count reached the end. */
	bool boot_on, boot_lamp;
	double boot_t0, boot_at, boot_p, boot_light, boot_floor;
	double boot_desk_at, boot_done_at, boot_warm_at;
	bool first_frame_logged, boot_lit;
	unsigned views_logged;   /* the views whose coming up is in the log, a bit each */
	bool soft_render;    /* the screen put together on the processor (pixman) */
	/* the screen drawn on the CPU at all -- pixman, or GL that is a
	 * rasterizer on the processor (render_check) -- and what draws it */
	bool on_cpu;
	char render_name[128];
	/* the letter in the middle of the desk on a first start (first_letter) */
	struct desk_sprite first_letter;
	bool first_letter_up;
	double boot_real0;   /* when it began by the wall clock, for the log: in
	                      * 2D the desk's own runs a hundred times fast */
	int boot_stage, boot_shown;
	struct wlr_scene_buffer *boot_night;
	int boot_night_w, boot_night_h, boot_night_q;
	struct desk_sprite boot_glow;
	/* What the desk was last painted from while its room is still being
	 * traced (wait_*): the room drawn -- its serial, 0 for the bare boards
	 * -- the size and the drawer. The tick asks after the room every 0.4
	 * s, and the desk is painted again only when one of them has changed:
	 * a screen-sized picture that has not changed, drawn and handed over
	 * again, was a slow machine's 65 ms in every 400 taken from the trace. */
	unsigned wait_room;
	int wait_w, wait_h;
	double wait_drawer;
	/* The binder of notes beside it (agd_*): agd_t 0 out of sight to 1 in;
	 * the day it is open at; the pad picked; the note being written -- its
	 * id, or DESK_AGENDA_NEW_PLAN/TODO -- its colour, what is written on
	 * it and where the caret is; the input method's part of it and its
	 * candidates on show; what the pointer is on; a new note on its way
	 * from its pad, a note being peeled off; the keys handed to the input
	 * method not yet answered, and the key being held down. */
	double agd_t, agd_from, agd_to, agd_start, agd_dur;
	int agd_moving;
	int agd_y, agd_m, agd_d;
	int agd_colour;
	int agd_edit, agd_edit_colour;
	char agd_text[256];
	int agd_caret;
	char agd_pre[256];
	int agd_pre_caret;
	char agd_cand[16][96];
	const char *agd_cand_p[16];
	int agd_ncand, agd_cand_cursor, agd_cand_page;
	int agd_hover, agd_hover_arg;
	bool agd_peeling;
	double agd_peel_start;
	int agd_gone_id;
	double agd_gone_start;
	bool agd_ime;
	struct {
		char utf8[8];
	} agd_keyq[32];
	int agd_keyq_n;
	struct wl_event_source *agd_repeat;
	uint32_t agd_repeat_sym;
	uint32_t agd_held_sym;      /* likewise for a note */
	struct desk_sprite agd_sp;
	struct desk_sprite agd_mark_sp;   /* the pencil mark on its ruler, on its own */
	/* the time of the plan being written (agd_when, -1 for none, and how
	 * long, agd_span), the ruler it is set on out by agd_ruler_t, the
	 * pointer on it (agd_mark) or held down on it from agd_drag_a; the
	 * note being written still on its way from the calendar; the calendar
	 * to turn to the binder's day once nothing is in the air on its way
	 * there; and the notes in the air */
	int agd_when, agd_span;
	double agd_ruler_t, agd_ruler_at;
	int agd_mark;
	bool agd_drag;
	int agd_drag_a, agd_drag_keep;
	bool agd_edit_away;
	bool agd_follow;
	double agd_scroll;
	struct agd_flight agd_fly[AGD_FLIGHTS];
	double binder_t;
	struct book_kept book_kept[BOOK_AHEAD];
	double binder_from, binder_to, binder_start, binder_dur;
	int binder_moving;
	/* Sheets taken out of binders and put down on the desk for programs
	 * whose windows have not come yet, and the one in the hand: where it
	 * hung, when you turn back to the desk with it, when it goes down. */
	struct desk_base bases[DESK_BASES];
	struct {
		int base;                 /* the one in the hand, or -1 */
		int idx;                  /* its place in the binder it came out of */
		double start, turn_at, land_at;
		bool turned;
		double cx0, cy0, w0, h0, a0;
		struct desk_sprite sp;
	} take;
	/* the shelf view, a layer a sprite: the case, the rectangle a binder
	 * slides out in, the three things on the lower board, the binder open
	 * on the desk, and a sheet in the air -- stacked in that order */
	struct wlr_scene_tree *sh_live;
	struct wlr_scene_tree *sh_layer[SH_LAYERS];
	struct desk_sprite sh_sprite[SH_LAYERS];
	int sh_w, sh_h;
	/* the traced tilt, hung on two nodes: a frame of the move is two source
	 * boxes and an opacity, with no cairo and no allocation. One film per
	 * place on the desk you can turn to look at. */
	struct wlr_scene_buffer *film_a[DESK_VIEW_COUNT], *film_b[DESK_VIEW_COUNT];
	int film_frames[DESK_VIEW_COUNT];
	int film_fw[DESK_VIEW_COUNT], film_fh[DESK_VIEW_COUNT];

	/* Turning to the control console at the back right. cc_t is the same
	 * kind of thing as shelf_t: 0 at the desk, 1 at the set. */
	double cc_t;
	double cc_from, cc_to, cc_start, cc_dur;
	int cc_moving;
	/* the filaments coming up, once you have got there */
	double cc_warm, cc_warm_start;
	struct desk_meters cc_meters;   /* as last read, at the tube's clock below */
	double cc_meters_clock;
	int cc_warming;
	int cc_hover;
	/* what the set has been left set to */
	int cc_wifi, cc_bt;
	double cc_vol, cc_bright;
	/* the screen's own light (a laptop's backlight): where its fader is,
	 * whether there is one, and the turn of it not yet sent, which goes a
	 * moment after the hand last moved it (cc_screen_send) */
	double cc_screen;
	bool cc_screen_ok;
	struct wl_event_source *cc_screen_send;
	/* The guarded set. The key turns itself once it is in, the glass swings
	 * up, and the button that was pushed starts the machine going dark --
	 * which is the only thing here that cannot be taken back. */
	double cc_key, cc_key_from, cc_key_to, cc_key_start, cc_key_dur;
	int cc_key_moving;
	double cc_guard, cc_guard_from, cc_guard_to, cc_guard_start, cc_guard_dur;
	int cc_guard_moving;
	int cc_pressed;          /* DESK_CC_PW_*, or -1 */
	/* the wireless aerial, going up and down on its motor with the lever */
	double cc_ant, cc_ant_from, cc_ant_to, cc_ant_start, cc_ant_dur;
	int cc_ant_moving, cc_ant_snd;
	double cc_dying, cc_die_start;
	/* a hand on the knob or the fader */
	int cc_grab;
	struct wl_event_source *cc_timer;
	struct wlr_scene_buffer *cc_base;   /* the traced sheet, put up once */
	int cc_base_w, cc_base_h;
	/* the instruments: a sprite a piece, each drawn only when it changes */
	struct wlr_scene_tree *cc_live;
	struct desk_sprite cc_sprite[DESK_CCP_COUNT];
	int cc_live_w, cc_live_h;
	struct wlr_scene_tree *layer_console;
	struct wlr_scene_buffer *cc_dim;

	/* Turning to the post box. The same kind of look as the console: 0 at
	 * the desk, 1 in front of the box with its door down. */
	double pb_t;
	double pb_from, pb_to, pb_start, pb_dur;
	int pb_moving;
	int pb_hover;
	/* where the letters in it have been turned to, on a spring, the way a
	 * binder's sheets are: a wheel notch is a letter */
	double pb_pos, pb_pos_to, pb_pos_vel, pb_pos_at;
	int pb_pos_moving;
	double pb_scroll;
	/* The turned-down corner of the letter the hand is at, as a sheet's
	 * corner is: which letter (and its id, should the box change under
	 * it), how far it is turned and how fast, whether it is turned up to
	 * be read, when the hand came to rest on its fold, the instruction
	 * under the hand, and the one being carried out. */
	int pbc_idx;
	unsigned pbc_id;
	double pbc_s, pbc_v, pbc_at, pbc_lift;
	bool pbc_near;
	double pbc_hover_at;
	int pbc_hot;
	int pbc_act;
	double pbc_act_at;
	/* The same corner on the binder sheet the hand is at: which sheet, how
	 * far it is turned and how fast, turned up or not, when the hand came
	 * to rest on its fold, the instruction under the hand and the one
	 * being carried out. */
	int bnc_idx;
	double bnc_s, bnc_v, bnc_at, bnc_lift;
	bool bnc_near;
	double bnc_hover_at;
	int bnc_hot;
	int bnc_act;
	double bnc_act_at;
	struct wlr_scene_tree *layer_post;
	struct wlr_scene_buffer *pb_base;
	struct desk_sprite pb_sprite;   /* the letters, and one in the air */
	int pb_sprite_w, pb_sprite_h;
	/* how far the views have been warmed for this size, ahead of a look */
	int warm_step, warm_w, warm_h;
	int pb_base_w, pb_base_h;
	/* a letter on its way out of the box and down onto the desk */
	cairo_surface_t *pb_fly;
	double pb_fly_x, pb_fly_y, pb_fly_w, pb_fly_h;
	double pb_fly_start, pb_fly_dur;
	int pb_fly_on;
	unsigned pb_fly_id;
	/* and where the ones already taken out are lying */
	struct desk_paper {
		struct wlr_scene_buffer *node;
		cairo_surface_t *sheet;
		unsigned id;
		int x, y, w, h;
		bool placed;         /* put somewhere by hand: kept there */
		double fx, fy;       /* where, as a part of the desk's width and height */
	} pb_paper[6];
	int pb_papers;
	/* A letter in the hand: which (+1; 0 none), where on it it was taken
	 * hold of, where the hand went down, and whether it has moved since --
	 * a letter clicked and not moved is put away, as before. */
	int pp_held;
	double pp_hx, pp_hy, pp_px, pp_py;
	bool pp_moved;
	/* Clicked from up at the bookcase: you have to come back down to the
	 * desk before you can turn to something on it, so the second move
	 * waits for the first to land. */
	int pending_look;        /* 0 none, 1 the console, 2 the post box */
	double drawer_t;
	double drawer_from;
	double drawer_to;
	double drawer_start;
	double drawer_dur;
	int drawer_moving;
	int cascade;
	bool headless;
	bool size_set;
	int headless_w, headless_h;
	char *startup_cmd;
	struct wl_listener request_activate;
	struct wlr_session *session;   /* the seat's, for changing VT; NULL nested */
	/* The screen lock (lock_*): the phase it is in, what is typed (wiped
	 * once it is handed to be checked) and how many letters that is, when
	 * it came down (walking up to it, fading in), when it went to be
	 * checked (and how far leant in to the key it was then), when a wrong
	 * one is turned away (refused: it has been, and the key has not yet
	 * been seen not to turn) and when the key turns to let you in; Caps
	 * Lock, the caret; going out (the unlocking backwards: lock_from, the
	 * view it starts from, whether the turn to the bookcase comes first,
	 * when the walk out began); who
	 * lives here and their picture; the room to go into and whether it was
	 * the desk you were at; the door's layer over everything, the black
	 * over every output under it, and the caret's timer. */
	int lock_phase;
	char lock_pass[512];
	int lock_len, lock_typed;
	double lock_at, lock_check_at, lock_lean0, lock_fail_at, lock_open_at, lock_leave_at;
	int lock_from;          /* LOCK_FROM_*: where you were, to come back to */
	double cc_lock_push;    /* the Control Panel's LOCK button, pushed in */
	bool lock_film;         /* turning to the bookcase before going out */
	bool lock_leaving;      /* going out of the room, backwards, and locking up */
	bool lock_fade_in, lock_walk, lock_refused, lock_caps, lock_caret_on, lock_from_desk;
	bool lock_picture_tried, lock_hand;
	char lock_name[128];
	cairo_surface_t *lock_picture, *lock_inside;
	struct wlr_scene_tree *layer_lock;
	struct wlr_scene_rect *lock_cover;
	struct wlr_scene_buffer *lock_node;
	struct wl_event_source *lock_timer;
	/* what moves out at the door, over it, drawn every frame while there
	 * is any (lock_fx), and when last */
	struct wlr_scene_buffer *lock_fx;
	double lock_fx_at;
	/* A day being kept (show_*): which (DESK_SHOW_*), since when, the
	 * next of its notes to ask for, the sounds asked for so far (to stop
	 * them), Esc having stopped it (its letting go is ours too); the day
	 * it was at the last look, for the stroke of the New Year; and over
	 * everything, the door included, the light of fireworks. */
	int show;
	double show_t0;
	int show_next;
#define SHOW_HANDLES 128
	int show_handles[SHOW_HANDLES];
	int show_nh;
	bool show_esc;
	int occ_ymd;
	struct wlr_scene_tree *layer_glow;
	struct wlr_scene_buffer *fw_node, *confetti_node;
	char *dump_path;
	char *screenshot_path;
	int screenshot_delay_ms;
	int exit_after_shot;
	struct wl_event_source *tick;
	struct wl_event_source *shot_timer;
	bool icons_shown;
	/* A resize arrives as a stream of events while an edge is dragged, and
	 * every distinct size is a room the ray tracer would have to bake. So
	 * the last one wins: nothing is rebuilt until the size stops moving. */
	struct wl_event_source *resize_timer;
	/* nested on an X desktop: the display it is on, and the window of ours
	 * there still to be named for its dock (see nested_name) */
	char *parent_display;
	/* DEsk's own window there, when it runs in one; a connection to find
	 * where on that screen it is */
	uint32_t host_window;
	void *host_xcb;
	int agd_cursor[4];      /* where the input method was last told it is */
	struct wl_event_source *name_timer;
	struct wlr_output *name_output;
	char name_tag[64];
	int name_tries;
	int resize_w, resize_h;
	/* The screen going dark when nobody has touched anything for a while
	 * (input_seen): when anybody last did, by the clock on the wall; the
	 * timer that looks; whether any screen has gone dark; when it was woken
	 * again, and the key or button that woke it, which is the waking's,
	 * down and up, and nobody else's. Programs that need the screen lit
	 * hold it up (idle_holds: idle-inhibit), programs that want to know how
	 * long it has been are told (idle_notifier: ext-idle-notify, for
	 * swayidle), and wlopm turns screens off and on through the same
	 * machinery (wlr-output-power-management). Over all of it, nested, the
	 * black. */
	double idle_at, wake_at;
	struct wl_event_source *idle_timer;
	bool dark, idle_held;
	uint32_t wake_key, wake_button;
	struct wlr_idle_notifier_v1 *idle_notifier;
	struct wl_list idle_holds;
	struct wl_listener new_idle_hold;
	struct wl_listener power_set_mode;
	struct wl_listener touch_down, touch_motion;
	struct wlr_scene_tree *layer_dark;
	/* The display tools -- wlr-randr, kanshi, wdisplays (wlr-output-
	 * management): what they are told the screens are, and what they ask.
	 * The size of a point one of them asks for goes into shell.conf. */
	struct wlr_output_manager_v1 *om;
	struct wl_listener om_apply, om_test;
	/* the screens set out again as displays.conf has them, once the ones
	 * that are coming have all come (see "the screens as they were left") */
	struct wl_event_source *disp_idle;
	/* the screens set out some other way: the bare desk on the ones beside
	 * the desk's, and the windows on a screen that has gone put on one
	 * that is there -- once, after all of a change (screens_idle_cb) */
	struct wl_listener layout_change;
	struct wl_event_source *screens_idle;
	/* when the frame's work (output_frame) was last done: once for all
	 * the screens, not once for each */
	double ticked_at;
	/* how far the lamp is up for the bare desk beside the desk's screen:
	 * as it is for the desk at login and in a show (night_paint) */
	double side_light;
	/* Ending the session (end_begin): every window asked to close first,
	 * and the logging out, restart or power off done once they all have.
	 * ending is 0, or 1 + enum desk_end. */
	int ending;
	bool end_anyway;            /* asked again just after a window held it up */
	bool end_done;              /* the ending itself is under way */
	double end_held_at;         /* when a window last held it up */
	struct wl_event_source *end_timer;
	/* Ctrl+Alt+Del, counted: how many in a row, the last when (real time) */
	int cad_n;
	double cad_at;
	struct wl_event_source *cad_timer;
};

/* Started and let go of. Forked twice, so that what runs is not a child of
 * ours: it is nobody's to wait for, and leaves no zombie when it ends.
 * (Not by ignoring SIGCHLD, which does the same for every child there is
 * -- including the ones that are waited for, like the fork wlroots starts
 * Xwayland through, which then fails to start.) */
static void spawn_argv(char *const argv[])
{
	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		sigset_t set;
		sigemptyset(&set);
		sigprocmask(SIG_SETMASK, &set, NULL);
		if (fork() == 0) {
			desk_close_from(3);
			execvp(argv[0], argv);
			_exit(127);
		}
		_exit(0);
	}
	if (pid > 0) {
		while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
			;
		}
	}
}

/* The same, for a command line as a shell has it: -s's, and the words of
 * one of the desk's own (none of it anybody else's to choose). A path is
 * never put in one -- that goes as a word of its own (spawn_argv). */
static void spawn(const char *cmd)
{
	char *const argv[] = { (char *)"/bin/sh", (char *)"-c", (char *)cmd, NULL };
	spawn_argv(argv);
}

static void own_letter_action(struct desk_server *server, unsigned id, const char *key);

static bool on_path(const char *name)
{
	const char *path = getenv("PATH");
	char buf[1024];
	for (const char *p = path && *path ? path : "/usr/local/bin:/usr/bin:/bin"; *p;) {
		const char *e = strchr(p, ':');
		size_t n = e ? (size_t)(e - p) : strlen(p);
		size_t m = strlen(name);
		if (n > 0 && n + 1 + m < sizeof buf) {
			memcpy(buf, p, n);
			buf[n] = '/';
			memcpy(buf + n + 1, name, m + 1);
			if (access(buf, X_OK) == 0) {
				return true;
			}
		}
		if (!e) {
			break;
		}
		p = e + 1;
	}
	return false;
}

/* The first terminal there is: the Wayland ones first, then the desktops'
 * own -- which a machine with a desktop already has -- and xterm, through
 * Xwayland, when there is nothing else. */
static const char *terminal_cmd(void)
{
	static const char *const terms[] = {
		"foot", "alacritty", "kitty", "wezterm", "weston-terminal",
		"konsole", "gnome-terminal", "kgx", "xfce4-terminal", "mate-terminal",
		"lxterminal", "tilix", "terminator",
	};
	for (size_t i = 0; i < sizeof terms / sizeof terms[0]; i++) {
		if (on_path(terms[i])) {
			return terms[i];
		}
	}
	return "xterm";
}

static void cairo_buffer_destroy(struct wlr_buffer *wlr_buffer)
{
	struct desk_buf *buffer = wl_container_of(wlr_buffer, buffer, base);
	if (buffer->cr) {
		cairo_destroy(buffer->cr);
	}
	cairo_surface_destroy(buffer->surface);
	free(buffer);
}

static bool cairo_buffer_begin(struct wlr_buffer *wlr_buffer, uint32_t flags,
	void **data, uint32_t *format, size_t *stride)
{
	struct desk_buf *buffer = wl_container_of(wlr_buffer, buffer, base);
	(void)flags;
	cairo_surface_flush(buffer->surface);
	*format = DRM_FORMAT_ARGB8888;
	*data = cairo_image_surface_get_data(buffer->surface);
	*stride = cairo_image_surface_get_stride(buffer->surface);
	return true;
}

static void cairo_buffer_end(struct wlr_buffer *wlr_buffer)
{
	(void)wlr_buffer;
}

static const struct wlr_buffer_impl cairo_buffer_impl = {
	.destroy = cairo_buffer_destroy,
	.begin_data_ptr_access = cairo_buffer_begin,
	.end_data_ptr_access = cairo_buffer_end,
};

static struct desk_buf *buf_create(int w, int h)
{
	if (w < 1) {
		w = 1;
	}
	if (h < 1) {
		h = 1;
	}
	struct desk_buf *buffer = calloc(1, sizeof(*buffer));
	wlr_buffer_init(&buffer->base, &cairo_buffer_impl, w, h);
	buffer->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	buffer->cr = cairo_create(buffer->surface);
	cairo_set_operator(buffer->cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(buffer->cr);
	cairo_set_operator(buffer->cr, CAIRO_OPERATOR_OVER);
	return buffer;
}

static void prof_upload(int w, int h);

/* len, shortened if it must be so that start + len, as the machine adds
 * it, does not pass end */
static double span_inside(double start, double len, double end)
{
	len = fmin(len, end - start);
	while (len > 0.0 && start + len > end) {
		len = nextafter(len, 0.0);
	}
	return len;
}

/* What a node's picture measures, kept beside the node. Once a picture is
 * on the GPU the scene lets go of it -- b->buffer is NULL from then on, on
 * a GLES2 screen, though never on a pixman one, which reads the picture
 * every time -- so its size is written down as it is handed over
 * (scene_new, scene_swap) and read back by whatever lays a box over it.
 * Read off b->buffer instead, a box went on being laid for the size of the
 * screen over a picture a third of it, and wlroots stopped the desk. */
struct pic_size {
	struct wlr_addon addon;
	int w, h;
};

static void pic_size_destroy(struct wlr_addon *addon)
{
	struct pic_size *p = wl_container_of(addon, p, addon);
	wlr_addon_finish(addon);
	free(p);
}

static const struct wlr_addon_interface PIC_SIZE = {
	.name = "desk-picture-size",
	.destroy = pic_size_destroy,
};

static void pic_size_keep(struct wlr_scene_buffer *b, const struct wlr_buffer *buffer)
{
	if (!buffer) {
		return;
	}
	struct wlr_addon *a = wlr_addon_find(&b->node.addons, b, &PIC_SIZE);
	struct pic_size *p;
	if (a) {
		p = wl_container_of(a, p, addon);
	} else {
		p = calloc(1, sizeof *p);
		if (!p) {
			return;
		}
		wlr_addon_init(&p->addon, &b->node.addons, b, &PIC_SIZE);
	}
	p->w = buffer->width;
	p->h = buffer->height;
}

/* The size of b's picture, whether or not the scene still holds it; false
 * if it has none. */
static bool scene_pic_size(struct wlr_scene_buffer *b, int *w, int *h)
{
	if (b->buffer) {
		*w = b->buffer->width;
		*h = b->buffer->height;
		return true;
	}
	struct wlr_addon *a = wlr_addon_find(&b->node.addons, b, &PIC_SIZE);
	if (!a) {
		return false;
	}
	struct pic_size *p = wl_container_of(a, p, addon);
	*w = p->w;
	*h = p->h;
	return true;
}

/* Show src of b's picture (NULL: all of it). Every source box goes through
 * here and is kept inside the picture to the last bit: wlroots will not
 * draw one that reaches past its picture's edge by any amount, and a box
 * worked out in floating point -- a scaled picture cut at a screen's edge
 * -- can come to 160.00000000000003 of a picture 160 wide. */
static void scene_source(struct wlr_scene_buffer *b, const struct wlr_fbox *src)
{
	int pw, ph;
	if (!src || wlr_fbox_empty(src) || !scene_pic_size(b, &pw, &ph)) {
		wlr_scene_buffer_set_source_box(b, src);
		return;
	}
	struct wlr_fbox box = *src;
	if (!isfinite(box.x) || !isfinite(box.y) || !isfinite(box.width)
		|| !isfinite(box.height)) {
		wlr_scene_buffer_set_source_box(b, NULL);
		return;
	}
	if (box.x < 0.0) {
		box.width += box.x;
		box.x = 0.0;
	}
	if (box.y < 0.0) {
		box.height += box.y;
		box.y = 0.0;
	}
	box.width = span_inside(box.x, box.width, pw);
	box.height = span_inside(box.y, box.height, ph);
	wlr_scene_buffer_set_source_box(b, &box);
}

/* A new picture for b; the box the last one was shown through is kept
 * inside it until whoever lays b gives it its own. */
static void scene_swap(struct wlr_scene_buffer *b, struct wlr_buffer *buffer)
{
	wlr_scene_buffer_set_buffer(b, buffer);
	pic_size_keep(b, buffer);
	if (!wlr_fbox_empty(&b->src_box)) {
		struct wlr_fbox keep = b->src_box;
		scene_source(b, &keep);
	}
}

/* A node for a picture of ours, its size kept (pic_size_keep). */
static struct wlr_scene_buffer *scene_new(struct wlr_scene_tree *parent,
	struct wlr_buffer *buffer)
{
	struct wlr_scene_buffer *b = wlr_scene_buffer_create(parent, buffer);
	if (b) {
		pic_size_keep(b, buffer);
	}
	return b;
}

static void scene_put(struct wlr_scene_buffer **slot, struct wlr_scene_tree *parent,
	struct desk_buf *buffer, int x, int y, void *data)
{
	cairo_surface_flush(buffer->surface);
	prof_upload(buffer->base.width, buffer->base.height);
	if (*slot == NULL) {
		*slot = scene_new(parent, &buffer->base);
	} else {
		scene_swap(*slot, &buffer->base);
	}
	wlr_scene_node_set_position(&(*slot)->node, x, y);
	(*slot)->node.data = data;
	wlr_buffer_drop(&buffer->base);
}

static double monotonic_sec(void);
static double clampf_d(double v, double lo, double hi);

/* ---- what a frame costs: DESK_PROFILE=1 ------------------------------
 *
 * A frame-time readout, the kind a game keeps in the corner of the screen,
 * written to the log instead. Every two seconds that anything happened in,
 * one line: how many frames went out and how long the longest gap between
 * two of them was while something was moving, what a frame cost on this
 * thread -- the ticks and painters, then wlroots rendering the scene --
 * and, painter by painter, how often it ran, for how long, and how many
 * bytes of pixels it handed the scene graph to upload. Painters nest, and
 * their times are inclusive. Off, it costs a branch. */
enum prof_what {
	PROF_DESK, PROF_DOCK, PROF_SHELF, PROF_CONSOLE, PROF_POST, PROF_CHROME,
	PROF_SHEET, PROF_SHADOW, PROF_FILM, PROF_THROW, PROF_OTHER, PROF_N,
};
static const char *const PROF_NAME[PROF_N] = {
	"desk", "dock", "shelf", "console", "post", "chrome",
	"sheet", "shadow", "film", "throw", "other",
};
static struct {
	bool on;
	double since;
	int frames, late;
	double frame_ms, frame_max, commit_ms, commit_max, gap_max, last_frame;
	double asked;       /* when a frame was first asked for and not yet made */
	int stack[8];
	double t0[8];
	int depth;
	int calls[PROF_N];
	double ms[PROF_N], bytes[PROF_N];
	int bufs[PROF_N];
} g_prof;

/* DESK_PROFILE_FRAMES=FILE: the same, a frame at a time, for a script to
 * read -- one line a frame: when it began (CLOCK_MONOTONIC), how long since
 * the frame before and since it was asked for, what the ticks cost and
 * what wlroots rendering it cost (on the wall clock and in this thread's
 * own CPU), what this thread spent between the last frame and this one,
 * the pixels handed to the scene since, and the painters that ran since.
 * Kept apart from g_prof, which is cleared at every report. */
static struct {
	FILE *f;
	double last_f0, last_cpu;
	double ms[PROF_N];
	double bytes;
	int bufs;
} g_trace;

static double thread_cpu_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* what the profile measures is what the machine spends: real time, not the
 * desk's own (monotonic_sec, which runs fast in 2D and slow with DESK_SLOW) */
static double real_sec(void);

static void prof_begin(int what)
{
	if (!g_prof.on || g_prof.depth >= 8) {
		return;
	}
	g_prof.stack[g_prof.depth] = what;
	g_prof.t0[g_prof.depth] = real_sec();
	g_prof.depth++;
}

static void prof_end(void)
{
	if (!g_prof.on || g_prof.depth <= 0) {
		return;
	}
	g_prof.depth--;
	int w = g_prof.stack[g_prof.depth];
	double ms = (real_sec() - g_prof.t0[g_prof.depth]) * 1000.0;
	g_prof.calls[w]++;
	g_prof.ms[w] += ms;
	g_trace.ms[w] += ms;
}

/* pixels handed to the scene graph, which it uploads whole */
static void prof_upload(int w, int h)
{
	if (!g_prof.on) {
		return;
	}
	int what = g_prof.depth > 0 ? g_prof.stack[g_prof.depth - 1] : PROF_OTHER;
	g_prof.bytes[what] += (double)w * h * 4.0;
	g_prof.bufs[what]++;
	g_trace.bytes += (double)w * h * 4.0;
	g_trace.bufs++;
}

/* a frame's line (DESK_PROFILE_FRAMES): f0, f1, f2 the frame's start, the
 * end of its ticks and the end of its rendering, c0..c2 the thread's CPU
 * time at each, wait how long since it was asked for (or -1) */
static void trace_frame(double f0, double f1, double f2, double c0, double c1, double c2,
	double wait)
{
	if (!g_trace.f) {
		return;
	}
	fprintf(g_trace.f, "%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.0f,%d,",
		f0, g_trace.last_f0 > 0.0 ? (f0 - g_trace.last_f0) * 1000.0 : 0.0,
		wait >= 0.0 ? wait * 1000.0 : -1.0, (f1 - f0) * 1000.0, (c1 - c0) * 1000.0,
		(f2 - f1) * 1000.0, (c2 - c1) * 1000.0,
		g_trace.last_cpu > 0.0 ? (c0 - g_trace.last_cpu) * 1000.0 : 0.0,
		g_trace.bytes / 1024.0, g_trace.bufs);
	for (int i = 0; i < PROF_N; i++) {
		if (g_trace.ms[i] >= 0.05) {
			fprintf(g_trace.f, "%s:%.2f;", PROF_NAME[i], g_trace.ms[i]);
		}
		g_trace.ms[i] = 0.0;
	}
	fputc('\n', g_trace.f);
	g_trace.bytes = 0.0;
	g_trace.bufs = 0;
	g_trace.last_f0 = f0;
	g_trace.last_cpu = c2;
}

static void prof_report(double now)
{
	if (!g_prof.on) {
		return;
	}
	if (g_trace.f) {
		fflush(g_trace.f);
	}
	if (g_prof.since == 0.0) {
		g_prof.since = now;
		return;
	}
	double span = now - g_prof.since;
	if (span < 2.0) {
		return;
	}
	bool any = g_prof.frames > 0;
	for (int i = 0; i < PROF_N; i++) {
		any = any || g_prof.calls[i] > 0 || g_prof.bufs[i] > 0;
	}
	if (any) {
		char line[1024];
		int n = snprintf(line, sizeof line,
			"profile %.1fs: %d frames, slowest to arrive %.0f ms (%d late); "
			"frame %.2f ms avg %.1f max, of which render %.2f avg %.1f max |",
			span, g_prof.frames, g_prof.gap_max * 1000.0, g_prof.late,
			g_prof.frames ? g_prof.frame_ms / g_prof.frames : 0.0, g_prof.frame_max,
			g_prof.frames ? g_prof.commit_ms / g_prof.frames : 0.0, g_prof.commit_max);
		for (int i = 0; i < PROF_N && n > 0 && (size_t)n < sizeof line; i++) {
			if (g_prof.calls[i] == 0 && g_prof.bufs[i] == 0) {
				continue;
			}
			n += snprintf(line + n, sizeof line - (size_t)n, " %s %dx %.1f ms %.1f MB/%d",
				PROF_NAME[i], g_prof.calls[i], g_prof.ms[i],
				g_prof.bytes[i] / 1048576.0, g_prof.bufs[i]);
		}
		wlr_log(WLR_INFO, "%s", line);
	}
	int depth = g_prof.depth;
	int stack[8];
	double t0[8];
	memcpy(stack, g_prof.stack, sizeof stack);
	memcpy(t0, g_prof.t0, sizeof t0);
	double last = g_prof.last_frame, asked = g_prof.asked;
	memset(&g_prof, 0, sizeof g_prof);
	g_prof.on = true;
	g_prof.since = now;
	g_prof.last_frame = last;
	g_prof.asked = asked;
	g_prof.depth = depth;
	memcpy(g_prof.stack, stack, sizeof stack);
	memcpy(g_prof.t0, t0, sizeof t0);
}

static void server_schedule_frames(struct desk_server *server);
static void settings_open(void);
/* more than one screen (see there) */
static struct wlr_output *first_output(struct desk_server *server);
static struct wlr_output *screen_at(struct desk_server *server, double x, double y);
static bool screen_is_side(struct desk_server *server, const struct wlr_output *out);
static struct wlr_box screen_area(struct desk_server *server, struct wlr_output *out);
static bool pointer_on_side(struct desk_server *server);
static struct wlr_output *view_screen(struct desk_toplevel *view);
static void desk_screen_lay(struct desk_server *server, struct wlr_scene_buffer *b,
	struct wlr_fbox src, double x, double y, double dw, double dh, int lx, int ly);

static bool shadow_accepts(struct wlr_scene_buffer *buffer, double *sx, double *sy)
{
	(void)buffer;
	(void)sx;
	(void)sy;
	/* a shadow is not a surface: clicks go through it */
	return false;
}

static void shadow_sync_body(struct desk_shadow *sh, struct wlr_scene_tree *tree,
	int w, int h, int screen_x, int screen_y, bool held)
{
	if (!tree || w < 8 || h < 8) {
		return;
	}
	int pad = desk_shadow_sprite_pad();
	/* A sheet being resized has its shadow stretched rather than drawn
	 * again every frame: it is all blur, and nobody can tell -- until it
	 * is stretched a long way from the size it was drawn for, when the
	 * blur at its edge goes hard or thin; then it is drawn again. At
	 * Ultra it is drawn again as it goes. */
	bool far = sh->node && (desk_gfx_level() == DESK_GFX_ULTRA || w > sh->w * 1.25
		|| w * 1.25 < sh->w || h > sh->h * 1.25 || h * 1.25 < sh->h);
	if (!sh->node || ((sh->w != w || sh->h != h) && (!sh->stretch || far))) {
		struct desk_buf *buffer = buf_create(w + pad * 2, h + pad * 2);
		desk_draw_shadow_sprite(buffer->cr, w, h, 7.0, sh->side, sh->amount);
		scene_put(&sh->node, tree, buffer, 0, 0, NULL);
		sh->w = w;
		sh->h = h;
		sh->node->point_accepts_input = shadow_accepts;
		wlr_scene_node_lower_to_bottom(&sh->node->node);
	}

	double now = monotonic_sec();
	double dt = sh->at > 0.0 ? clampf_d(now - sh->at, 0.0, 0.05) : 0.0;
	sh->at = now;
	if (sh->settled) {
		/* what the sheet did since we last looked, taken out of the
		 * shadow: for that instant the paper is bent between them */
		sh->lx -= (screen_x - sh->px) * 0.62;
		sh->ly -= (screen_y - sh->py) * 0.62;
		sh->lx = clampf_d(sh->lx, -34.0, 34.0);
		sh->ly = clampf_d(sh->ly, -34.0, 34.0);
	}
	sh->px = screen_x;
	sh->py = screen_y;
	sh->settled = 1;

	/* and it flattens out again -- underdamped, so it goes a shade past
	 * and comes back, which is what a sheet of paper does */
	const double K = 210.0, C = 2.0 * 14.49 * 0.52;
	if (dt > 0.0) {
		sh->vx += (-K * sh->lx - C * sh->vx) * dt;
		sh->vy += (-K * sh->ly - C * sh->vy) * dt;
		sh->lx += sh->vx * dt;
		sh->ly += sh->vy * dt;
		double want = held ? 1.0 : 0.0;
		sh->held += (want - sh->held) * clampf_d(dt * 9.0, 0.0, 1.0);
	}

	double ox, oy;
	/* held, the sheet is off the desk, so its shadow goes further out,
	 * spreads, and goes soft */
	desk_shadow_offset(screen_x + w * 0.5, screen_y + h * 0.5,
		44.0 + 38.0 * sh->held, &ox, &oy);
	double grow = 1.0 + 0.045 * sh->held;
	int dw = (int)((w + pad * 2) * grow), dh = (int)((h + pad * 2) * grow);
	wlr_scene_buffer_set_dest_size(sh->node, dw, dh);
	wlr_scene_buffer_set_opacity(sh->node, (float)(1.0 - 0.22 * sh->held));
	wlr_scene_node_set_position(&sh->node->node,
		(int)(-pad + ox + sh->lx - (dw - (w + pad * 2)) * 0.5),
		(int)(-pad + oy + sh->ly - (dh - (h + pad * 2)) * 0.5));
}

static void shadow_sync(struct desk_shadow *sh, struct wlr_scene_tree *tree,
	int w, int h, int screen_x, int screen_y, bool held)
{
	prof_begin(PROF_SHADOW);
	shadow_sync_body(sh, tree, w, h, screen_x, screen_y, held);
	prof_end();
}

static bool shadow_restless(const struct desk_shadow *sh)
{
	return fabs(sh->lx) > 0.12 || fabs(sh->ly) > 0.12
		|| fabs(sh->vx) > 1.0 || fabs(sh->vy) > 1.0 || sh->held > 0.004;
}

/* ---- a window, whichever protocol it came by ------------------------ */

static struct wlr_surface *view_surface(const struct desk_toplevel *view)
{
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		return view->xsurface->surface;
	}
#endif
	return view->xdg_toplevel ? view->xdg_toplevel->base->surface : NULL;
}

static const char *view_title(const struct desk_toplevel *view)
{
	const char *t = NULL;
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		t = view->xsurface->title ? view->xsurface->title : view->xsurface->class;
	}
#endif
	if (view->xdg_toplevel) {
		t = view->xdg_toplevel->title;
	}
	return t ? t : TR("Window");
}

/* How far in from the edge of the sheet the client starts. */
static void view_insets(const struct desk_toplevel *view, int *l, int *t, int *r, int *b)
{
	if (view->bare || view->fullscreen) {
		*l = *t = *r = *b = 0;
		return;
	}
	if (view->own_frame) {
		/* paper round a program with a heading of its own */
		*l = *r = *b = DESK_MOUNT;
		*t = DESK_MOUNT_TOP;
		return;
	}
	*l = DESK_BORDER;
	*t = DESK_TITLE_H;
	*r = DESK_BORDER;
	*b = DESK_BORDER;
}

static bool view_framed(const struct desk_toplevel *view)
{
	return !view->bare && !view->fullscreen;
}

/* What of a window is trimmed to the paper: its own surfaces, and not the
 * menus it has open. The scene's node for an xdg surface has the surfaces'
 * tree first, and its menus after; an X window's node is its surfaces'. */
static struct wlr_scene_node *view_content_node(struct desk_toplevel *view)
{
	if (!view->surface_tree) {
		return NULL;
	}
	if (view->xdg_toplevel) {
		if (wl_list_empty(&view->surface_tree->children)) {
			return NULL;
		}
		struct wlr_scene_node *first;
		first = wl_container_of(view->surface_tree->children.next, first, link);
		return first;
	}
	return &view->surface_tree->node;
}

static void view_geometry(const struct desk_toplevel *view, struct wlr_box *geo);

/* Its trim at rest: none -- except a program laid on our paper that draws
 * a shadow round itself all the same (it was told it is tiled, and did
 * not listen), which is trimmed to the window proper, the paper casting
 * the shadow. */
static void view_clip_rest(struct desk_toplevel *view, const struct wlr_box *geo)
{
	struct wlr_scene_node *content = view_content_node(view);
	if (!content) {
		return;
	}
	struct wlr_surface *s = view->xdg_toplevel ? view->xdg_toplevel->base->surface : NULL;
	bool margin = s && geo->width > 0 && geo->height > 0 && (geo->x > 0 || geo->y > 0
		|| s->current.width > geo->width || s->current.height > geo->height);
	if (view->own_frame && !view->bare && !view->fullscreen && margin) {
		struct wlr_box clip = *geo;
		wlr_scene_subsurface_tree_set_clip(content, &clip);
	} else {
		wlr_scene_subsurface_tree_set_clip(content, NULL);
	}
}

/* The client's own idea of its window, in its surface's coordinates: an
 * xdg client may draw a margin of its own round it. */
static void view_geometry(const struct desk_toplevel *view, struct wlr_box *geo)
{
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		*geo = (struct wlr_box){ 0, 0, view->xsurface->width, view->xsurface->height };
		return;
	}
#endif
	wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, geo);
}

/* An X client keeps its own idea of where its window is on the screen, and
 * places its menus by it; tell it whenever the sheet moves. A Wayland client
 * is never told and never needs to be. */
static void view_send_position(struct desk_toplevel *view)
{
#ifdef DESK_HAVE_XWAYLAND
	struct wlr_xwayland_surface *xs = view->xsurface;
	if (!xs || !view->scene_tree || view->unmanaged) {
		return;
	}
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	wlr_xwayland_surface_configure(xs, (int16_t)(view->scene_tree->node.x + l),
		(int16_t)(view->scene_tree->node.y + t), xs->width, xs->height);
#else
	(void)view;
#endif
}

/* Ask for a new size, the sheet already being where it is to go. */
static void view_set_size(struct desk_toplevel *view, int w, int h)
{
	if (w < 1) {
		w = 1;
	}
	if (h < 1) {
		h = 1;
	}
#ifdef DESK_HAVE_XWAYLAND
	struct wlr_xwayland_surface *xs = view->xsurface;
	if (xs) {
		int l = 0, t = 0, r, b;
		view_insets(view, &l, &t, &r, &b);
		int x = view->scene_tree ? view->scene_tree->node.x + l : xs->x;
		int y = view->scene_tree ? view->scene_tree->node.y + t : xs->y;
		wlr_xwayland_surface_configure(xs, (int16_t)x, (int16_t)y,
			(uint16_t)(w > 32767 ? 32767 : w), (uint16_t)(h > 32767 ? 32767 : h));
		return;
	}
#endif
	wlr_xdg_toplevel_set_size(view->xdg_toplevel, w, h);
}

static void view_set_maximized(struct desk_toplevel *view, bool on)
{
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		wlr_xwayland_surface_set_maximized(view->xsurface, on);
		return;
	}
#endif
	wlr_xdg_toplevel_set_maximized(view->xdg_toplevel, on);
}

static void view_set_activated(struct desk_toplevel *view, bool on)
{
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		wlr_xwayland_surface_activate(view->xsurface, on);
		if (on) {
			wlr_xwayland_surface_restack(view->xsurface, NULL, XCB_STACK_MODE_ABOVE);
		}
		return;
	}
#endif
	wlr_xdg_toplevel_set_activated(view->xdg_toplevel, on);
}

/* Does it take the keyboard at all? An X window can say it never does. */
static bool view_takes_keys(const struct desk_toplevel *view)
{
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		return wlr_xwayland_icccm_input_model(view->xsurface) != WLR_ICCCM_INPUT_MODEL_NONE;
	}
#endif
	(void)view;
	return true;
}

static void view_close(struct desk_toplevel *view)
{
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		wlr_xwayland_surface_close(view->xsurface);
		return;
	}
#endif
	wlr_xdg_toplevel_send_close(view->xdg_toplevel);
}

static bool gfx_flat(void);

static void view_sync_shadow(struct desk_toplevel *view)
{
	if (view->fullscreen || (view->own_shadow && !view->own_frame) || gfx_flat()) {
		/* nothing under it to fall on -- or one of its own already (laid
		 * on our paper, the paper casts ours) */
		if (view->shadow.node) {
			wlr_scene_node_set_enabled(&view->shadow.node->node, false);
		}
		return;
	}
	if (view->shadow.node) {
		wlr_scene_node_set_enabled(&view->shadow.node->node, true);
	}
	if (view->shadow.amount <= 0.0) {
		desk_paper_lift(view_title(view), &view->shadow.side, &view->shadow.amount);
	}
	bool held = view->server->grabbed == view
		&& view->server->cursor_mode == DESK_CUR_MOVE;
	view->shadow.stretch = view->sizing || view->ua_on;
	shadow_sync(&view->shadow, view->scene_tree, view->outer_w, view->outer_h,
		view->scene_tree->node.x, view->scene_tree->node.y, held);
}

/* Every sheet on the desk, once a frame, so the ones still settling do. */
static void paper_tick(struct desk_server *server)
{
	struct desk_toplevel *view;
	bool more = false;
	wl_list_for_each(view, &server->toplevels, link) {
		if (!view->scene_tree || !view->scene_tree->node.enabled) {
			continue;
		}
		view_sync_shadow(view);
		more = more || shadow_restless(&view->shadow);
	}
	if (more) {
		server_schedule_frames(server);
	}
}

static void focus_toplevel(struct desk_toplevel *toplevel);
static void views_above_raise(struct desk_server *server);

/* DESK_SLOW=n: everything that moves takes n times as long, to be
 * watched (or photographed) a frame at a time. */
static double g_time_slow = 1.0;    /* DESK_SLOW */
static double g_time_motion = 1.0;  /* Shell Settings' motion */
static double g_time_real0, g_time_virt0;   /* where the rate last changed */
static bool g_time_rebased;

static double real_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* when this process began, for the log's account of how long the way up
 * took: the first frame, the desk, the lamp */
static double g_start_real;

static double monotonic_sec(void)
{
	double t = real_sec();
	double k = g_time_slow * g_time_motion;
	if (!g_time_rebased) {
		return k == 1.0 ? t : t / k;
	}
	return g_time_virt0 + (t - g_time_real0) / k;
}

/* How many seconds on the clock a span of the desk's own time is. */
static double real_secs(double virt)
{
	return virt * g_time_slow * g_time_motion;
}

/* How long things take, as the settings have it: their motion, and in 2D
 * a hundredth of it -- everything that moves gets where it is going within
 * a frame, every animation as good as off, without anything that waits on
 * one waiting for ever (and never 0: it is divided by). */
#define GFX_2D_MOTION 0.01
static double motion_of(const struct shell_conf *c)
{
	return c->motion * (desk_gfx_level() == DESK_GFX_2D ? GFX_2D_MOTION : 1.0);
}

/* in 2D: what eases or springs to where it is going is put there at once */
static bool gfx_flat(void)
{
	return desk_gfx_level() == DESK_GFX_2D;
}

/* Everything that moves to take k times as long from now on, carrying on
 * from where it has got to. */
static void motion_set(double k)
{
	if (k == g_time_motion) {
		return;
	}
	double now = monotonic_sec();
	g_time_real0 = real_sec();
	g_time_virt0 = now;
	g_time_rebased = true;
	g_time_motion = k;
}

static double clampf_d(double v, double lo, double hi)
{
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

static double smoothstep01(double a, double b, double x)
{
	double t = clampf_d((x - a) / (b - a > 1e-9 ? b - a : 1e-9), 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}

/* A drawer, which is a different thing from a head: no wind-up, because
 * you have hold of it, and a knock at the end, because it arrives against
 * a stop and the things inside it keep going for a moment. */
static double drawer_ease(double u)
{
	const double Z = 0.80, W = 12.0;
	u = clampf_d(u, 0.0, 1.0);
	double wd = W * sqrt(1.0 - Z * Z);
	return 1.0 - exp(-Z * W * u) * (cos(wd * u) + (Z * W / wd) * sin(wd * u));
}

/* ---- how the camera moves ----------------------------------------
 *
 * The way the drawer does, which was right from the start: it sets off at
 * once, travels, goes a little past where it was going and comes back to
 * it. A spring, underdamped enough to be seen going past -- three per
 * cent, which the film has frames for (ROOM_TILT_U1) -- and settled well
 * before its time is up. From zero it is 60 per cent of the way at a fifth
 * of the time, over the top at half and back by nine tenths.
 *
 * It used to wind up first, leaning back a hair before it went. While the
 * move was shown as a dissolve nobody could see that, nor the arrival --
 * the view at either end was a still -- and what was left was a machine
 * going at one speed. Shown as a camera moving, a lean back reads as a
 * hesitation, and the arrival is the part that says it is a head.
 */
/* camera_bounce: 1 as above; less, more damped (0.4: under half a per cent
 * past); 0, not past at all -- critically damped, a head that simply
 * arrives -- for anyone the swing makes feel unwell */
static double g_bounce = 1.0;

static double look_ease(double u)
{
	const double W = 8.5;
	if (u >= 1.0) {
		return 1.0;
	}
	u = clampf_d(u, 0.0, 1.0);
	if (g_bounce <= 0.0) {
		return 1.0 - exp(-W * u) * (1.0 + W * u);
	}
	double Z = 0.74 + (1.0 - fmin(g_bounce, 1.0)) * 0.20;
	double wd = W * sqrt(1.0 - Z * Z);
	return 1.0 - exp(-Z * W * u) * (cos(wd * u) + (Z * W / wd) * sin(wd * u));
}

static double ease_d(double t)
{
	t = clampf_d(t, 0, 1);
	return t * t * (3.0 - 2.0 * t);
}

/* ---- what the desk sounds like (sound.c) ----
 *
 * Every sound is placed where the thing that makes it is: a little left or
 * right with it on the screen, never all the way to one side. */

static double snd_pan(const struct desk_server *server, double x)
{
	double w = server->desk_w > 0 ? server->desk_w : 1.0;
	return clampf_d(x / w * 2.0 - 1.0, -1.0, 1.0) * 0.7;
}

static int snd_at(const struct desk_server *server, enum desk_sound s, double x,
	double gain)
{
	return desk_sound_play(s, snd_pan(server, x), gain, 0.0);
}

/* Keeping the sound with the picture.
 *
 * What is played now is heard some while later -- the sound server's
 * buffers and the card's, from a few milliseconds to a tenth of a second
 * and more in a virtual machine -- and what is drawn now is seen a frame
 * later. A sound for something the user did (a click) can only follow it;
 * but where a motion is going to end is known while it is on its way, so
 * the sound for its end -- the drawer at its stop, a flap pressed down, a
 * ball of paper landing -- is asked for that much ahead, for the moment
 * the end is seen, and the sound server puts it out early by as long as
 * its way to the speakers takes. */
#define SND_SEEN 0.012   /* drawn, and seen: about a frame */

/* how far ahead, in the desk's time: the way to the speakers, and a frame
 * more, since the tick that notices is a frame apart */
static double snd_lead(void)
{
	return (desk_sound_latency() + 0.025) / (g_time_slow * g_time_motion);
}

/* s, to be heard as what it goes with is seen, `in` of the desk's time
 * from now */
static int snd_in(const struct desk_server *server, enum desk_sound s, double x,
	double gain, double rate, double in)
{
	return desk_sound_play_in(s, snd_pan(server, x), gain, rate,
		real_secs(fmax(in, 0.0)) + SND_SEEN);
}

/* A sound for something that takes `virt` of the desk's time, beginning
 * `in` from now: played as long as it does -- and cut to end as it is seen
 * to, even if it is heard late -- and faster (higher) for a quicker one
 * than designed. */
static int snd_for_in(const struct desk_server *server, enum desk_sound s, double x,
	double gain, double designed, double virt, double in)
{
	in = fmax(in, 0.0);
	double secs = real_secs(virt);
	int h = desk_sound_play_in(s, snd_pan(server, x), gain,
		clampf_d(designed / fmax(secs, 0.05), 0.6, 1.8), real_secs(in) + SND_SEEN);
	desk_sound_stop_after(h, real_secs(in + virt) + SND_SEEN, 0.04);
	return h;
}

static void snd_for(const struct desk_server *server, enum desk_sound s, double x,
	double gain, double designed, double virt)
{
	snd_for_in(server, s, x, gain, designed, virt, 0.0);
}

/* The sound for the end of a motion that began at `start` (the desk's
 * time) and lasts `dur`: asked for once, as soon as the end is near
 * enough. *h is 0 until it has been; whoever starts the motion again
 * forgets it (snd_forget), so that a sound for an end that is not
 * reached after all is not heard. */
static void snd_end(int *h, double start, double dur, enum desk_sound s, double pan,
	double gain, double rate)
{
	if (*h) {
		return;
	}
	double left = start + dur - monotonic_sec();
	if (left <= snd_lead()) {
		*h = desk_sound_play_in(s, pan, gain, rate, real_secs(fmax(left, 0.0)) + SND_SEEN);
		*h = *h ? *h : -1;   /* asked, even if there was nothing to play to */
	}
}

static void snd_forget(int *h)
{
	desk_sound_cancel(*h);
	*h = 0;
}

/* sounds of a sheet asked for ahead: kept, to be forgotten if it turns */
static void sheet_snd(struct desk_sheet *sheet, int h)
{
	int i = sheet->snd_due_i++ % 6;
	sheet->snd_due[i] = h;
}

static void sheet_snd_forget(struct desk_sheet *sheet)
{
	for (int i = 0; i < 6; i++) {
		desk_sound_cancel(sheet->snd_due[i]);
		sheet->snd_due[i] = 0;
	}
	sheet->snd_end = false;
}

static void server_schedule_frames(struct desk_server *server)
{
	if (g_prof.on && g_prof.asked == 0.0) {
		g_prof.asked = real_sec();
	}
	struct desk_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		wlr_output_schedule_frame(output->wlr_output);
	}
}

/* The screen a sheet lies on, or is on its way to: the one the middle of
 * where it comes to rest is on (x1, y1, in the paper's coordinates). */
static struct wlr_output *sheet_screen(struct desk_server *server, const struct desk_sheet *s)
{
	int ow = (int)s->hit_w, oh = (int)s->hit_h, pad;
	if (s->fold) {
		desk_fold_extent(s->fold, 1.0, &ow, &oh, &pad);
	}
	return screen_at(server, s->x1 + ow * 0.5 + server->desk_dx,
		s->y1 + oh * 0.5 + server->desk_dy + server->win_dy);
}

/* A place along the front of screen out for a sheet to be put down in:
 * the first not held by another (not self) lying on that screen. */
static int sheet_alloc_slot(struct desk_server *server, struct wlr_output *out,
	const struct desk_sheet *self)
{
	bool used[12] = {0};
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (view->sheet.phase != SHEET_IDLE && !view->sheet.in_drawer
			&& &view->sheet != self && view->sheet.slot >= 0 && view->sheet.slot < 12
			&& sheet_screen(server, &view->sheet) == out) {
			used[view->sheet.slot] = true;
		}
	}
	for (int i = 0; i < 12; i++) {
		if (!used[i]) {
			return i;
		}
	}
	return 0;
}

static struct wlr_box work_area(struct desk_server *server);

/* Where a folded sheet is put down: along the front of the desk -- or of
 * the screen beside it, out, it was folded on -- from the left, a row at a
 * time -- the next row further back, when one is full. */
static void sheet_slot_xy(struct desk_server *server, struct wlr_output *out, int slot,
	int pw, int ph, double *x, double *y)
{
	struct wlr_box wa = screen_area(server, out);
	int per_row = (wa.width - 48) / (pw + 18);
	if (per_row < 1) {
		per_row = 1;
	}
	int col = slot % per_row, row = slot / per_row;
	*x = wa.x + 36 + col * (pw + 18);
	*y = wa.y + wa.height - ph - 18 - row * (ph + 16);
}

/* How finely a thing in motion is drawn: whole at High and above, three
 * quarters across at Medium, half at Low, stretched back to its size by the
 * scene. A sheet folding is on screen for half a second and is mostly a
 * blur to the eye; drawn at half its size it costs a quarter to paint and
 * to hand over -- on an old machine, the difference between thirty frames
 * a second and sixty. What comes to rest is drawn whole again. */
static double motion_res(void)
{
	enum desk_gfx g = desk_gfx_level();
	return g >= DESK_GFX_HIGH ? 1.0 : g == DESK_GFX_MEDIUM ? 0.75 : 0.5;
}

static void sheet_present_body(struct desk_server *server, struct desk_sheet *sheet, double t, double x, double y)
{
	if (!sheet->src || !sheet->fold) {
		return;
	}
	int ow, oh, pad;
	desk_fold_extent(sheet->fold, t, &ow, &oh, &pad);
	int fw = ow + pad * 2, fh = oh + pad * 2;
	/* in motion, drawn smaller and stretched; at rest, whole */
	double k = t > 0.0 && t < 1.0 ? motion_res() : 1.0;
	int bw = (int)ceil(fw * k), bh = (int)ceil(fh * k);
	struct desk_buf *buffer = buf_create(bw, bh);
	if (bw != fw || bh != fh) {
		cairo_scale(buffer->cr, (double)bw / fw, (double)bh / fh);
	}
	cairo_translate(buffer->cr, pad, pad);
	/* the sheet is painted off-screen, so tell the shadow where it lands */
	desk_set_paint_origin(x - pad, y - pad);
	desk_fold_draw(buffer->cr, sheet->fold, t);
	desk_set_paint_origin(0, 0);
	/* lying folded it is under the windows; folding, opening out, over
	 * them, as it is held up off the desk */
	struct wlr_scene_tree *in = sheet->phase == SHEET_PARKED ? server->layer_folded
		: server->layer_parked;
	if (!sheet->tree) {
		sheet->tree = wlr_scene_tree_create(in);
		sheet->tree->node.data = (void *)(uintptr_t)DESK_ROLE_SHEET;
	} else if (sheet->tree->node.parent != in) {
		wlr_scene_node_reparent(&sheet->tree->node, in);
	}
	wlr_scene_node_set_enabled(&sheet->tree->node, true);
	scene_put(&sheet->buf, sheet->tree, buffer, 0, 0, (void *)(uintptr_t)DESK_ROLE_SHEET);
	wlr_scene_buffer_set_dest_size(sheet->buf, fw, fh);
	wlr_scene_node_set_position(&sheet->tree->node, (int)(x - pad), (int)(y - pad));
	wlr_scene_node_raise_to_top(&sheet->tree->node);
	sheet->hit_x = x;
	sheet->hit_y = y;
	sheet->hit_w = ow;
	sheet->hit_h = oh;
}

static void sheet_present(struct desk_server *server, struct desk_sheet *sheet, double t, double x, double y)
{
	prof_begin(PROF_SHEET);
	sheet_present_body(server, sheet, t, x, y);
	prof_end();
}

static void drawer_put_in(struct desk_server *server, struct desk_sheet *sheet);

static void sheet_tick_one(struct desk_server *server, struct desk_sheet *sheet)
{
	if (sheet->phase != SHEET_FOLDING && sheet->phase != SHEET_UNFOLDING) {
		return;
	}
	double u = (monotonic_sec() - sheet->start) / sheet->dur;
	u = clampf_d(u, 0, 1);
	double t = (sheet->phase == SHEET_FOLDING) ? u : (1.0 - u);
	double travel = ease_d(clampf_d((t - 0.55) / 0.45, 0, 1));
	double x = sheet->x0 + (sheet->x1 - sheet->x0) * travel;
	double y = sheet->y0 + (sheet->y1 - sheet->y0) * travel;
	sheet_present(server, sheet, t, x, y);
	if (sheet->fold) {
		/* A flap landing is pressed down, the next goes over; opening
		 * out, each crease lets go and its flap lifts away -- each asked
		 * for ahead, for the moment it is seen (snd_lead): the stage the
		 * sheet will be at by then, and when it got there. */
		int n = desk_fold_stages(sheet->fold);
		double now = monotonic_sec();
		double ua = clampf_d((now + snd_lead() - sheet->start) / sheet->dur, 0, 1);
		int k = desk_fold_stage(sheet->fold, sheet->phase == SHEET_FOLDING ? ua : 1.0 - ua);
		double done = desk_fold_done();
		double stage = sheet->dur * done / (n > 0 ? n : 1);
		double mid = x + sheet->hit_w * 0.5;
		if (sheet->phase == SHEET_FOLDING) {
			for (int j = sheet->snd_k + 1; j <= k && n > 0; j++) {
				/* the j-th flap down, j/n of the way to done */
				double in = sheet->start + sheet->dur * done * j / n - now;
				sheet_snd(sheet, snd_in(server, DESK_SND_PAPER_PRESS, mid, 1.0, 0.0, in));
				if (j < n) {
					sheet_snd(sheet, snd_for_in(server, DESK_SND_PAPER_SWING, mid, 0.9,
						0.40, stage, in));
				}
			}
			if (ua >= 1.0 && !sheet->snd_end) {
				/* down where it is to lie, or into the drawer */
				double in = sheet->start + sheet->dur - now;
				sheet->snd_end = true;
				sheet_snd(sheet, sheet->to_drawer
					? snd_in(server, DESK_SND_FELT_DROP, server->desk_w * 0.5, 0.6, 0.0, in)
					: snd_in(server, DESK_SND_PAPER_SETTLE, sheet->x1 + sheet->hit_w * 0.5,
						1.0, 0.0, in));
			}
		} else {
			for (int j = sheet->snd_k; j > k && n > 0; j--) {
				/* the j-th crease letting go, as the sheet opens back
				 * past it */
				double in = sheet->start + sheet->dur * (1.0 - done * j / n) - now;
				sheet_snd(sheet, snd_in(server, DESK_SND_PAPER_UNPRESS, mid, 1.0, 0.0, in));
				sheet_snd(sheet, snd_for_in(server, DESK_SND_PAPER_SWING, mid, 0.8, 0.40,
					stage, in));
			}
		}
		sheet->snd_k = k;
	}
	if (u >= 1.0) {
		if (sheet->phase == SHEET_FOLDING) {
			sheet->phase = SHEET_PARKED;
			sheet_present(server, sheet, 1.0, sheet->x1, sheet->y1);
			/* (heard as it lands: asked for ahead, above) */
			if (sheet->to_drawer) {
				sheet->to_drawer = false;
				drawer_put_in(server, sheet);
			}
		} else {
			sheet->phase = SHEET_IDLE;
			if (sheet->tree) {
				wlr_scene_node_set_enabled(&sheet->tree->node, false);
			}
			if (sheet->live) {
				wlr_scene_node_set_enabled(&sheet->live->node, true);
				wlr_scene_node_raise_to_top(&sheet->live->node);
			}
			if (sheet->src) {
				cairo_surface_destroy(sheet->src);
				sheet->src = NULL;
			}
			desk_fold_free(sheet->fold);
			sheet->fold = NULL;
			if (sheet->hit) {
				focus_toplevel(sheet->hit);
			}
		}
	}
}

static void folds_tick(struct desk_server *server)
{
	bool busy = false;
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (view->sheet.phase == SHEET_FOLDING || view->sheet.phase == SHEET_UNFOLDING) {
			sheet_tick_one(server, &view->sheet);
			busy = true;
		}
	}
	if (busy) {
		server_schedule_frames(server);
	}
}

static void room_layout(struct desk_server *server);
static void desktop_tick(struct desk_server *server);
static void overlay_place(struct desk_server *server);
static void desk_resize_to(struct desk_server *server, int w, int h);

/* Draw one sprite again. draw paints in screen coordinates, exactly as it
 * would into a buffer the size of the screen; box, when the caller knows
 * where the drawing lands, is that, and the sprite is made that size and
 * drawn straight into. Without a box it is drawn into a screen-sized
 * scratch and cropped to what it covers -- the right thing for something
 * that changes rarely and whose extent is easier to find than to say.
 * Above the overlay size the sprite is drawn smaller and blown back up by
 * the scene graph, the same as every other overlay. */
typedef void (*sprite_fn)(cairo_t *cr, int w, int h, void *data);

/* A sprite's picture for a box it is known to land in, drawn and not put
 * anywhere: where it goes on the screen (x, y), how big it is shown there
 * (dw, dh), and the overlay's pixel its first pixel is (ox, oy). NULL for
 * a box with nothing in it. */
static struct desk_buf *sprite_picture(struct desk_server *server, const double *box,
	sprite_fn draw, void *data, int *x, int *y, int *dw, int *dh, int *ox, int *oy)
{
	int w = server->desk_w, h = server->desk_h;
	int ow, oh;
	desk_overlay_size(w, h, &ow, &oh);
	double fx = (double)ow / w, fy = (double)oh / h;
	int x0 = (int)floor(box[0] * fx);
	int y0 = (int)floor(box[1] * fy);
	int x1 = (int)ceil((box[0] + box[2]) * fx);
	int y1 = (int)ceil((box[1] + box[3]) * fy);
	x0 = x0 < 0 ? 0 : x0;
	y0 = y0 < 0 ? 0 : y0;
	x1 = x1 > ow ? ow : x1;
	y1 = y1 > oh ? oh : y1;
	int bw = x1 - x0, bh = y1 - y0;
	if (bw < 1 || bh < 1) {
		return NULL;
	}
	struct desk_buf *b = buf_create(bw, bh);
	cairo_save(b->cr);
	cairo_translate(b->cr, -x0, -y0);
	cairo_scale(b->cr, fx, fy);
	draw(b->cr, w, h, data);
	cairo_restore(b->cr);
	*x = (int)floor(x0 / fx);
	*y = (int)floor(y0 / fy);
	*dw = (int)ceil(bw / fx);
	*dh = (int)ceil(bh / fy);
	*ox = x0;
	*oy = y0;
	return b;
}

static void sprite_paint(struct desk_server *server, struct desk_sprite *sp,
	struct wlr_scene_tree *parent, const double *box, sprite_fn draw, void *data)
{
	int w = server->desk_w, h = server->desk_h;
	int ow, oh;
	desk_overlay_size(w, h, &ow, &oh);
	double fx = (double)ow / w, fy = (double)oh / h;
	int x0, y0, bw, bh;
	struct desk_buf *b = NULL;
	if (box) {
		b = sprite_picture(server, box, draw, data, &sp->x, &sp->y, &sp->dw, &sp->dh, &sp->ox,
			&sp->oy);
		if (!b) {
			if (sp->node) {
				wlr_scene_node_set_enabled(&sp->node->node, false);
			}
			sp->drawn = false;
			return;
		}
		sp->bw = b->base.width;
		sp->bh = b->base.height;
		scene_put(&sp->node, parent, b, sp->x, sp->y, NULL);
		wlr_scene_buffer_set_dest_size(sp->node, sp->dw, sp->dh);
		wlr_scene_node_set_enabled(&sp->node->node, true);
		sp->drawn = true;
		return;
	} else {
		cairo_surface_t *scratch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ow, oh);
		cairo_t *cr = cairo_create(scratch);
		cairo_scale(cr, fx, fy);
		draw(cr, w, h, data);
		cairo_destroy(cr);
		cairo_surface_flush(scratch);
		const unsigned char *px = cairo_image_surface_get_data(scratch);
		int stride = cairo_image_surface_get_stride(scratch);
		int ax0 = ow, ay0 = oh, ax1 = -1, ay1 = -1;
		for (int y = 0; y < oh; y++) {
			const uint32_t *row = (const uint32_t *)(px + (size_t)y * stride);
			for (int x = 0; x < ow; x++) {
				if (row[x] >> 24) {
					ax0 = x < ax0 ? x : ax0;
					ax1 = x > ax1 ? x : ax1;
					ay0 = y < ay0 ? y : ay0;
					ay1 = y;
				}
			}
		}
		if (ax1 < 0) {
			/* nothing to show yet, or at all */
			if (sp->node) {
				wlr_scene_node_set_enabled(&sp->node->node, false);
			}
			sp->drawn = false;
			cairo_surface_destroy(scratch);
			return;
		}
		x0 = ax0;
		y0 = ay0;
		bw = ax1 - ax0 + 1;
		bh = ay1 - ay0 + 1;
		b = buf_create(bw, bh);
		cairo_set_operator(b->cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_surface(b->cr, scratch, -x0, -y0);
		cairo_paint(b->cr);
		cairo_set_operator(b->cr, CAIRO_OPERATOR_OVER);
		cairo_surface_destroy(scratch);
	}
	sp->x = (int)floor(x0 / fx);
	sp->y = (int)floor(y0 / fy);
	sp->dw = (int)ceil(bw / fx);
	sp->dh = (int)ceil(bh / fy);
	sp->ox = x0;
	sp->oy = y0;
	sp->bw = bw;
	sp->bh = bh;
	scene_put(&sp->node, parent, b, sp->x, sp->y, NULL);
	/* every time: at 1:1 too, since wlroots keeps the last size given
	 * across new buffers, and the overlay can come back to 1:1 */
	wlr_scene_buffer_set_dest_size(sp->node, sp->dw, sp->dh);
	wlr_scene_node_set_enabled(&sp->node->node, true);
	sp->drawn = true;
}

/* A sprite drawn on another's pixels: its picture a window of the other's
 * -- the overlay's same pixels, drawn the same way -- and shown through a
 * source box that puts each of its pixels where the other shows the same
 * one. Two sprites each placed by itself agree to within a pixel where
 * the overlay is drawn smaller than the screen (Low, a wide screen), which
 * is nothing for a thing laid over another and a seam for a patch of it:
 * the hand on the book is a patch of the book. The box is clipped to the
 * picture under it. */
static void sprite_paint_on(struct desk_server *server, struct desk_sprite *sp,
	struct wlr_scene_tree *parent, const struct desk_sprite *under, const double *box,
	sprite_fn draw, void *data)
{
	int w = server->desk_w, h = server->desk_h;
	int ow, oh;
	desk_overlay_size(w, h, &ow, &oh);
	double fx = (double)ow / w, fy = (double)oh / h;
	int x0 = (int)floor(box[0] * fx), y0 = (int)floor(box[1] * fy);
	int x1 = (int)ceil((box[0] + box[2]) * fx), y1 = (int)ceil((box[1] + box[3]) * fy);
	x0 = x0 < under->ox ? under->ox : x0;
	y0 = y0 < under->oy ? under->oy : y0;
	x1 = x1 > under->ox + under->bw ? under->ox + under->bw : x1;
	y1 = y1 > under->oy + under->bh ? under->oy + under->bh : y1;
	/* the picture under it shows the overlay's pixel p at
	 * under->x + (p - under->ox) * sx on the screen */
	double sx = under->bw > 0 ? (double)under->dw / under->bw : 1.0;
	double sy = under->bh > 0 ? (double)under->dh / under->bh : 1.0;
	double lx0 = under->x + (x0 - under->ox) * sx, lx1 = under->x + (x1 - under->ox) * sx;
	double ly0 = under->y + (y0 - under->oy) * sy, ly1 = under->y + (y1 - under->oy) * sy;
	/* shown on whole pixels of the screen, inside the picture */
	int X = (int)ceil(lx0 - 1e-6), X1 = (int)floor(lx1 + 1e-6);
	int Y = (int)ceil(ly0 - 1e-6), Y1 = (int)floor(ly1 + 1e-6);
	if (!under->drawn || x1 <= x0 || y1 <= y0 || X1 <= X || Y1 <= Y) {
		if (sp->node) {
			wlr_scene_node_set_enabled(&sp->node->node, false);
		}
		sp->drawn = false;
		return;
	}
	struct desk_buf *b = buf_create(x1 - x0, y1 - y0);
	cairo_save(b->cr);
	cairo_translate(b->cr, -x0, -y0);
	cairo_scale(b->cr, fx, fy);
	draw(b->cr, w, h, data);
	cairo_restore(b->cr);
	struct wlr_fbox src = { (X - lx0) / sx, (Y - ly0) / sy, (X1 - X) / sx, (Y1 - Y) / sy };
	scene_put(&sp->node, parent, b, X, Y, NULL);
	scene_source(sp->node, &src);
	wlr_scene_buffer_set_dest_size(sp->node, X1 - X, Y1 - Y);
	wlr_scene_node_set_enabled(&sp->node->node, true);
	sp->x = X;
	sp->y = Y;
	sp->dw = X1 - X;
	sp->dh = Y1 - Y;
	sp->ox = x0;
	sp->oy = y0;
	sp->bw = x1 - x0;
	sp->bh = y1 - y0;
	sp->drawn = true;
}

static void drawer_compose(struct desk_server *server);

/* Is the bookcase in the frame at all? */
static bool shelf_up(const struct desk_server *server)
{
	return server->shelf_t > 0.0005 || server->shelf_moving;
}

/* The move is a flipbook on two scene nodes; the settled view -- the shelf
 * with its printed labels, or the console with its needles -- is one cairo
 * buffer repainted only when it changes. */
static void film_ensure_body(struct desk_server *server, int which,
	struct wlr_scene_tree *tree)
{
	if (server->film_a[which]) {
		return;
	}
	cairo_surface_t *strip;
	int frames, fw, fh;
	if (!desk_tilt_film(server->desk_w, server->desk_h, which,
			&strip, &frames, &fw, &fh)) {
		return;
	}
	int sw = cairo_image_surface_get_width(strip);
	int sh = cairo_image_surface_get_height(strip);
	struct desk_buf *buf = buf_create(sw, sh);
	cairo_set_source_surface(buf->cr, strip, 0, 0);
	cairo_paint(buf->cr);
	cairo_surface_flush(buf->surface);
	prof_upload(sw, sh);
	server->film_frames[which] = frames;
	server->film_fw[which] = fw;
	server->film_fh[which] = fh;
	server->film_a[which] = scene_new(tree, &buf->base);
	server->film_b[which] = scene_new(tree, &buf->base);
	wlr_buffer_drop(&buf->base);
	wlr_scene_node_set_enabled(&server->film_a[which]->node, false);
	wlr_scene_node_set_enabled(&server->film_b[which]->node, false);
	/* Under everything else in the view's layer, the later keyframe over
	 * the earlier: the settled view fades in over the film (film_play).
	 * A film is traced after its view, so it can come after the view's own
	 * nodes were made, which would have put it on top of them. */
	wlr_scene_node_lower_to_bottom(&server->film_b[which]->node);
	wlr_scene_node_lower_to_bottom(&server->film_a[which]->node);
}

static void film_ensure(struct desk_server *server, int which,
	struct wlr_scene_tree *tree)
{
	if (server->film_a[which]) {
		return;
	}
	prof_begin(PROF_FILM);
	film_ensure_body(server, which, tree);
	prof_end();
}

/* One keyframe, laid where it stands in for the view at t. */
static void film_place(struct desk_server *server, int which,
	struct wlr_scene_buffer *node, int frame, double t, double opacity)
{
	struct wlr_fbox src = {
		0.0, (double)frame * server->film_fh[which],
		(double)server->film_fw[which], (double)server->film_fh[which],
	};
	double box[4];
	if (desk_tilt_place(server->desk_w, server->desk_h, which, frame, t, box)) {
		int x0 = (int)floor(box[0]), y0 = (int)floor(box[1]);
		int x1 = (int)ceil(box[0] + box[2]), y1 = (int)ceil(box[1] + box[3]);
		desk_screen_lay(server, node, src, x0, y0, x1 - x0, y1 - y0, 0, 0);
	} else {
		desk_screen_lay(server, node, src, 0, 0, server->desk_w, server->desk_h, 0, 0);
	}
	wlr_scene_buffer_set_opacity(node, (float)opacity);
	wlr_scene_node_set_enabled(&node->node, true);
}

static void film_hide(struct desk_server *server, int which)
{
	if (server->film_a[which]) {
		wlr_scene_node_set_enabled(&server->film_a[which]->node, false);
		wlr_scene_node_set_enabled(&server->film_b[which]->node, false);
	}
}

/* A move's film at t. The keyframes either side of it are laid where they
 * stand in for the view there, the later fading in over the earlier; from
 * nine tenths of the way the settled view fades in over them (`over`),
 * moved to where the view at t has it (dx, dy) -- which, as the move goes
 * past the end and comes back, is what shows it doing so. The film stays
 * under the settled view until the move is over, so that what shows round
 * the edge of it, moved, is the room and not the desk. */
static void film_play(struct desk_server *server, int which, double t,
	double *over, int *dx, int *dy)
{
	double in = clampf_d(t / 0.10, 0.0, 1.0);
	*over = clampf_d((t - 0.90) / 0.0995, 0.0, 1.0);
	int frame;
	double mix;
	desk_tilt_at(t, server->film_frames[which], &frame, &mix);
	if (server->soft_render && desk_gfx_level() <= DESK_GFX_LOW) {
		/* Put together on the processor, at Low: the nearer keyframe
		 * alone, not the two faded one over the other. Each is the whole
		 * screen scaled, which pixman does by the pixel; the two were most
		 * of what a move cost a machine with no GPU to do it. */
		film_place(server, which, server->film_a[which], mix > 0.5 ? frame + 1 : frame, t, in);
		wlr_scene_node_set_enabled(&server->film_b[which]->node, false);
	} else {
		film_place(server, which, server->film_a[which], frame, t, in);
		film_place(server, which, server->film_b[which], frame + 1, t, in * mix);
	}
	*dx = *dy = 0;
	if (*over > 0.0) {
		double fx, fy;
		desk_tilt_offset(server->desk_w, server->desk_h, which, t, &fx, &fy);
		*dx = (int)lround(fx);
		*dy = (int)lround(fy);
	}
}

/* a key out of whatever a layer is drawn from */
static uint64_t key_add(uint64_t h, const void *p, size_t n)
{
	const unsigned char *c = p;
	for (size_t i = 0; i < n; i++) {
		h = (h ^ c[i]) * 1099511628211ull;
	}
	return h;
}

struct sh_args {
	struct desk_server *server;
	int hover, taken, gadget, part;
	double ht;
	const double *box;
	time_t now;
};

static void sh_case_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct sh_args *a = data;
	desk_draw_shelf(cr, w, h, -1, 0.0, a->taken);
}

/* The same painter, clipped to the rectangle the binder slides in: the
 * case under it is opaque, so this is the whole view there, to the pixel. */
static void sh_hover_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct sh_args *a = data;
	cairo_rectangle(cr, a->box[0], a->box[1], a->box[2], a->box[3]);
	cairo_clip(cr);
	desk_draw_shelf(cr, w, h, a->hover, a->ht, a->taken);
}

static void sh_gadget_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct sh_args *a = data;
	desk_draw_shelf_gadget(cr, w, h, a->gadget, a->now);
}

/* The binder as it lies open. Coming down off the shelf it is the same
 * picture moved and faded (sh_binder_follow), not a new one every frame. */
static bool bnc_in_drawer(struct desk_server *server, int idx);

static void book_look(const struct desk_server *server, struct desk_book_look *lk);

/* The book as it lies still, a leaf going over or not: the leaf goes on as
 * a layer of its own (SH_LEAF), and what is under it is the same picture
 * from the start of a turn to its middle (where the thumb index moves on)
 * and from there to its end. Its page is put at a quarter or three
 * quarters of the way through, which draws the same. */
static void book_look_still(const struct desk_server *server, struct desk_book_look *lk)
{
	book_look(server, lk);
	/* and without the hand on it, which goes on by itself (SH_BOOKHOT,
	 * SH_BOOKPLATE): the pointer going down the index drew the whole book
	 * again for every line it crossed */
	lk->hover = -1;
	lk->hover_tab = -1;
	if (lk->open_t < 0.999) {
		return;
	}
	/* Settling, it is the spread it is settling on. The turn goes a shade
	 * past and back, and each time it crossed the page the book under the
	 * leaf was a different spread to draw again; this close, the leaf lies
	 * over all of the page that would have changed. */
	if (fabs(server->binder_pos_to - lk->page) < 0.10) {
		lk->page = server->binder_pos_to;
		return;
	}
	double n = floor(lk->page), frac = lk->page - n;
	lk->page = n + (frac > 0.5 ? 0.75 : frac > 0.0005 ? 0.25 : 0.0);
}

/* ---- the book still, drawn ahead ----------------------------------------
 *
 * A leaf going over draws the book under it again three times: as the turn
 * starts (the next page under the leaf), halfway (the thumb index moves on)
 * and as it settles. Each time it was the whole book laid into the view a
 * pixel at a time, 25 ms and more, and as the first three frames of a turn
 * were those, the leaf was half way over before it was seen to move. So
 * the stills a turn either way will want are drawn ahead while the book
 * lies open and nothing moves, one a tick, and kept with what was drawn on
 * the way; a turn finds them and only hands them over. */

/* the key of the book still for a look, as the shelf keys it */
static uint64_t book_still_key(struct desk_server *server, const struct desk_book_look *still)
{
	const struct desk_app_list *l = desk_apps_acquire();
	unsigned gen = l->generation;
	desk_apps_release(l);
	int taken = server->shelf_binder, tk = server->take.base >= 0 ? server->take.idx : -1;
	int size[2] = { server->desk_w, server->desk_h };
	uint64_t k = 14695981039346656037ull;
	k = key_add(k, &gen, sizeof gen);
	k = key_add(k, &taken, sizeof taken);
	k = key_add(k, &tk, sizeof tk);
	k = key_add(k, &server->binder_t, sizeof server->binder_t);
	k = key_add(k, size, sizeof size);
	k = key_add(k, server->book_q, strlen(server->book_q));
	k = key_add(k, server->book_pre, strlen(server->book_pre));
	for (int i = 0; i < server->book_ncand; i++) {
		k = key_add(k, server->book_cand[i], strlen(server->book_cand[i]));
	}
	return key_add(k, still, sizeof *still);
}

static void book_kept_forget(struct desk_server *server)
{
	for (int i = 0; i < BOOK_AHEAD; i++) {
		struct book_kept *e = &server->book_kept[i];
		if (e->buf) {
			wlr_buffer_unlock(&e->buf->base);
		}
		memset(e, 0, sizeof *e);
	}
}

static struct book_kept *book_kept_find(struct desk_server *server, uint64_t key)
{
	for (int i = 0; i < BOOK_AHEAD; i++) {
		if (server->book_kept[i].buf && server->book_kept[i].key == key) {
			return &server->book_kept[i];
		}
	}
	return NULL;
}

/* buf kept as the still for key, in the slot of the one wanted longest ago */
static void book_kept_keep(struct desk_server *server, uint64_t key, struct desk_buf *buf,
	int x, int y, int dw, int dh, int ox, int oy)
{
	struct book_kept *slot = &server->book_kept[0];
	for (int i = 0; i < BOOK_AHEAD; i++) {
		struct book_kept *e = &server->book_kept[i];
		if (!e->buf) {
			slot = e;
			break;
		}
		if (e->used < slot->used) {
			slot = e;
		}
	}
	if (slot->buf) {
		wlr_buffer_unlock(&slot->buf->base);
	}
	wlr_buffer_lock(&buf->base);
	*slot = (struct book_kept){ buf, key, x, y, dw, dh, ox, oy, monotonic_sec() };
}

static void sh_book_still_draw(cairo_t *cr, int w, int h, void *data)
{
	desk_draw_book_still(cr, w, h, data);
}

/* The book still, for key: handed over if it is kept, else drawn and kept. */
static void book_still_put(struct desk_server *server, uint64_t key, bool force,
	const double *box, struct desk_book_look *still)
{
	struct desk_sprite *sp = &server->sh_sprite[SH_BINDER];
	if (!force && sp->drawn && sp->key == key) {
		if (sp->node) {
			wlr_scene_node_set_enabled(&sp->node->node, true);
		}
		return;
	}
	struct book_kept *e = force ? NULL : book_kept_find(server, key);
	struct desk_buf *b;
	int x, y, dw, dh, ox, oy;
	if (e) {
		b = e->buf;
		x = e->x;
		y = e->y;
		dw = e->dw;
		dh = e->dh;
		ox = e->ox;
		oy = e->oy;
		e->used = monotonic_sec();
	} else {
		b = sprite_picture(server, box, sh_book_still_draw, still, &x, &y, &dw, &dh, &ox, &oy);
		if (!b) {
			if (sp->node) {
				wlr_scene_node_set_enabled(&sp->node->node, false);
			}
			sp->drawn = false;
			return;
		}
		book_kept_keep(server, key, b, x, y, dw, dh, ox, oy);
		/* the scene and the kept slot hold it now: drawn, it is let go */
		wlr_buffer_drop(&b->base);
	}
	cairo_surface_flush(b->surface);
	prof_upload(b->base.width, b->base.height);
	if (!sp->node) {
		sp->node = scene_new(server->sh_layer[SH_BINDER], &b->base);
	} else {
		scene_swap(sp->node, &b->base);
	}
	wlr_scene_node_set_position(&sp->node->node, x, y);
	wlr_scene_buffer_set_dest_size(sp->node, dw, dh);
	wlr_scene_node_set_enabled(&sp->node->node, true);
	sp->x = x;
	sp->y = y;
	sp->dw = dw;
	sp->dh = dh;
	sp->ox = ox;
	sp->oy = oy;
	sp->bw = b->base.width;
	sp->bh = b->base.height;
	sp->key = key;
	sp->drawn = true;
}

static int binder_count(struct desk_server *server);

/* One still the next turn either way will want, drawn while the book lies
 * open and nothing moves; false when there was none left to draw. */
static bool book_ahead(struct desk_server *server)
{
	if (!shelf_up(server) || server->shelf_moving || server->shelf_binder != DESK_SHELF_BOOK
		|| server->binder_moving || server->binder_pos_moving || server->binder_t < 0.999
		|| server->binder_putback || server->take.base >= 0) {
		return false;
	}
	int n = (int)lround(server->binder_pos), last = binder_count(server) - 1;
	/* on: under the leaf as it goes, half way, settled; and back */
	static const double AHEAD[] = { 0.25, 0.75, 1.0, -0.25, -0.75, -1.0 };
	for (size_t i = 0; i < sizeof AHEAD / sizeof *AHEAD; i++) {
		double p = n + AHEAD[i];
		if (p < 0.0 || p > last) {
			continue;
		}
		struct desk_book_look lk;
		book_look_still(server, &lk);
		lk.page = p;
		uint64_t key = book_still_key(server, &lk);
		if (book_kept_find(server, key)) {
			continue;
		}
		double box[4];
		desk_book_box(server->desk_w, server->desk_h, &lk, box);
		int x, y, dw, dh, ox, oy;
		struct desk_buf *b = sprite_picture(server, box, sh_book_still_draw, &lk, &x, &y, &dw,
			&dh, &ox, &oy);
		if (b) {
			book_kept_keep(server, key, b, x, y, dw, dh, ox, oy);
			wlr_buffer_drop(&b->base);
		}
		return true;
	}
	return false;
}

/* the leaf of the book going over, by itself */
static void sh_leaf_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct sh_args *a = data;
	struct desk_book_look lk;
	book_look(a->server, &lk);
	desk_draw_book_leaf(cr, w, h, &lk);
}

/* The book still with the hand put back on it: the look its patches are
 * drawn from. */
static void book_look_hot(const struct desk_server *server, struct desk_book_look *lk)
{
	book_look_still(server, lk);
	lk->hover = server->binder_sel;
	lk->hover_tab = server->book_tab;
}

/* a patch of the book where the hand is on it (part: desk_draw_book_hot) */
static void sh_book_hot_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct sh_args *a = data;
	struct desk_book_look lk;
	book_look_hot(a->server, &lk);
	desk_draw_book_hot(cr, w, h, &lk, a->part);
}

static void sh_binder_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct sh_args *a = data;
	struct desk_server *server = a->server;
	if (server->shelf_binder == DESK_SHELF_BOOK) {
		struct desk_book_look lk;
		book_look_still(server, &lk);
		desk_draw_book_still(cr, w, h, &lk);
		return;
	}
	struct desk_binder_corner c = { server->bnc_idx, server->bnc_s, server->bnc_lift,
		server->bnc_hot, bnc_in_drawer(server, server->bnc_idx) };
	desk_draw_binder(cr, w, h, server->shelf_binder,
		server->binder_pos, server->binder_sel, clampf_d(server->binder_t, 0.0, 1.0),
		server->bnc_idx >= 0 ? &c : NULL, server->take.base >= 0 ? server->take.idx : -1);
}

/* How far down off the shelf the binder has come: 0 up out of sight, 1
 * lying open -- as it lands, a little past 1 and back. As an opacity, no
 * further than 1. */
static double binder_down(const struct desk_server *server)
{
	return clampf_d(server->binder_t, 0.0, 1.0);
}

/* The book or binder draws its own way off the shelf, the whole of it:
 * here it is only shown, or not. */
static void sh_binder_follow(struct desk_server *server)
{
	struct desk_sprite *sp = &server->sh_sprite[SH_BINDER];
	if (!sp->node || !sp->drawn) {
		return;
	}
	wlr_scene_node_set_position(&sp->node->node, sp->x, sp->y);
	wlr_scene_node_set_enabled(&sp->node->node, binder_down(server) > 0.002);
}

/* One layer of the view: drawn again if its key has moved, shown either way. */
static void sh_put(struct desk_server *server, int k, uint64_t key, bool force,
	const double *box, sprite_fn fn, void *args)
{
	struct desk_sprite *sp = &server->sh_sprite[k];
	if (!force && sp->drawn && sp->key == key) {
		if (sp->node) {
			wlr_scene_node_set_enabled(&sp->node->node, true);
		}
		return;
	}
	sp->key = key;
	sprite_paint(server, sp, server->sh_layer[k], box, fn, args);
}

/* The same for a layer that is a patch of the one under it (sprite_paint_on). */
static void sh_put_on(struct desk_server *server, int k, int under, uint64_t key, bool force,
	const double *box, sprite_fn fn, void *args)
{
	struct desk_sprite *sp = &server->sh_sprite[k];
	if (!force && sp->drawn && sp->key == key) {
		if (sp->node) {
			wlr_scene_node_set_enabled(&sp->node->node, true);
		}
		return;
	}
	sp->key = key;
	sprite_paint_on(server, sp, server->sh_layer[k], &server->sh_sprite[under], box, fn, args);
}

static void sh_hide(struct desk_server *server, int k)
{
	struct desk_sprite *sp = &server->sh_sprite[k];
	if (sp->node) {
		wlr_scene_node_set_enabled(&sp->node->node, false);
	}
}

static void sh_opacity(struct desk_server *server, double a)
{
	for (int k = 0; k < SH_LAYERS; k++) {
		if (server->sh_sprite[k].node) {
			double o = a;
			wlr_scene_buffer_set_opacity(server->sh_sprite[k].node, (float)o);
		}
	}
}

static void paint_shelf_body(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	if (w < 8 || h < 8) {
		return;
	}
	if (!shelf_up(server)) {
		if (server->sh_live) {
			wlr_scene_node_set_enabled(&server->sh_live->node, false);
		}
		if (server->film_a[DESK_VIEW_SHELF]) {
			wlr_scene_node_set_enabled(&server->film_a[DESK_VIEW_SHELF]->node, false);
			wlr_scene_node_set_enabled(&server->film_b[DESK_VIEW_SHELF]->node, false);
		}
		return;
	}
	film_ensure(server, DESK_VIEW_SHELF, server->layer_shelf);
	if (!server->sh_live) {
		server->sh_live = wlr_scene_tree_create(server->layer_shelf);
		for (int k = 0; k < SH_LAYERS; k++) {
			server->sh_layer[k] = wlr_scene_tree_create(server->sh_live);
		}
	}
	bool force = server->sh_w != w || server->sh_h != h;
	server->sh_w = w;
	server->sh_h = h;

	/* The cairo half: the settled bookcase, the open binder, the sheet in
	 * the air -- and none of it while the move has not got far enough to
	 * show it. Each layer is drawn again only when what it shows has
	 * changed: a binder coming out of the row is its own rectangle, and
	 * the case under it is not touched. */
	bool settled = server->shelf_t > 0.70;
	bool have_view = settled;
	struct sh_args a = { server, -1, -1, -1, 0, 0.0, NULL, time(NULL) };
	if (settled) {
		a.taken = server->shelf_binder;
		a.hover = a.taken >= 0 ? -1
			: (server->shelf_hover >= 0 ? server->shelf_hover : server->hover_from);
		a.ht = a.taken >= 0 ? 0.0 : server->hover_t;
		const struct desk_app_list *l = desk_apps_acquire();
		unsigned gen = l->generation;
		desk_apps_release(l);
		double full[4] = { 0.0, 0.0, (double)w, (double)h };

		uint64_t key = 14695981039346656037ull;
		key = key_add(key, &gen, sizeof gen);
		key = key_add(key, &a.taken, sizeof a.taken);
		sh_put(server, SH_CASE, key, force, full, sh_case_draw, &a);

		double box[4];
		if (a.hover >= 0 && a.ht > 0.004 && desk_shelf_hover_box(w, h, a.hover, box)) {
			uint64_t hk = key_add(key, &a.hover, sizeof a.hover);
			hk = key_add(hk, &a.ht, sizeof a.ht);
			a.box = box;
			sh_put(server, SH_HOVER, hk, force, box, sh_hover_draw, &a);
		} else {
			sh_hide(server, SH_HOVER);
		}

		int slot = SH_GADGET0;
		for (int i = 0; i < DESK_GADGET_COUNT && slot <= SH_GADGET2; i++) {
			double gb[4];
			if (!desk_shelf_gadget_box(w, h, i, gb)) {
				continue;
			}
			a.gadget = i;
			sh_put(server, slot, desk_shelf_gadget_key(i, a.now), force, gb,
				sh_gadget_draw, &a);
			slot++;
		}

		if (a.taken >= 0 && server->binder_putback && server->sh_sprite[SH_BINDER].drawn) {
			/* the book left open as you turn away: as it was, not drawn
			 * again for nobody to look at */
			if (server->sh_sprite[SH_BINDER].node) {
				wlr_scene_node_set_enabled(&server->sh_sprite[SH_BINDER].node->node, true);
			}
			sh_hide(server, SH_LEAF);
		} else if (a.taken >= 0) {
			uint64_t bk = key_add(key, &server->binder_pos, sizeof server->binder_pos);
			bk = key_add(bk, &server->binder_sel, sizeof server->binder_sel);
			/* and its corner, and what is printed on the back of it */
			bool ind = bnc_in_drawer(server, server->bnc_idx);
			bk = key_add(bk, &server->bnc_idx, sizeof server->bnc_idx);
			bk = key_add(bk, &server->bnc_s, sizeof server->bnc_s);
			bk = key_add(bk, &server->bnc_hot, sizeof server->bnc_hot);
			bk = key_add(bk, &ind, sizeof ind);
			int tk = server->take.base >= 0 ? server->take.idx : -1;
			bk = key_add(bk, &tk, sizeof tk);
			/* and how far it has come off the shelf */
			bk = key_add(bk, &server->binder_t, sizeof server->binder_t);
			double bb[4];
			uint64_t lkey = 0;
			double lb[4];
			bool leaf = false, book = false;
			struct desk_book_look book_still;
			if (server->shelf_binder == DESK_SHELF_BOOK) {
				/* and what is written in it, and what the pointer is on;
				 * the leaf going over apart, under it the book still
				 * (book_look_still) -- not binder_pos, which moves on
				 * every frame of a turn */
				struct desk_book_look lk;
				book_look(server, &lk);
				book_look_still(server, &book_still);
				bk = book_still_key(server, &book_still);
				lkey = key_add(bk, &lk, sizeof lk);
				desk_book_box(w, h, &book_still, bb);
				leaf = desk_book_leaf_box(w, h, &lk, lb);
				book = true;
			} else {
				desk_binder_box(w, h, server->binder_pos, server->binder_t, bb);
			}
			if (book) {
				book_still_put(server, bk, force, bb, &book_still);
			} else {
				sh_put(server, SH_BINDER, bk, force, bb, sh_binder_draw, &a);
			}
			sh_binder_follow(server);
			/* the hand on the book: the line it is on, or the tab, and the
			 * plate that line puts on an empty page -- patches of it */
			struct desk_book_look hot;
			book_look_hot(server, &hot);
			for (int part = 0; part < 2; part++) {
				double hb[4];
				if (book && desk_book_hot_box(w, h, &hot, part, hb)) {
					uint64_t hk = key_add(bk, &part, sizeof part);
					hk = key_add(hk, &hot.hover, sizeof hot.hover);
					hk = key_add(hk, &hot.hover_tab, sizeof hot.hover_tab);
					a.part = part;
					sh_put_on(server, SH_BOOKHOT + part, SH_BINDER, hk, force, hb,
						sh_book_hot_draw, &a);
				} else {
					sh_hide(server, SH_BOOKHOT + part);
				}
			}
			if (leaf) {
				sh_put(server, SH_LEAF, lkey, force, lb, sh_leaf_draw, &a);
			} else {
				sh_hide(server, SH_LEAF);
			}
		} else {
			for (int k = SH_BINDER; k <= SH_LEAF; k++) {
				sh_hide(server, k);
			}
		}
	} else {
		for (int k = SH_CASE; k <= SH_LEAF; k++) {
			sh_hide(server, k);
		}
	}
	wlr_scene_node_set_enabled(&server->sh_live->node, have_view);

	double t = server->shelf_t;
	if (!server->film_a[DESK_VIEW_SHELF] || (!server->shelf_moving && t >= 0.9995)) {
		film_hide(server, DESK_VIEW_SHELF);
		wlr_scene_node_set_position(&server->sh_live->node, 0, 0);
		sh_opacity(server, 1.0);
		return;
	}
	/* Hand over to the full resolution sheet at the end of the move, and
	 * fade the whole overlay in at the start so the drawer being pushed
	 * shut is a thing that happens rather than a thing that pops. */
	double over;
	int dx, dy;
	film_play(server, DESK_VIEW_SHELF, t, &over, &dx, &dy);
	wlr_scene_node_set_position(&server->sh_live->node, dx, dy);
	sh_opacity(server, have_view ? over : 0.0);
	wlr_scene_node_set_enabled(&server->sh_live->node, have_view && over > 0.001);
}

static void paint_shelf(struct desk_server *server)
{
	prof_begin(PROF_SHELF);
	paint_shelf_body(server);
	prof_end();
}

static void shelf_toggle(struct desk_server *server);
static void lock_request(struct desk_server *server);
static void paint_lock(struct desk_server *server);
static bool locked(const struct desk_server *server);
static void show_for_today(struct desk_server *server);
static void show_stop(struct desk_server *server, bool skipped);
static void show_tick(struct desk_server *server);
static void show_midnight_check(struct desk_server *server);
static void drawer_toggle(struct desk_server *server);
static void fullscreen_layer_order(struct desk_server *server);
static int drawer_lift(struct desk_server *server);
static double drawer_line(struct desk_server *server);
static int drawer_index_of(struct desk_server *server, const struct desk_sheet *sheet);
static void drawer_lift_sheet(struct desk_server *server, int i);
static void drawer_forget(struct desk_server *server, struct desk_sheet *sheet);
static void console_toggle(struct desk_server *server);
static void post_toggle(struct desk_server *server);
static void card_gone(struct desk_server *server);
static bool console_dying(const struct desk_server *server);
static void console_power_reset(struct desk_server *server);

/* How many sheets that binder holds. */
static int binder_count(struct desk_server *server)
{
	if (server->shelf_binder < 0) {
		return 0;
	}
	if (server->shelf_binder == DESK_SHELF_BOOK) {
		/* the book turns a spread at a time */
		return desk_book_spreads(server->desk_w, server->desk_h, server->book_q);
	}
	const struct desk_app_list *l = desk_apps_acquire();
	int n = l->shelf[server->shelf_binder].count;
	desk_apps_release(l);
	return n;
}

/* Taken down, the book or a binder is brought round and opened; put back,
 * the same the other way, a little quicker. */
#define BOOK_OPEN_S 1.25
#define BOOK_CLOSE_S 0.95

static void binder_animate(struct desk_server *server, double to)
{
	double now = monotonic_sec();
	server->binder_from = server->binder_t;
	server->binder_to = to;
	server->binder_start = now;
	/* from wherever it had got to */
	double left = fabs(to - clampf_d(server->binder_t, 0.0, 1.0));
	server->binder_dur = fmax((to > 0.5 ? BOOK_OPEN_S : BOOK_CLOSE_S) * left, 0.05);
	snd_forget(&server->snd_binder_down);
	server->binder_moving = 1;
	server_schedule_frames(server);
	paint_shelf(server);
}

static void bnc_reset(struct desk_server *server);

static void book_begin(struct desk_server *server);
static void book_end(struct desk_server *server);

static void binder_open(struct desk_server *server, int which)
{
	bnc_reset(server);
	desk_sound_play(DESK_SND_BINDER_LIFT, 0.0, 1.0, 0.0);
	server->snd_page = 0;
	server->shelf_binder = which;
	server->binder_pos = 0.0;
	server->binder_pos_to = 0.0;
	server->binder_pos_vel = 0.0;
	server->binder_pos_moving = 0;
	server->binder_scroll = 0.0;
	server->binder_sel = -1;
	server->binder_t = 0.0;
	if (which == DESK_SHELF_BOOK) {
		book_begin(server);
	}
	binder_animate(server, 1.0);
}

static void binder_close(struct desk_server *server)
{
	if (server->shelf_binder < 0) {
		return;
	}
	server->binder_sel = -1;
	bnc_reset(server);
	if (server->shelf_binder == DESK_SHELF_BOOK) {
		book_end(server);
	}
	desk_sound_play(DESK_SND_BINDER_CLOSE, 0.05, 1.0, 0.0);
	binder_animate(server, 0.0);
}

/* Turn the block over. step is in sheets, so a notch of the wheel is one
 * sheet and the paper visibly moves on the ring. */
/* Turn the block to a place picked outright -- a thumb tab, a line chosen
 * with the keys -- and let the spring take it there. */
static void binder_turn_to(struct desk_server *server, double to)
{
	int n = binder_count(server);
	if (n <= 1) {
		return;
	}
	/* You can turn a block right through to its last sheet. Stopping a
	 * fanful short meant a binder of nine or fewer would not turn at all
	 * and a longer one could never reach its end. */
	int last = n - 1;
	if (to > last) {
		to = last;
	}
	if (to < 0) {
		to = 0;
	}
	if (to == server->binder_pos_to && !server->binder_pos_moving) {
		return;
	}
	server->binder_pos_to = to;
	if (!server->binder_pos_moving) {
		server->binder_pos_at = monotonic_sec();
		server->binder_pos_vel = 0.0;
	}
	server->binder_pos_moving = 1;
	server->binder_sel = -1;
	paint_shelf(server);
	server_schedule_frames(server);
}

/* Turn the block over by step sheets: a notch of the wheel is one sheet,
 * so the paper visibly moves on the ring. */
static void binder_riffle(struct desk_server *server, int step)
{
	double cur = server->binder_pos;
	double to = server->binder_pos_to + step;
	/* You cannot shove the block further ahead than the paper has got to.
	 * One flick of a trackpad arrives as two dozen notches inside a
	 * millisecond, and without this it tears straight through to the last
	 * sheet and sits there -- which looks exactly like nothing turning. */
	const double lead = 2.5;
	if (to > cur + lead) {
		to = floor(cur + lead);
	}
	if (to < cur - lead) {
		to = ceil(cur - lead);
	}
	binder_turn_to(server, to);
}

/* The block follows where it has been turned to on a spring, so a sheet
 * goes over, a shade too far, and settles back against the others; and a
 * run of notches is one movement that keeps its way, rather than a start
 * and a stop for each. A sheet takes about a fifth of a second. */
static void binder_pos_tick(struct desk_server *server)
{
	if (!server->binder_pos_moving) {
		return;
	}
	const double W = 24.0, Z = 0.72;
	double now = monotonic_sec();
	double dt = clampf_d(now - server->binder_pos_at, 0.0, 0.05);
	server->binder_pos_at = now;
	/* in small steps, so a slow frame does not throw it */
	int n = (int)ceil(dt / (1.0 / 480.0));
	double h = n > 0 ? dt / n : 0.0;
	for (int i = 0; i < n; i++) {
		double acc = W * W * (server->binder_pos_to - server->binder_pos)
			- 2.0 * Z * W * server->binder_pos_vel;
		server->binder_pos_vel += acc * h;
		server->binder_pos += server->binder_pos_vel * h;
	}
	/* a sheet going over the ring, heard as it passes the top */
	int page = (int)lround(server->binder_pos);
	if (page != server->snd_page) {
		if (now - server->snd_page_at > 0.035) {
			desk_sound_play(DESK_SND_PAGE_TURN, 0.12, 1.0, 0.0);
			server->snd_page_at = now;
		}
		server->snd_page = page;
	}
	if (gfx_flat()) {
		server->binder_pos = server->binder_pos_to;   /* 2D: there at once */
		server->binder_pos_vel = 0.0;
	}
	if (fabs(server->binder_pos_to - server->binder_pos) < 0.002
		&& fabs(server->binder_pos_vel) < 0.02) {
		server->binder_pos = server->binder_pos_to;
		server->binder_pos_vel = 0.0;
		server->binder_pos_moving = 0;
	}
	paint_shelf(server);
	if (server->binder_pos_moving) {
		server_schedule_frames(server);
	}
}

/* The binder under the pointer comes out of the row. */
static void hover_set(struct desk_server *server, int which)
{
	if (which == server->shelf_hover) {
		return;
	}
	server->shelf_hover = which;
	if (which >= 0) {
		server->hover_from = which;
	}
	server->hover_start = monotonic_sec();
	server->hover_moving = 1;
	server_schedule_frames(server);
	paint_shelf(server);
}

static void hover_tick(struct desk_server *server)
{
	if (!server->hover_moving) {
		return;
	}
	double u = clampf_d((monotonic_sec() - server->hover_start) / 0.17, 0, 1);
	double e = u * u * (3.0 - 2.0 * u);
	server->hover_t = server->shelf_hover >= 0 ? e : 1.0 - e;
	if (u >= 1.0) {
		server->hover_t = server->shelf_hover >= 0 ? 1.0 : 0.0;
		server->hover_moving = 0;
		if (server->shelf_hover < 0) {
			server->hover_from = -1;
		}
	}
	paint_shelf(server);
	if (server->hover_moving) {
		server_schedule_frames(server);
	}
}

/* Sheets printed ahead, one a frame: the first ones of the binder under
 * the pointer on the shelf, so taking it down does not stop to print them
 * all at once; and, with one open and at rest, the next ones a turn will
 * bring in either way. */
static void binder_warm_tick(struct desk_server *server)
{
	if (server->shelf_t < 0.70) {
		return;
	}
	int shelf = server->shelf_binder;
	double first = server->binder_pos;
	if (shelf < 0) {
		shelf = server->shelf_hover;
		first = 0.0;
	} else if (server->binder_pos_moving || server->binder_moving) {
		return;
	}
	if (shelf == DESK_SHELF_BOOK && server->shelf_binder < 0) {
		if (desk_book_warm(server->desk_w, server->desk_h)) {
			server_schedule_frames(server);
		}
		return;
	}
	if (shelf >= 0 && shelf < DESK_SHELF_COUNT
		&& desk_binder_warm(server->desk_w, server->desk_h, shelf, first, 1)) {
		server_schedule_frames(server);
		return;
	}
	/* and, under the pointer on the shelf, what it is drawn from as it is
	 * taken down and opened */
	if (shelf >= 0 && shelf < DESK_SHELF_COUNT && server->shelf_binder < 0
		&& desk_binder_anim_warm(server->desk_w, server->desk_h, shelf)) {
		server_schedule_frames(server);
	}
}

static void binder_tick(struct desk_server *server)
{
	if (!server->binder_moving) {
		return;
	}
	double u = clampf_d((monotonic_sec() - server->binder_start) / server->binder_dur, 0, 1);
	/* in time as it goes: the moves are eased in it (op_draw) */
	double was = server->binder_t;
	server->binder_t = server->binder_from + (server->binder_to - server->binder_from) * u;
	if (was < 0.62 && server->binder_t >= 0.62) {
		/* the board lifting off the pages */
		desk_sound_play(DESK_SND_PAGE_TURN, 0.05, 0.9, 0.85);
	}
	if (server->binder_to > 0.5) {
		/* down on the desk, open */
		snd_end(&server->snd_binder_down, server->binder_start, server->binder_dur,
			DESK_SND_BINDER_OPEN, 0.05, 1.0, 0.0);
	}
	if (u >= 1.0) {
		server->binder_t = server->binder_to;
		server->binder_moving = 0;
		if (server->binder_t <= 0.001) {
			server->shelf_binder = -1;
			desk_binder_forget();
		}
	}
	paint_shelf(server);
	if (server->binder_moving) {
		server_schedule_frames(server);
	}
}

static int base_new(struct desk_server *server, const struct desk_app *app,
	cairo_surface_t *face, double sw, double sh);

/* Take the sheet out and go and do what is printed on it: the sheet comes
 * off the ring into the hand and goes down to the desk with you, to where
 * the program's window will be (take_tick). Whether the program is ready
 * by the time it is put down is the program's business. */
static void binder_take(struct desk_server *server, int idx)
{
	const struct desk_app_list *l = desk_apps_acquire();
	const struct desk_shelf_info *si = &l->shelf[server->shelf_binder];
	bool taken = false;
	if (idx >= 0 && idx < si->count) {
		const struct desk_app *app = &l->apps[si->first + idx];
		wlr_log(WLR_INFO, "taking %s off the %s binder", app->id, si->label_en);

		double cx, cy, sw, sh, a;
		desk_binder_sheet_pose(server->desk_w, server->desk_h,
			(double)idx - server->binder_pos, idx == server->binder_sel,
			&cx, &cy, &sw, &sh, &a);
		char code[32];
		char key[4] = { 0 };
		for (int i = 0; i < 3 && si->key[i]; i++) {
			key[i] = (char)toupper((unsigned char)si->key[i]);
		}
		snprintf(code, sizeof code, "%s\xc2\xb7%03d", key, idx + 1);
		cairo_surface_t *face = server->take.base < 0 && server->conf.take_sheet
			? desk_app_sheet_surface(app->id, sw, sh, code) : NULL;
		int b = face ? base_new(server, app, face, sw, sh) : -1;
		if (b >= 0) {
			double now = monotonic_sec();
			snd_at(server, DESK_SND_SHEET_TAKE, cx, 1.0);
			server->take.base = b;
			server->take.idx = idx;
			server->take.start = now;
			server->take.turn_at = now + TAKE_TURN;
			server->take.land_at = now + TAKE_TURN + 0.40;
			server->take.turned = false;
			server->take.cx0 = cx;
			server->take.cy0 = cy;
			server->take.w0 = sw;
			server->take.h0 = sh;
			server->take.a0 = a;
			taken = true;
		}
		pid_t sid = desk_app_launch(app);
		if (b >= 0) {
			server->bases[b].sid = sid > 0 ? sid : 0;
		}
	}
	desk_apps_release(l);
	/* With a sheet in the hand you turn back to the desk a moment later
	 * (take_tick), without one now; the binder is left open as you go and
	 * put back while you are not looking (shelf_tick), as the book is --
	 * not shut in front of you, a moment you would not see the end of.
	 * Meanwhile it takes no more clicks. */
	server->binder_sel = -1;
	bnc_reset(server);
	server->binder_to = 0.0;
	server->binder_putback = true;
	if (!taken && server->shelf_t > 0.5) {
		shelf_toggle(server);
	}
	paint_shelf(server);
	server_schedule_frames(server);
}

/* ---- the book of every program -------------------------------------
 *
 * Taken down, it is open at its first spread with the caret on the line
 * at the head of the left-hand page: whatever is typed goes there (through
 * the input method, as the binder of notes takes it), and the index is of
 * only what answers to what is written, the first of it chosen -- Enter
 * takes it. Up and down choose along the index, the wheel and Page Up and
 * Down turn a spread, a tab of the thumb index opens it at that letter,
 * Escape rubs out what is written and then puts the book back. A line
 * clicked is taken, as a binder's sheet is: a sheet comes up off the page
 * and is carried down to the desk, where the program's window will be. */

static void book_look(const struct desk_server *server, struct desk_book_look *lk)
{
	memset(lk, 0, sizeof *lk);
	lk->query = server->book_q;
	lk->caret = server->book_caret;
	lk->preedit = server->book_pre;
	lk->preedit_caret = server->book_pre_caret;
	lk->cands = server->book_cand_p;
	lk->ncand = server->book_ncand;
	lk->cand_cursor = server->book_cand_cursor;
	lk->cand_page = server->book_cand_page;
	lk->page = server->binder_pos;
	lk->hover = server->binder_sel;
	lk->sel = server->book_sel;
	lk->hover_tab = server->book_tab;
	lk->open_t = clampf_d(server->binder_t, 0.0, 1.0);
	lk->ime = server->book_ime;
}

static bool host_origin(struct desk_server *server, int *ox, int *oy);

/* Tell the input method where the line being written on is. */
static void book_tell_cursor(struct desk_server *server)
{
	if (!server->book_ime || server->shelf_binder != DESK_SHELF_BOOK) {
		return;
	}
	struct desk_book_look lk;
	book_look(server, &lk);
	double b[4];
	if (!desk_book_find_box(server->desk_w, server->desk_h, &lk, b)) {
		return;
	}
	int ox = 0, oy = 0;
	host_origin(server, &ox, &oy);
	int c[4] = { ox + (int)lround(b[0] + b[2] * 0.12), oy + (int)lround(b[1]),
		(int)lround(b[2] * 0.5), (int)lround(b[3]) };
	if (memcmp(c, server->book_cursor, sizeof c) != 0) {
		memcpy(server->book_cursor, c, sizeof c);
		desk_ime_typing_cursor(c[0], c[1], c[2], c[3]);
	}
}

static void book_changed(struct desk_server *server)
{
	paint_shelf(server);
	book_tell_cursor(server);
	server_schedule_frames(server);
}

/* What is written has changed: the index is a new one, open at its start,
 * the best of it chosen. */
static void book_written(struct desk_server *server)
{
	server->binder_pos = server->binder_pos_to = 0.0;
	server->binder_pos_vel = 0.0;
	server->binder_pos_moving = 0;
	int n = desk_book_matches(server->book_q, NULL, 0);
	server->book_sel = server->book_q[0] && n > 0 ? 0 : -1;
	server->binder_sel = -1;
	book_changed(server);
}

static void book_clear_ime(struct desk_server *server)
{
	server->book_pre[0] = '\0';
	server->book_pre_caret = 0;
	server->book_ncand = 0;
	server->book_keyq_n = 0;
}

static void book_insert(struct desk_server *server, const char *t)
{
	size_t n = strlen(server->book_q), k = strlen(t);
	if (!k || n + k >= sizeof server->book_q - 1) {
		return;
	}
	int c = server->book_caret;
	memmove(server->book_q + c + k, server->book_q + c, n - (size_t)c + 1);
	memcpy(server->book_q + c, t, k);
	server->book_caret = c + (int)k;
}

static int utf8_prev(const char *s, int i);
static int utf8_next(const char *s, int i);
static void book_take(struct desk_server *server, int i);

/* Move the line chosen with the keys d along the index, and open the book
 * where it is. */
static void book_choose(struct desk_server *server, int d)
{
	int n = desk_book_matches(server->book_q, NULL, 0);
	if (n <= 0) {
		return;
	}
	int sel = server->book_sel < 0 ? (d > 0 ? 0 : n - 1) : server->book_sel + d;
	sel = sel < 0 ? 0 : (sel >= n ? n - 1 : sel);
	server->book_sel = sel;
	int sp = desk_book_spread_of(server->desk_w, server->desk_h, server->book_q, sel);
	if (sp >= 0 && sp != (int)lround(server->binder_pos_to)) {
		binder_turn_to(server, sp);
	}
	book_changed(server);
}

/* a key the input method did not take, or there is no input method */
static void book_plain(struct desk_server *server, uint32_t sym, const char *utf8,
	uint32_t mods)
{
	char *q = server->book_q;
	int n = (int)strlen(q), c = server->book_caret;
	switch (sym) {
	case XKB_KEY_Escape:
		if (q[0] || server->book_pre[0]) {
			q[0] = '\0';
			server->book_caret = 0;
			if (server->book_ime) {
				desk_ime_typing_reset();
			}
			book_clear_ime(server);
			book_written(server);
		} else {
			binder_close(server);
		}
		return;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter: {
		int sel = server->book_sel;
		if (sel < 0 && q[0] && desk_book_matches(q, NULL, 0) > 0) {
			sel = 0;
		}
		if (sel >= 0) {
			book_take(server, sel);
		}
		return;
	}
	case XKB_KEY_BackSpace:
		if (c > 0) {
			int p = utf8_prev(q, c);
			memmove(q + p, q + c, (size_t)(n - c + 1));
			server->book_caret = p;
			book_written(server);
		}
		return;
	case XKB_KEY_Delete:
		if (c < n) {
			int e = utf8_next(q, c);
			memmove(q + c, q + e, (size_t)(n - e + 1));
			book_written(server);
		}
		return;
	case XKB_KEY_Left:
		if (q[0]) {
			server->book_caret = utf8_prev(q, c);
			book_changed(server);
		} else {
			binder_riffle(server, -1);
		}
		return;
	case XKB_KEY_Right:
		if (q[0]) {
			server->book_caret = utf8_next(q, c);
			book_changed(server);
		} else {
			binder_riffle(server, 1);
		}
		return;
	case XKB_KEY_Home:
		server->book_caret = 0;
		book_changed(server);
		return;
	case XKB_KEY_End:
		server->book_caret = n;
		book_changed(server);
		return;
	case XKB_KEY_Up:
		book_choose(server, -1);
		return;
	case XKB_KEY_Down:
		book_choose(server, 1);
		return;
	case XKB_KEY_Page_Up:
		binder_riffle(server, -1);
		return;
	case XKB_KEY_Page_Down:
		binder_riffle(server, 1);
		return;
	default:
		if (utf8 && (unsigned char)utf8[0] >= 0x20 && utf8[0] != 0x7f
			&& !(mods & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO))) {
			book_insert(server, utf8);
			book_written(server);
		}
		return;
	}
}

/* what the input method sends back */

static void book_ime_commit(const char *text, void *user)
{
	struct desk_server *server = user;
	if (server->shelf_binder != DESK_SHELF_BOOK) {
		return;
	}
	book_insert(server, text);
	server->book_pre[0] = '\0';
	book_written(server);
}

static void book_ime_preedit(const char *text, int cursor, bool visible, void *user)
{
	struct desk_server *server = user;
	snprintf(server->book_pre, sizeof server->book_pre, "%s", visible ? text : "");
	/* its caret is counted in letters */
	int b = 0;
	for (int i = 0; i < cursor && server->book_pre[b]; i++) {
		b = utf8_next(server->book_pre, b);
	}
	server->book_pre_caret = b;
	if (server->shelf_binder == DESK_SHELF_BOOK) {
		book_changed(server);
	}
}

static void book_ime_lookup(const char *const *cands, int n, int cursor, int page, bool visible,
	void *user)
{
	struct desk_server *server = user;
	server->book_ncand = 0;
	if (visible && n > 0) {
		/* the page the picked one is on */
		page = page > 0 ? (page > 16 ? 16 : page) : 9;
		cursor = cursor < 0 ? 0 : (cursor >= n ? n - 1 : cursor);
		int first = cursor / page * page;
		for (int i = first; i < n && server->book_ncand < page; i++) {
			snprintf(server->book_cand[server->book_ncand], sizeof server->book_cand[0], "%s",
				cands[i]);
			server->book_cand_p[server->book_ncand] = server->book_cand[server->book_ncand];
			server->book_ncand++;
		}
		server->book_cand_cursor = cursor - first;
		server->book_cand_page = page;
	}
	if (server->shelf_binder == DESK_SHELF_BOOK) {
		book_changed(server);
	}
}

static void book_ime_key(uint32_t sym, uint32_t keycode, uint32_t mods, bool taken, bool answer,
	void *user)
{
	(void)keycode;
	struct desk_server *server = user;
	char utf8[8] = "";
	if (answer) {
		/* the oldest key handed over: what it would type */
		if (server->book_keyq_n > 0) {
			memcpy(utf8, server->book_keyq[0].utf8, sizeof utf8);
			server->book_keyq_n--;
			memmove(&server->book_keyq[0], &server->book_keyq[1],
				sizeof server->book_keyq[0] * (size_t)server->book_keyq_n);
		}
	} else {
		xkb_keysym_to_utf8(sym, utf8, sizeof utf8);
	}
	if (!taken && server->shelf_binder == DESK_SHELF_BOOK) {
		book_plain(server, sym, utf8, mods);
	}
}

/* in and out, with binder_open and binder_close */

static void book_begin(struct desk_server *server)
{
	server->book_q[0] = '\0';
	server->book_caret = 0;
	book_clear_ime(server);
	server->book_sel = -1;
	server->book_tab = -1;
	memset(server->book_cursor, 0, sizeof server->book_cursor);
	static const struct desk_ime_typing hooks = { book_ime_commit, book_ime_preedit,
		book_ime_lookup, book_ime_key, NULL };
	struct desk_ime_typing h = hooks;
	h.user = server;
	server->book_ime = desk_ime_typing_begin(&h);
	book_tell_cursor(server);
}

static void book_end(struct desk_server *server)
{
	/* by who has the typing, not by what beginning it said: it is ours
	 * even when the input method was not there yet to type through */
	if (desk_ime_typing_owner() == server) {
		desk_ime_typing_end();
	}
	server->book_ime = false;
	book_clear_ime(server);
	server->book_tab = -1;
}

/* the keys and the pointer, while it is open */

static bool book_has_keys(const struct desk_server *server)
{
	return shelf_up(server) && server->shelf_binder == DESK_SHELF_BOOK
		&& server->binder_to > 0.5;
}

static void book_key(struct desk_server *server, struct xkb_state *xkb, uint32_t keycode,
	uint32_t sym, uint32_t mods, bool released)
{
	char utf8[8] = "";
	xkb_state_key_get_utf8(xkb, keycode + 8, utf8, sizeof utf8);
	if (released) {
		if (server->book_ime) {
			desk_ime_typing_key(sym, keycode, mods, true);
		}
		return;
	}
	if (server->book_ime && desk_ime_typing_key(sym, keycode, mods, false)) {
		if (server->book_keyq_n < (int)(sizeof server->book_keyq / sizeof server->book_keyq[0])) {
			memcpy(server->book_keyq[server->book_keyq_n].utf8, utf8, sizeof utf8);
			server->book_keyq_n++;
		}
		return;
	}
	book_plain(server, sym, utf8, mods);
}

/* Take line i of the index, as a binder's sheet is taken: a sheet comes up
 * off the page at the line's mark and goes down to the desk with you. */
static void book_take(struct desk_server *server, int i)
{
	static int idx[2048];
	int n = desk_book_matches(server->book_q, idx, 2048);
	if (i < 0 || i >= n || i >= 2048) {
		return;
	}
	const struct desk_app_list *l = desk_apps_acquire();
	bool taken = false;
	if (idx[i] < l->n) {
		const struct desk_app *app = &l->apps[idx[i]];
		wlr_log(WLR_INFO, "taking %s out of the book", app->id);
		struct desk_book_look lk;
		book_look(server, &lk);
		double cx, cy, ew, eh, a;
		desk_book_entry_pose(server->desk_w, server->desk_h, &lk, i, &cx, &cy, &ew, &eh, &a);
		/* the sheet a binder would have for it, printed at a binder's size
		 * and starting out as small as the line's mark */
		double sx, sy, sw, sh, sa;
		desk_binder_sheet_pose(server->desk_w, server->desk_h, 0.0, false, &sx, &sy, &sw,
			&sh, &sa);
		/* numbered as its sheet in the binder it is filed in is, and as the
		 * book's plate for it has it */
		const struct desk_shelf_info *si = &l->shelf[app->shelf];
		char code[32];
		char key[4] = { 0 };
		for (int k = 0; k < 3 && si->key[k]; k++) {
			key[k] = (char)toupper((unsigned char)si->key[k]);
		}
		snprintf(code, sizeof code, "%s\xc2\xb7%03d", key, idx[i] - si->first + 1);
		cairo_surface_t *face = server->take.base < 0 && server->conf.take_sheet
			? desk_app_sheet_surface(app->id, sw, sh, code) : NULL;
		int b = face ? base_new(server, app, face, sw, sh) : -1;
		if (b >= 0) {
			double now = monotonic_sec();
			double w0 = eh * 1.6;
			snd_at(server, DESK_SND_SHEET_TAKE, cx, 1.0);
			server->take.base = b;
			server->take.idx = -1;
			server->take.start = now;
			server->take.turn_at = now + TAKE_TURN;
			server->take.land_at = now + TAKE_TURN + 0.40;
			server->take.turned = false;
			server->take.cx0 = cx - (ew - w0) * 0.5 * cos(a);
			server->take.cy0 = cy - (ew - w0) * 0.5 * sin(a);
			server->take.w0 = w0;
			server->take.h0 = w0 * sh / fmax(sw, 1.0);
			server->take.a0 = a;
			taken = true;
		}
		pid_t sid = desk_app_launch(app);
		if (b >= 0) {
			server->bases[b].sid = sid > 0 ? sid : 0;
		}
	}
	desk_apps_release(l);
	/* You turn to the desk with the sheet in your hand: the book is put
	 * back while you are not looking (shelf_tick), not shut in front of you
	 * as you go -- a moment you would not see the end of. Meanwhile it
	 * takes no more keys or clicks. */
	book_end(server);
	server->binder_sel = -1;
	server->binder_to = 0.0;
	server->binder_putback = true;
	if (!taken && server->shelf_t > 0.5) {
		shelf_toggle(server);
	}
	paint_shelf(server);
	server_schedule_frames(server);
}

/* the book or binder left open, back on the shelf at once */
static void binder_put_back(struct desk_server *server)
{
	server->binder_putback = false;
	if (server->shelf_binder < 0) {
		return;
	}
	server->shelf_binder = -1;
	desk_binder_forget();
	book_kept_forget(server);
	server->binder_t = 0.0;
	server->binder_to = 0.0;
	server->binder_moving = 0;
	server->binder_sel = -1;
	paint_shelf(server);
}

static void book_hover_at(struct desk_server *server, double x, double y)
{
	if (server->binder_to < 0.5) {
		return;
	}
	struct desk_book_look lk;
	book_look(server, &lk);
	int arg = -1;
	int z = server->binder_moving ? DESK_BOOK_HIT_NONE
		: desk_book_hit(server->desk_w, server->desk_h, &lk, x, y, &arg);
	int sel = z == DESK_BOOK_HIT_ENTRY && !server->binder_pos_moving ? arg : -1;
	int tab = z == DESK_BOOK_HIT_TAB ? arg : -1;
	if (sel != server->binder_sel || tab != server->book_tab) {
		server->binder_sel = sel;
		server->book_tab = tab;
		paint_shelf(server);
		server_schedule_frames(server);
	}
}

static void book_press(struct desk_server *server, double x, double y)
{
	if (server->binder_moving || server->binder_to < 0.5) {
		return;
	}
	struct desk_book_look lk;
	book_look(server, &lk);
	int arg = -1;
	int z = desk_book_hit(server->desk_w, server->desk_h, &lk, x, y, &arg);
	switch (z) {
	case DESK_BOOK_HIT_ENTRY:
		book_take(server, arg);
		break;
	case DESK_BOOK_HIT_TAB: {
		int sp = desk_book_tab_spread(server->desk_w, server->desk_h, server->book_q, arg);
		if (sp >= 0) {
			binder_turn_to(server, sp);
		}
		break;
	}
	case DESK_BOOK_HIT_PREV:
		binder_riffle(server, -1);
		break;
	case DESK_BOOK_HIT_NEXT:
		binder_riffle(server, 1);
		break;
	case DESK_BOOK_HIT_NONE:
		binder_close(server);
		break;
	default:
		break;
	}
}

/* ---- a binder sheet's corner -------------------------------------
 *
 * The sheet the hand is at has its top right corner turned down, as a
 * sheet on the desk and a letter in the post box have: the hand resting on
 * its fold stirs it, held there or clicked it turns up, and on its back
 * are what can be done with the program printed on the sheet -- open it,
 * or put it in the drawer (take it out, if it is there already). */

static bool drawer_has_app(const struct desk_server *server, const char *id);
static void drawer_keep_app(struct desk_server *server, const char *id, bool keep);

static void bnc_reset(struct desk_server *server)
{
	server->bnc_idx = -1;
	server->bnc_s = 0.0;
	server->bnc_v = 0.0;
	server->bnc_at = 0.0;
	server->bnc_lift = 0.0;
	server->bnc_near = false;
	server->bnc_hover_at = 0.0;
	server->bnc_hot = -1;
	server->bnc_act = -1;
}

/* The program printed on sheet idx of the open binder. */
static bool binder_app_id(struct desk_server *server, int idx, char *out, size_t n)
{
	bool ok = false;
	const struct desk_app_list *l = desk_apps_acquire();
	if (server->shelf_binder >= 0 && server->shelf_binder < DESK_SHELF_COUNT) {
		const struct desk_shelf_info *si = &l->shelf[server->shelf_binder];
		if (idx >= 0 && idx < si->count) {
			snprintf(out, n, "%s", l->apps[si->first + idx].id);
			ok = true;
		}
	}
	desk_apps_release(l);
	return ok;
}

static bool bnc_in_drawer(struct desk_server *server, int idx)
{
	char id[96];
	return idx >= 0 && binder_app_id(server, idx, id, sizeof id) && drawer_has_app(server, id);
}

/* Where the hand is on the corner of the sheet it is at. */
static void bnc_local(struct desk_server *server, double *dx, double *ly, double *lx)
{
	double sw, sh;
	desk_binder_local(server->desk_w, server->desk_h, server->binder_pos, server->bnc_idx,
		true, server->cursor->x, server->cursor->y, lx, ly, &sw, &sh);
	*dx = sw - *lx;
}

static void bnc_track(struct desk_server *server)
{
	if (server->bnc_act >= 0 || server->binder_pos_moving || server->shelf_binder < 0) {
		return;
	}
	int sel = server->binder_sel;
	double rest, open;
	desk_binder_curl_sizes(server->desk_w, server->desk_h, &rest, &open);
	if (sel >= 0 && sel != server->bnc_idx) {
		bnc_reset(server);
		server->bnc_idx = sel;
		server->bnc_s = rest;
	}
	bool near = false, on_fold = false;
	int hot = -1;
	if (sel >= 0) {
		double dx, ly, lx;
		bnc_local(server, &dx, &ly, &lx);
		double zone = rest * 2.6;
		on_fold = dx >= -8.0 && dx <= zone && ly >= -8.0 && ly <= zone;
		near = server->bnc_near && dx >= -8.0 && dx <= open * 1.30 && ly >= -8.0
			&& ly <= open * 1.30;
		if (near) {
			hot = desk_binder_curl_hit(server->desk_w, server->desk_h, server->bnc_s,
				bnc_in_drawer(server, sel), lx, ly);
		}
	}
	if (!near && on_fold) {
		if (server->bnc_hover_at <= 0.0) {
			server->bnc_hover_at = real_sec();
		}
	} else {
		server->bnc_hover_at = 0.0;
	}
	if (near != server->bnc_near || hot != server->bnc_hot || on_fold) {
		server->bnc_near = near;
		server->bnc_hot = hot;
		paint_shelf(server);
		server_schedule_frames(server);
	}
}

/* A click at the open binder: on the back of the turned-up corner, on the
 * corner's fold, or neither (false: the sheet is taken). */
static bool bnc_press(struct desk_server *server)
{
	if (server->bnc_act >= 0) {
		return true;
	}
	int sel = server->binder_sel;
	if (sel < 0 || sel != server->bnc_idx) {
		return false;
	}
	if (server->bnc_near && server->bnc_hot >= 0) {
		server->bnc_act = server->bnc_hot;
		desk_sound_play(DESK_SND_CORNER_TAP, 0.2, 1.0, 0.0);
		server->bnc_act_at = monotonic_sec();
		if (server->bnc_act == DESK_BINDER_DRAWER) {
			char id[96];
			if (binder_app_id(server, sel, id, sizeof id)) {
				drawer_keep_app(server, id, !drawer_has_app(server, id));
			}
		}
		paint_shelf(server);
		server_schedule_frames(server);
		return true;
	}
	double rest, open, dx, ly, lx;
	desk_binder_curl_sizes(server->desk_w, server->desk_h, &rest, &open);
	bnc_local(server, &dx, &ly, &lx);
	if (server->bnc_near) {
		/* on the back of the flap, between the words: nothing */
		return dx >= -8.0 && dx <= open && ly >= -8.0 && ly <= open;
	}
	if (dx >= -8.0 && dx <= rest * 2.6 && ly >= -8.0 && ly <= rest * 2.6) {
		desk_sound_play(DESK_SND_CORNER_LIFT, 0.2, 1.0, 0.0);
		server->bnc_near = true;
		server->bnc_hover_at = 0.0;
		server->bnc_hot = -1;
		server_schedule_frames(server);
		return true;
	}
	return false;
}

/* Once a frame: the corner goes where it is going, on the spring a sheet's
 * corner has, and what was pressed on it is done. */
static void bnc_tick(struct desk_server *server)
{
	if (server->bnc_idx < 0) {
		return;
	}
	if (server->shelf_binder < 0 || !shelf_up(server)) {
		bnc_reset(server);
		return;
	}
	double rest, open;
	desk_binder_curl_sizes(server->desk_w, server->desk_h, &rest, &open);
	double now = monotonic_sec();
	bool more = false;
	if (!server->bnc_near && server->bnc_hover_at > 0.0) {
		if (real_sec() - server->bnc_hover_at >= 0.5 && server->conf.corner_hover) {
			desk_sound_play(DESK_SND_CORNER_LIFT, 0.2, 1.0, 0.0);
			server->bnc_near = true;
			server->bnc_hover_at = 0.0;
		} else {
			more = true;
		}
	}
	double to = server->bnc_near ? open : (server->bnc_hover_at > 0.0 ? rest * 1.7 : rest);
	if (server->bnc_act >= 0) {
		double e = now - server->bnc_act_at;
		if (server->bnc_act == DESK_BINDER_OPEN) {
			/* laid down again, and the sheet is taken */
			to = rest;
			if (e > 0.14) {
				int idx = server->bnc_idx;
				bnc_reset(server);
				binder_take(server, idx);
				return;
			}
		} else {
			/* a nod of the corner: it is done, and the words have changed */
			to = open * 0.86;
			if (e > 0.16) {
				server->bnc_act = -1;
			}
		}
		more = true;
	}
	double s0 = server->bnc_s;
	int hot0 = server->bnc_hot;
	double dt = server->bnc_at > 0.0 ? clampf_d(now - server->bnc_at, 0.0, 0.05) : 0.0;
	server->bnc_at = now;
	if (server->bnc_s <= 0.0) {
		server->bnc_s = rest;
	}
	const double k = 240.0, c = 2.0 * 0.60 * 15.49;
	double acc = k * (to - server->bnc_s) - c * server->bnc_v;
	server->bnc_v += acc * dt;
	server->bnc_s += server->bnc_v * dt;
	if (gfx_flat()) {
		server->bnc_s = to;
		server->bnc_v = 0.0;
	}
	if (fabs(to - server->bnc_s) < 0.15 && fabs(server->bnc_v) < 2.0) {
		server->bnc_s = to;
		server->bnc_v = 0.0;
	} else {
		more = true;
	}
	server->bnc_lift = clampf_d((server->bnc_s - rest) / fmax(open - rest, 1.0), 0.0, 1.0);
	/* the words move with the flap, under a pointer that may not */
	if (server->bnc_near && server->bnc_act < 0 && server->binder_sel == server->bnc_idx) {
		double dx, ly, lx;
		bnc_local(server, &dx, &ly, &lx);
		server->bnc_hot = desk_binder_curl_hit(server->desk_w, server->desk_h,
			server->bnc_s, bnc_in_drawer(server, server->bnc_idx), lx, ly);
	}
	if (server->bnc_s != s0 || server->bnc_hot != hot0) {
		paint_shelf(server);
	}
	if (more) {
		server_schedule_frames(server);
	}
}

/* ---- the desk calendar, close up ------------------------------------
 *
 * Clicked on the shelf, you step up to the bookcase and take it: the view
 * closes in a little on where it stood, the bookcase going soft as the eye
 * comes to the calendar in your hand, which comes up to the middle of the
 * view -- the picture drawn close up, laid over the little one on the
 * shelf at first and scaled up from there. The bookcase is a picture too,
 * taken as you reach for it, sharp and blurred: the step in is both
 * scaled about the calendar, and the one fading into the other. All of it
 * moves are the scene graph's, and cost no drawing. A click on the top
 * half of the page brings the last month back over the wire, on the
 * bottom half sends this one over to the next; anywhere else puts it
 * back. The edge the pointer is at stands up a little, the way a page
 * does when a finger is put under it. */

#define CAL_OPEN_S 0.42
#define CAL_SHUT_S 0.34
#define CAL_TURN_S 0.52
#define CAL_LIFT_S 0.14
#define CAL_SWITCH_S 0.56  /* a bundle over, and a little longer the thicker */
#define CAL_STEP_IN 0.09   /* how much nearer the bookcase comes */
#define CAL_HELD_YAW 0.30  /* held up on its own: turned, its right side away */
#define CAL_ASIDE_YAW 0.10 /* and with the binder beside it, turned to it */
#define CAL_HELD_PITCH 0.12 /* leaning back a little */

static bool cal_up(const struct desk_server *server)
{
	return server->cal_t > 0.0005 || server->cal_moving;
}

/* close up and still: what the page is for */
static bool cal_settled(const struct desk_server *server)
{
	return server->cal_to > 0.5 && !server->cal_moving;
}

struct cal_args {
	struct desk_cal_look look;
	time_t now;
};

static void cal_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct cal_args *a = data;
	desk_draw_calendar(cr, w, h, &a->look, a->now);
}

static bool cal_switching(const struct desk_server *server)
{
	return server->cal_to_view != server->cal_view;
}

static double cal_switch_t(const struct desk_server *server)
{
	if (!cal_switching(server)) {
		return 0.0;
	}
	return clampf_d((monotonic_sec() - server->cal_switch_start) / server->cal_switch_dur, 0, 1);
}

static double cal_turn_t(const struct desk_server *server)
{
	if (!server->cal_turn) {
		return 0.0;
	}
	return clampf_d((monotonic_sec() - server->cal_turn_start) / server->cal_turn_dur, 0, 1);
}

/* Where the calendar close up is on the screen: a point of it at x, y is
 * at x * xf[0] + xf[1], y * xf[0] + xf[2] -- in the middle of the view,
 * or gone aside for the binder of notes, as far as that has got. */
static void cal_aside(const struct desk_server *server, double xf[3])
{
	xf[0] = 1.0;
	xf[1] = xf[2] = 0.0;
	double t = clampf_d(server->agd_t, 0.0, 1.0);
	if (t <= 0.0) {
		return;
	}
	double b[4], cal[3];
	desk_agenda_layout(server->desk_w, server->desk_h, b, cal);
	xf[0] = 1.0 + (cal[0] - 1.0) * t;
	xf[1] = cal[1] * t;
	xf[2] = cal[2] * t;
}

/* a point on the screen, as a point of the calendar close up */
static void cal_unaside(const struct desk_server *server, double *x, double *y)
{
	double xf[3];
	cal_aside(server, xf);
	*x = (*x - xf[1]) / xf[0];
	*y = (*y - xf[2]) / xf[0];
}

/* The step in towards something on the lower board, t of the way: the
 * pictures of the bookcase (sharp, soft) scaled about zx, zy, where it
 * stood, the soft one coming over the sharp. How much nearer it is. */
static double step_in_place(struct desk_server *server, double t, double zx, double zy,
	struct wlr_scene_buffer *sharp, struct wlr_scene_buffer *soft)
{
	int w = server->desk_w, h = server->desk_h;
	double z = 1.0 + CAL_STEP_IN * t;
	struct wlr_scene_buffer *bk[2] = { sharp, soft };
	for (int i = 0; i < 2; i++) {
		if (!bk[i]) {
			continue;
		}
		int pw = w, ph = h;
		scene_pic_size(bk[i], &pw, &ph);
		struct wlr_fbox all = { 0.0, 0.0, (double)pw, (double)ph };
		desk_screen_lay(server, bk[i], all, (int)lround(zx - zx * z), (int)lround(zy - zy * z),
			(int)ceil(w * z), (int)ceil(h * z), 0, 0);
		wlr_scene_node_set_enabled(&bk[i]->node, t > 0.001);
	}
	if (soft) {
		wlr_scene_buffer_set_opacity(soft, (float)t);
	}
	return z;
}

/* Lay the picture where cal_t has got to: its page over the face of the
 * calendar on the shelf at 0, in the middle of the view at 1 -- or aside
 * for the binder. */
static void cal_place(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	double t = clampf_d(server->cal_t, 0.0, 1.0);
	/* the step in: the bookcase scaled about where the calendar stood */
	double fb[4];
	bool have_fb = desk_shelf_gadget_box(w, h, DESK_GADGET_CALENDAR, fb);
	double zx = have_fb ? fb[0] + fb[2] * 0.5 : w * 0.5;
	double zy = have_fb ? fb[1] + fb[3] * 0.5 : h * 0.5;
	double z = step_in_place(server, t, zx, zy, server->cal_sharp, server->cal_soft);
	struct desk_sprite *sp = &server->cal_sp;
	if (!sp->node || !sp->drawn) {
		return;
	}
	double pg[4], box[4], xf[3];
	desk_calendar_rect(w, h, pg, box);
	cal_aside(server, xf);
	double k = xf[0], dx = xf[1], dy = xf[2];
	if (t < 0.9995 && have_fb) {
		/* from the face on the stepped-in shelf -- the page and its tabs;
		 * the box has two pixels to spare all round */
		double fx = zx + (fb[0] + 2.0 - zx) * z, fy = zy + (fb[1] + 2.0 - zy) * z;
		double fw = (fb[2] - 4.0) * z, cw = pg[2] * desk_calendar_face_ratio();
		double tx = pg[0] * xf[0] + xf[1], ty = pg[1] * xf[0] + xf[2];
		double rx = fx + (tx - fx) * t, ry = fy + (ty - fy) * t;
		double rw = fw + (cw * xf[0] - fw) * t;
		k = rw / cw;
		dx = rx - pg[0] * k;
		dy = ry - pg[1] * k;
	}
	wlr_scene_node_set_position(&sp->node->node, (int)lround(sp->x * k + dx),
		(int)lround(sp->y * k + dy));
	int dw = (int)lround(server->cal_nat_w * k), dh = (int)lround(server->cal_nat_h * k);
	wlr_scene_buffer_set_dest_size(sp->node, dw > 1 ? dw : 1, dh > 1 ? dh : 1);
	/* it lifts off the one on the shelf rather than being pasted over it */
	wlr_scene_buffer_set_opacity(sp->node, (float)clampf_d(t * 5.0, 0.0, 1.0));
}

static void cal_look_now(const struct desk_server *server, struct desk_cal_look *lk)
{
	memset(lk, 0, sizeof *lk);
	lk->view = server->cal_view;
	lk->year = server->cal_year;
	lk->month = server->cal_month;
	lk->day = server->cal_day;
	lk->turn = server->cal_turn;
	lk->turn_t = cal_turn_t(server);
	lk->lift = server->cal_lift;
	lk->lift_t = server->cal_lift_t;
	lk->to_view = server->cal_to_view;
	lk->switch_t = cal_switch_t(server);
	lk->hover_tab = server->cal_hover_tab;
	/* Held up, a little turned and leaning back, so that it is seen to be
	 * a tent of boards and not a picture of one: turned more on its own,
	 * where nothing else shows its depth, and less once the binder is
	 * beside it -- off to the side of the eye, its inner side shows
	 * anyway. Coming down off the shelf it turns as it comes. The eye is
	 * over the middle of the screen, wherever the calendar has gone. */
	double up = clampf_d(server->cal_t, 0.0, 1.0);
	up = up * up * (3.0 - 2.0 * up);
	double aside = clampf_d(server->agd_t, 0.0, 1.0);
	lk->yaw = (CAL_HELD_YAW - (CAL_HELD_YAW - CAL_ASIDE_YAW) * aside) * up;
	lk->pitch = CAL_HELD_PITCH * up;
	double xf[3];
	cal_aside(server, xf);
	lk->eye_x = (server->desk_w * 0.5 - xf[1]) / xf[0];
	lk->eye_y = (server->desk_h * 0.5 - xf[2]) / xf[0];
	lk->eye_f = server->desk_h * 1.6 / xf[0];
	/* the notes off the page just now: in the air, being written on in
	 * the binder, being peeled off */
	int n = 0;
	for (int i = 0; i < AGD_FLIGHTS && n < DESK_CAL_HIDE; i++) {
		if (server->agd_fly[i].mode != AGD_FLY_NONE) {
			lk->hide[n++] = server->agd_fly[i].s.id;
		}
	}
	if (server->agd_edit > 0 && n < DESK_CAL_HIDE) {
		lk->hide[n++] = server->agd_edit;
	}
	if (server->agd_gone_id && n < DESK_CAL_HIDE) {
		lk->hide[n++] = server->agd_gone_id;
	}
}

/* The page the calendar will be at once what is going over has gone:
 * where a note sent to it now will be found. */
static void cal_look_settled(const struct desk_server *server, struct desk_cal_look *lk)
{
	cal_look_now(server, lk);
	lk->view = server->cal_to_view;
	lk->to_view = lk->view;
	lk->switch_t = 0.0;
	lk->lift = 0;
	lk->lift_t = 0.0;
	if (server->cal_turn) {
		desk_calendar_step(server->cal_view, server->cal_turn, &lk->year, &lk->month, &lk->day);
	}
	if (server->cal_more) {
		desk_calendar_step(server->cal_view, server->cal_more, &lk->year, &lk->month,
			&lk->day);
	}
	lk->turn = 0;
	lk->turn_t = 0.0;
}

static void agd_fly_raise(struct desk_server *server);

static void paint_calendar(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	if (!cal_up(server) || !shelf_up(server) || w < 8 || h < 8) {
		if (server->cal_tree) {
			wlr_scene_node_set_enabled(&server->cal_tree->node, false);
		}
		return;
	}
	if (!server->cal_tree) {
		server->cal_tree = wlr_scene_tree_create(server->layer_shelf);
	}
	wlr_scene_node_set_enabled(&server->cal_tree->node, true);
	wlr_scene_node_raise_to_top(&server->cal_tree->node);

	struct cal_args a;
	cal_look_now(server, &a.look);
	a.now = time(NULL);
	struct tm tmv;
	localtime_r(&a.now, &tmv);
	uint64_t key = 14695981039346656037ull;
	key = key_add(key, &a.look, sizeof a.look);
	unsigned gen = desk_agenda_generation();
	key = key_add(key, &gen, sizeof gen);
	key = key_add(key, &tmv.tm_yday, sizeof tmv.tm_yday);
	key = key_add(key, &w, sizeof w);
	key = key_add(key, &h, sizeof h);
	struct desk_sprite *sp = &server->cal_sp;
	if (!sp->drawn || sp->key != key) {
		double box[4];
		desk_calendar_extent(w, h, &a.look, box);
		sp->key = key;
		sprite_paint(server, sp, server->cal_tree, box, cal_draw, &a);
		if (sp->node) {
			wlr_scene_node_raise_to_top(&sp->node->node);
		}
		int bw, bh;
		if (sp->node && scene_pic_size(sp->node, &bw, &bh)) {
			int ow, oh;
			desk_overlay_size(w, h, &ow, &oh);
			server->cal_nat_w = (double)bw * w / ow;
			server->cal_nat_h = (double)bh * h / oh;
		}
	}
	cal_place(server);
	agd_fly_raise(server);
}

static double cal_pan(struct desk_server *server)
{
	double fb[4];
	if (!desk_shelf_gadget_box(server->desk_w, server->desk_h, DESK_GADGET_CALENDAR, fb)) {
		return -0.2;
	}
	return snd_pan(server, fb[0] + fb[2] * 0.5);
}

static void cal_move(struct desk_server *server, double to, double dur)
{
	server->cal_from = server->cal_t;
	server->cal_to = to;
	server->cal_start = monotonic_sec();
	server->cal_dur = dur * fmax(fabs(to - server->cal_t), 0.35);
	server->cal_moving = 1;
	server_schedule_frames(server);
	paint_calendar(server);
}

/* A box blur of an ARGB picture, r pixels each way, along and then down
 * (premultiplied, so every channel alike). */
static void cal_box_blur(uint32_t *px, int w, int h, int stride, int r)
{
	uint32_t *line = malloc(sizeof(uint32_t) * (size_t)(w > h ? w : h));
	if (!line) {
		return;
	}
	for (int pass = 0; pass < 2; pass++) {
		int n = pass ? h : w, m = pass ? w : h;
		for (int j = 0; j < m; j++) {
			for (int i = 0; i < n; i++) {
				line[i] = pass ? px[(size_t)i * stride + j] : px[(size_t)j * stride + i];
			}
			for (int i = 0; i < n; i++) {
				unsigned sum[4] = { 0, 0, 0, 0 }, c = 0;
				for (int k = i - r; k <= i + r; k++) {
					uint32_t v = line[k < 0 ? 0 : (k >= n ? n - 1 : k)];
					for (int b = 0; b < 4; b++) {
						sum[b] += (v >> (8 * b)) & 0xffu;
					}
					c++;
				}
				uint32_t o = 0;
				for (int b = 0; b < 4; b++) {
					o |= ((sum[b] + c / 2) / c) << (8 * b);
				}
				if (pass) {
					px[(size_t)i * stride + j] = o;
				} else {
					px[(size_t)j * stride + i] = o;
				}
			}
		}
	}
	free(line);
}

/* The bookcase as it stands, to step in towards: as drawn, and small and
 * blurred (the scene graph stretches it back, which softens it further),
 * put in tree under whatever else is in it. */
static void shelf_snapshot(struct desk_server *server, struct wlr_scene_tree *tree,
	struct wlr_scene_buffer **sharp_slot, struct wlr_scene_buffer **soft_slot)
{
	int w = server->desk_w, h = server->desk_h;
	int ow, oh;
	desk_overlay_size(w, h, &ow, &oh);
	time_t now = time(NULL);
	struct desk_buf *sharp = buf_create(ow, oh);
	cairo_save(sharp->cr);
	cairo_scale(sharp->cr, (double)ow / w, (double)oh / h);
	desk_draw_shelf(sharp->cr, w, h, -1, 0.0, server->shelf_binder);
	desk_draw_shelf_gadgets(sharp->cr, w, h, now);
	cairo_restore(sharp->cr);
	cairo_surface_flush(sharp->surface);
	if (desk_gfx_level() <= DESK_GFX_LOW) {
		/* Low and 2D: the bookcase as it is, not softened behind */
		if (*soft_slot) {
			wlr_scene_node_destroy(&(*soft_slot)->node);
			*soft_slot = NULL;
		}
		scene_put(sharp_slot, tree, sharp, 0, 0, NULL);
		wlr_scene_node_lower_to_bottom(&(*sharp_slot)->node);
		return;
	}

	const int down = 3;
	int sw = ow / down > 1 ? ow / down : 1, sh = oh / down > 1 ? oh / down : 1;
	struct desk_buf *soft = buf_create(sw, sh);
	cairo_save(soft->cr);
	cairo_scale(soft->cr, (double)sw / ow, (double)sh / oh);
	cairo_set_source_surface(soft->cr, sharp->surface, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(soft->cr), CAIRO_FILTER_GOOD);
	cairo_paint(soft->cr);
	cairo_restore(soft->cr);
	cairo_surface_flush(soft->surface);
	cal_box_blur((uint32_t *)cairo_image_surface_get_data(soft->surface), sw, sh,
		cairo_image_surface_get_stride(soft->surface) / 4, 1);
	cairo_surface_mark_dirty(soft->surface);

	scene_put(sharp_slot, tree, sharp, 0, 0, NULL);
	scene_put(soft_slot, tree, soft, 0, 0, NULL);
	/* under the thing in your hand */
	wlr_scene_node_lower_to_bottom(&(*soft_slot)->node);
	wlr_scene_node_lower_to_bottom(&(*sharp_slot)->node);
}

static void cal_snapshot(struct desk_server *server)
{
	if (!server->cal_tree) {
		server->cal_tree = wlr_scene_tree_create(server->layer_shelf);
	}
	shelf_snapshot(server, server->cal_tree, &server->cal_sharp, &server->cal_soft);
}

static void cal_open(struct desk_server *server)
{
	if (cal_up(server) && server->cal_to > 0.5) {
		return;
	}
	/* it comes down at today's page, open at the section it was left at */
	if (!cal_up(server)) {
		time_t now = time(NULL);
		struct tm tmv;
		localtime_r(&now, &tmv);
		server->cal_year = tmv.tm_year + 1900;
		server->cal_month = tmv.tm_mon;
		server->cal_day = tmv.tm_mday;
		server->cal_to_view = server->cal_view;
		server->cal_hover_tab = -1;
		server->cal_turn = 0;
		server->cal_more = 0;
		server->cal_zone = 0;
		server->cal_lift = 0;
		server->cal_lift_t = 0.0;
		server->cal_sp.drawn = false;
		cal_snapshot(server);
	}
	/* the binder the pointer was on goes back into the row */
	hover_set(server, -1);
	desk_sound_play(DESK_SND_SHEET_TAKE, cal_pan(server), 0.8, 1.1);
	cal_move(server, 1.0, CAL_OPEN_S);
}

static void agd_close(struct desk_server *server);
static void agd_drop(struct desk_server *server);
static void agd_fly_clear(struct desk_server *server);

static void cal_close(struct desk_server *server)
{
	if (!cal_up(server) || server->cal_to < 0.5) {
		return;
	}
	agd_close(server);
	/* what is in the air is down at once: the calendar is going back */
	agd_fly_clear(server);
	paint_calendar(server);
	server->cal_zone = 0;
	double dur = CAL_SHUT_S * fmax(server->cal_t, 0.35);
	/* stood back on the board as it gets there */
	desk_sound_play_in(DESK_SND_SHEET_DOWN, cal_pan(server), 0.6, 0.0,
		real_secs(dur) + SND_SEEN);
	cal_move(server, 0.0, CAL_SHUT_S);
}

/* Off the screen at once: the view it was in is going. */
static void cal_drop(struct desk_server *server)
{
	agd_drop(server);
	agd_fly_clear(server);
	server->cal_t = 0.0;
	server->cal_to = 0.0;
	server->cal_moving = 0;
	server->cal_turn = 0;
	server->cal_more = 0;
	server->cal_zone = 0;
	server->cal_lift_t = 0.0;
	server->cal_hover_tab = -1;
	if (cal_switching(server)) {
		server->cal_view = server->cal_to_view;
		desk_calendar_set_view(server->cal_view);
	}
	if (server->cal_tree) {
		wlr_scene_node_set_enabled(&server->cal_tree->node, false);
	}
}

/* Turn a page: +1 on to the next of the section (year, month, week or
 * day), -1 back to the last. Asked again while one is going over, the next
 * goes as soon as it is down, quicker. */
static void cal_turn_page(struct desk_server *server, int dir)
{
	if (!cal_settled(server) || cal_switching(server)) {
		return;
	}
	if (server->cal_turn) {
		server->cal_more = clampf_d(server->cal_more + dir, -4, 4);
		return;
	}
	server->cal_turn = dir;
	server->cal_turn_start = monotonic_sec();
	server->cal_turn_dur = server->cal_more ? CAL_TURN_S * 0.62 : CAL_TURN_S;
	/* the lifted edge is the page going over now */
	server->cal_lift_t = 0.0;
	server->cal_lift = 0;
	double rate = server->cal_turn_dur < CAL_TURN_S ? 1.25 : 1.0;
	desk_sound_play(DESK_SND_PAGE_TURN, cal_pan(server) * 0.3, 1.0, rate);
	server_schedule_frames(server);
	paint_calendar(server);
}

/* Go to section v by its tab: the sections in front of it go over the
 * wire to the back together, or those behind it come back over. */
static void cal_switch(struct desk_server *server, int v)
{
	if (!cal_settled(server) || cal_switching(server) || server->cal_turn
		|| v < 0 || v >= DESK_CAL_VIEWS || v == server->cal_view) {
		return;
	}
	int n = abs(v - server->cal_view);
	server->cal_to_view = v;
	server->cal_switch_start = monotonic_sec();
	server->cal_switch_dur = CAL_SWITCH_S + 0.07 * (n - 1);
	server->cal_lift_t = 0.0;
	server->cal_lift = 0;
	server->cal_zone = 0;
	/* a thumb run along the edges, and the bundle going over */
	desk_sound_play(DESK_SND_LETTER_FLICK, cal_pan(server) * 0.3, 0.8 + 0.15 * n, 1.0);
	desk_sound_play(DESK_SND_PAGE_TURN, cal_pan(server) * 0.3, 1.1, 0.80 - 0.05 * n);
	server_schedule_frames(server);
	paint_calendar(server);
}

static void cal_tick(struct desk_server *server)
{
	if (!cal_up(server)) {
		return;
	}
	double now = monotonic_sec();
	bool changed = false;
	if (server->cal_moving) {
		double u = clampf_d((now - server->cal_start) / server->cal_dur, 0, 1);
		/* up quickly and settling; back down evenly */
		double e = server->cal_to > 0.5 ? 1.0 - pow(1.0 - u, 3.0) : u * u * (3.0 - 2.0 * u);
		server->cal_t = server->cal_from + (server->cal_to - server->cal_from) * e;
		if (u >= 1.0) {
			server->cal_t = server->cal_to;
			server->cal_moving = 0;
		}
		changed = true;
	}
	if (cal_switching(server)) {
		if (cal_switch_t(server) >= 1.0) {
			server->cal_view = server->cal_to_view;
			/* the calendar on the shelf is open there too, now */
			desk_calendar_set_view(server->cal_view);
			paint_shelf(server);
		}
		changed = true;
	}
	if (server->cal_turn) {
		if (cal_turn_t(server) >= 1.0) {
			desk_calendar_step(server->cal_view, server->cal_turn, &server->cal_year,
				&server->cal_month, &server->cal_day);
			server->cal_turn = 0;
			if (server->cal_more) {
				int dir = server->cal_more > 0 ? 1 : -1;
				server->cal_more -= dir;
				cal_turn_page(server, dir);
			}
		}
		changed = true;
	}
	/* the edge under the pointer up, the other down first */
	int want = server->cal_zone == 1 ? -1 : (server->cal_zone == 2 ? 1 : 0);
	if (server->cal_turn || cal_switching(server) || !cal_settled(server)) {
		want = 0;
	}
	double dt = server->cal_lift_at > 0.0 ? now - server->cal_lift_at : 0.0;
	server->cal_lift_at = now;
	double step = fmin(dt, 0.05) / CAL_LIFT_S;
	if (want != 0 && server->cal_lift == want) {
		if (server->cal_lift_t < 1.0) {
			server->cal_lift_t = fmin(1.0, server->cal_lift_t + step);
			changed = true;
		}
	} else if (server->cal_lift_t > 0.0) {
		server->cal_lift_t = fmax(0.0, server->cal_lift_t - step);
		changed = true;
	} else if (server->cal_lift != want) {
		server->cal_lift = want;
		changed = true;
	}
	if (server->cal_t <= 0.0005 && !server->cal_moving) {
		cal_drop(server);
		return;
	}
	if (changed) {
		paint_calendar(server);
		server_schedule_frames(server);
	}
}

#ifndef DESK_HAVE_POST
/* Built without the bus, there is no input method to type through: the
 * binder types the keys itself. */
bool desk_ime_typing_begin(const struct desk_ime_typing *t)
{
	(void)t;
	return false;
}

void desk_ime_typing_end(void)
{
}

void *desk_ime_typing_owner(void)
{
	return NULL;
}

bool desk_ime_typing_key(uint32_t keysym, uint32_t keycode, uint32_t mods, bool released)
{
	(void)keysym;
	(void)keycode;
	(void)mods;
	(void)released;
	return false;
}

void desk_ime_typing_reset(void)
{
}

void desk_ime_typing_cursor(int x, int y, int w, int h)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}
#endif

/* ---- the memo pad, close up ------------------------------------------
 *
 * Clicked on the shelf, it is taken down as the calendar is -- the step
 * in towards the bookcase, the pad coming up to the middle of the view a
 * little turned in the hand -- open at the sheet it was left at, the
 * caret at the end of what is written there. What is typed is written on
 * it (through the input method, as the binder of notes and the book take
 * it). The wheel, Page Up and Page Down turn the sheets over the wire;
 * turning on past the last one written on gives a clean one, as Ctrl+N
 * does. A click on the sheet puts the caret there; off the pad, Escape or
 * the other button put it back on the shelf. What is written is kept a
 * moment after the last change, and whenever a sheet is turned or the pad
 * put back (memo.c). */

#define MEMO_OPEN_S 0.42
#define MEMO_SHUT_S 0.34
#define MEMO_TURN_S 0.46
#define MEMO_HELD_YAW 0.18   /* held to be written on: turned less than the calendar */
#define MEMO_HELD_PITCH 0.10
#define MEMO_KEEP_MS 1000    /* kept this long after the last change */
#define DESK_NOTCH 15.0    /* a wheel's notch, in the units scrolling comes in */

static void memo_close(struct desk_server *server);

static bool memo_up(const struct desk_server *server)
{
	return server->memo_t > 0.0005 || server->memo_moving;
}

static bool memo_settled(const struct desk_server *server)
{
	return server->memo_to > 0.5 && !server->memo_moving;
}

static bool memo_has_keys(const struct desk_server *server)
{
	return shelf_up(server) && memo_up(server) && server->memo_to > 0.5;
}

static double memo_turn_t(const struct desk_server *server)
{
	if (!server->memo_turn) {
		return 0.0;
	}
	return clampf_d((monotonic_sec() - server->memo_turn_start) / MEMO_TURN_S, 0.0, 1.0);
}

static void memo_look(const struct desk_server *server, struct desk_memo_look *lk)
{
	memset(lk, 0, sizeof *lk);
	lk->text = server->memo_text;
	lk->caret = server->memo_caret;
	lk->preedit = server->memo_pre;
	lk->preedit_caret = server->memo_pre_caret;
	lk->cands = server->memo_cand_p;
	lk->ncand = server->memo_ncand;
	lk->cand_cursor = server->memo_cand_cursor;
	lk->sheet = server->memo_sheet;
	lk->sheets = desk_memo_sheets();
	lk->turn = server->memo_turn;
	lk->turn_t = memo_turn_t(server);
	lk->other = server->memo_other;
	lk->other_sheet = server->memo_turn_to;
	/* held up as it comes, a little turned and leaning back, the eye over
	 * the middle of the screen */
	double up = clampf_d(server->memo_t, 0.0, 1.0);
	up = up * up * (3.0 - 2.0 * up);
	lk->yaw = MEMO_HELD_YAW * up;
	lk->pitch = MEMO_HELD_PITCH * up;
	lk->eye_x = server->desk_w * 0.5;
	lk->eye_y = server->desk_h * 0.5;
	lk->eye_f = server->desk_h * 1.6;
}

/* Lay the picture where memo_t has got to: its sheet over the face of the
 * pad on the shelf at 0, in the middle of the view at 1. */
static void memo_place(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	double t = clampf_d(server->memo_t, 0.0, 1.0);
	double fb[4];
	bool have_fb = desk_shelf_gadget_box(w, h, DESK_GADGET_NOTEPAD, fb);
	double zx = have_fb ? fb[0] + fb[2] * 0.5 : w * 0.5;
	double zy = have_fb ? fb[1] + fb[3] * 0.5 : h * 0.5;
	double z = step_in_place(server, t, zx, zy, server->memo_sharp, server->memo_soft);
	struct desk_sprite *sp = &server->memo_sp;
	if (!sp->node || !sp->drawn) {
		return;
	}
	double pg[4], box[4];
	desk_memo_rect(w, h, pg, box);
	double k = 1.0, dx = 0.0, dy = 0.0;
	if (t < 0.9995 && have_fb) {
		/* from the face on the stepped-in shelf (the box has two pixels to
		 * spare all round) */
		double fx = zx + (fb[0] + 2.0 - zx) * z, fy = zy + (fb[1] + 2.0 - zy) * z;
		double fw = (fb[2] - 4.0) * z, cw = pg[2] * desk_memo_face_ratio();
		double rx = fx + (pg[0] - fx) * t, ry = fy + (pg[1] - fy) * t;
		double rw = fw + (cw - fw) * t;
		k = rw / cw;
		dx = rx - pg[0] * k;
		dy = ry - pg[1] * k;
	}
	wlr_scene_node_set_position(&sp->node->node, (int)lround(sp->x * k + dx),
		(int)lround(sp->y * k + dy));
	int dw = (int)lround(server->memo_nat_w * k), dh = (int)lround(server->memo_nat_h * k);
	wlr_scene_buffer_set_dest_size(sp->node, dw > 1 ? dw : 1, dh > 1 ? dh : 1);
	/* it lifts off the one on the shelf rather than being pasted over it */
	wlr_scene_buffer_set_opacity(sp->node, (float)clampf_d(t * 5.0, 0.0, 1.0));
}

struct memo_args {
	struct desk_memo_look look;
};

static void memo_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct memo_args *a = data;
	desk_draw_memo(cr, w, h, &a->look);
}

static void paint_memo(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	if (!memo_up(server) || !shelf_up(server) || w < 8 || h < 8) {
		if (server->memo_tree) {
			wlr_scene_node_set_enabled(&server->memo_tree->node, false);
		}
		return;
	}
	if (!server->memo_tree) {
		server->memo_tree = wlr_scene_tree_create(server->layer_shelf);
	}
	wlr_scene_node_set_enabled(&server->memo_tree->node, true);
	wlr_scene_node_raise_to_top(&server->memo_tree->node);
	struct memo_args a;
	memo_look(server, &a.look);
	uint64_t key = 14695981039346656037ull;
	key = key_add(key, &a.look, sizeof a.look);
	key = key_add(key, server->memo_text, strlen(server->memo_text));
	key = key_add(key, server->memo_pre, strlen(server->memo_pre) + 1);
	for (int i = 0; i < server->memo_ncand; i++) {
		key = key_add(key, server->memo_cand[i], strlen(server->memo_cand[i]) + 1);
	}
	if (server->memo_turn) {
		key = key_add(key, server->memo_other, strlen(server->memo_other));
	}
	key = key_add(key, &w, sizeof w);
	key = key_add(key, &h, sizeof h);
	struct desk_sprite *sp = &server->memo_sp;
	if (!sp->drawn || sp->key != key) {
		double box[4];
		desk_memo_extent(w, h, &a.look, box);
		sp->key = key;
		sprite_paint(server, sp, server->memo_tree, box, memo_draw, &a);
		if (sp->node) {
			wlr_scene_node_raise_to_top(&sp->node->node);
		}
		int bw, bh;
		if (sp->node && scene_pic_size(sp->node, &bw, &bh)) {
			int ow, oh;
			desk_overlay_size(w, h, &ow, &oh);
			server->memo_nat_w = (double)bw * w / ow;
			server->memo_nat_h = (double)bh * h / oh;
		}
	}
	memo_place(server);
}

static double memo_pan(struct desk_server *server)
{
	double fb[4];
	if (!desk_shelf_gadget_box(server->desk_w, server->desk_h, DESK_GADGET_NOTEPAD, fb)) {
		return -0.5;
	}
	return snd_pan(server, fb[0] + fb[2] * 0.5);
}

/* Tell the input method where the caret is. */
static void memo_tell_cursor(struct desk_server *server)
{
	if (!server->memo_ime || !memo_up(server)) {
		return;
	}
	struct desk_memo_look lk;
	memo_look(server, &lk);
	double b[4];
	desk_memo_caret_box(server->desk_w, server->desk_h, &lk, b);
	int ox = 0, oy = 0;
	host_origin(server, &ox, &oy);
	int c[4] = { ox + (int)lround(b[0]), oy + (int)lround(b[1]), (int)lround(fmax(b[2], 1.0)),
		(int)lround(b[3]) };
	if (memcmp(c, server->memo_cursor, sizeof c) != 0) {
		memcpy(server->memo_cursor, c, sizeof c);
		desk_ime_typing_cursor(c[0], c[1], c[2], c[3]);
	}
}

static void memo_changed(struct desk_server *server)
{
	paint_memo(server);
	memo_tell_cursor(server);
	server_schedule_frames(server);
}

/* What is written on the sheet open, into memo.c (which keeps it on disk). */
static void memo_keep_now(struct desk_server *server)
{
	if (server->memo_keep) {
		wl_event_source_timer_update(server->memo_keep, 0);
	}
	if (!server->memo_dirty) {
		return;
	}
	server->memo_dirty = false;
	desk_memo_set(server->memo_sheet, server->memo_text);
	desk_memo_set_top(server->memo_sheet);
}

static int memo_keep_cb(void *data)
{
	memo_keep_now(data);
	return 0;
}

/* Something was written or rubbed out: kept a moment after the last of it. */
static void memo_written(struct desk_server *server)
{
	server->memo_dirty = true;
	if (!server->memo_keep) {
		server->memo_keep = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), memo_keep_cb, server);
	}
	if (server->memo_keep) {
		wl_event_source_timer_update(server->memo_keep, MEMO_KEEP_MS);
	}
	memo_changed(server);
}

static void memo_clear_ime(struct desk_server *server)
{
	server->memo_pre[0] = '\0';
	server->memo_pre_caret = 0;
	server->memo_ncand = 0;
	server->memo_keyq_n = 0;
}

static void memo_repeat_stop(struct desk_server *server)
{
	server->memo_repeat_sym = 0;
	if (server->memo_repeat) {
		wl_event_source_timer_update(server->memo_repeat, 0);
	}
}

/* Whatever the input method was writing is thrown away (the caret moved
 * by the pointer, a sheet turned). */
static void memo_ime_drop(struct desk_server *server)
{
	if (server->memo_ime && (server->memo_pre[0] || server->memo_ncand)) {
		desk_ime_typing_reset();
	}
	memo_clear_ime(server);
}

static void memo_insert(struct desk_server *server, const char *t)
{
	size_t n = strlen(server->memo_text), k = strlen(t);
	if (!k || n + k >= sizeof server->memo_text) {
		return;   /* the sheet is full */
	}
	int c = server->memo_caret;
	memmove(server->memo_text + c + k, server->memo_text + c, n - (size_t)c + 1);
	memcpy(server->memo_text + c, t, k);
	server->memo_caret = c + (int)k;
}

static void memo_move(struct desk_server *server, double to, double dur)
{
	server->memo_from = server->memo_t;
	server->memo_to = to;
	server->memo_start = monotonic_sec();
	server->memo_dur = dur * fmax(fabs(to - server->memo_t), 0.35);
	server->memo_moving = 1;
	server_schedule_frames(server);
	paint_memo(server);
}

/* Turn to sheet `to`, over the wire: on, past those between, or back.
 * Past the last there is a clean one, unless the last is clean already. */
static void memo_turn_to(struct desk_server *server, int to)
{
	if (!memo_settled(server) || server->memo_turn || to < 0) {
		return;
	}
	memo_keep_now(server);
	if (to >= desk_memo_sheets()) {
		to = desk_memo_add();
	}
	if (to == server->memo_sheet) {
		return;
	}
	memo_ime_drop(server);
	memo_repeat_stop(server);
	server->memo_turn = to > server->memo_sheet ? 1 : -1;
	server->memo_turn_to = to;
	server->memo_turn_start = monotonic_sec();
	snprintf(server->memo_other, sizeof server->memo_other, "%s", desk_memo_sheet(to));
	desk_sound_play(DESK_SND_PAGE_TURN, memo_pan(server) * 0.3, 0.9, 1.1);
	memo_changed(server);
}

static void memo_plain(struct desk_server *server, uint32_t sym, const char *utf8,
	uint32_t mods);

static int memo_repeat_cb(void *data)
{
	struct desk_server *server = data;
	if (!server->memo_repeat_sym || !memo_has_keys(server)) {
		return 0;
	}
	memo_plain(server, server->memo_repeat_sym, "", 0);
	wl_event_source_timer_update(server->memo_repeat, 33);
	return 0;
}

/* a key the input method did not take, or there is no input method: what
 * a key does on paper */
static void memo_plain(struct desk_server *server, uint32_t sym, const char *utf8,
	uint32_t mods)
{
	char *t = server->memo_text;
	int n = (int)strlen(t), c = server->memo_caret;
	bool ctrl = mods & WLR_MODIFIER_CTRL;
	bool rep = false, wrote = false;
	/* (a sheet in the air: nothing is written till it is down) */
	switch (sym) {
	case XKB_KEY_Escape:
		memo_close(server);
		return;
	case XKB_KEY_Page_Up:
		memo_turn_to(server, server->memo_sheet - 1);
		return;
	case XKB_KEY_Page_Down:
		memo_turn_to(server, server->memo_sheet + 1);
		return;
	case XKB_KEY_n:
	case XKB_KEY_N:
		if (ctrl) {
			memo_turn_to(server, desk_memo_sheets());
			return;
		}
		break;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		if (!server->memo_turn) {
			memo_insert(server, "\n");
			wrote = true;
		}
		break;
	case XKB_KEY_BackSpace:
		if (c > 0 && !server->memo_turn) {
			int p = utf8_prev(t, c);
			memmove(t + p, t + c, (size_t)(n - c + 1));
			server->memo_caret = p;
			wrote = true;
		}
		rep = true;
		break;
	case XKB_KEY_Delete:
		if (c < n && !server->memo_turn) {
			int e = utf8_next(t, c);
			memmove(t + c, t + e, (size_t)(n - e + 1));
			wrote = true;
		}
		rep = true;
		break;
	case XKB_KEY_Left:
		server->memo_caret = utf8_prev(t, c);
		rep = true;
		break;
	case XKB_KEY_Right:
		server->memo_caret = utf8_next(t, c);
		rep = true;
		break;
	case XKB_KEY_Up:
	case XKB_KEY_Down: {
		struct desk_memo_look lk;
		memo_look(server, &lk);
		server->memo_caret = desk_memo_caret_step(server->desk_w, server->desk_h, &lk,
			sym == XKB_KEY_Up ? -1 : 1);
		rep = true;
		break;
	}
	case XKB_KEY_Home:
		if (ctrl) {
			server->memo_caret = 0;
		} else {
			while (server->memo_caret > 0 && t[server->memo_caret - 1] != '\n') {
				server->memo_caret = utf8_prev(t, server->memo_caret);
			}
		}
		break;
	case XKB_KEY_End:
		if (ctrl) {
			server->memo_caret = n;
		} else {
			while (server->memo_caret < n && t[server->memo_caret] != '\n') {
				server->memo_caret = utf8_next(t, server->memo_caret);
			}
		}
		break;
	default:
		break;
	}
	if (!wrote && !rep && utf8 && (unsigned char)utf8[0] >= 0x20 && utf8[0] != 0x7f
		&& !(mods & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO))
		&& !server->memo_turn && sym != XKB_KEY_Return && sym != XKB_KEY_KP_Enter) {
		memo_insert(server, utf8);
		wrote = true;
	}
	if (rep && server->memo_repeat_sym != sym && sym == server->memo_held_sym && utf8) {
		/* held down, it goes on: a moment's wait, then thirty a second */
		server->memo_repeat_sym = sym;
		if (!server->memo_repeat) {
			server->memo_repeat = wl_event_loop_add_timer(
				wl_display_get_event_loop(server->wl_display), memo_repeat_cb, server);
		}
		if (server->memo_repeat) {
			wl_event_source_timer_update(server->memo_repeat, 450);
		}
	}
	if (wrote) {
		memo_written(server);
	} else {
		memo_changed(server);
	}
}

/* what the input method sends back */

static void memo_ime_commit(const char *text, void *user)
{
	struct desk_server *server = user;
	if (!memo_has_keys(server)) {
		return;
	}
	server->memo_pre[0] = '\0';
	if (!server->memo_turn) {
		memo_insert(server, text);
	}
	memo_written(server);
}

static void memo_ime_preedit(const char *text, int cursor, bool visible, void *user)
{
	struct desk_server *server = user;
	snprintf(server->memo_pre, sizeof server->memo_pre, "%s", visible ? text : "");
	/* its caret is counted in letters */
	int b = 0;
	for (int i = 0; i < cursor && server->memo_pre[b]; i++) {
		b = utf8_next(server->memo_pre, b);
	}
	server->memo_pre_caret = b;
	if (memo_up(server)) {
		memo_changed(server);
	}
}

static void memo_ime_lookup(const char *const *cands, int n, int cursor, int page, bool visible,
	void *user)
{
	struct desk_server *server = user;
	server->memo_ncand = 0;
	if (visible && n > 0) {
		/* the page the picked one is on */
		page = page > 0 ? (page > 16 ? 16 : page) : 9;
		cursor = cursor < 0 ? 0 : (cursor >= n ? n - 1 : cursor);
		int first = cursor / page * page;
		for (int i = first; i < n && server->memo_ncand < page; i++) {
			snprintf(server->memo_cand[server->memo_ncand], sizeof server->memo_cand[0], "%s",
				cands[i]);
			server->memo_cand_p[server->memo_ncand] = server->memo_cand[server->memo_ncand];
			server->memo_ncand++;
		}
		server->memo_cand_cursor = cursor - first;
	}
	if (memo_up(server)) {
		memo_changed(server);
	}
}

static void memo_ime_key(uint32_t sym, uint32_t keycode, uint32_t mods, bool taken, bool answer,
	void *user)
{
	(void)keycode;
	struct desk_server *server = user;
	char utf8[8] = "";
	if (answer) {
		/* the oldest key handed over: what it would type */
		if (server->memo_keyq_n > 0) {
			memcpy(utf8, server->memo_keyq[0].utf8, sizeof utf8);
			server->memo_keyq_n--;
			memmove(&server->memo_keyq[0], &server->memo_keyq[1],
				sizeof server->memo_keyq[0] * (size_t)server->memo_keyq_n);
		}
	} else {
		xkb_keysym_to_utf8(sym, utf8, sizeof utf8);
	}
	if (!taken && memo_has_keys(server)) {
		memo_plain(server, sym, utf8, mods);
	}
}

/* writing on it, from when it is taken down till it is put back */
static void memo_typing_begin(struct desk_server *server)
{
	if (server->memo_typing) {
		return;
	}
	memo_clear_ime(server);
	memset(server->memo_cursor, 0, sizeof server->memo_cursor);
	static const struct desk_ime_typing hooks = { memo_ime_commit, memo_ime_preedit,
		memo_ime_lookup, memo_ime_key, NULL };
	struct desk_ime_typing h = hooks;
	h.user = server;
	server->memo_typing = true;
	server->memo_ime = desk_ime_typing_begin(&h);
	memo_tell_cursor(server);
}

static void memo_typing_end(struct desk_server *server)
{
	memo_repeat_stop(server);
	if (!server->memo_typing) {
		return;
	}
	if (desk_ime_typing_owner() == server) {
		desk_ime_typing_end();
	}
	server->memo_typing = false;
	server->memo_ime = false;
	memo_clear_ime(server);
}

static void memo_key(struct desk_server *server, struct xkb_state *xkb, uint32_t keycode,
	uint32_t sym, uint32_t mods, bool released)
{
	char utf8[8] = "";
	xkb_state_key_get_utf8(xkb, keycode + 8, utf8, sizeof utf8);
	if (released) {
		if (server->memo_ime) {
			desk_ime_typing_key(sym, keycode, mods, true);
		}
		if (sym == server->memo_held_sym) {
			server->memo_held_sym = 0;
		}
		if (sym == server->memo_repeat_sym) {
			memo_repeat_stop(server);
		}
		return;
	}
	/* the input method may answer for it after it is let go: it repeats
	 * only while it is still held (memo_plain) */
	server->memo_held_sym = sym;
	if (server->memo_ime && desk_ime_typing_key(sym, keycode, mods, false)) {
		if (server->memo_keyq_n < (int)(sizeof server->memo_keyq / sizeof server->memo_keyq[0])) {
			memcpy(server->memo_keyq[server->memo_keyq_n].utf8, utf8, sizeof utf8);
			server->memo_keyq_n++;
		}
		return;
	}
	memo_plain(server, sym, utf8, mods);
}

static void memo_snapshot(struct desk_server *server)
{
	if (!server->memo_tree) {
		server->memo_tree = wlr_scene_tree_create(server->layer_shelf);
	}
	shelf_snapshot(server, server->memo_tree, &server->memo_sharp, &server->memo_soft);
}

static void memo_open(struct desk_server *server)
{
	if (memo_up(server) && server->memo_to > 0.5) {
		return;
	}
	if (!memo_up(server)) {
		/* at the sheet it was left at, the caret after what is on it */
		server->memo_sheet = desk_memo_top();
		snprintf(server->memo_text, sizeof server->memo_text, "%s",
			desk_memo_sheet(server->memo_sheet));
		server->memo_caret = (int)strlen(server->memo_text);
		server->memo_dirty = false;
		server->memo_turn = 0;
		server->memo_scroll = 0.0;
		server->memo_sp.drawn = false;
		server->memo_gen_open = desk_memo_generation();
		memo_snapshot(server);
	}
	hover_set(server, -1);
	memo_typing_begin(server);
	desk_sound_play(DESK_SND_SHEET_TAKE, memo_pan(server), 0.8, 1.15);
	memo_move(server, 1.0, MEMO_OPEN_S);
}

static void memo_close(struct desk_server *server)
{
	if (!memo_up(server) || server->memo_to < 0.5) {
		return;
	}
	memo_typing_end(server);
	if (server->memo_turn) {
		/* the sheet in the air comes down where it was going */
		server->memo_sheet = server->memo_turn_to;
		snprintf(server->memo_text, sizeof server->memo_text, "%s", server->memo_other);
		server->memo_turn = 0;
		desk_memo_set_top(server->memo_sheet);
	}
	memo_keep_now(server);
	/* the bookcase it goes back to has what was written on the pad's face */
	if (desk_memo_generation() != server->memo_gen_open) {
		paint_shelf(server);
		memo_snapshot(server);
	}
	double dur = MEMO_SHUT_S * fmax(server->memo_t, 0.35);
	desk_sound_play_in(DESK_SND_SHEET_DOWN, memo_pan(server), 0.6, 0.0,
		real_secs(dur) + SND_SEEN);
	memo_move(server, 0.0, MEMO_SHUT_S);
}

/* Off the screen at once (kept first): the view it was in is going. */
static void memo_drop(struct desk_server *server)
{
	if (memo_up(server)) {
		if (server->memo_turn) {
			server->memo_sheet = server->memo_turn_to;
			snprintf(server->memo_text, sizeof server->memo_text, "%s", server->memo_other);
			server->memo_turn = 0;
			desk_memo_set_top(server->memo_sheet);
		}
		memo_keep_now(server);
	}
	memo_typing_end(server);
	server->memo_t = 0.0;
	server->memo_to = 0.0;
	server->memo_moving = 0;
	server->memo_turn = 0;
	if (server->memo_tree) {
		wlr_scene_node_set_enabled(&server->memo_tree->node, false);
	}
}

static void memo_tick(struct desk_server *server)
{
	if (!memo_up(server)) {
		return;
	}
	double now = monotonic_sec();
	bool changed = false;
	if (server->memo_moving) {
		double u = clampf_d((now - server->memo_start) / server->memo_dur, 0, 1);
		/* up quickly and settling; back down evenly */
		double e = server->memo_to > 0.5 ? 1.0 - pow(1.0 - u, 3.0) : u * u * (3.0 - 2.0 * u);
		server->memo_t = server->memo_from + (server->memo_to - server->memo_from) * e;
		if (u >= 1.0) {
			server->memo_t = server->memo_to;
			server->memo_moving = 0;
		}
		changed = true;
	}
	if (server->memo_turn) {
		if (memo_turn_t(server) >= 1.0) {
			/* down: open at that sheet now, the caret after what is on it */
			server->memo_sheet = server->memo_turn_to;
			snprintf(server->memo_text, sizeof server->memo_text, "%s", server->memo_other);
			server->memo_caret = (int)strlen(server->memo_text);
			server->memo_turn = 0;
			desk_memo_set_top(server->memo_sheet);
		}
		changed = true;
	}
	if (server->memo_t <= 0.0005 && !server->memo_moving) {
		memo_drop(server);
		return;
	}
	if (changed) {
		paint_memo(server);
		memo_tell_cursor(server);
		server_schedule_frames(server);
	}
}

/* A press while it is in your hand: on the sheet, the caret there; off
 * the pad, or the other button, it goes back on the shelf. */
static void memo_press(struct desk_server *server, uint32_t button)
{
	if (!memo_settled(server)) {
		return;
	}
	if (button != BTN_LEFT) {
		memo_close(server);
		return;
	}
	double x = server->cursor->x, y = server->cursor->y;
	struct desk_memo_look lk;
	memo_look(server, &lk);
	desk_memo_unturn(server->desk_w, server->desk_h, &lk, &x, &y);
	int at = desk_memo_hit(server->desk_w, server->desk_h, &lk, x, y);
	if (at < 0) {
		memo_close(server);
		return;
	}
	if (server->memo_turn) {
		return;
	}
	memo_ime_drop(server);
	server->memo_caret = at;
	memo_changed(server);
}

/* the wheel: a sheet a notch, down on and up back */
static void memo_scroll(struct desk_server *server, double d)
{
	if (!memo_settled(server)) {
		return;
	}
	server->memo_scroll += d;
	while (server->memo_scroll >= DESK_NOTCH) {
		server->memo_scroll -= DESK_NOTCH;
		memo_turn_to(server, server->memo_sheet + 1);
	}
	while (server->memo_scroll <= -DESK_NOTCH) {
		server->memo_scroll += DESK_NOTCH;
		memo_turn_to(server, server->memo_sheet - 1);
	}
}

/* ---- the binder of notes, beside the calendar -------------------------
 *
 * A day clicked on the calendar in your hand, the calendar moves over and
 * a binder comes in from the right, open at that day: pads of sticky notes
 * inside its front cover, the day's plans and things to do on the page.
 * The pad picked gives a new note its colour; a space on the page, or just
 * typing, peels one off and sticks it there to be written on -- in
 * Japanese too, through the input method (ime.c), whose candidates the
 * binder shows itself. A plan's time is set on a ruler laid across the
 * binder, or written at the front. Enter sticks it -- peeled off the
 * binder, it goes over to the calendar and is stuck on its day there, and
 * the binder keeps a note of it in pencil -- Escape stops. While the
 * binder is in, the keys are its. */

#define AGD_HELD_YAW (-0.22)   /* held up: turned a little towards the calendar */
#define AGD_HELD_PITCH 0.08
#define AGD_SWING_YAW 0.85     /* and further, still on its way in */
#define AGD_IN_S 0.46
#define AGD_OUT_S 0.30
#define AGD_PEEL_S 0.34
#define AGD_GONE_S 0.30

static bool agd_up(const struct desk_server *server)
{
	return server->agd_t > 0.0005 || server->agd_moving;
}

/* what the note being written is: a plan or a thing to do */
static int agd_edit_kind(const struct desk_server *server)
{
	if (server->agd_edit == DESK_AGENDA_NEW_TODO) {
		return DESK_STICK_TODO;
	}
	struct desk_stick s;
	if (server->agd_edit > 0 && desk_agenda_get(server->agd_edit, &s)) {
		return s.kind;
	}
	return DESK_STICK_PLAN;
}

/* The time of the plan being written: written at the front of it, or else
 * set on the ruler; -1 for none (and for a thing to do). */
static int agd_time(const struct desk_server *server, int *span)
{
	*span = 0;
	if (!server->agd_edit || agd_edit_kind(server) != DESK_STICK_PLAN) {
		return -1;
	}
	int sp, t = desk_agenda_when(server->agd_text, &sp, NULL);
	if (t >= 0) {
		*span = sp;
		return t;
	}
	*span = server->agd_when >= 0 ? server->agd_span : 0;
	return server->agd_when;
}

static void agd_rect(const struct desk_server *server, double r[4]);

static void agd_look(const struct desk_server *server, struct desk_agenda_look *lk)
{
	memset(lk, 0, sizeof *lk);
	lk->year = server->agd_y;
	lk->month = server->agd_m;
	lk->day = server->agd_d;
	lk->colour = server->agd_colour;
	lk->edit = server->agd_edit;
	lk->edit_colour = server->agd_edit_colour;
	lk->text = server->agd_text;
	lk->caret = server->agd_caret;
	lk->preedit = server->agd_pre;
	lk->preedit_caret = server->agd_pre_caret;
	lk->cands = server->agd_cand_p;
	lk->ncand = server->agd_ncand;
	lk->cand_cursor = server->agd_cand_cursor;
	lk->cand_page = server->agd_cand_page;
	lk->hover = server->agd_hover;
	lk->hover_arg = server->agd_hover_arg;
	lk->peel_t = server->agd_peeling
		? clampf_d((monotonic_sec() - server->agd_peel_start) / AGD_PEEL_S, 0, 1) : 1.0;
	lk->gone_id = server->agd_gone_id;
	lk->gone_t = server->agd_gone_id
		? clampf_d((monotonic_sec() - server->agd_gone_start) / AGD_GONE_S, 0, 1) : 0.0;
	lk->ime = server->agd_ime;
	int span;
	lk->when = agd_time(server, &span);
	lk->span = span;
	if (!server->agd_edit) {
		/* the ruler going away still has on it what it was set at */
		lk->when = server->agd_when;
		lk->span = server->agd_span;
	}
	lk->ruler_t = server->agd_ruler_t;
	lk->ruler_mark = server->agd_mark;
	lk->edit_away = server->agd_edit_away;
	/* Held up beside the calendar, a little turned towards it, as seen
	 * from over the middle of the screen. Coming in from the side it
	 * swings round as it comes, its near edge first, and goes back out the
	 * same way: a thing brought in by hand, not a panel slid across. */
	double r[4], cal[3];
	desk_agenda_layout(server->desk_w, server->desk_h, r, cal);
	double t = clampf_d(server->agd_t, 0.0, 1.2);
	double away = fmax(0.0, 1.0 - t);
	lk->yaw = AGD_HELD_YAW - AGD_SWING_YAW * away * away;
	lk->pitch = AGD_HELD_PITCH;
	double now[4];
	agd_rect(server, now);
	lk->eye_x = server->desk_w * 0.5 - now[0];
	lk->eye_y = server->desk_h * 0.5 - now[1];
	lk->eye_f = server->desk_h * 1.6;
}

struct agd_args {
	struct desk_agenda_look look;
	double rect[4];
};

static void agd_draw(cairo_t *cr, int w, int h, void *data)
{
	(void)w;
	(void)h;
	const struct agd_args *a = data;
	cairo_translate(cr, a->rect[0], a->rect[1]);
	desk_draw_agenda(cr, a->rect[2], a->rect[3], &a->look);
}

static void agd_mark_draw(cairo_t *cr, int w, int h, void *data)
{
	(void)w;
	(void)h;
	const struct agd_args *a = data;
	cairo_translate(cr, a->rect[0], a->rect[1]);
	desk_draw_agenda_mark(cr, a->rect[2], a->rect[3], &a->look);
}

/* how far in it has come, eased: in with a little overshoot, out evenly */
static double agd_in(const struct desk_server *server)
{
	return clampf_d(server->agd_t, 0.0, 1.2);
}

/* where it lies now: in place, or as far to the right as it has to come */
static void agd_rect(const struct desk_server *server, double r[4])
{
	double cal[3];
	desk_agenda_layout(server->desk_w, server->desk_h, r, cal);
	r[0] += (1.0 - agd_in(server)) * (server->desk_w - r[0] + r[2] * 0.08);
}

static void paint_agenda(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	struct desk_sprite *sp = &server->agd_sp, *mk = &server->agd_mark_sp;
	if (!agd_up(server) || !cal_up(server) || !server->cal_tree || w < 8 || h < 8) {
		if (sp->node) {
			wlr_scene_node_set_enabled(&sp->node->node, false);
		}
		if (mk->node) {
			wlr_scene_node_set_enabled(&mk->node->node, false);
		}
		return;
	}
	struct agd_args a;
	agd_look(server, &a.look);
	/* The pencil mark where the pointer is on the ruler goes on by itself:
	 * the binder is a whole page drawn and turned into the view a pixel at
	 * a time, and drawn again for every quarter hour the pointer crossed
	 * it was a pointer going along the ruler at a few frames a second. */
	struct agd_args m = a;
	a.look.ruler_mark = -1;
	if (a.look.hover == DESK_AGENDA_HIT_RULER) {
		a.look.hover_arg = 0;
	}
	double cal[3];
	desk_agenda_layout(w, h, a.rect, cal);
	memcpy(m.rect, a.rect, sizeof m.rect);
	/* drawn where it lies when it is in; brought there by moving it */
	uint64_t key = 14695981039346656037ull;
	key = key_add(key, &a.look, sizeof a.look);
	key = key_add(key, server->agd_text, strlen(server->agd_text));
	key = key_add(key, server->agd_pre, strlen(server->agd_pre));
	for (int i = 0; i < server->agd_ncand; i++) {
		key = key_add(key, server->agd_cand[i], strlen(server->agd_cand[i]));
	}
	unsigned gen = desk_agenda_generation();
	key = key_add(key, &gen, sizeof gen);
	key = key_add(key, &w, sizeof w);
	key = key_add(key, &h, sizeof h);
	if (!sp->drawn || sp->key != key) {
		double box[4];
		desk_agenda_extent(a.rect[2], a.rect[3], &a.look, box);
		box[0] += a.rect[0];
		box[1] += a.rect[1];
		sp->key = key;
		sprite_paint(server, sp, server->cal_tree, box, agd_draw, &a);
	}
	if (!sp->node) {
		return;
	}
	wlr_scene_node_raise_to_top(&sp->node->node);
	double r[4];
	agd_rect(server, r);
	int dx = (int)lround(r[0] - a.rect[0]);
	wlr_scene_node_set_position(&sp->node->node, sp->x + dx, sp->y);
	wlr_scene_node_set_enabled(&sp->node->node, server->agd_t > 0.0005);

	double mb[4];
	if (desk_agenda_mark_extent(m.rect[2], m.rect[3], &m.look, mb)) {
		uint64_t mkey = 14695981039346656037ull;
		mkey = key_add(mkey, &m.look, sizeof m.look);
		mkey = key_add(mkey, &w, sizeof w);
		mkey = key_add(mkey, &h, sizeof h);
		if (!mk->drawn || mk->key != mkey) {
			mb[0] += m.rect[0];
			mb[1] += m.rect[1];
			mk->key = mkey;
			sprite_paint(server, mk, server->cal_tree, mb, agd_mark_draw, &m);
		}
		if (mk->node) {
			wlr_scene_node_raise_to_top(&mk->node->node);
			wlr_scene_node_set_position(&mk->node->node, mk->x + dx, mk->y);
			wlr_scene_node_set_enabled(&mk->node->node, server->agd_t > 0.0005);
		}
	} else if (mk->node) {
		wlr_scene_node_set_enabled(&mk->node->node, false);
	}
	agd_fly_raise(server);
}

/* Tell the input method where the note being written is, on the screen
 * its windows go on: this one, or the one DEsk's window is on. */
static void agd_tell_cursor(struct desk_server *server)
{
	if (!server->agd_ime || !server->agd_edit) {
		return;
	}
	struct desk_agenda_look lk;
	agd_look(server, &lk);
	double r[4], n[4];
	agd_rect(server, r);
	if (!desk_agenda_writing(r[2], r[3], &lk, n)) {
		return;
	}
	/* as the binder is held: the box round the note's corners turned */
	double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
	for (int i = 0; i < 4; i++) {
		double x = n[0] + ((i & 1) ? n[2] : 0.0), y = n[1] + ((i & 2) ? n[3] : 0.0);
		desk_agenda_turn_point(r[2], r[3], &lk, &x, &y);
		x0 = fmin(x0, x);
		y0 = fmin(y0, y);
		x1 = fmax(x1, x);
		y1 = fmax(y1, y);
	}
	int ox = 0, oy = 0;
	host_origin(server, &ox, &oy);
	int c[4] = { ox + (int)lround(r[0] + x0), oy + (int)lround(r[1] + y0),
		(int)lround(x1 - x0), (int)lround(y1 - y0) };
	if (memcmp(c, server->agd_cursor, sizeof c) != 0) {
		memcpy(server->agd_cursor, c, sizeof c);
		desk_ime_typing_cursor(c[0], c[1], c[2], c[3]);
	}
}

static void agd_changed(struct desk_server *server)
{
	paint_agenda(server);
	agd_tell_cursor(server);
	server_schedule_frames(server);
}

/* the notes changed: the calendar's pages, here and on the shelf, too */
static void agd_stuck(struct desk_server *server)
{
	paint_calendar(server);
	paint_shelf(server);
	agd_changed(server);
}

static double agd_pan(const struct desk_server *server)
{
	double r[4], cal[3];
	desk_agenda_layout(server->desk_w, server->desk_h, r, cal);
	return clampf_d(((r[0] + r[2] * 0.5) / fmax(server->desk_w, 1) * 2.0 - 1.0) * 0.7, -0.7, 0.7);
}

/* ---- notes in the air ----
 *
 * Written and stuck, a note is peeled off the binder's page -- its foot
 * first, coming up off the paper -- carried over to the calendar along an
 * arc, a little nearer the eye on the way, and pressed down on its day
 * where the page has room for it: as it goes it takes on the look it will
 * have there (agd_fly_state). Clicked to be written again, it comes back
 * off the calendar the same way; peeled off, it comes away and drops. */

#define AGD_FLY_S 0.82
#define AGD_FLY_OFF_S 0.62
#define AGD_FLY_PEEL 0.22   /* of the way: coming up off the paper */
#define AGD_FLY_LAND 0.80   /* and pressed down from here on */

/* A pose from where the corners of pose p went (top left, top right,
 * bottom right, bottom left): as near a turned rectangle as they make. */
static void agd_pose_from_quad(const double q[4][2], const struct desk_note_pose *p,
	struct desk_note_pose *out)
{
	double cx = (q[0][0] + q[1][0] + q[2][0] + q[3][0]) * 0.25;
	double cy = (q[0][1] + q[1][1] + q[2][1] + q[3][1]) * 0.25;
	double nw = (hypot(q[1][0] - q[0][0], q[1][1] - q[0][1])
		+ hypot(q[2][0] - q[3][0], q[2][1] - q[3][1])) * 0.5;
	double nh = (hypot(q[3][0] - q[0][0], q[3][1] - q[0][1])
		+ hypot(q[2][0] - q[1][0], q[2][1] - q[1][1])) * 0.5;
	*out = *p;
	out->rot = atan2(q[1][1] - q[0][1] + q[2][1] - q[3][1], q[1][0] - q[0][0] + q[2][0] - q[3][0]);
	out->font *= p->w > 1e-6 ? nw / p->w : 1.0;
	out->w = nw;
	out->h = nh;
	out->x = cx - nw * 0.5;
	out->y = cy - nh * 0.5;
}

/* a pose on the binder or the calendar, on the screen as they are now */
static void agd_fly_place(const struct desk_server *server, const struct desk_note_pose *p,
	int on, struct desk_note_pose *out)
{
	*out = *p;
	if (on == AGD_ON_BINDER) {
		/* its corners turned with the binder as it is held, where the
		 * binder is now */
		double r[4];
		agd_rect(server, r);
		struct desk_agenda_look lk;
		agd_look(server, &lk);
		double c = cos(p->rot), sn = sin(p->rot);
		double mx = p->x + p->w * 0.5, my = p->y + p->h * 0.5;
		double q[4][2];
		for (int i = 0; i < 4; i++) {
			double u = (i == 1 || i == 2) ? p->w * 0.5 : -p->w * 0.5;
			double v = i >= 2 ? p->h * 0.5 : -p->h * 0.5;
			double x = mx + u * c - v * sn, y = my + u * sn + v * c;
			desk_agenda_turn_point(r[2], r[3], &lk, &x, &y);
			q[i][0] = x + r[0];
			q[i][1] = y + r[1];
		}
		agd_pose_from_quad(q, p, out);
	} else {
		/* its corners turned with the calendar as it is held, then where
		 * the calendar is: as near a turned rectangle as they make */
		struct desk_cal_look lk;
		cal_look_now(server, &lk);
		double xf[3];
		cal_aside(server, xf);
		double c = cos(p->rot), sn = sin(p->rot);
		double mx = p->x + p->w * 0.5, my = p->y + p->h * 0.5;
		double q[4][2];
		for (int i = 0; i < 4; i++) {
			double u = (i == 1 || i == 2) ? p->w * 0.5 : -p->w * 0.5;
			double v = i >= 2 ? p->h * 0.5 : -p->h * 0.5;
			double x = mx + u * c - v * sn, y = my + u * sn + v * c;
			desk_calendar_turn_point(server->desk_w, server->desk_h, &lk, &x, &y);
			q[i][0] = x * xf[0] + xf[1];
			q[i][1] = y * xf[0] + xf[2];
		}
		agd_pose_from_quad(q, p, out);
	}
}

static double agd_smooth(double u)
{
	u = clampf_d(u, 0.0, 1.0);
	return u * u * (3.0 - 2.0 * u);
}

struct agd_fly_args {
	struct desk_stick s;
	struct desk_note_pose at, a, b;
	double look, lift, alpha;
};

/* Where flight f has got to: its paper (at), how much of the look of
 * where it is going it has (look), how far off the paper it is (lift),
 * how faded (alpha). False once it is over. */
static bool agd_fly_state(const struct desk_server *server, const struct agd_flight *f,
	double now, struct agd_fly_args *o)
{
	struct desk_note_pose a, b;
	agd_fly_place(server, &f->a, f->a_on, &a);
	agd_fly_place(server, &f->b, f->b_on, &b);
	o->s = f->s;
	o->a = a;
	o->b = b;
	double u = clampf_d((now - f->start) / f->dur, 0.0, 1.0);
	/* in the binder a note being written lies a little up off the page */
	double base_a = f->a_on == AGD_ON_BINDER ? 0.35 : 0.0;
	double base_b = f->b_on == AGD_ON_BINDER ? 0.35 : 0.0;
	if (f->mode == AGD_FLY_OFF) {
		/* up off the paper, then dropping away, turning, gone */
		double p = agd_smooth(u / 0.30), q = clampf_d((u - 0.30) / 0.70, 0.0, 1.0);
		o->at = a;
		o->at.y += -a.h * 0.30 * p + server->desk_h * 0.45 * q * q;
		o->at.x += a.w * 0.12 * q;
		o->at.rot += -0.10 * p + 0.80 * q * q;
		o->look = 0.0;
		o->lift = base_a + (1.0 - base_a) * p;
		o->alpha = 1.0 - q * q;
		return u < 1.0;
	}
	double p = agd_smooth(u / AGD_FLY_PEEL);
	double q = agd_smooth((u - AGD_FLY_PEEL) / (AGD_FLY_LAND - AGD_FLY_PEEL));
	double l = agd_smooth((u - AGD_FLY_LAND) / (1.0 - AGD_FLY_LAND));
	/* along an arc over from the middle of the one to the middle of the
	 * other, rising the more the farther it goes */
	double x0 = a.x + a.w * 0.5, y0 = a.y + a.h * 0.5;
	double x2 = b.x + b.w * 0.5, y2 = b.y + b.h * 0.5;
	double x1 = (x0 + x2) * 0.5, y1 = fmin(y0, y2) - fabs(x2 - x0) * 0.22 - server->desk_h * 0.04;
	double mx = (1 - q) * (1 - q) * x0 + 2 * (1 - q) * q * x1 + q * q * x2;
	double my = (1 - q) * (1 - q) * y0 + 2 * (1 - q) * q * y1 + q * q * y2;
	/* lifted off before it goes */
	my -= a.h * 0.20 * p * (1.0 - q);
	double near = 1.0 + 0.10 * sin(3.14159265358979 * q) + 0.05 * (1.0 - l) * (q >= 1.0 ? 1.0 : 0.0);
	double w = (a.w + (b.w - a.w) * q) * near, h = (a.h + (b.h - a.h) * q) * near;
	double side = x2 >= x0 ? 1.0 : -1.0;
	o->at.x = mx - w * 0.5;
	o->at.y = my - h * 0.5;
	o->at.w = w;
	o->at.h = h;
	o->at.rot = a.rot + (b.rot - a.rot) * q - 0.07 * p * (1.0 - q) + 0.12 * side * sin(3.14159265358979 * q);
	o->at.font = 0.0;
	o->at.style = b.style;
	o->look = agd_smooth((q - 0.30) / 0.40);
	o->lift = q < 1.0 ? base_a + (1.0 - base_a) * p : 1.0 + (base_b - 1.0) * l;
	o->alpha = 1.0;
	return u < 1.0;
}

static void agd_fly_draw(cairo_t *cr, int w, int h, void *data)
{
	(void)w;
	(void)h;
	const struct agd_fly_args *f = data;
	desk_draw_note_between(cr, &f->s, &f->at, &f->a, &f->b, f->look, f->lift, f->alpha);
}

/* the notes in the air over the calendar and the binder */
static void agd_fly_raise(struct desk_server *server)
{
	for (int i = 0; i < AGD_FLIGHTS; i++) {
		struct agd_flight *f = &server->agd_fly[i];
		if (f->mode != AGD_FLY_NONE && f->sp.node) {
			wlr_scene_node_raise_to_top(&f->sp.node->node);
		}
	}
}

static void agd_fly_paint(struct desk_server *server, struct agd_flight *f, double now)
{
	struct agd_fly_args a;
	if (!server->cal_tree || f->mode == AGD_FLY_NONE) {
		return;
	}
	agd_fly_state(server, f, now, &a);
	/* all of it, turned, and its shadow */
	double cx = a.at.x + a.at.w * 0.5, cy = a.at.y + a.at.h * 0.5;
	double c = fabs(cos(a.at.rot)), sn = fabs(sin(a.at.rot));
	double hw = (a.at.w * c + a.at.h * sn) * 0.5, hh = (a.at.w * sn + a.at.h * c) * 0.5;
	double m = fmin(a.at.w, a.at.h) * 0.45 + 3.0;
	double box[4] = { cx - hw - m, cy - hh - m, hw * 2.0 + m * 2.0, hh * 2.0 + m * 2.5 };
	sprite_paint(server, &f->sp, server->cal_tree, box, agd_fly_draw, &a);
	if (f->sp.node) {
		wlr_scene_node_raise_to_top(&f->sp.node->node);
	}
}

static void agd_fly_end(struct agd_flight *f)
{
	f->mode = AGD_FLY_NONE;
	if (f->sp.node) {
		wlr_scene_node_set_enabled(&f->sp.node->node, false);
	}
	f->sp.drawn = false;
}

static void agd_fly_clear(struct desk_server *server)
{
	for (int i = 0; i < AGD_FLIGHTS; i++) {
		agd_fly_end(&server->agd_fly[i]);
	}
	server->agd_edit_away = false;
}

static bool agd_flying_to(const struct desk_server *server, int mode)
{
	for (int i = 0; i < AGD_FLIGHTS; i++) {
		if (server->agd_fly[i].mode == mode) {
			return true;
		}
	}
	return false;
}

/* Send note s from a (on a_on) to b (on b_on): one already in the air
 * with it is put down first. */
static void agd_fly_start(struct desk_server *server, int mode, const struct desk_stick *s,
	const struct desk_note_pose *a, int a_on, const struct desk_note_pose *b, int b_on)
{
	struct agd_flight *f = NULL;
	for (int i = 0; i < AGD_FLIGHTS; i++) {
		struct agd_flight *g = &server->agd_fly[i];
		if (g->mode != AGD_FLY_NONE && g->s.id == s->id) {
			if (g->mode == AGD_FLY_TO_BINDER) {
				server->agd_edit_away = false;
			}
			agd_fly_end(g);
		}
		if (g->mode == AGD_FLY_NONE && !f) {
			f = g;
		}
	}
	if (!f) {
		/* the oldest lands at once */
		f = &server->agd_fly[0];
		for (int i = 1; i < AGD_FLIGHTS; i++) {
			if (server->agd_fly[i].start < f->start) {
				f = &server->agd_fly[i];
			}
		}
		/* the note being written, if that was it, is in the binder now */
		if (f->mode == AGD_FLY_TO_BINDER && f->s.id == server->agd_edit) {
			server->agd_edit_away = false;
		}
		agd_fly_end(f);
	}
	f->mode = mode;
	f->s = *s;
	f->a = *a;
	f->b = *b;
	f->a_on = a_on;
	f->b_on = b_on;
	f->start = monotonic_sec();
	f->dur = mode == AGD_FLY_OFF ? AGD_FLY_OFF_S : AGD_FLY_S;
	f->snd = 0;
	desk_sound_play(DESK_SND_CORNER_LIFT, agd_pan(server), mode == AGD_FLY_OFF ? 1.0 : 0.8,
		1.1);
	if (mode == AGD_FLY_OFF) {
		desk_sound_play_in(DESK_SND_PAPER_TOSS, agd_pan(server), 0.35, 1.3,
			real_secs(f->dur * 0.3) + SND_SEEN);
	}
	agd_fly_paint(server, f, f->start);
	server_schedule_frames(server);
}

static void agd_fly_tick(struct desk_server *server, double now)
{
	bool any = false;
	for (int i = 0; i < AGD_FLIGHTS; i++) {
		any |= server->agd_fly[i].mode != AGD_FLY_NONE;
	}
	if (!any) {
		return;
	}
	server_schedule_frames(server);
	for (int i = 0; i < AGD_FLIGHTS; i++) {
		struct agd_flight *f = &server->agd_fly[i];
		if (f->mode == AGD_FLY_NONE) {
			continue;
		}
		if (f->mode != AGD_FLY_OFF) {
			/* pressed down where it lands */
			double pan = f->b_on == AGD_ON_BINDER ? agd_pan(server) : cal_pan(server) * 0.5;
			snd_end(&f->snd, f->start, f->dur * AGD_FLY_LAND, DESK_SND_PAPER_PRESS, pan, 0.9,
				1.25);
		}
		struct agd_fly_args a;
		if (agd_fly_state(server, f, now, &a)) {
			agd_fly_paint(server, f, now);
			continue;
		}
		int mode = f->mode;
		agd_fly_end(f);
		if (mode == AGD_FLY_TO_CAL) {
			/* stuck on its day: on the page now, and the one on the shelf */
			paint_calendar(server);
			paint_shelf(server);
		} else if (mode == AGD_FLY_TO_BINDER) {
			server->agd_edit_away = false;
			paint_agenda(server);
		}
	}
}

/* The note being written, as a pose on the binder: false if none is. */
static bool agd_edit_pose(const struct desk_server *server, struct desk_note_pose *p);

/* Note s, just stuck (or put back as it was), from where it is in the
 * binder over to its day on the calendar -- the calendar turned to it
 * first if it is not on the page. */
static void cal_show(struct desk_server *server, int y, int m, int d);

static void agd_send(struct desk_server *server, const struct desk_stick *s,
	const struct desk_note_pose *from)
{
	cal_show(server, s->y, s->m, s->d);
	struct desk_cal_look lk;
	cal_look_settled(server, &lk);
	struct desk_note_pose to;
	if (!desk_calendar_note_pose(server->desk_w, server->desk_h, &lk, s, &to)) {
		/* its day is not on the page after all: into the middle of it */
		double pg[4], box[4];
		desk_calendar_rect(server->desk_w, server->desk_h, pg, box);
		to = (struct desk_note_pose){ pg[0] + pg[2] * 0.5, pg[1] + pg[3] * 0.5, 2.0, 2.0, 0.0,
			0.0, DESK_NOTE_DOT };
	}
	agd_fly_start(server, AGD_FLY_TO_CAL, s, from, AGD_ON_BINDER, &to, AGD_ON_CAL);
}

/* Turn the calendar to the page with day y-m-d on it: over a page or a
 * few, or, farther than that, opened there. */
static void cal_show(struct desk_server *server, int y, int m, int d)
{
	struct desk_cal_look lk;
	cal_look_settled(server, &lk);
	if (desk_calendar_page_has(lk.view, lk.year, lk.month, lk.day, y, m, d)) {
		if (!server->cal_turn && !server->cal_more) {
			/* on this page already: about that day, for the other
			 * sections to open at */
			server->cal_year = y;
			server->cal_month = m;
			server->cal_day = d;
		}
		return;
	}
	if (cal_settled(server) && !cal_switching(server)) {
		for (int n = 1; n <= 4; n++) {
			for (int dir = -1; dir <= 1; dir += 2) {
				int yy = lk.year, mm = lk.month, dd = lk.day;
				desk_calendar_step(lk.view, dir * n, &yy, &mm, &dd);
				if (!desk_calendar_page_has(lk.view, yy, mm, dd, y, m, d)) {
					continue;
				}
				int left = n;
				if (!server->cal_turn) {
					cal_turn_page(server, dir);
					left--;
				}
				if (abs(server->cal_more + dir * left) <= 4) {
					server->cal_more += dir * left;
					return;
				}
				goto jump;
			}
		}
	}
jump:
	server->cal_turn = 0;
	server->cal_more = 0;
	server->cal_year = y;
	server->cal_month = m;
	server->cal_day = d;
	paint_calendar(server);
	server_schedule_frames(server);
}

/* ---- writing on a note ---- */

static int utf8_prev(const char *s, int i)
{
	while (i > 0) {
		i--;
		if (((unsigned char)s[i] & 0xc0) != 0x80) {
			break;
		}
	}
	return i;
}

static int utf8_next(const char *s, int i)
{
	int n = (int)strlen(s);
	if (i >= n) {
		return n;
	}
	i++;
	while (i < n && ((unsigned char)s[i] & 0xc0) == 0x80) {
		i++;
	}
	return i;
}

static void agd_insert(struct desk_server *server, const char *t)
{
	size_t n = strlen(server->agd_text), k = strlen(t);
	if (n + k >= sizeof server->agd_text - 1) {
		return;
	}
	int c = server->agd_caret;
	memmove(server->agd_text + c + k, server->agd_text + c, n - (size_t)c + 1);
	memcpy(server->agd_text + c, t, k);
	server->agd_caret = c + (int)k;
}

static void agd_clear_ime(struct desk_server *server)
{
	server->agd_pre[0] = '\0';
	server->agd_pre_caret = 0;
	server->agd_ncand = 0;
	server->agd_keyq_n = 0;
}

static void agd_repeat_stop(struct desk_server *server)
{
	server->agd_repeat_sym = 0;
	if (server->agd_repeat) {
		wl_event_source_timer_update(server->agd_repeat, 0);
	}
}

/* The note being written, as a pose on the binder: false if none is. */
static bool agd_edit_pose(const struct desk_server *server, struct desk_note_pose *p)
{
	if (!server->agd_edit) {
		return false;
	}
	struct desk_agenda_look lk;
	agd_look(server, &lk);
	double r[4], cal[3];
	desk_agenda_layout(server->desk_w, server->desk_h, r, cal);
	return desk_agenda_note_pose(r[2], r[3], &lk, p);
}

/* Where note id is in the air just now, as a pose on the binder: false if
 * it is not in the air. */
static bool agd_fly_where(const struct desk_server *server, int id,
	struct desk_note_pose *p)
{
	for (int i = 0; i < AGD_FLIGHTS; i++) {
		const struct agd_flight *f = &server->agd_fly[i];
		struct agd_fly_args a;
		if (f->mode == AGD_FLY_NONE || f->s.id != id) {
			continue;
		}
		agd_fly_state(server, f, monotonic_sec(), &a);
		/* where it is, taken back into the binder's own terms */
		double r[4];
		agd_rect(server, r);
		struct desk_agenda_look lk;
		agd_look(server, &lk);
		double c = cos(a.at.rot), sn = sin(a.at.rot);
		double mx = a.at.x + a.at.w * 0.5, my = a.at.y + a.at.h * 0.5;
		double q[4][2];
		for (int k = 0; k < 4; k++) {
			double u = (k == 1 || k == 2) ? a.at.w * 0.5 : -a.at.w * 0.5;
			double v = k >= 2 ? a.at.h * 0.5 : -a.at.h * 0.5;
			double x = mx + u * c - v * sn - r[0], y = my + u * sn + v * c - r[1];
			desk_agenda_unturn(r[2], r[3], &lk, &x, &y);
			q[k][0] = x;
			q[k][1] = y;
		}
		struct desk_note_pose was = a.at;
		was.font = f->b_on == AGD_ON_BINDER ? f->b.font : f->a.font;
		agd_pose_from_quad(q, &was, p);
		p->style = DESK_NOTE_BINDER;
		return true;
	}
	return false;
}

/* Stop writing: whatever the input method had, the keys held, the ruler. */
static void agd_let_go(struct desk_server *server)
{
	agd_repeat_stop(server);
	desk_ime_typing_reset();
	agd_clear_ime(server);
	server->agd_edit = 0;
	server->agd_peeling = false;
	server->agd_edit_away = false;
	server->agd_drag = false;
	server->agd_mark = -1;
}

/* Put the note down: stuck (keep), or as it was. Either way it is peeled
 * off the binder's page and goes to the calendar: written on, to stick
 * there; an old one put back as it was, back to its place; one with
 * nothing on it is no note, and an old one wiped clean comes off. */
static void agd_finish(struct desk_server *server, bool keep)
{
	if (!server->agd_edit) {
		return;
	}
	if (keep && server->agd_pre[0]) {
		/* what the input method had not yet written goes on as it is */
		agd_insert(server, server->agd_pre);
	}
	int id = server->agd_edit;
	int kind = agd_edit_kind(server);
	int span, when = agd_time(server, &span);
	/* where it is as it is let go: from there it goes */
	struct desk_note_pose from;
	bool have_from = server->agd_edit_away ? agd_fly_where(server, id, &from)
		: agd_edit_pose(server, &from);
	agd_let_go(server);
	server->agd_when = when;
	server->agd_span = span;
	struct desk_stick s = { 0 };
	if (id > 0 && !desk_agenda_get(id, &s)) {
		agd_stuck(server);
		return;
	}
	if (!keep) {
		if (id > 0 && have_from) {
			agd_send(server, &s, &from);
		}
		agd_stuck(server);
		return;
	}
	char *t = server->agd_text;
	while (*t == ' ') {
		t++;
	}
	size_t n = strlen(t);
	while (n > 0 && t[n - 1] == ' ') {
		t[--n] = '\0';
	}
	if (id <= 0) {
		s.y = server->agd_y;
		s.m = server->agd_m;
		s.d = server->agd_d;
		s.kind = kind;
	}
	s.colour = server->agd_edit_colour;
	s.minute = when;
	s.span = when >= 0 ? span : 0;
	const char *rest = t;
	int sp;
	if (kind != DESK_STICK_PLAN || desk_agenda_when(t, &sp, &rest) < 0) {
		rest = t;
	}
	if (!*t) {
		/* nothing written: an old note comes off, a new one never was */
		if (id > 0) {
			desk_agenda_remove(id);
			if (have_from) {
				agd_fly_start(server, AGD_FLY_OFF, &s, &from, AGD_ON_BINDER, &from,
					AGD_ON_BINDER);
			}
		}
		agd_stuck(server);
		return;
	}
	snprintf(s.text, sizeof s.text, "%s", rest);
	int nid = id;
	if (id > 0) {
		desk_agenda_set(&s);
	} else {
		nid = desk_agenda_add(&s);
	}
	struct desk_stick saved;
	if (nid > 0 && desk_agenda_get(nid, &saved) && have_from) {
		agd_send(server, &saved, &from);
	}
	agd_stuck(server);
}

/* Start writing: on note id, or a new one (DESK_AGENDA_NEW_PLAN/TODO)
 * peeled off the pad picked. An old one is taken off the calendar and
 * comes over to the binder to be written on. */
static void agd_begin(struct desk_server *server, int id)
{
	agd_finish(server, true);
	server->agd_text[0] = '\0';
	server->agd_caret = 0;
	agd_clear_ime(server);
	server->agd_when = -1;
	server->agd_span = 0;
	server->agd_mark = -1;
	server->agd_edit_away = false;
	if (id > 0) {
		struct desk_stick s;
		if (!desk_agenda_get(id, &s)) {
			return;
		}
		snprintf(server->agd_text, sizeof server->agd_text, "%s", s.text);
		server->agd_caret = (int)strlen(server->agd_text);
		server->agd_edit_colour = s.colour;
		server->agd_when = s.minute;
		server->agd_span = s.minute >= 0 ? s.span : 0;
		server->agd_peeling = false;
		/* where it is on the calendar, before it is taken off it */
		struct desk_cal_look lk;
		cal_look_now(server, &lk);
		struct desk_note_pose from, to;
		bool on_cal = cal_settled(server) && !server->cal_turn && !cal_switching(server)
			&& desk_calendar_note_pose(server->desk_w, server->desk_h, &lk, &s, &from);
		server->agd_edit = id;
		if (on_cal && agd_edit_pose(server, &to)) {
			server->agd_edit_away = true;
			agd_fly_start(server, AGD_FLY_TO_BINDER, &s, &from, AGD_ON_CAL, &to, AGD_ON_BINDER);
		}
	} else {
		server->agd_edit_colour = server->agd_colour;
		server->agd_peeling = true;
		server->agd_peel_start = monotonic_sec();
		desk_sound_play(DESK_SND_CORNER_LIFT, agd_pan(server), 0.9, 1.15);
		server->agd_edit = id;
	}
	paint_calendar(server);
	agd_changed(server);
}

/* What is written at the front of the note as a time goes, the time being
 * set on the ruler instead. */
static void agd_untype_time(struct desk_server *server)
{
	const char *rest;
	int sp;
	if (desk_agenda_when(server->agd_text, &sp, &rest) < 0) {
		return;
	}
	int cut = (int)(rest - server->agd_text);
	memmove(server->agd_text, server->agd_text + cut, strlen(server->agd_text + cut) + 1);
	server->agd_caret = server->agd_caret > cut ? server->agd_caret - cut : 0;
}

/* The ruler: pressed at minute -- the time from there, or what was set
 * moved there as it was -- and drawn along, from there to where it is. */
static void agd_ruler_press(struct desk_server *server, int minute)
{
	/* what it was at: written, or on the ruler */
	int span, when = agd_time(server, &span);
	agd_untype_time(server);
	server->agd_drag = true;
	server->agd_drag_a = minute;
	server->agd_drag_keep = when >= 0 ? span : 0;
	server->agd_when = minute > 24 * 60 - 15 ? 24 * 60 - 15 : minute;
	server->agd_span = server->agd_drag_keep;
	server->agd_mark = minute;
	desk_sound_play(DESK_SND_PENCIL_TICK, agd_pan(server), 0.35, 1.3);
	agd_changed(server);
}

static void agd_drag_to(struct desk_server *server, double x, double y)
{
	double r[4];
	agd_rect(server, r);
	struct desk_agenda_look lk;
	agd_look(server, &lk);
	double lx = x - r[0], ly = y - r[1];
	desk_agenda_unturn(r[2], r[3], &lk, &lx, &ly);
	int b = desk_agenda_ruler_minute(r[2], r[3], lx), a = server->agd_drag_a;
	int when = server->agd_when, span = server->agd_span;
	if (b != a) {
		when = a < b ? a : b;
		span = abs(b - a);
	} else {
		when = a > 24 * 60 - 15 ? 24 * 60 - 15 : a;
		span = server->agd_drag_keep;
	}
	if (when != server->agd_when || span != server->agd_span || b != server->agd_mark) {
		server->agd_when = when;
		server->agd_span = span;
		server->agd_mark = b;
		agd_changed(server);
	}
}

/* the wheel on the ruler: the time a quarter of an hour on or back */
static void agd_ruler_roll(struct desk_server *server, int dir)
{
	int span;
	int when = agd_time(server, &span);
	agd_untype_time(server);
	if (when < 0) {
		when = server->agd_mark >= 0 ? server->agd_mark : 9 * 60;
		span = 0;
	} else {
		when += 15 * dir;
	}
	server->agd_when = when < 0 ? 0 : (when > 24 * 60 - 15 ? 24 * 60 - 15 : when);
	server->agd_span = span;
	desk_sound_play(DESK_SND_KNOB_TICK, agd_pan(server), 0.30, 1.4);
	agd_changed(server);
}

static void agd_plain(struct desk_server *server, uint32_t sym, const char *utf8, uint32_t mods);

static int agd_repeat_cb(void *data)
{
	struct desk_server *server = data;
	if (!server->agd_repeat_sym || !server->agd_edit) {
		return 0;
	}
	agd_plain(server, server->agd_repeat_sym, "", 0);
	wl_event_source_timer_update(server->agd_repeat, 33);
	return 0;
}

/* a key the input method did not take, or no input method: what a key
 * does on paper */
static void agd_plain(struct desk_server *server, uint32_t sym, const char *utf8, uint32_t mods)
{
	if (!server->agd_edit) {
		return;
	}
	char *t = server->agd_text;
	int n = (int)strlen(t), c = server->agd_caret;
	bool rep = false;
	switch (sym) {
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		agd_finish(server, true);
		return;
	case XKB_KEY_Escape:
		agd_finish(server, false);
		return;
	case XKB_KEY_BackSpace:
		if (c > 0) {
			int p = utf8_prev(t, c);
			memmove(t + p, t + c, (size_t)(n - c + 1));
			server->agd_caret = p;
		}
		rep = true;
		break;
	case XKB_KEY_Delete:
		if (c < n) {
			int q = utf8_next(t, c);
			memmove(t + c, t + q, (size_t)(n - q + 1));
		}
		rep = true;
		break;
	case XKB_KEY_Left:
		server->agd_caret = utf8_prev(t, c);
		rep = true;
		break;
	case XKB_KEY_Right:
		server->agd_caret = utf8_next(t, c);
		rep = true;
		break;
	case XKB_KEY_Home:
		server->agd_caret = 0;
		break;
	case XKB_KEY_End:
		server->agd_caret = n;
		break;
	default:
		if (utf8 && (unsigned char)utf8[0] >= 0x20 && utf8[0] != 0x7f
			&& !(mods & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO))) {
			agd_insert(server, utf8);
		}
		break;
	}
	if (rep && server->agd_repeat_sym != sym && sym == server->agd_held_sym && utf8) {
		/* held down, it goes on: a moment's wait, then thirty a second */
		server->agd_repeat_sym = sym;
		if (!server->agd_repeat) {
			server->agd_repeat = wl_event_loop_add_timer(
				wl_display_get_event_loop(server->wl_display), agd_repeat_cb, server);
		}
		if (server->agd_repeat) {
			wl_event_source_timer_update(server->agd_repeat, 450);
		}
	}
	agd_changed(server);
}

/* ---- what the input method sends back ---- */

static void agd_ime_commit(const char *text, void *user)
{
	struct desk_server *server = user;
	if (!server->agd_edit) {
		return;
	}
	agd_insert(server, text);
	server->agd_pre[0] = '\0';
	agd_changed(server);
}

static void agd_ime_preedit(const char *text, int cursor, bool visible, void *user)
{
	struct desk_server *server = user;
	snprintf(server->agd_pre, sizeof server->agd_pre, "%s", visible ? text : "");
	/* its caret is counted in letters */
	int b = 0;
	for (int i = 0; i < cursor && server->agd_pre[b]; i++) {
		b = utf8_next(server->agd_pre, b);
	}
	server->agd_pre_caret = b;
	if (server->agd_edit) {
		agd_changed(server);
	}
}

static void agd_ime_lookup(const char *const *cands, int n, int cursor, int page, bool visible,
	void *user)
{
	struct desk_server *server = user;
	server->agd_ncand = 0;
	if (visible && n > 0) {
		/* the page the picked one is on */
		page = page > 0 ? (page > 16 ? 16 : page) : 9;
		cursor = cursor < 0 ? 0 : (cursor >= n ? n - 1 : cursor);
		int first = cursor / page * page;
		for (int i = first; i < n && server->agd_ncand < page; i++) {
			snprintf(server->agd_cand[server->agd_ncand], sizeof server->agd_cand[0], "%s",
				cands[i]);
			server->agd_cand_p[server->agd_ncand] = server->agd_cand[server->agd_ncand];
			server->agd_ncand++;
		}
		server->agd_cand_cursor = cursor - first;
		server->agd_cand_page = page;
	}
	if (server->agd_edit) {
		agd_changed(server);
	}
}

static void agd_ime_key(uint32_t sym, uint32_t keycode, uint32_t mods, bool taken, bool answer,
	void *user)
{
	(void)keycode;
	struct desk_server *server = user;
	char utf8[8] = "";
	if (answer) {
		/* the oldest key handed over: what it would type */
		if (server->agd_keyq_n > 0) {
			memcpy(utf8, server->agd_keyq[0].utf8, sizeof utf8);
			server->agd_keyq_n--;
			memmove(&server->agd_keyq[0], &server->agd_keyq[1],
				sizeof server->agd_keyq[0] * (size_t)server->agd_keyq_n);
		}
	} else {
		xkb_keysym_to_utf8(sym, utf8, sizeof utf8);
	}
	if (!taken) {
		agd_plain(server, sym, utf8, mods);
	}
}

/* ---- in and out ---- */

static void agd_move(struct desk_server *server, double to, double dur)
{
	server->agd_from = server->agd_t;
	server->agd_to = to;
	server->agd_start = monotonic_sec();
	server->agd_dur = dur;
	server->agd_moving = 1;
	server_schedule_frames(server);
	paint_agenda(server);
}

/* Open at day y-m-d (month 0-11) -- the binder brought in, or turned to it;
 * start: a thing to do there, or a plan, at when (minutes; -1 none). */
enum { AGD_START_NONE, AGD_START_TODO, AGD_START_PLAN };
static void agd_open(struct desk_server *server, int y, int m, int d, int start, int when)
{
	bool in = agd_up(server) && server->agd_to > 0.5;
	if (in && (y != server->agd_y || m != server->agd_m || d != server->agd_d)) {
		agd_finish(server, true);
		desk_sound_play(DESK_SND_PAGE_TURN, agd_pan(server), 0.7, 1.15);
	}
	server->agd_y = y;
	server->agd_m = m;
	server->agd_d = d;
	/* the calendar goes to the day too, once what is in the air is down */
	server->agd_follow = true;
	if (!in) {
		server->agd_hover = 0;
		static const struct desk_ime_typing hooks = { agd_ime_commit, agd_ime_preedit,
			agd_ime_lookup, agd_ime_key, NULL };
		struct desk_ime_typing h = hooks;
		h.user = server;
		server->agd_ime = desk_ime_typing_begin(&h);
		server->agd_sp.drawn = false;
		server->agd_mark_sp.drawn = false;
		desk_sound_play(DESK_SND_BINDER_LIFT, agd_pan(server), 0.8, 1.1);
		agd_move(server, 1.0, AGD_IN_S);
	}
	if (start == AGD_START_TODO) {
		agd_begin(server, DESK_AGENDA_NEW_TODO);
	} else if (start == AGD_START_PLAN) {
		agd_begin(server, DESK_AGENDA_NEW_PLAN);
		server->agd_when = when;
		server->agd_span = 0;
	}
	paint_calendar(server);
	agd_changed(server);
}

static void agd_close(struct desk_server *server)
{
	if (!agd_up(server) || server->agd_to < 0.5) {
		return;
	}
	agd_finish(server, true);
	desk_ime_typing_end();
	server->agd_ime = false;
	server->agd_hover = 0;
	desk_sound_play(DESK_SND_BINDER_CLOSE, agd_pan(server), 0.7, 1.1);
	agd_move(server, 0.0, AGD_OUT_S);
}

/* gone at once, with the calendar or the shelf */
static void agd_drop(struct desk_server *server)
{
	if (server->agd_edit) {
		agd_finish(server, true);
	}
	if (agd_up(server)) {
		desk_ime_typing_end();
	}
	agd_repeat_stop(server);
	server->agd_ime = false;
	server->agd_t = 0.0;
	server->agd_to = 0.0;
	server->agd_moving = 0;
	if (server->agd_gone_id) {
		desk_agenda_remove(server->agd_gone_id);
		server->agd_gone_id = 0;
	}
	server->agd_ruler_t = 0.0;
	server->agd_drag = false;
	server->agd_mark = -1;
	server->agd_follow = false;
	if (server->agd_sp.node) {
		wlr_scene_node_set_enabled(&server->agd_sp.node->node, false);
	}
	if (server->agd_mark_sp.node) {
		wlr_scene_node_set_enabled(&server->agd_mark_sp.node->node, false);
	}
}

/* Peeled off: a note comes away from wherever it is -- the calendar, or
 * the binder where it is being written on -- and drops, and is rubbed out
 * of the binder. */
static void agd_peel_off(struct desk_server *server, int id)
{
	struct desk_stick s;
	if (!desk_agenda_get(id, &s)) {
		return;
	}
	struct desk_note_pose p;
	if (server->agd_edit == id) {
		bool have = server->agd_edit_away ? agd_fly_where(server, id, &p)
			: agd_edit_pose(server, &p);
		agd_let_go(server);
		desk_agenda_remove(id);
		if (have) {
			agd_fly_start(server, AGD_FLY_OFF, &s, &p, AGD_ON_BINDER, &p, AGD_ON_BINDER);
		}
		agd_stuck(server);
		return;
	}
	struct desk_cal_look lk;
	cal_look_now(server, &lk);
	if (cal_settled(server) && !server->cal_turn && !cal_switching(server)
		&& desk_calendar_note_pose(server->desk_w, server->desk_h, &lk, &s, &p)) {
		agd_fly_start(server, AGD_FLY_OFF, &s, &p, AGD_ON_CAL, &p, AGD_ON_CAL);
	}
	if (agd_up(server) && server->agd_to > 0.5 && s.y == server->agd_y && s.m == server->agd_m
		&& s.d == server->agd_d) {
		/* rubbed out of the binder, and gone when it is */
		if (server->agd_gone_id) {
			desk_agenda_remove(server->agd_gone_id);
		}
		server->agd_gone_id = id;
		server->agd_gone_start = monotonic_sec();
	} else {
		desk_agenda_remove(id);
	}
	agd_stuck(server);
}

static void agd_tick(struct desk_server *server)
{
	if (!agd_up(server)) {
		return;
	}
	double now = monotonic_sec();
	bool changed = false;
	if (server->agd_moving) {
		double u = clampf_d((now - server->agd_start) / server->agd_dur, 0, 1);
		double e;
		if (server->agd_to > 0.5) {
			/* in from the right, a hair past its place and back */
			const double c1 = 1.25, c3 = c1 + 1.0;
			e = 1.0 + c3 * pow(u - 1.0, 3.0) + c1 * pow(u - 1.0, 2.0);
		} else {
			e = u * u;
		}
		server->agd_t = server->agd_from + (server->agd_to - server->agd_from) * e;
		if (u >= 1.0) {
			server->agd_t = server->agd_to;
			server->agd_moving = 0;
		}
		changed = true;
	}
	if (server->agd_peeling) {
		if (now - server->agd_peel_start >= AGD_PEEL_S) {
			server->agd_peeling = false;
		}
		changed = true;
	}
	if (server->agd_gone_id) {
		if (now - server->agd_gone_start >= AGD_GONE_S) {
			desk_agenda_remove(server->agd_gone_id);
			server->agd_gone_id = 0;
			agd_stuck(server);
		}
		changed = true;
	}
	/* the ruler out while a plan is being written */
	double want = server->agd_edit && !server->agd_edit_away
		&& agd_edit_kind(server) == DESK_STICK_PLAN ? 1.0 : 0.0;
	double dt = server->agd_ruler_at > 0.0 ? fmin(now - server->agd_ruler_at, 0.05) : 0.0;
	server->agd_ruler_at = now;
	if (server->agd_ruler_t != want) {
		double step = gfx_flat() ? 1.0 : dt / 0.22;
		server->agd_ruler_t = want > server->agd_ruler_t ? fmin(want, server->agd_ruler_t + step)
			: fmax(want, server->agd_ruler_t - step);
		changed = true;
	}
	/* the calendar after the binder's day, once nothing is on its way to it */
	if (server->agd_follow && server->agd_to > 0.5 && !agd_flying_to(server, AGD_FLY_TO_CAL)) {
		server->agd_follow = false;
		cal_show(server, server->agd_y, server->agd_m, server->agd_d);
	}
	if (server->agd_t <= 0.0005 && !server->agd_moving) {
		agd_drop(server);
		paint_calendar(server);
		server_schedule_frames(server);
		return;
	}
	if (changed) {
		paint_agenda(server);
		paint_calendar(server);
		server_schedule_frames(server);
	}
}

/* ---- the keys and the pointer, while it is in ---- */

static bool agd_has_keys(const struct desk_server *server)
{
	return agd_up(server) && server->agd_to > 0.5;
}

static void agd_key(struct desk_server *server, struct xkb_state *xkb, uint32_t keycode,
	uint32_t sym, uint32_t mods, bool released)
{
	char utf8[8] = "";
	xkb_state_key_get_utf8(xkb, keycode + 8, utf8, sizeof utf8);
	if (released) {
		if (server->agd_edit && server->agd_ime) {
			desk_ime_typing_key(sym, keycode, mods, true);
		}
		if (sym == server->agd_held_sym) {
			server->agd_held_sym = 0;
		}
		if (sym == server->agd_repeat_sym) {
			agd_repeat_stop(server);
		}
		return;
	}
	server->agd_held_sym = sym;
	if (!server->agd_edit) {
		/* not writing: Escape puts the binder back, the arrows go along
		 * the days, a letter starts a plan */
		if (sym == XKB_KEY_Escape) {
			agd_close(server);
			return;
		}
		int step = sym == XKB_KEY_Left ? -1 : sym == XKB_KEY_Right ? 1
			: sym == XKB_KEY_Up ? -7 : sym == XKB_KEY_Down ? 7 : 0;
		if (step) {
			int y = server->agd_y, m = server->agd_m, d = server->agd_d;
			desk_calendar_step(DESK_CAL_DAY, step, &y, &m, &d);
			agd_open(server, y, m, d, AGD_START_NONE, -1);
			return;
		}
		bool letter = (unsigned char)utf8[0] >= 0x20 && utf8[0] != 0x7f
			&& !(mods & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO));
		if (!letter) {
			/* the input method's own keys (kana on and off) still go to it */
			if (server->agd_ime) {
				desk_ime_typing_key(sym, keycode, mods, false);
			}
			return;
		}
		agd_begin(server, DESK_AGENDA_NEW_PLAN);
	}
	if (server->agd_ime && desk_ime_typing_key(sym, keycode, mods, false)) {
		if (server->agd_keyq_n < (int)(sizeof server->agd_keyq / sizeof server->agd_keyq[0])) {
			memcpy(server->agd_keyq[server->agd_keyq_n].utf8, utf8, sizeof utf8);
			server->agd_keyq_n++;
		}
		return;
	}
	agd_plain(server, sym, utf8, mods);
}

/* the pointer at x, y on the screen: on the binder, what of it */
static int agd_hit_at(struct desk_server *server, double x, double y, int *arg)
{
	if (!agd_has_keys(server) || server->agd_moving) {
		return DESK_AGENDA_HIT_NONE;
	}
	double r[4];
	agd_rect(server, r);
	struct desk_agenda_look lk;
	agd_look(server, &lk);
	double lx = x - r[0], ly = y - r[1];
	desk_agenda_unturn(r[2], r[3], &lk, &lx, &ly);
	return desk_agenda_hit(r[2], r[3], &lk, lx, ly, arg);
}

static void agd_hover_at(struct desk_server *server, double x, double y)
{
	int arg = 0;
	int z = agd_hit_at(server, x, y, &arg);
	/* on the ruler, a pencil mark where the pointer is */
	int mark = z == DESK_AGENDA_HIT_RULER ? arg : -1;
	if (z != server->agd_hover || arg != server->agd_hover_arg || mark != server->agd_mark) {
		server->agd_hover = z;
		server->agd_hover_arg = arg;
		server->agd_mark = mark;
		agd_changed(server);
	}
}

/* a click on the binder: true if it was on it */
static bool agd_press(struct desk_server *server, double x, double y, uint32_t button)
{
	int arg = 0;
	int z = agd_hit_at(server, x, y, &arg);
	if (z == DESK_AGENDA_HIT_NONE) {
		return false;
	}
	if (button == BTN_RIGHT) {
		if (z == DESK_AGENDA_HIT_NOTE || z == DESK_AGENDA_HIT_CHECK) {
			if (arg > 0) {
				agd_peel_off(server, arg);
			} else {
				agd_finish(server, false);
			}
		}
		return true;
	}
	if (button != BTN_LEFT) {
		return true;
	}
	switch (z) {
	case DESK_AGENDA_HIT_RULER:
		agd_ruler_press(server, arg);
		break;
	case DESK_AGENDA_HIT_ALLDAY:
		agd_untype_time(server);
		server->agd_when = -1;
		server->agd_span = 0;
		desk_sound_play(DESK_SND_PENCIL_RING, agd_pan(server), 0.35, 1.2);
		agd_changed(server);
		break;
	case DESK_AGENDA_HIT_PAD:
		server->agd_colour = arg;
		if (server->agd_edit) {
			/* the note being written takes the colour too */
			server->agd_edit_colour = arg;
		}
		desk_sound_play(DESK_SND_FELT_PICK, agd_pan(server), 0.6, 1.3);
		agd_changed(server);
		break;
	case DESK_AGENDA_HIT_SLOT_PLAN:
		agd_begin(server, DESK_AGENDA_NEW_PLAN);
		break;
	case DESK_AGENDA_HIT_SLOT_TODO:
		agd_begin(server, DESK_AGENDA_NEW_TODO);
		break;
	case DESK_AGENDA_HIT_NOTE:
		if (arg != server->agd_edit) {
			agd_begin(server, arg);
		}
		break;
	case DESK_AGENDA_HIT_CHECK: {
		struct desk_stick s;
		agd_finish(server, true);
		if (desk_agenda_get(arg, &s)) {
			s.done = !s.done;
			desk_agenda_set(&s);
			desk_sound_play(DESK_SND_PENCIL_TICK, agd_pan(server), 0.9, 0.0);
			agd_stuck(server);
		}
		break;
	}
	default:
		/* the binder, not a note on it: put down what was being written */
		agd_finish(server, true);
		break;
	}
	return true;
}

static void shelf_toggle(struct desk_server *server)
{
	/* whatever was pulled out from under the Control Panel goes back */
	card_gone(server);
	/* and the calendar or the memo pad, if it was in your hand */
	cal_drop(server);
	memo_drop(server);
	/* The desk is traced first and put up on its own; the bookcase, the
	 * console and the post box follow it, each up as its own sheet is.
	 * Until the bookcase lands there is nothing behind the desk to turn
	 * to, so the plate does not answer. Coming back down is always
	 * allowed. */
	if (server->shelf_t < 0.5
		&& !desk_scene_view_ready(server->desk_w, server->desk_h, DESK_VIEW_SHELF)) {
		return;
	}
	double now = monotonic_sec();
	/* turned back partway, it goes back from where it has got to */
	double cur = server->shelf_t;
	server->shelf_from = cur;
	server->shelf_to = cur > 0.5 ? 0.0 : 1.0;
	server->shelf_hover = -1;
	server->hover_from = -1;
	server->hover_t = 0.0;
	server->hover_moving = 0;
	if (server->shelf_to > 0.5 && server->binder_putback) {
		/* back up before it was put away: it is on the shelf */
		binder_put_back(server);
	}
	if (server->shelf_to < 0.5 && server->shelf_binder == DESK_SHELF_BOOK) {
		/* whatever was being written in it is done with */
		book_end(server);
	}
	if (server->shelf_to < 0.5 && !server->binder_putback) {
		server->shelf_binder = -1;
		desk_binder_forget();
		server->binder_t = 0.0;
		server->binder_moving = 0;
		server->binder_sel = -1;
	}
	server->shelf_start = now;
	double span = fabs(server->shelf_to - cur);
	if (span < 0.30) {
		span = 0.30;
	}
	server->shelf_dur = 0.80 * span;
	server->shelf_moving = 1;
	/* You shut the drawer before you sit up. The film is traced with it
	 * shut, so this is also what makes the first frame match the desk. */
	if (server->shelf_to > 0.5 && server->drawer_t > 0.004) {
		drawer_toggle(server);
	}
	server_schedule_frames(server);
	paint_shelf(server);
}

static void shelf_tick(struct desk_server *server)
{
	if (!server->shelf_moving) {
		return;
	}
	double u = clampf_d((monotonic_sec() - server->shelf_start) / server->shelf_dur, 0, 1);
	double e = look_ease(u);
	server->shelf_t = server->shelf_from + (server->shelf_to - server->shelf_from) * e;
	if (u >= 1.0) {
		server->shelf_t = server->shelf_to;
		server->shelf_moving = 0;
	}
	if (server->binder_putback && server->shelf_t < 0.70) {
		/* out of sight now (paint_shelf_body draws it no lower) */
		binder_put_back(server);
	}
	paint_shelf(server);
	if (server->shelf_moving) {
		server_schedule_frames(server);
		return;
	}
	if (server->shelf_t < 0.5 && server->pending_look) {
		int what = server->pending_look;
		server->pending_look = 0;
		if (what == 1) {
			console_toggle(server);
		} else {
			post_toggle(server);
		}
	}
}

/* ================================================================
 * The control console
 *
 * It stands at the back right of the desk and it is always there. Opening
 * it is not a window appearing: it is turning your head to the machine, so
 * the camera goes over to it and the panel fills the frame. Then the set
 * warms up -- the lamps come on one after another, the needles lift off
 * their stops, the tube strikes -- because a valve set does not come on,
 * it comes round.
 * ================================================================ */

/* Is the set in the frame at all? */
static bool console_up(const struct desk_server *server)
{
	return server->cc_t > 0.0005 || server->cc_moving;
}

/* How far the desk is turned down. The machine has no backlight of its own,
 * so the fader is the lamp over the desk. */
static void console_dim_apply(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	double a = (1.0 - clampf_d(server->cc_bright, 0.0, 1.0)) * 0.62;
	/* Leaning into the set, you are looking at a thing that lights itself,
	 * so the room going down barely reaches it -- but it has to reach it a
	 * little, or the fader would feel like it was not connected. */
	a *= 1.0 - 0.72 * clampf_d(server->cc_t, 0.0, 1.0);
	/* and the machine going out takes the room with it, all the way */
	if (server->cc_pressed >= 0) {
		a = fmax(a, smoothstep01(0.10, 1.00, server->cc_dying));
	}
	if (w < 8 || h < 8) {
		return;
	}
	if (!server->cc_dim) {
		/* one flat colour, stretched over the screen by the scene graph.
		 * It used to be a screenful of it: thirty two megabytes of the
		 * same three numbers on a large display. */
		struct desk_buf *buf = buf_create(16, 16);
		cairo_set_source_rgb(buf->cr, 0.004, 0.004, 0.006);
		cairo_paint(buf->cr);
		scene_put(&server->cc_dim, server->layer_top, buf, 0, 0, NULL);
		if (server->cc_dim) {
			/* it is the light in the room, not something you can click */
			server->cc_dim->point_accepts_input = shadow_accepts;
		}
	}
	if (!server->cc_dim) {
		return;
	}
	wlr_scene_buffer_set_dest_size(server->cc_dim, w, h);
	wlr_scene_buffer_set_opacity(server->cc_dim, (float)a);
	wlr_scene_node_set_enabled(&server->cc_dim->node, a > 0.004);
}

static void console_live(struct desk_server *server, bool on);

/* The screen's light, sent to the machine: no more often than it can take
 * it, so a hand running the fader along is one turn now and then and the
 * last one when it stops. */
static int screen_send_cb(void *data)
{
	struct desk_server *server = data;
	if (!desk_backlight_set(server->cc_screen)) {
		static bool said;
		if (!said) {
			said = true;
			wlr_log(WLR_ERROR, "the screen's light would not turn: neither logind nor "
				"the backlight's own file would take it");
		}
	}
	return 0;
}

static void screen_apply(struct desk_server *server)
{
	if (!server->cc_screen_ok) {
		return;
	}
	if (!server->cc_screen_send) {
		server->cc_screen_send = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), screen_send_cb, server);
	}
	if (server->cc_screen_send) {
		wl_event_source_timer_update(server->cc_screen_send, 40);
	} else {
		screen_send_cb(server);
	}
}

/* Where the screen's light is now, as the machine has it -- something else
 * (the keys of a laptop, its power manager) may have turned it. */
static void screen_read(struct desk_server *server)
{
	struct desk_supply su;
	desk_supply_read(&su);
	server->cc_screen_ok = su.have_backlight;
	if (su.have_backlight && server->cc_grab != DESK_CC_SCREEN) {
		server->cc_screen = su.backlight;
	}
}

static void paint_console(struct desk_server *server);

/* The brightness keys a laptop has: the screen's light a step -- or, on a
 * machine with no backlight, the lamp over the desk. */
static void screen_step(struct desk_server *server, int dir)
{
	screen_read(server);
	double *v = server->cc_screen_ok ? &server->cc_screen : &server->cc_bright;
	*v = clampf_d(*v + dir * 0.0625, 0.0, 1.0);
	if (server->cc_screen_ok) {
		screen_apply(server);
	} else {
		console_dim_apply(server);
	}
	paint_console(server);
	server_schedule_frames(server);
}

static int cc_live_ms(void);

/* The clock the tube goes by: on at the pace the set is drawn of itself
 * (cc_live_ms), whatever else asks for it to be drawn. A knob being turned
 * asks for a frame sixty times a second, and the tube -- a new face at
 * every tick of its clock -- was drawn again on every one of them, five
 * milliseconds each, for a flicker nobody sees go that fast. A tenth short
 * of the tick, so that every tick moves it on. */
static double cc_clock(void)
{
	double q = cc_live_ms() * 0.0009;
	return floor(monotonic_sec() / q) * q;
}

static void console_state(const struct desk_server *server,
	struct desk_console_state *st)
{
	st->wifi_on = server->cc_wifi != 0;
	st->bt_on = server->cc_bt != 0;
	st->volume = server->cc_vol;
	st->brightness = server->cc_bright;
	st->screen = server->cc_screen;
	st->screen_ok = server->cc_screen_ok;
	st->warm = server->cc_warm;
	st->clock = cc_clock();
	st->key = server->cc_key;
	st->guard = server->cc_guard;
	st->pressed = server->cc_pressed;
	st->dying = server->cc_dying;
	st->lock_push = server->cc_lock_push;
	st->antenna = server->cc_ant;
}

struct cc_piece_args {
	int piece;
	const struct desk_console_state *st;
	int hover;
	const struct desk_meters *m;
};

static void cc_piece_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct cc_piece_args *a = data;
	desk_draw_console_piece(cr, w, h, a->piece, a->st, a->hover, a->m);
}

/* the camera's fade, across every piece */
static void cc_live_opacity(struct desk_server *server, double a)
{
	for (int p = 0; p < DESK_CCP_COUNT; p++) {
		if (server->cc_sprite[p].node) {
			wlr_scene_buffer_set_opacity(server->cc_sprite[p].node, (float)a);
		}
	}
}

/* The settled view of the set, moved with the camera at the end of a move. */
static void cc_follow(struct desk_server *server, int dx, int dy)
{
	if (server->cc_live) {
		wlr_scene_node_set_position(&server->cc_live->node, dx, dy);
	}
	if (server->cc_base) {
		wlr_scene_node_set_position(&server->cc_base->node, dx, dy);
	}
}

static void paint_console_body(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	if (w < 8 || h < 8) {
		return;
	}
	console_dim_apply(server);
	if (!console_up(server)) {
		if (server->cc_live) {
			wlr_scene_node_set_enabled(&server->cc_live->node, false);
		}
		if (server->cc_base) {
			wlr_scene_node_set_enabled(&server->cc_base->node, false);
		}
		if (server->film_a[DESK_VIEW_CONSOLE]) {
			wlr_scene_node_set_enabled(&server->film_a[DESK_VIEW_CONSOLE]->node, false);
			wlr_scene_node_set_enabled(&server->film_b[DESK_VIEW_CONSOLE]->node, false);
		}
		return;
	}
	film_ensure(server, DESK_VIEW_CONSOLE, server->layer_console);

	/* The traced sheet goes up once. Copying a screenful of baked steel
	 * on every frame was most of what the set cost to look at. */
	if (!server->cc_base || server->cc_base_w != w || server->cc_base_h != h) {
		struct desk_buf *bb = buf_create(w, h);
		desk_draw_console_base(bb->cr, w, h);
		scene_put(&server->cc_base, server->layer_console, bb, 0, 0, NULL);
		server->cc_base_w = w;
		server->cc_base_h = h;
	}

	/* The live half: needles, lamps and the tube, on nothing -- and only
	 * once the move has got far enough for it to show. A sprite a piece,
	 * and a piece is drawn only when its key moves: on most frames that is
	 * the tube and nothing else. */
	bool have_view = server->cc_t > 0.70;
	if (have_view) {
		if (!server->cc_live) {
			server->cc_live = wlr_scene_tree_create(server->layer_console);
		}
		struct desk_console_state st;
		console_state(server, &st);
		/* the meters read once a tick of the tube's clock, the same tick
		 * that moves the tube on: frames asked for in between (a knob
		 * being turned) find nothing on it to draw again */
		if (st.clock != server->cc_meters_clock) {
			desk_meters_read(&server->cc_meters);
			server->cc_meters_clock = st.clock;
		}
		struct desk_meters m = server->cc_meters;
		desk_console_advance(&st, &m);
		bool force = server->cc_live_w != w || server->cc_live_h != h;
		server->cc_live_w = w;
		server->cc_live_h = h;
		for (int p = 0; p < DESK_CCP_COUNT; p++) {
			struct desk_sprite *sp = &server->cc_sprite[p];
			uint64_t key = desk_console_piece_key(p, &st, server->cc_hover, &m);
			if (!force && sp->drawn && sp->key == key) {
				continue;
			}
			sp->key = key;
			double box[4];
			bool known = desk_console_piece_box(w, h, p, box);
			struct cc_piece_args a = { p, &st, server->cc_hover, &m };
			sprite_paint(server, sp, server->cc_live, known ? box : NULL, cc_piece_draw, &a);
		}
	}
	if (server->cc_live) {
		wlr_scene_node_set_enabled(&server->cc_live->node, have_view);
	}
	wlr_scene_node_set_enabled(&server->cc_base->node, have_view);

	double t = server->cc_t;
	if (!server->film_a[DESK_VIEW_CONSOLE] || (!server->cc_moving && t >= 0.9995)) {
		film_hide(server, DESK_VIEW_CONSOLE);
		cc_follow(server, 0, 0);
		cc_live_opacity(server, 1.0);
		wlr_scene_buffer_set_opacity(server->cc_base, 1.0f);
		return;
	}
	double over;
	int dx, dy;
	film_play(server, DESK_VIEW_CONSOLE, t, &over, &dx, &dy);
	cc_follow(server, dx, dy);
	cc_live_opacity(server, have_view ? over : 0.0);
	if (server->cc_live) {
		wlr_scene_node_set_enabled(&server->cc_live->node, have_view && over > 0.001);
	}
	wlr_scene_buffer_set_opacity(server->cc_base, (float)(have_view ? over : 0.0));
	wlr_scene_node_set_enabled(&server->cc_base->node, have_view && over > 0.001);
}

static void paint_console(struct desk_server *server)
{
	prof_begin(PROF_CONSOLE);
	paint_console_body(server);
	prof_end();
}

static void console_toggle(struct desk_server *server)
{
	/* whatever was pulled out from under the Control Panel goes back */
	card_gone(server);
	if (server->cc_t < 0.5
		&& !desk_scene_view_ready(server->desk_w, server->desk_h, DESK_VIEW_CONSOLE)) {
		return;
	}
	if (console_dying(server)) {
		return;   /* the lamp is going out; nobody is turning away now */
	}
	/* Whichever way this goes, the guarded set is put back as it was
	 * found: glass down, key out. Nobody wants to come back to the desk
	 * and find it standing open. */
	console_power_reset(server);
	double now = monotonic_sec();
	double cur = server->cc_t;
	server->cc_from = cur;
	server->cc_to = cur > 0.5 ? 0.0 : 1.0;
	server->cc_start = now;
	snd_forget(&server->snd_cc_land);
	server->cc_hover = -1;
	server->cc_grab = -1;
	double span = fabs(server->cc_to - cur);
	if (span < 0.30) {
		span = 0.30;
	}
	server->cc_dur = 0.84 * span;
	server->cc_moving = 1;
	console_live(server, false);
	if (server->cc_to > 0.5) {
		/* the screen's light, wherever something else has left it */
		screen_read(server);
		/* You push the drawer in before you turn away from the desk, and
		 * the film is traced with it shut. */
		if (server->drawer_t > 0.004) {
			drawer_toggle(server);
		}
		server->cc_warm = 0.0;
		server->cc_warming = 0;
	} else {
		/* turning away leaves it to go cold again -- and switches it off */
		if (server->cc_t > 0.5) {
			desk_sound_play(DESK_SND_SET_OFF, -0.2, 1.0, 0.0);
		}
		server->cc_warming = 0;
	}
	server_schedule_frames(server);
	paint_console(server);
}

/* ---- the guarded set -------------------------------------------
 *
 * Nothing here is a setting, so nothing here is one click. You turn the
 * key, you lift the glass, and then you push. The machine takes its time
 * over each of them on purpose: this is the one part of the desk that ends
 * the session, and it should feel like ending it.
 *
 * The push is the only thing that cannot be taken back. Once the filament
 * starts going out, it goes out. */
#define DESK_CC_KEY_S   1.45   /* the key, seating itself and turning */
#define DESK_CC_GUARD_S 0.42   /* the glass, going up */
#define DESK_CC_ANT_S   1.55   /* the aerial, all the way up or down */
#define DESK_CC_DIE_S   2.80   /* the lamp, going out */

static bool console_dying(const struct desk_server *server)
{
	return server->cc_pressed >= 0;
}

static void console_key_to(struct desk_server *server, double to)
{
	if (console_dying(server)) {
		return;
	}
	/* in, or the bolt drawn back first to take it out */
	if (to > 0.5) {
		desk_sound_play(DESK_SND_KEY_IN, -0.55, 1.0, 0.0);
	} else {
		desk_sound_play(DESK_SND_LOCK_CLUNK, -0.55, 0.7, 1.1);
	}
	server->cc_key_from = server->cc_key;
	server->cc_key_to = to;
	server->cc_key_start = monotonic_sec();
	snd_forget(&server->snd_key_end);
	server->cc_key_dur = DESK_CC_KEY_S * fmax(0.15, fabs(to - server->cc_key));
	server->cc_key_moving = 1;
	console_live(server, true);
}

/* The aerial after the lever: up for wireless on, down for off, from
 * wherever it has got to, the motor heard all the while it runs. */
static void console_aerial_to(struct desk_server *server, double to)
{
	double now = monotonic_sec();
	double span = fabs(to - server->cc_ant);
	if (span < 1e-3) {
		return;
	}
	server->cc_ant_from = server->cc_ant;
	server->cc_ant_to = to;
	server->cc_ant_start = now;
	server->cc_ant_dur = DESK_CC_ANT_S * span;
	server->cc_ant_moving = 1;
	if (server->cc_ant_snd > 0) {
		desk_sound_stop_after(server->cc_ant_snd, 0.0, 0.05);
	}
	server->cc_ant_snd = desk_sound_play(to > 0.5 ? DESK_SND_AERIAL_UP
		: DESK_SND_AERIAL_DOWN, 0.55, 0.7, 0.0);
	if (server->cc_ant_snd > 0 && span < 0.999) {
		desk_sound_stop_after(server->cc_ant_snd, real_secs(server->cc_ant_dur), 0.05);
	}
	console_live(server, true);
	if (server->cc_timer) {
		wl_event_source_timer_update(server->cc_timer, 16);
	}
}

static void console_guard_to(struct desk_server *server, double to)
{
	if (console_dying(server)) {
		return;
	}
	desk_sound_play(DESK_SND_GUARD_LIFT, -0.55, to > 0.5 ? 1.0 : 0.8, to > 0.5 ? 0.0 : 0.9);
	server->cc_guard_from = server->cc_guard;
	server->cc_guard_to = to;
	server->cc_guard_start = monotonic_sec();
	snd_forget(&server->snd_guard_end);
	server->cc_guard_dur = DESK_CC_GUARD_S
		* fmax(0.20, fabs(to - server->cc_guard));
	server->cc_guard_moving = 1;
	console_live(server, true);
}

/* Everything back where it was: the glass down, the key out. Turning away
 * from the machine does this, so you never come back to it standing open. */
static void console_power_reset(struct desk_server *server)
{
	server->cc_key = server->cc_key_to = 0.0;
	server->cc_guard = server->cc_guard_to = 0.0;
	server->cc_key_moving = server->cc_guard_moving = 0;
	server->cc_pressed = -1;
	server->cc_dying = 0.0;
}

/* The lamps back on after the machine did not go after all: never a dark
 * screen over a machine that is still running. */
static void console_come_back(struct desk_server *server)
{
	if (!console_dying(server)) {
		return;
	}
	server->cc_pressed = -1;
	server->cc_dying = 0.0;
	console_guard_to(server, 1.0);
	paint_console(server);
	server_schedule_frames(server);
}

/* ---- ending the session ---------------------------------------------
 *
 * Logging out, restarting and powering off all end what is on the desk,
 * so every window is asked to close first, as its own close button would
 * -- a program with work not saved asks about it then -- and the session
 * ends only once the last one has gone. One that is still there after
 * END_WAIT_S (a question about saving, or a program that does not answer)
 * holds it up: the lamps come back on, the desk is turned back to, and that
 * window is put in front. Asked again within END_AGAIN_S of that, it goes
 * ahead once the windows have had their time, whatever is still open. */
enum desk_end { END_LOG_OUT, END_REBOOT, END_POWER_OFF };
#define END_WAIT_S 8.0
#define END_AGAIN_S 60.0

static const char *const END_NAME[] = { "log out", "restart", "power off" };

static void end_finish(void *data)
{
	struct desk_server *server = data;
	int what = server->ending - 1;
	if (what < 0 || server->end_done) {
		return;
	}
	server->end_done = true;
	if (server->end_timer) {
		wl_event_source_timer_update(server->end_timer, 0);
	}
	if (what == END_LOG_OUT) {
		wlr_log(WLR_INFO, "ending: logging out");
		wl_display_terminate(server->wl_display);
		return;
	}
	wlr_log(WLR_INFO, "ending: asking logind to %s", END_NAME[what]);
	if (desk_power_request(what == END_REBOOT ? DESK_POWER_REBOOT : DESK_POWER_OFF)) {
		return;   /* the screen stays dark, because the machine is going */
	}
	/* It said no -- most likely polkit wanting somebody to be at the
	 * keyboard in a way we are not. */
	wlr_log(WLR_ERROR, "ending: logind refused; coming back up");
	server->ending = 0;
	server->end_done = false;
	console_come_back(server);
}

/* The time is up: if the windows have all gone, it ends; else the window
 * still there holds it up -- unless this is the second time of asking. */
static int end_timer_cb(void *data)
{
	struct desk_server *server = data;
	if (!server->ending) {
		return 0;
	}
	int left = wl_list_length(&server->toplevels);
	if (left == 0 || server->end_anyway) {
		if (left) {
			wlr_log(WLR_INFO, "ending: %d window%s still open; going ahead, as asked twice",
				left, left == 1 ? "" : "s");
		}
		end_finish(server);
		return 0;
	}
	wlr_log(WLR_INFO, "ending: %d window%s did not close; staying (ask again to go ahead)",
		left, left == 1 ? "" : "s");
	server->ending = 0;
	server->end_held_at = real_sec();
	console_come_back(server);
	if (console_up(server)) {
		console_toggle(server);
	}
	struct desk_toplevel *view = wl_container_of(server->toplevels.next, view, link);
	focus_toplevel(view);
	server_schedule_frames(server);
	return 0;
}

static void end_begin(struct desk_server *server, int what)
{
	if (server->ending) {
		return;
	}
	server->ending = what + 1;
	server->end_anyway = server->end_held_at > 0.0
		&& real_sec() - server->end_held_at < END_AGAIN_S;
	int n = 0;
	struct desk_toplevel *view, *tmp;
	wl_list_for_each_safe(view, tmp, &server->toplevels, link) {
		view_close(view);
		n++;
	}
	wlr_log(WLR_INFO, "ending (%s): %d window%s asked to close", END_NAME[what], n,
		n == 1 ? "" : "s");
	if (n == 0) {
		end_finish(server);
		return;
	}
	if (!server->end_timer) {
		server->end_timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), end_timer_cb, server);
	}
	if (server->end_timer) {
		wl_event_source_timer_update(server->end_timer, (int)(END_WAIT_S * 1000.0));
	}
}

/* A window has gone: the last one, while ending, ends it -- not from inside
 * the window's own going, but just after. */
static void end_check(struct desk_server *server)
{
	if (server->ending && wl_list_empty(&server->toplevels)) {
		wl_event_loop_add_idle(wl_display_get_event_loop(server->wl_display), end_finish,
			server);
	}
}

static void console_power_fire(struct desk_server *server)
{
	int which = server->cc_pressed;
	wlr_log(WLR_INFO, "console: the %s one",
		which == DESK_CC_PW_OUT ? "green" : which == DESK_CC_PW_BOOT ? "amber" : "red");
	end_begin(server, which == DESK_CC_PW_OUT ? END_LOG_OUT
		: which == DESK_CC_PW_BOOT ? END_REBOOT : END_POWER_OFF);
}

/* Once it has landed the set is alive but nothing else is: the tube draws,
 * the needles wander, the lamps finish coming up. That is a repaint twelve
 * times a second, not a frame sixty times a second -- asking for a frame on
 * every vblank and then having nothing to put in it was what the machine
 * was actually spending its time on. */
#define DESK_CC_LIVE_MS 83

/* and less often for less drawing: eight times a second on Medium, six on
 * Low, twice in 2D -- and on Ultra, which keeps nothing back to save the
 * work, thirty: the tube's beam and its hum bar going over smoothly */
static int cc_live_ms(void)
{
	enum desk_gfx g = desk_gfx_level();
	return g == DESK_GFX_2D ? 500 : g == DESK_GFX_LOW ? 166
		: g == DESK_GFX_MEDIUM ? 125 : g == DESK_GFX_ULTRA ? 33 : DESK_CC_LIVE_MS;
}

static int console_live_cb(void *data)
{
	struct desk_server *server = data;
	if (!console_up(server) || server->cc_moving) {
		return 0;   /* the move drives itself; this re-arms when it lands */
	}
	if (server->cc_warming) {
		server->cc_warm = clampf_d(
			(monotonic_sec() - server->cc_warm_start) / 1.60, 0, 1);
		if (server->cc_warm >= 1.0) {
			server->cc_warming = 0;
		}
	}
	double now = monotonic_sec();
	if (server->cc_key_moving) {
		double u = clampf_d((now - server->cc_key_start) / server->cc_key_dur, 0, 1);
		server->cc_key = server->cc_key_from
			+ (server->cc_key_to - server->cc_key_from) * smoothstep01(0, 1, u);
		/* turned: the bolt thrown; or drawn out of the lock */
		bool in = server->cc_key_to > 0.5;
		snd_end(&server->snd_key_end, server->cc_key_start, server->cc_key_dur,
			in ? DESK_SND_LOCK_CLUNK : DESK_SND_KEY_IN, -0.55,
			in ? 1.0 : 0.7, in ? 0.0 : 1.2);
		if (u >= 1.0) {
			server->cc_key = server->cc_key_to;
			server->cc_key_moving = 0;
		}
	}
	if (server->cc_ant_moving) {
		/* a motor: the same speed all the way, but for getting going */
		double u = clampf_d((now - server->cc_ant_start) / server->cc_ant_dur, 0, 1);
		double e = u < 0.08 ? u * u / 0.16 : u > 0.92 ? 1.0 - (1.0 - u) * (1.0 - u) / 0.16
			: (u - 0.04) / 0.92;
		server->cc_ant = server->cc_ant_from + (server->cc_ant_to - server->cc_ant_from) * e;
		if (u >= 1.0) {
			server->cc_ant = server->cc_ant_to;
			server->cc_ant_moving = 0;
		}
	}
	if (server->cc_guard_moving) {
		double u = clampf_d((now - server->cc_guard_start)
			/ server->cc_guard_dur, 0, 1);
		server->cc_guard = server->cc_guard_from
			+ (server->cc_guard_to - server->cc_guard_from) * smoothstep01(0, 1, u);
		snd_end(&server->snd_guard_end, server->cc_guard_start, server->cc_guard_dur,
			DESK_SND_GUARD_STOP, -0.55, server->cc_guard_to > 0.5 ? 1.0 : 0.8, 0.0);
		if (u >= 1.0) {
			server->cc_guard = server->cc_guard_to;
			server->cc_guard_moving = 0;
		}
	}
	if (console_dying(server)) {
		server->cc_dying = clampf_d((now - server->cc_die_start)
			/ DESK_CC_DIE_S, 0, 1);
		if (server->cc_dying >= 1.0) {
			/* Dark. This is the moment the machine was asked for, and not
			 * one frame before it. */
			paint_console(server);
			console_dim_apply(server);
			server_schedule_frames(server);
			console_power_fire(server);
			if (console_dying(server)) {
				return 0;   /* it is going: nothing more to draw */
			}
			/* refused -- carry on below, which puts the light back */
		}
	}
	paint_console(server);
	console_dim_apply(server);
	server_schedule_frames(server);
	if (server->cc_timer) {
		/* the going-out wants smoother frames than a needle does, and
		 * so does the aerial */
		wl_event_source_timer_update(server->cc_timer,
			console_dying(server) || server->cc_ant_moving ? 16 : cc_live_ms());
	}
	return 0;
}

static void console_live(struct desk_server *server, bool on)
{
	if (server->cc_timer) {
		wl_event_source_timer_update(server->cc_timer, on ? cc_live_ms() : 0);
	}
}

static void console_tick(struct desk_server *server)
{
	if (!server->cc_moving) {
		return;
	}
	double u = clampf_d((monotonic_sec() - server->cc_start) / server->cc_dur, 0, 1);
	double e = look_ease(u);
	server->cc_t = server->cc_from + (server->cc_to - server->cc_from) * e;
	if (server->cc_to > 0.5) {
		/* the relay as it lands, and the hum swelling */
		snd_end(&server->snd_cc_land, server->cc_start, server->cc_dur,
			DESK_SND_SET_WARM, -0.2, 1.0, 0.0);
	}
	if (u >= 1.0) {
		server->cc_t = server->cc_to;
		server->cc_moving = 0;
		if (server->cc_t > 0.5) {
			/* it has landed: put the filaments on */
			server->cc_warm_start = monotonic_sec();
			server->cc_warming = 1;
			console_live(server, true);
		} else {
			server->cc_warm = 0.0;
			console_live(server, false);
		}
	}
	paint_console(server);
	server_schedule_frames(server);
}

/* Throw, turn or push whatever is under the hand. */
static void console_press(struct desk_server *server, int which)
{
	if (console_dying(server)) {
		return;   /* it is going out; there is nothing left to press */
	}
	switch (which) {
	case DESK_CC_LOCK:
		/* Pushed in as you go -- and seen pushed in, in the picture of
		 * the set you walk out of -- and out again by the time you are
		 * back. */
		server->cc_lock_push = 1.0;
		desk_sound_play(DESK_SND_POWER_PRESS, -0.55, 0.6, 1.2);
		lock_request(server);
		server->cc_lock_push = 0.0;
		paint_console(server);
		return;
	case DESK_CC_PW_KEY:
		/* It goes in, and then it turns itself. Which is not how a key
		 * works, and is exactly how the key to a thing like this works in
		 * every room it was ever kept in. */
		console_key_to(server, server->cc_key_to > 0.5 ? 0.0 : 1.0);
		if (server->cc_key_to < 0.5) {
			console_guard_to(server, 0.0);
		}
		return;
	case DESK_CC_PW_OFF:
	case DESK_CC_PW_BOOT:
	case DESK_CC_PW_OUT:
		if (server->cc_key < 0.999) {
			return;   /* locked: the glass will not lift and neither will it */
		}
		if (server->cc_guard_to < 0.5 || server->cc_guard < 0.999) {
			/* the glass is in the way, and clicking the glass is how you
			 * get it out of the way */
			console_guard_to(server, 1.0);
			return;
		}
		server->cc_pressed = which;
		server->cc_dying = 0.0;
		server->cc_die_start = monotonic_sec();
		/* the button, the relay, and the set going out as long as it takes */
		desk_sound_play(DESK_SND_POWER_PRESS, -0.55, 1.0, 0.0);
		{
			int h = desk_sound_play(DESK_SND_POWER_DOWN, -0.5, 1.0, 0.0);
			desk_sound_stop_after(h, real_secs(DESK_CC_DIE_S), 0.3);
		}
		console_live(server, true);
		paint_console(server);
		server_schedule_frames(server);
		return;
	default:
		break;
	}
	switch (which) {
	case DESK_CC_WIFI:
	case DESK_CC_BT: {
		bool wifi = which == DESK_CC_WIFI;
		int *flag = wifi ? &server->cc_wifi : &server->cc_bt;
		*flag = !*flag;
		snd_at(server, DESK_SND_TOGGLE, server->cursor->x, 1.0);
		bool took = desk_meters_set_radio(wifi, *flag != 0);
		if (wifi) {
			console_aerial_to(server, *flag ? 1.0 : 0.0);
		}
		wlr_log(WLR_INFO, "console: %s lever %s%s",
			wifi ? "wifi" : "bluetooth", *flag ? "on" : "off",
			took ? "" : " (nothing on this machine to throw)");
		break;
	}
	case DESK_CC_SCREEN:
		if (!server->cc_screen_ok) {
			/* nothing behind it: it does not move */
			snd_at(server, DESK_SND_KEY_PRESS, server->cursor->x, 0.5);
			return;
		}
		/* fall through */
	case DESK_CC_VOL:
	case DESK_CC_BRIGHT: {
		/* you take hold of it, and it follows the hand until you let go */
		double v;
		if (desk_console_value_at(server->desk_w, server->desk_h, which,
				server->cursor->x, server->cursor->y, &v)) {
			server->cc_grab = which;
			server->snd_knob = (int)floor(v * 24.0);
			snd_at(server, DESK_SND_KNOB_TICK, server->cursor->x, 0.8);
			if (which == DESK_CC_VOL) {
				server->cc_vol = v;
				desk_meters_set_volume(v);
			} else if (which == DESK_CC_SCREEN) {
				server->cc_screen = v;
				screen_apply(server);
			} else {
				server->cc_bright = v;
				console_dim_apply(server);
			}
		}
		break;
	}
	default:
		/* an instrument is something you read, not something you work */
		return;
	}
	paint_console(server);
	server_schedule_frames(server);
}

/* The free program that does in full what a control on the set does in
 * brief: the networks behind the meter and the wireless lever, Bluetooth's
 * devices behind its lever, the sound devices behind the volume knob --
 * the same programs, in the same order, as the rows of Shell Settings'
 * SYSTEM part (settings.c), found by their desktop files whether or not
 * those put them on a shelf. The first of them installed; you turn back
 * to the desk, where its window comes. */
static void console_open_settings(struct desk_server *server, int which)
{
	static const char *const NETWORK[] = { "nm-connection-editor.desktop", NULL };
	static const char *const BLUETOOTH[] = { "blueman-manager.desktop", NULL };
	/* pavucontrol's desktop file has had both names */
	static const char *const SOUND[] = { "org.pulseaudio.pavucontrol.desktop",
		"pavucontrol.desktop", "pavucontrol-qt.desktop", "mate-volume-control.desktop",
		NULL };
	const char *const *ids = which == DESK_CC_NET || which == DESK_CC_WIFI ? NETWORK
		: which == DESK_CC_BT ? BLUETOOTH : which == DESK_CC_VOL ? SOUND : NULL;
	if (!ids) {
		return;
	}
	const struct desk_app_list *l = desk_apps_acquire();
	const struct desk_app *app = NULL;
	for (int i = 0; ids[i] && !app; i++) {
		app = desk_apps_find(l, ids[i]);
	}
	if (app) {
		wlr_log(WLR_INFO, "console: %s, for the %s", app->id,
			which == DESK_CC_BT ? "Bluetooth lever" : which == DESK_CC_VOL
				? "volume knob" : "network");
		desk_app_launch(app);
	} else {
		wlr_log(WLR_INFO, "console: nothing installed to show this in full");
	}
	desk_apps_release(l);
	if (app) {
		snd_at(server, DESK_SND_KEY_PRESS, server->cursor->x, 0.6);
		console_toggle(server);
	}
}

static void console_drag(struct desk_server *server)
{
	if (server->cc_grab < 0) {
		return;
	}
	double v;
	if (!desk_console_value_at(server->desk_w, server->desk_h, server->cc_grab,
			server->cursor->x, server->cursor->y, &v)) {
		return;
	}
	/* the detents going by under the fingers */
	int step = (int)floor(v * 24.0);
	if (step != server->snd_knob) {
		server->snd_knob = step;
		snd_at(server, DESK_SND_KNOB_TICK, server->cursor->x, 1.0);
	}
	if (server->cc_grab == DESK_CC_VOL) {
		if (fabs(v - server->cc_vol) < 0.004) {
			return;
		}
		server->cc_vol = v;
		desk_meters_set_volume(v);
	} else if (server->cc_grab == DESK_CC_SCREEN) {
		if (fabs(v - server->cc_screen) < 0.004) {
			return;
		}
		server->cc_screen = v;
		screen_apply(server);
	} else {
		if (fabs(v - server->cc_bright) < 0.004) {
			return;
		}
		server->cc_bright = v;
		console_dim_apply(server);
	}
	paint_console(server);
	server_schedule_frames(server);
}

/* ================================================================
 * The post box
 *
 * It stands at the back right, and from the desk all you see of it is its
 * cap: the slot, the nameplate, and the corner of an envelope when there
 * is something in it. Clicking takes you round to the front, where the
 * door has fallen open and the letters are standing in it.
 *
 * Taking one out is not opening a panel. The envelope comes out of the
 * box, the paper comes out of the envelope, and the paper ends up on the
 * desk -- where it stays, because a letter you have read is still a piece
 * of paper on your desk until you do something with it.
 * ================================================================ */

/* Where the k-th letter taken out of the post lies on a desk w by h: a
 * sheet of letter paper, so many points by so many, in the middle of the
 * desk and a little further along each time. */
static void paper_place(struct desk_paper *pp, int w, int h, int k)
{
	double pt = desk_px_per_pt(w);
	pp->w = (int)lround(511.0 * pt);
	pp->h = (int)lround(410.0 * pt);
	pp->x = (int)lround(w * 0.5 - pp->w * 0.5 + k * 37.0 * pt);
	pp->y = (int)lround(h * 0.4825 - pp->h * 0.5 + k * 25.0 * pt);
}

/* The letters on the desk laid out again, each where its place in the pile
 * puts it -- after one has gone from under the others, so that none ends up
 * exactly on top of another. */
static void papers_place(struct desk_server *server, int w, int h)
{
	for (int i = 0; i < server->pb_papers; i++) {
		struct desk_paper *pp = &server->pb_paper[i];
		paper_place(pp, w, h, i);
		if (pp->placed) {
			/* where it was put, on a desk of another size the same part
			 * of the way across */
			pp->x = (int)lround(pp->fx * w);
			pp->y = (int)lround(pp->fy * h);
		}
		if (pp->node) {
			wlr_scene_buffer_set_dest_size(pp->node, pp->w, pp->h);
			wlr_scene_node_set_position(&pp->node->node, pp->x, pp->y);
		}
	}
}

static void post_land(struct desk_server *server);

static bool post_up(const struct desk_server *server)
{
	return server->pb_t > 0.0005 || server->pb_moving || server->pb_fly_on;
}

#ifdef DESK_HAVE_POST
static int post_count(const struct desk_note **out)
{
	return desk_post_letters(out);
}
static int post_unread(void)
{
	return desk_post_unread();
}
#else
static int post_count(const struct desk_note **out)
{
	*out = NULL;
	return 0;
}
static int post_unread(void)
{
	return 0;
}
#endif

static void pb_view_draw(cairo_t *cr, int w, int h, void *data)
{
	struct desk_server *server = data;
	if (server->pb_t > 0.70) {
		const struct desk_note *notes = NULL;
		int n = post_count(&notes);
		struct desk_post_curl pc = { server->pbc_idx, server->pbc_s, server->pbc_lift,
			server->pbc_hot };
		desk_draw_post(cr, w, h, notes, n, server->pb_hover, 1.0, server->pb_pos, &pc);
	}
	if (server->pb_fly_on) {
		double u = clampf_d((monotonic_sec() - server->pb_fly_start)
			/ server->pb_fly_dur, 0, 1);
		/* to where it will lie on the desk */
		struct desk_paper at;
		/* where it will lie: the last place, when the pile is full and the
		 * oldest is to go from under it (post_land) */
		int cap = (int)(sizeof server->pb_paper / sizeof server->pb_paper[0]);
		paper_place(&at, w, h, server->pb_papers < cap ? server->pb_papers : cap - 1);
		desk_draw_sheet_flight(cr, server->pb_fly,
			server->pb_fly_x, server->pb_fly_y, server->pb_fly_w, server->pb_fly_h,
			at.x, at.y, at.w, at.h, u);
	}
}

/* The settled view of the box, moved with the camera at the end of a move. */
static void pb_follow(struct desk_server *server, int dx, int dy)
{
	if (server->pb_sprite.node) {
		wlr_scene_node_set_position(&server->pb_sprite.node->node,
			server->pb_sprite.x + dx, server->pb_sprite.y + dy);
	}
	if (server->pb_base) {
		wlr_scene_node_set_position(&server->pb_base->node, dx, dy);
	}
}

static void paint_post_body(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	if (w < 8 || h < 8) {
		return;
	}
	if (!post_up(server)) {
		if (server->pb_sprite.node) {
			wlr_scene_node_set_enabled(&server->pb_sprite.node->node, false);
		}
		if (server->pb_base) {
			wlr_scene_node_set_enabled(&server->pb_base->node, false);
		}
		if (server->film_a[DESK_VIEW_POST]) {
			wlr_scene_node_set_enabled(&server->film_a[DESK_VIEW_POST]->node, false);
			wlr_scene_node_set_enabled(&server->film_b[DESK_VIEW_POST]->node, false);
		}
		return;
	}
	film_ensure(server, DESK_VIEW_POST, server->layer_post);

	if (!server->pb_base || server->pb_base_w != w || server->pb_base_h != h) {
		struct desk_buf *bb = buf_create(w, h);
		desk_draw_post(bb->cr, w, h, NULL, 0, -1, 0.0, 0.0, NULL);
		scene_put(&server->pb_base, server->layer_post, bb, 0, 0, NULL);
		server->pb_base_w = w;
		server->pb_base_h = h;
	}

	/* The letters standing in the box change when the post does, or the
	 * hand moves to another one -- not on the frames of the camera coming
	 * round to them, which only fade them in. So they are drawn again when
	 * the key says so and only faded otherwise; a letter in the air is
	 * somewhere new every frame. */
	bool have_view = server->pb_t > 0.70 || server->pb_fly_on;
	if (have_view) {
		const struct desk_note *notes = NULL;
		int n = post_count(&notes);
		bool settled = server->pb_t > 0.70;
		uint64_t key = key_add(14695981039346656037ull, &settled, sizeof settled);
		key = key_add(key, &n, sizeof n);
		key = key_add(key, &server->pb_hover, sizeof server->pb_hover);
		key = key_add(key, &server->pb_pos, sizeof server->pb_pos);
		int corner[3] = { server->pbc_idx, (int)lround(server->pbc_s * 4.0), server->pbc_hot };
		key = key_add(key, corner, sizeof corner);
		for (int i = 0; i < n; i++) {
			key = key_add(key, &notes[i].id, sizeof notes[i].id);
			key = key_add(key, &notes[i].read, sizeof notes[i].read);
			key = key_add(key, &notes[i].at, sizeof notes[i].at);
			key = key_add(key, &notes[i].urgency, sizeof notes[i].urgency);
			key = key_add(key, notes[i].app, strnlen(notes[i].app, sizeof notes[i].app));
			key = key_add(key, notes[i].summary,
				strnlen(notes[i].summary, sizeof notes[i].summary));
			key = key_add(key, notes[i].body, strnlen(notes[i].body, sizeof notes[i].body));
		}
		if (server->pb_fly_on) {
			double at = monotonic_sec();
			key = key_add(key, &at, sizeof at);
		}
		bool force = server->pb_sprite_w != w || server->pb_sprite_h != h;
		server->pb_sprite_w = w;
		server->pb_sprite_h = h;
		if (force || !server->pb_sprite.drawn || server->pb_sprite.key != key) {
			server->pb_sprite.key = key;
			/* the letters' own corner of the view, except while one is on
			 * its way to the desk */
			double box[4] = { 0.0, 0.0, (double)w, (double)h };
			if (!server->pb_fly_on && !desk_post_letters_area(w, h, box)) {
				box[0] = box[1] = 0.0;
				box[2] = w;
				box[3] = h;
			}
			sprite_paint(server, &server->pb_sprite, server->layer_post, box,
				pb_view_draw, server);
		}
	}
	if (server->pb_sprite.node) {
		wlr_scene_node_set_enabled(&server->pb_sprite.node->node, have_view);
	}
	wlr_scene_node_set_enabled(&server->pb_base->node, server->pb_t > 0.70);

	double t = server->pb_t;
	if (!server->film_a[DESK_VIEW_POST] || (!server->pb_moving && t >= 0.9995)) {
		film_hide(server, DESK_VIEW_POST);
		pb_follow(server, 0, 0);
		if (server->pb_sprite.node) {
			wlr_scene_buffer_set_opacity(server->pb_sprite.node, 1.0f);
		}
		wlr_scene_buffer_set_opacity(server->pb_base, 1.0f);
		return;
	}
	double over;
	int dx, dy;
	film_play(server, DESK_VIEW_POST, t, &over, &dx, &dy);
	pb_follow(server, dx, dy);
	double a = server->pb_fly_on ? 1.0 : over;
	if (server->pb_sprite.node) {
		wlr_scene_buffer_set_opacity(server->pb_sprite.node, (float)(have_view ? a : 0.0));
		wlr_scene_node_set_enabled(&server->pb_sprite.node->node,
			have_view && (over > 0.001 || server->pb_fly_on));
	}
	wlr_scene_buffer_set_opacity(server->pb_base, (float)over);
	wlr_scene_node_set_enabled(&server->pb_base->node, over > 0.001);
}

static void paint_post(struct desk_server *server)
{
	prof_begin(PROF_POST);
	paint_post_body(server);
	prof_end();
}

/* Turn the letters in the box over, a letter a step, as a binder's sheets
 * are turned: the one turned to stands up, the ones before it go over
 * forward and the ones after lean back. */
static void pb_riffle(struct desk_server *server, int step)
{
	const struct desk_note *notes = NULL;
	int n = post_count(&notes);
	double to = server->pb_pos_to + step;
	/* not further ahead of where the letters have got to than a couple */
	if (to > server->pb_pos + 2.5) {
		to = floor(server->pb_pos + 2.5);
	}
	if (to < server->pb_pos - 2.5) {
		to = ceil(server->pb_pos - 2.5);
	}
	if (to > n - 1) {
		to = n - 1;
	}
	if (to < 0) {
		to = 0;
	}
	if (to == server->pb_pos_to && !server->pb_pos_moving) {
		return;
	}
	server->pb_pos_to = to;
	if (!server->pb_pos_moving) {
		server->pb_pos_at = monotonic_sec();
		server->pb_pos_vel = 0.0;
	}
	server->pb_pos_moving = 1;
	/* a corner that was up goes down as its letter goes by */
	server->pbc_near = false;
	server->pbc_hover_at = 0.0;
	server->pbc_hot = -1;
	paint_post(server);
	server_schedule_frames(server);
}

/* The letters follow where they have been turned to on the binder's own
 * spring: over, a shade too far, and back. */
static void pb_pos_tick(struct desk_server *server)
{
	if (!server->pb_pos_moving) {
		return;
	}
	const double W = 24.0, Z = 0.72;
	double now = monotonic_sec();
	double dt = clampf_d(now - server->pb_pos_at, 0.0, 0.05);
	server->pb_pos_at = now;
	int k = (int)ceil(dt / (1.0 / 480.0));
	double h = k > 0 ? dt / k : 0.0;
	for (int i = 0; i < k; i++) {
		double acc = W * W * (server->pb_pos_to - server->pb_pos)
			- 2.0 * Z * W * server->pb_pos_vel;
		server->pb_pos_vel += acc * h;
		server->pb_pos += server->pb_pos_vel * h;
	}
	int letter = (int)lround(server->pb_pos);
	if (letter != server->snd_letter) {
		desk_sound_play(DESK_SND_LETTER_FLICK, 0.4, 1.0, 0.0);
		server->snd_letter = letter;
	}
	if (gfx_flat()) {
		server->pb_pos = server->pb_pos_to;
		server->pb_pos_vel = 0.0;
	}
	if (fabs(server->pb_pos_to - server->pb_pos) < 0.002 && fabs(server->pb_pos_vel) < 0.02) {
		server->pb_pos = server->pb_pos_to;
		server->pb_pos_vel = 0.0;
		server->pb_pos_moving = 0;
	}
	/* what is under the hand has moved */
	const struct desk_note *notes = NULL;
	int n = post_count(&notes);
	server->pb_hover = desk_post_hit(server->desk_w, server->desk_h, n, server->pb_pos,
		server->cursor->x, server->cursor->y);
	paint_post(server);
	if (server->pb_pos_moving) {
		server_schedule_frames(server);
	}
}

/* ---- a letter's corner -------------------------------------------
 *
 * The same corner a sheet on the desk has, on the letter that is standing
 * up to be read: the hand resting on its fold stirs it, held there or
 * clicked it turns up, and what is printed on its back is pressed there.
 * Clicking the corner of a letter further back turns the letters to it,
 * and its corner comes up as it arrives. */

static void post_take(struct desk_server *server, int idx);

static void pbc_reset(struct desk_server *server)
{
	server->pbc_idx = -1;
	server->pbc_id = 0;
	server->pbc_s = 0.0;
	server->pbc_v = 0.0;
	server->pbc_at = 0.0;
	server->pbc_lift = 0.0;
	server->pbc_near = false;
	server->pbc_hover_at = 0.0;
	server->pbc_hot = -1;
	server->pbc_act = -1;
}

/* the size of a letter's face, and its corner's */
static bool pbc_sizes(struct desk_server *server, double *fw, double *fh, double *rest,
	double *open)
{
	double fx, fy;
	*fw = *fh = 0.0;
	desk_post_on_letter(server->desk_w, server->desk_h, -1, 0, 0.0, 0.0, 0.0,
		&fx, &fy, fw, fh);
	if (*fw < 8.0) {
		return false;
	}
	desk_post_curl_sizes(*fw, *fh, rest, open);
	return true;
}

/* Where the pointer is on letter i's face, if it is on it. */
static bool pbc_on(struct desk_server *server, int i, int n, double *fx, double *fy,
	double *fw, double *fh)
{
	return desk_post_on_letter(server->desk_w, server->desk_h, i, n, server->pb_pos,
		server->cursor->x, server->cursor->y, fx, fy, fw, fh);
}

static void pbc_track(struct desk_server *server)
{
	if (server->pbc_act >= 0 || server->pb_pos_moving) {
		return;
	}
	const struct desk_note *notes = NULL;
	int n = post_count(&notes);
	int sel = (int)lround(server->pb_pos);
	bool near = server->pbc_near, on_fold = false;
	int hot = -1;
	double fx, fy, fw, fh, rest, open;
	if (sel >= 0 && sel < n && pbc_on(server, sel, n, &fx, &fy, &fw, &fh)) {
		desk_post_curl_sizes(fw, fh, &rest, &open);
		double dx = fw - fx;
		double zone = rest * 2.6;
		on_fold = dx >= -8.0 && dx <= zone && fy >= -8.0 && fy <= zone;
		if (near) {
			near = dx >= -8.0 && dx <= open * 1.30 && fy >= -8.0 && fy <= open * 1.30;
		}
		if (near) {
			hot = desk_post_curl_hit(&notes[sel], fw, fh, server->pbc_s, fx, fy);
		}
	} else {
		near = false;
	}
	if (near || on_fold) {
		if (server->pbc_idx != sel) {
			server->pbc_idx = sel;
			server->pbc_id = notes[sel].id;
			server->pbc_at = 0.0;
		}
	}
	if (!near && on_fold) {
		if (server->pbc_hover_at <= 0.0) {
			server->pbc_hover_at = real_sec();
		}
	} else {
		server->pbc_hover_at = 0.0;
	}
	if (near != server->pbc_near || hot != server->pbc_hot || on_fold) {
		server->pbc_near = near;
		server->pbc_hot = hot;
		paint_post(server);
		server_schedule_frames(server);
	}
}

/* A click at the post box: on the back of a turned-up corner, on a
 * corner's fold, or neither (false). */
static bool pbc_press(struct desk_server *server)
{
	const struct desk_note *notes = NULL;
	int n = post_count(&notes);
	if (server->pbc_act >= 0) {
		return true;
	}
	int idx = server->pbc_idx;
	if (server->pbc_near && server->pbc_hot >= 0 && idx >= 0 && idx < n) {
		server->pbc_act = server->pbc_hot;
		desk_sound_play(DESK_SND_CORNER_TAP, 0.2, 1.0, 0.0);
		server->pbc_act_at = monotonic_sec();
		if (server->pbc_act == DESK_LETTER_MARK) {
#ifdef DESK_HAVE_POST
			desk_post_set_read(notes[idx].id, !notes[idx].read);
#endif
		}
		server_schedule_frames(server);
		return true;
	}
	/* the fold of the letter standing up, or of one further back, whose
	 * head is all that shows: the front-most letter under the hand */
	int sel = (int)lround(server->pb_pos);
	for (int i = sel; i >= 0 && i < n && i < sel + 5; i++) {
		double fx, fy, fw, fh, rest, open;
		if (!pbc_on(server, i, n, &fx, &fy, &fw, &fh)) {
			continue;
		}
		desk_post_curl_sizes(fw, fh, &rest, &open);
		double zone = rest * 2.6;
		if (!(fw - fx <= zone && fy <= zone)) {
			return false;
		}
		if (i != sel) {
			pb_riffle(server, i - sel);
		}
		server->pbc_idx = i;
		server->pbc_id = notes[i].id;
		desk_sound_play(DESK_SND_CORNER_LIFT, 0.2, 1.0, 0.0);
		server->pbc_near = true;
		server->pbc_hover_at = 0.0;
		server->pbc_hot = -1;
		server_schedule_frames(server);
		return true;
	}
	return false;
}

/* Once a frame: the corner goes where it is going, on the spring a
 * sheet's corner has, and what was pressed on it is done. */
static void pbc_tick(struct desk_server *server)
{
	if (server->pbc_idx < 0) {
		return;
	}
	const struct desk_note *notes = NULL;
	int n = post_count(&notes);
	int idx = server->pbc_idx;
	double fw, fh, rest, open;
	if (!post_up(server) || idx >= n || notes[idx].id != server->pbc_id
		|| !pbc_sizes(server, &fw, &fh, &rest, &open)) {
		/* the box was shut, or the letter went */
		pbc_reset(server);
		paint_post(server);
		return;
	}
	double now = monotonic_sec();
	bool more = false;
	if (!server->pbc_near && server->pbc_hover_at > 0.0) {
		if (real_sec() - server->pbc_hover_at >= 0.5 && server->conf.corner_hover) {
			desk_sound_play(DESK_SND_CORNER_LIFT, 0.2, 1.0, 0.0);
			server->pbc_near = true;
			server->pbc_hover_at = 0.0;
		} else {
			more = true;
		}
	}
	double to = server->pbc_near ? open : (server->pbc_hover_at > 0.0 ? rest * 1.7 : rest);
	if (server->pbc_act >= 0) {
		double e = now - server->pbc_act_at;
		unsigned id = notes[idx].id;
		switch (server->pbc_act) {
		case DESK_LETTER_READ:
			/* laid down again, and the letter comes out */
			to = rest;
			if (e > 0.14) {
				pbc_reset(server);
				post_take(server, idx);
				return;
			}
			break;
		case DESK_LETTER_MARK:
			/* a nod of the corner: it is done, and the words have changed */
			to = open * 0.86;
			if (e > 0.16) {
				server->pbc_act = -1;
			}
			break;
		case DESK_LETTER_ACTION:
			to = open * 1.25;
			if (e > 0.18) {
				const char *key = desk_post_letter_action(&notes[idx], NULL);
				bool own = notes[idx].own;
				pbc_reset(server);
				if (key && own) {
					own_letter_action(server, id, key);
				} else if (key) {
#ifdef DESK_HAVE_POST
					desk_post_invoke(id, key);
#endif
				}
				paint_post(server);
				return;
			}
			break;
		default:
			/* peeled right back, and the letter is thrown away */
			to = open * 1.9;
			if (e > 0.22) {
				pbc_reset(server);
#ifdef DESK_HAVE_POST
				desk_post_dismiss(id);
#endif
				(void)id;
				paint_post(server);
				return;
			}
			break;
		}
		more = true;
	}
	double dt = server->pbc_at > 0.0 ? clampf_d(now - server->pbc_at, 0.0, 0.05) : 0.0;
	server->pbc_at = now;
	if (server->pbc_s <= 0.0) {
		server->pbc_s = rest;
	}
	const double k = 240.0, c = 2.0 * 0.60 * 15.49;
	double acc = k * (to - server->pbc_s) - c * server->pbc_v;
	server->pbc_v += acc * dt;
	server->pbc_s += server->pbc_v * dt;
	if (gfx_flat()) {
		server->pbc_s = to;
		server->pbc_v = 0.0;
	}
	if (fabs(to - server->pbc_s) < 0.15 && fabs(server->pbc_v) < 2.0) {
		server->pbc_s = to;
		server->pbc_v = 0.0;
	} else {
		more = true;
	}
	server->pbc_lift = clampf_d((server->pbc_s - rest) / fmax(open - rest, 1.0), 0.0, 1.0);
	/* the words move with the flap, under a pointer that may not */
	if (server->pbc_near && server->pbc_act < 0) {
		double fx, fy, w2, h2;
		server->pbc_hot = pbc_on(server, idx, n, &fx, &fy, &w2, &h2)
			? desk_post_curl_hit(&notes[idx], fw, fh, server->pbc_s, fx, fy) : -1;
	}
	paint_post(server);
	if (more) {
		server_schedule_frames(server);
	} else if (!server->pbc_near && server->pbc_hover_at <= 0.0 && server->pbc_act < 0
		&& fabs(server->pbc_s - rest) < 0.2) {
		/* down again: nobody's corner */
		server->pbc_idx = -1;
		paint_post(server);
	}
}

/* The paper the flight was carrying comes to rest on the desk. It stays
 * there: a letter you have read is still a piece of paper on your desk
 * until you do something with it, and putting it away is a thing you do,
 * not a thing that happens to you. */
static void post_land(struct desk_server *server)
{
	server->pb_fly_on = 0;
	if (!server->pb_fly) {
		return;
	}
	int w = server->desk_w, h = server->desk_h;
	if (server->pb_papers >= (int)(sizeof server->pb_paper / sizeof server->pb_paper[0])) {
		/* the desk only takes so much before the bottom one goes away */
		struct desk_paper *old = &server->pb_paper[0];
		if (old->node) {
			wlr_scene_node_destroy(&old->node->node);
		}
		if (old->sheet) {
			cairo_surface_destroy(old->sheet);
		}
		memmove(&server->pb_paper[0], &server->pb_paper[1],
			(size_t)(server->pb_papers - 1) * sizeof server->pb_paper[0]);
		server->pb_papers--;
		/* (the one in the hand is one further down now, or gone) */
		if (server->pp_held > 0) {
			server->pp_held--;
		}
		papers_place(server, w, h);
	}
	struct desk_paper *pp = &server->pb_paper[server->pb_papers++];
	memset(pp, 0, sizeof *pp);
	pp->sheet = server->pb_fly;
	pp->id = server->pb_fly_id;
	server->pb_fly = NULL;

	/* a sheet of letter paper, in points, laid down in the middle of the
	 * desk a little further along each time, and never square */
	int k = server->pb_papers - 1;
	paper_place(pp, w, h, k);

	struct desk_buf *buf = buf_create(pp->w, pp->h);
	desk_set_paint_origin(pp->x, pp->y);
	desk_draw_window_shadow(buf->cr, 0, 0, pp->w, pp->h, 6.0);
	cairo_set_source_surface(buf->cr, pp->sheet, 0, 0);
	cairo_paint(buf->cr);
	desk_set_paint_origin(0, 0);
	scene_put(&pp->node, server->layer_win, buf, pp->x, pp->y, NULL);
	wlr_scene_node_set_enabled(&pp->node->node, true);
	snd_at(server, DESK_SND_SHEET_DOWN, pp->x + pp->w * 0.5, 0.8);
	paint_post(server);
	server_schedule_frames(server);
}

/* Put one away: it is thrown out, and the sender is told so. */
static void post_paper_away(struct desk_server *server, int i)
{
	if (i < 0 || i >= server->pb_papers) {
		return;
	}
	struct desk_paper *pp = &server->pb_paper[i];
	snd_at(server, DESK_SND_PAPER_CRUMPLE, pp->x + pp->w * 0.5, 0.7);
	if (pp->node) {
		wlr_scene_node_destroy(&pp->node->node);
	}
	if (pp->sheet) {
		cairo_surface_destroy(pp->sheet);
	}
#ifdef DESK_HAVE_POST
	desk_post_dismiss(pp->id);
#endif
	memmove(&server->pb_paper[i], &server->pb_paper[i + 1],
		(size_t)(server->pb_papers - i - 1) * sizeof server->pb_paper[0]);
	server->pb_papers--;
	if (server->pp_held == i + 1) {
		server->pp_held = 0;
	} else if (server->pp_held > i + 1) {
		server->pp_held--;
	}
	papers_place(server, server->desk_w, server->desk_h);
	server_schedule_frames(server);
}

static bool paper_release(struct desk_server *server);

/* The letter in the hand follows it, once it has moved more than a
 * twitch: picked up, it comes over everything on the desk. */
static bool paper_drag(struct desk_server *server)
{
	if (server->pp_held <= 0 || server->pp_held > server->pb_papers) {
		return false;
	}
	/* the door shut, or the bookcase, the set or the post box come up,
	 * with it in the hand: it is put down where it is */
	if (locked(server) || shelf_up(server) || console_up(server) || post_up(server)) {
		if (server->pp_moved) {
			paper_release(server);
		}
		server->pp_held = 0;
		return false;
	}
	struct desk_paper *pp = &server->pb_paper[server->pp_held - 1];
	double cx = server->cursor->x, cy = server->cursor->y;
	if (!server->pp_moved) {
		double twitch = 5.0 * desk_px_per_pt(server->desk_w);
		if (hypot(cx - server->pp_px, cy - server->pp_py) < twitch) {
			return true;
		}
		server->pp_moved = true;
		if (pp->node) {
			wlr_scene_node_raise_to_top(&pp->node->node);
			views_above_raise(server);
		}
		snd_at(server, DESK_SND_PAPER_SWING, cx, 0.35);
	}
	/* kept on the desk: at least a corner of it in reach */
	int w = server->desk_w, h = server->desk_h;
	double x = fmin(fmax(cx - server->pp_hx, 32.0 - pp->w), w - 32.0);
	double y = fmin(fmax(cy - server->pp_hy, 0.0), h - 32.0);
	pp->x = (int)lround(x);
	pp->y = (int)lround(y);
	if (pp->node) {
		wlr_scene_node_set_position(&pp->node->node, pp->x, pp->y);
	}
	server_schedule_frames(server);
	return true;
}

/* Let go of the letter in the hand: put down where it is, or -- clicked
 * and not moved -- put away. True if there was one. */
static bool paper_release(struct desk_server *server)
{
	if (server->pp_held <= 0) {
		return false;
	}
	int i = server->pp_held - 1;
	server->pp_held = 0;
	if (i >= server->pb_papers) {
		return true;
	}
	if (!server->pp_moved) {
		post_paper_away(server, i);
		return true;
	}
	struct desk_paper *pp = &server->pb_paper[i];
	pp->placed = true;
	pp->fx = server->desk_w > 0 ? (double)pp->x / server->desk_w : 0.0;
	pp->fy = server->desk_h > 0 ? (double)pp->y / server->desk_h : 0.0;
	snd_at(server, DESK_SND_SHEET_DOWN, pp->x + pp->w * 0.5, 0.6);
	server_schedule_frames(server);
	return true;
}

/* The letter lying on the desk that is the uppermost thing under the
 * pointer, or -1: a window over it is the window's. The letters lie among
 * the windows, in their layer, so that a window can lie over one -- which
 * is why a click is asked about them before it is taken to be a window's. */
static int post_paper_under(struct desk_server *server)
{
	double sx, sy;
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node,
		server->cursor->x, server->cursor->y, &sx, &sy);
	for (int i = server->pb_papers - 1; node && i >= 0; i--) {
		if (server->pb_paper[i].node && node == &server->pb_paper[i].node->node) {
			return i;
		}
	}
	return -1;
}

/* Take a letter out of the box: the envelope comes out, the paper comes
 * out of the envelope, and it goes down onto the desk with you. */
static void post_take(struct desk_server *server, int idx)
{
	const struct desk_note *notes = NULL;
	int n = post_count(&notes);
	if (idx < 0 || idx >= n) {
		return;
	}
	const struct desk_note *note = &notes[idx];
	int w = server->desk_w, h = server->desk_h;
	double bx, by, bw, bh;
	if (!desk_post_letter_box(w, h, idx, n, server->pb_pos, &bx, &by, &bw, &bh)) {
		return;
	}
	snd_at(server, DESK_SND_LETTER_TAKE, bx + bw * 0.5, 1.0);
	if (server->pb_fly) {
		cairo_surface_destroy(server->pb_fly);
	}
	/* the size it will lie on the desk at (paper_place) */
	double pt = desk_px_per_pt(w);
	server->pb_fly = desk_letter_sheet(note, lround(511.0 * pt), lround(410.0 * pt));
	server->pb_fly_x = bx;
	server->pb_fly_y = by;
	server->pb_fly_w = bw;
	server->pb_fly_h = bh;
	server->pb_fly_id = note->id;
	server->pb_fly_start = monotonic_sec();
	server->pb_fly_dur = 0.82;
	server->pb_fly_on = server->pb_fly != NULL;
#ifdef DESK_HAVE_POST
	desk_post_mark_read(note->id);
#endif
	wlr_log(WLR_INFO, "post: taking out \"%s\" from %s", note->summary, note->app);
	/* and you come back to the desk to read it */
	if (server->pb_t > 0.5) {
		post_toggle(server);
	}
}

static void post_toggle(struct desk_server *server)
{
	/* whatever was pulled out from under the Control Panel goes back */
	card_gone(server);
	if (server->pb_t < 0.5
		&& !desk_scene_view_ready(server->desk_w, server->desk_h, DESK_VIEW_POST)) {
		return;
	}
	double now = monotonic_sec();
	double cur = server->pb_t;
	server->pb_from = cur;
	server->pb_to = cur > 0.5 ? 0.0 : 1.0;
	server->pb_start = now;
	server->pb_hover = -1;
	/* it opens at the front letter, with every corner down */
	server->pb_pos = server->pb_pos_to = server->pb_pos_vel = 0.0;
	server->pb_pos_moving = 0;
	server->pb_scroll = 0.0;
	server->snd_letter = 0;
	pbc_reset(server);
	double span = fabs(server->pb_to - cur);
	if (span < 0.30) {
		span = 0.30;
	}
	server->pb_dur = 0.86 * span;
	server->pb_moving = 1;
	desk_sound_play(server->pb_to > 0.5 ? DESK_SND_POST_OPEN : DESK_SND_POST_CLOSE,
		0.55, 1.0, 0.0);
	if (server->pb_to > 0.5 && server->drawer_t > 0.004) {
		drawer_toggle(server);
	}
	server_schedule_frames(server);
	paint_post(server);
}

static void post_tick(struct desk_server *server)
{
	if (server->pb_moving) {
		double u = clampf_d((monotonic_sec() - server->pb_start) / server->pb_dur, 0, 1);
		double e = look_ease(u);
		server->pb_t = server->pb_from + (server->pb_to - server->pb_from) * e;
		if (u >= 1.0) {
			server->pb_t = server->pb_to;
			server->pb_moving = 0;
		}
		paint_post(server);
		server_schedule_frames(server);
	}
	if (!server->pb_fly_on) {
		return;
	}
	double u = clampf_d((monotonic_sec() - server->pb_fly_start)
		/ server->pb_fly_dur, 0, 1);
	if (u >= 1.0) {
		post_land(server);
		return;
	}
	paint_post(server);
	server_schedule_frames(server);
}

static void drawer_toggle(struct desk_server *server)
{
	/* whatever was pulled out from under the Control Panel goes back */
	card_gone(server);
	double now = monotonic_sec();
	/* pushed back partway, it goes back from where it has got to */
	double cur = server->drawer_t;
	server->drawer_from = cur;
	server->drawer_to = cur > 0.5 ? 0.0 : 1.0;
	if (server->drawer_to < 0.5) {
		server->drw_hover = -1;
	}
	server->drw_dirty = true;
	server->drawer_start = now;
	double span = fabs(server->drawer_to - cur);
	if (span < 0.28) {
		span = 0.28;
	}
	server->drawer_dur = 0.54 * span;
	server->drawer_moving = 1;
	/* along the runners, for as long as it moves (the stop, if it was on
	 * its way to one, is not reached now) */
	desk_sound_stop(server->snd_drawer, 0.04);
	snd_forget(&server->snd_drawer_stop);
	server->snd_drawer = desk_sound_play(server->drawer_to > 0.5 ? DESK_SND_DRAWER_OUT
		: DESK_SND_DRAWER_IN, 0.0, 0.55 + 0.45 * fmin(span, 1.0), 0.0);
	desk_sound_stop_after(server->snd_drawer, real_secs(server->drawer_dur) + SND_SEEN, 0.05);
	server_schedule_frames(server);
	drawer_compose(server);
	fullscreen_layer_order(server);
}

static void drawer_tick(struct desk_server *server)
{
	if (!server->drawer_moving) {
		return;
	}
	double u = clampf_d((monotonic_sec() - server->drawer_start) / server->drawer_dur, 0, 1);
	server->drawer_t = server->drawer_from
		+ (server->drawer_to - server->drawer_from) * drawer_ease(u);
	/* at its stop, or home (the slide has been cut to end there) */
	snd_end(&server->snd_drawer_stop, server->drawer_start, server->drawer_dur,
		server->drawer_to > 0.5 ? DESK_SND_DRAWER_STOP_OUT : DESK_SND_DRAWER_STOP_IN,
		0.0, 1.0, 0.0);
	if (u >= 1.0) {
		server->drawer_t = server->drawer_to;
		server->drawer_moving = 0;
		server->snd_drawer = 0;
		/* home: a game on the whole screen is in front of it again */
		fullscreen_layer_order(server);
	}
	/* Both sheets are baked, so a frame of the animation is two node
	 * moves -- and what lies on the desk moves with it rather than being
	 * drawn again. */
	room_layout(server);
	overlay_place(server);
	if (server->drawer_moving) {
		server_schedule_frames(server);
	}
}

static void sheet_begin_unfold(struct desk_server *server, struct desk_sheet *sheet)
{
	if (sheet->phase == SHEET_IDLE) {
		return;
	}
	/* in the drawer: out of it, from where it lies there; in the hand:
	 * let go of first */
	if (sheet->in_drawer) {
		int i = drawer_index_of(server, sheet);
		if (i >= 0) {
			drawer_lift_sheet(server, i);
		}
	}
	drawer_forget(server, sheet);
	sheet->to_drawer = false;   /* turned back before it got there */
	double now = monotonic_sec();
	if (sheet->phase == SHEET_FOLDING) {
		double u = clampf_d((now - sheet->start) / sheet->dur, 0, 1);
		/* turned back: the presses and the landing asked for ahead will
		 * not come; it opens from the stage it has really got to */
		sheet_snd_forget(sheet);
		sheet->snd_k = desk_fold_stage(sheet->fold, u);
		sheet->phase = SHEET_UNFOLDING;
		sheet->dur = 0.95;
		sheet->start = now - (1.0 - u) * sheet->dur;
		server_schedule_frames(server);
		return;
	}
	if (sheet->phase != SHEET_PARKED) {
		return;
	}
	sheet->phase = SHEET_UNFOLDING;
	sheet->dur = 0.95;
	sheet->start = now;
	sheet_snd_forget(sheet);
	sheet->snd_k = desk_fold_stages(sheet->fold);
	/* picked up off the desk to be opened out */
	snd_at(server, DESK_SND_CORNER_LIFT, sheet->hit_x + sheet->hit_w * 0.5, 1.4);
	server_schedule_frames(server);
	sheet_tick_one(server, sheet);
}

static void sheet_begin_fold(struct desk_server *server, struct desk_sheet *sheet,
	cairo_surface_t *src, double x, double y, struct wlr_scene_tree *live, void *hit,
	cairo_surface_t *icon, const char *name, struct wlr_output *out)
{
	if (sheet->phase == SHEET_FOLDING) {
		cairo_surface_destroy(src);
		return;
	}
	if (sheet->phase == SHEET_PARKED || sheet->phase == SHEET_UNFOLDING) {
		cairo_surface_destroy(src);
		sheet_begin_unfold(server, sheet);
		return;
	}
	if (sheet->src) {
		cairo_surface_destroy(sheet->src);
	}
	sheet->src = src;
	sheet->phase = SHEET_FOLDING;
	sheet->dur = 1.10;
	sheet->start = monotonic_sec();
	sheet->x0 = x;
	sheet->y0 = y;
	sheet->live = live;
	sheet->hit = hit;
	sheet->in_drawer = false;
	/* the window's title, as much as fits, cut between characters: cairo
	 * refuses half of one, and everything after it on the same sheet */
	size_t nl = name ? strlen(name) : 0;
	if (nl >= sizeof sheet->name) {
		nl = sizeof sheet->name - 1;
		while (nl > 0 && ((unsigned char)name[nl] & 0xc0) == 0x80) {
			nl--;
		}
	}
	memcpy(sheet->name, name ? name : "", nl);
	sheet->name[nl] = '\0';
	if (sheet->tree && sheet->tree->node.parent != server->layer_parked) {
		wlr_scene_node_reparent(&sheet->tree->node, server->layer_parked);
	}
	sheet->slot = sheet_alloc_slot(server, out, sheet);
	static unsigned sheet_seed;
	sheet->seed = ++sheet_seed;
	/* folded down to about a postcard, whatever size it was */
	double packet = fmax(153.0 * desk_px_per_pt(server->desk_w), 96.0);
	desk_fold_free(sheet->fold);
	sheet->fold = desk_fold_new(src, sheet->seed, packet, icon, name);
	int ow, oh, pad;
	desk_fold_extent(sheet->fold, 1.0, &ow, &oh, &pad);
	sheet->to_drawer = server->conf.fold_drawer && server->drw_n < DRAWER_MAX;
	if (sheet->to_drawer) {
		/* down over the front edge of the desk, where the drawer is, and
		 * into it */
		sheet->slot = -1;
		sheet->x1 = server->desk_w * 0.5 - ow * 0.5 - server->layer_parked->node.x;
		sheet->y1 = server->desk_h + 4 - server->layer_parked->node.y;
	} else {
		/* along the front of the screen it was on */
		sheet_slot_xy(server, out, sheet->slot, ow, oh, &sheet->x1, &sheet->y1);
	}
	if (live) {
		wlr_scene_node_set_enabled(&live->node, false);
	}
	/* the first flap goes over */
	sheet_snd_forget(sheet);
	sheet->snd_k = 0;
	{
		int n = desk_fold_stages(sheet->fold);
		snd_for(server, DESK_SND_PAPER_SWING, x + ow * 0.5, 0.9, 0.40,
			sheet->dur * desk_fold_done() / (n > 0 ? n : 1));
	}
	server_schedule_frames(server);
	sheet_tick_one(server, sheet);
}

static void sheet_cleanup(struct desk_server *server, struct desk_sheet *sheet)
{
	drawer_forget(server, sheet);
	if (sheet->src) {
		cairo_surface_destroy(sheet->src);
		sheet->src = NULL;
	}
	desk_fold_free(sheet->fold);
	sheet->fold = NULL;
	if (sheet->tree) {
		wlr_scene_node_destroy(&sheet->tree->node);
		sheet->tree = NULL;
		sheet->buf = NULL;
	}
	sheet->phase = SHEET_IDLE;
	sheet->live = NULL;
}

static bool sheet_hit(struct desk_sheet *sheet, double x, double y)
{
	if (sheet->phase == SHEET_IDLE || !sheet->src) {
		return false;
	}
	double pad = 26;
	return x >= sheet->hit_x - pad && y >= sheet->hit_y - pad &&
		x <= sheet->hit_x + sheet->hit_w + pad && y <= sheet->hit_y + sheet->hit_h + pad;
}

/* What the client last drew, onto cr at (x, y): out of memory where the
 * client shares memory, and back off the GPU where it hands over GPU
 * buffers -- which is what every X program does, Xwayland drawing with GL. */
static bool paint_client(cairo_t *cr, struct wlr_surface *ws, double x, double y)
{
	if (!ws || !ws->buffer) {
		return false;
	}
	struct wlr_buffer *src = ws->buffer->source ? ws->buffer->source : &ws->buffer->base;
	void *data = NULL;
	uint32_t fmt = 0;
	size_t stride = 0;
	if (wlr_buffer_begin_data_ptr_access(src, WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &fmt, &stride)) {
		bool ok = data && (fmt == DRM_FORMAT_ARGB8888 || fmt == DRM_FORMAT_XRGB8888);
		if (ok) {
			/* XRGB's fourth byte is anything at all, not an alpha */
			cairo_surface_t *img = cairo_image_surface_create_for_data(data,
				fmt == DRM_FORMAT_XRGB8888 ? CAIRO_FORMAT_RGB24 : CAIRO_FORMAT_ARGB32,
				src->width, src->height, (int)stride);
			cairo_set_source_surface(cr, img, x, y);
			cairo_paint(cr);
			cairo_surface_destroy(img);
		}
		wlr_buffer_end_data_ptr_access(src);
		if (ok) {
			return true;
		}
	}
	struct wlr_texture *tex = wlr_surface_get_texture(ws);
	if (!tex) {
		return false;
	}
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		(int)tex->width, (int)tex->height);
	if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(img);
		return false;
	}
	cairo_surface_flush(img);
	bool ok = wlr_texture_read_pixels(tex, &(struct wlr_texture_read_pixels_options){
		.data = cairo_image_surface_get_data(img),
		.format = DRM_FORMAT_ARGB8888,
		.stride = (uint32_t)cairo_image_surface_get_stride(img),
	});
	if (ok) {
		cairo_surface_mark_dirty(img);
		cairo_set_source_surface(cr, img, x, y);
		cairo_paint(cr);
	}
	cairo_surface_destroy(img);
	return ok;
}

static void curl_sizes(const struct desk_toplevel *view, double *rest, double *open);

static cairo_surface_t *view_snapshot(struct desk_toplevel *view)
{
	int w = view->outer_w;
	int h = view->outer_h;
	if (w < 40) {
		w = 40;
	}
	if (h < 40) {
		h = 40;
	}
	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cairo_t *cr = cairo_create(surf);
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	if (view_framed(view) && view->own_frame) {
		desk_draw_window_mount(cr, 0, 0, w, h, view_title(view));
	} else if (view_framed(view)) {
		desk_draw_window_frame(cr, 0, 0, w, h, view_title(view), true, 0, false);
	} else {
		/* nothing of ours to fold: a plain sheet under what the client drew */
		cairo_set_source_rgb(cr, 0.96, 0.95, 0.92);
		cairo_paint(cr);
	}
	struct wlr_box geo;
	view_geometry(view, &geo);
	cairo_save(cr);
	cairo_rectangle(cr, l, t, w - l - r, h - t - b);
	cairo_clip(cr);
	paint_client(cr, view_surface(view), l - geo.x, t - geo.y);
	cairo_restore(cr);
	/* and its corner as it is this moment, turned down or lifted, so what
	 * is folded or thrown away is the sheet as it was left */
	if (view->curl && view->curl->node.enabled && view->curl_s > 0.5) {
		double rest, open;
		curl_sizes(view, &rest, &open);
		double sz = clampf_d(view->curl_s, 0.0, fmin(view->outer_w, view->outer_h) * 0.8);
		double B = view->curl_box;
		double lift = clampf_d((sz - rest) / fmax(open - rest, 1.0), 0.0, 1.3);
		cairo_save(cr);
		cairo_translate(cr, w - B, 0);
		cairo_move_to(cr, B - sz, 0);
		cairo_line_to(cr, B, 0);
		cairo_line_to(cr, B, sz);
		cairo_close_path(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
		cairo_fill(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		desk_draw_curl(cr, B, sz, open, lift, -1, view->maximized != 0, view_title(view));
		cairo_restore(cr);
	}
	cairo_destroy(cr);
	return surf;
}

static void focus_next(struct desk_server *server);

/* ---- a sheet taken out of a binder ---------------------------------
 *
 * Taking a program's sheet out of its binder takes the sheet: it comes off
 * the ring into the hand, the binder goes back up, you turn back to the
 * desk with it, and it is put down where the program's window will be.
 * There the paper comes out from behind it, the way it does when a sheet
 * is made bigger, until it is the window's size, the catalogue's print
 * going off it: a blank sheet with the program's name at its head -- the
 * window, waiting for the program. When the program's window comes, it
 * comes there, onto it (held back, if it is quicker than the paper, until
 * the paper is down and open); a program that never sends one has its
 * sheet screwed up and thrown away after a while. */

static void throw_paper(struct desk_server *server, cairo_surface_t *snap, double x0,
	double y0);

/* Where the next new window goes: down and to the right of the last, round
 * again after six. */
static void cascade_next(struct desk_server *server, int *x, int *y)
{
	int n = server->cascade++;
	struct wlr_output *out = screen_at(server, server->cursor->x, server->cursor->y);
	if (screen_is_side(server, out)) {
		/* on the screen beside the desk's that the pointer is on */
		struct wlr_box a = screen_area(server, out);
		*x = a.x + a.width / 6 + (n % 6) * 28;
		*y = a.y + a.height / 10 + (n % 6) * 24;
		return;
	}
	*x = 320 + (n % 6) * 28;
	*y = work_area(server).y + 8 + (n % 6) * 24;
}

/* The size a new window is offered, within what the screen it will come
 * onto (the pointer's) has room for. */
static void window_offer(struct desk_server *server, int *ow, int *oh)
{
	struct wlr_box wa = screen_area(server,
		screen_at(server, server->cursor->x, server->cursor->y));
	*ow = 680;
	*oh = 430;
	if (*ow > wa.width - DESK_BORDER * 2) {
		*ow = wa.width - DESK_BORDER * 2;
	}
	if (*oh > wa.height - DESK_TITLE_H - DESK_BORDER - 16) {
		*oh = wa.height - DESK_TITLE_H - DESK_BORDER - 16;
	}
}

static void base_free(struct desk_base *b)
{
	if (b->node) {
		wlr_scene_node_destroy(&b->node->node);
	}
	if (b->face) {
		cairo_surface_destroy(b->face);
	}
	if (b->blank) {
		cairo_surface_destroy(b->blank);
	}
	memset(b, 0, sizeof *b);
}

/* A sheet for app, taken out of its binder (face, sw by sh, is how it was
 * printed there), to be put down where its window will go. */
static int base_new(struct desk_server *server, const struct desk_app *app,
	cairo_surface_t *face, double sw, double sh)
{
	int slot = -1;
	double oldest = 1e30;
	int old = -1;
	for (int i = 0; i < DESK_BASES; i++) {
		struct desk_base *b = &server->bases[i];
		if (b->state == BASE_FREE) {
			slot = i;
			break;
		}
		if (b->state == BASE_WAITING && b->taken_at < oldest) {
			oldest = b->taken_at;
			old = i;
		}
	}
	if (slot < 0 && old >= 0) {
		/* the desk only takes so many sheets waiting for their programs */
		base_free(&server->bases[old]);
		slot = old;
	}
	if (slot < 0) {
		cairo_surface_destroy(face);
		return -1;
	}
	struct desk_base *b = &server->bases[slot];
	int ow, oh;
	window_offer(server, &ow, &oh);
	b->w = ow + DESK_BORDER * 2;
	b->h = oh + DESK_TITLE_H + DESK_BORDER;
	cascade_next(server, &b->x, &b->y);
	struct wlr_box wa = work_area(server);
	if (b->x + b->w > wa.x + wa.width) {
		b->x = wa.x + wa.width - b->w;
	}
	if (b->y + b->h > wa.y + wa.height) {
		b->y = wa.y + wa.height - b->h;
	}
	b->x = b->x < wa.x ? wa.x : b->x;
	b->y = b->y < wa.y ? wa.y : b->y;
	/* put down, it is further from the eye than it was in the hand */
	double k = fmin(0.78, fmin(b->w * 0.92 / sw, b->h * 0.92 / sh));
	b->lw = sw * k;
	b->lh = sh * k;
	b->face = face;
	b->blank = desk_window_base_sheet(b->w, b->h, app->name);
	snprintf(b->app, sizeof b->app, "%s", app->id);
	snprintf(b->name, sizeof b->name, "%s", app->name);
	b->seed = app->seed;
	b->sid = 0;
	b->view = NULL;
	b->state = BASE_CARRIED;
	b->at = monotonic_sec();
	/* how long it has waited for its program is real time, however fast
	 * things move on the desk */
	b->taken_at = real_sec();
	return slot;
}

/* The sheet on the desk, as far open as it is. */
static void base_paint(struct desk_server *server, struct desk_base *b, double open)
{
	int pad = DESK_SHADOW;
	struct desk_buf *buf = buf_create(b->w + pad * 2, b->h + pad * 2);
	desk_set_paint_origin(b->x - pad, b->y - pad);
	cairo_translate(buf->cr, pad, pad);
	desk_draw_window_base(buf->cr, b->blank, b->face, b->w, b->h, b->lw, b->lh, open,
		b->seed);
	desk_set_paint_origin(0, 0);
	bool fresh = b->node == NULL;
	scene_put(&b->node, server->layer_win, buf, b->x - pad, b->y - pad, NULL);
	if (fresh) {
		/* over the windows already on the desk: it was put down on them */
		wlr_scene_node_raise_to_top(&b->node->node);
		views_above_raise(server);
	}
	wlr_scene_node_set_enabled(&b->node->node, true);
}

/* The window that has come to it is let out, and the sheet goes. */
static void base_hand_over(struct desk_server *server, struct desk_base *b)
{
	struct desk_toplevel *view = b->view;
	if (view && view->scene_tree) {
		wlr_scene_node_set_enabled(&view->scene_tree->node, true);
		wlr_scene_node_raise_to_top(&view->scene_tree->node);
		views_above_raise(server);
	}
	b->state = BASE_GOING;
	b->at = monotonic_sec();
	server_schedule_frames(server);
}

/* The program a window belongs to, by its desktop file id. */
static bool view_app_id(const struct desk_toplevel *view, char *out, size_t n)
{
	const char *app_id = NULL, *cls = NULL, *inst = NULL;
	if (view->xdg_toplevel) {
		app_id = view->xdg_toplevel->app_id;
	}
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		cls = view->xsurface->class;
		inst = view->xsurface->instance;
	}
#endif
	const struct desk_app_list *l = desk_apps_acquire();
	const struct desk_app *app = desk_apps_match(l, app_id, cls, inst);
	if (app) {
		snprintf(out, n, "%s", app->id);
	}
	desk_apps_release(l);
	return app != NULL;
}

/* The session the process behind a window is in, or 0. */
static pid_t view_session(const struct desk_toplevel *view)
{
	pid_t pid = 0;
	if (view->xdg_toplevel && view->xdg_toplevel->resource) {
		wl_client_get_credentials(wl_resource_get_client(view->xdg_toplevel->resource),
			&pid, NULL, NULL);
	}
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		pid = view->xsurface->pid;
	}
#endif
	pid_t sid = pid > 0 ? getsid(pid) : -1;
	return sid > 0 ? sid : 0;
}

/* A window being put on the desk for the first time: is there a sheet
 * waiting for it? The one put down for the program started in the session
 * the window's process is in -- whatever the window calls itself: a
 * program run in a terminal (btop) shows the terminal's window, and some
 * name themselves other than their desktop file does --; else its own
 * program's by name; or -- a window that will not say whose it is -- the
 * one sheet taken out lately, if only one is waiting. */
static struct desk_base *base_claim(struct desk_server *server, struct desk_toplevel *view)
{
	char id[96];
	bool known = view_app_id(view, id, sizeof id);
	pid_t sid = view_session(view);
	struct desk_base *best = NULL, *lone = NULL, *ours = NULL;
	int young = 0;
	for (int i = 0; i < DESK_BASES; i++) {
		struct desk_base *b = &server->bases[i];
		if (b->state == BASE_FREE || b->state == BASE_GOING || b->view) {
			continue;
		}
		if (sid > 0 && b->sid == sid) {
			ours = b;
		}
		if (known && strcmp(b->app, id) == 0 && (!best || b->taken_at < best->taken_at)) {
			best = b;
		}
		if (real_sec() - b->taken_at < 25.0) {
			young++;
			lone = b;
		}
	}
	if (ours) {
		best = ours;
	}
	/* only a window that will not say whose it is: one that does, and is
	 * some other program's, is not what anybody put a sheet down for */
	if (!best && !known && young == 1) {
		best = lone;
	}
	if (best) {
		best->view = view;
		wlr_log(WLR_INFO, "a window for %s comes to the sheet put down for it", best->app);
	}
	return best;
}

static struct desk_base *base_of_view(struct desk_server *server,
	const struct desk_toplevel *view)
{
	for (int i = 0; i < DESK_BASES; i++) {
		if (server->bases[i].state != BASE_FREE && server->bases[i].view == view) {
			return &server->bases[i];
		}
	}
	return NULL;
}

/* After it is mapped and placed: held back while its sheet is still on its
 * way down or opening, otherwise the sheet goes from under it now. */
static void base_after_map(struct desk_server *server, struct desk_toplevel *view)
{
	struct desk_base *b = base_of_view(server, view);
	if (!b || !view->scene_tree) {
		return;
	}
	if (b->state == BASE_CARRIED && view->outer_w > 60 && view->outer_h > 60) {
		/* here before the sheet is down: it goes down where the window is,
		 * and opens out to the window's own size */
		b->x = view->scene_tree->node.x;
		b->y = view->scene_tree->node.y;
		if (b->w != view->outer_w || b->h != view->outer_h) {
			b->w = view->outer_w;
			b->h = view->outer_h;
			cairo_surface_destroy(b->blank);
			b->blank = desk_window_base_sheet(b->w, b->h, b->name);
			double k = fmin(1.0, fmin(b->w * 0.92 / b->lw, b->h * 0.92 / b->lh));
			b->lw *= k;
			b->lh *= k;
		}
	}
	if (b->state == BASE_CARRIED || b->state == BASE_OPENING) {
		wlr_scene_node_set_enabled(&view->scene_tree->node, false);
	} else if (b->state == BASE_WAITING) {
		base_hand_over(server, b);
	}
}

/* A window going: the sheet that was waiting for it waits on (a splash
 * screen comes and goes before the window proper). */
static void base_forget_view(struct desk_server *server, const struct desk_toplevel *view)
{
	struct desk_base *b = base_of_view(server, view);
	if (b) {
		b->view = NULL;
	}
}

/* The sheet in the hand, at u seconds since it was taken. */
struct take_pose {
	double cx, cy, w, h, a, height;
};

static double take_ease_out(double t)
{
	t = clampf_d(t, 0.0, 1.0);
	return 1.0 - pow(1.0 - t, 3.0);
}

static double take_smoother(double t)
{
	t = clampf_d(t, 0.0, 1.0);
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

static void take_pose_at(struct desk_server *server, double u, struct take_pose *p)
{
	struct desk_base *b = &server->bases[server->take.base];
	int h = server->desk_h;
	/* hanging on the ring, then held up off it */
	struct take_pose p0 = { server->take.cx0, server->take.cy0, server->take.w0,
		server->take.h0, server->take.a0, 0.10 };
	struct take_pose p1 = { p0.cx, p0.cy - h * 0.035, p0.w * 1.07, p0.h * 1.07,
		p0.a * 0.35, 0.85 };
	/* over where it goes down, still in the hand; and down */
	double ox = server->layer_win ? server->layer_win->node.x : 0.0;
	double oy = server->layer_win ? server->layer_win->node.y : 0.0;
	double lcx = b->x + b->w * 0.5 + ox, lcy = b->y + b->h * 0.5 + oy;
	double tilt = (b->seed & 1u) ? 0.035 : -0.035;
	struct take_pose p2 = { lcx, lcy - h * 0.012, b->lw * 1.06, b->lh * 1.06, tilt, 0.62 };
	struct take_pose p3 = { lcx, lcy, b->lw, b->lh, 0.0, 0.0 };
	double land = server->take.land_at - server->take.start;
	const struct take_pose *a, *c;
	double t;
	double arc = 0.0;
	if (u < TAKE_LIFT) {
		a = &p0;
		c = &p1;
		t = take_ease_out(u / TAKE_LIFT);
	} else if (u < land) {
		a = &p1;
		c = &p2;
		double q = (u - TAKE_LIFT) / fmax(land - TAKE_LIFT, 1e-3);
		t = take_smoother(q);
		/* carried, not slid: it comes up a little on the way */
		arc = -sin(3.14159265358979 * clampf_d(q, 0.0, 1.0)) * h * 0.05;
	} else {
		a = &p2;
		c = &p3;
		double q = clampf_d((u - land) / TAKE_DOWN, 0.0, 1.0);
		/* let down, slowing as it meets the desk */
		t = 1.0 - (1.0 - q) * (1.0 - q);
	}
	p->cx = a->cx + (c->cx - a->cx) * t;
	p->cy = a->cy + (c->cy - a->cy) * t + arc;
	p->w = a->w + (c->w - a->w) * t;
	p->h = a->h + (c->h - a->h) * t;
	p->a = a->a + (c->a - a->a) * t;
	p->height = a->height + (c->height - a->height) * t;
}

struct take_args {
	struct take_pose p;
	cairo_surface_t *face;
	double box[4];
};

static void take_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct take_args *a = data;
	(void)w;
	(void)h;
	desk_set_paint_origin(floor(a->box[0]), floor(a->box[1]));
	desk_draw_taken_sheet(cr, a->face, a->p.cx, a->p.cy, a->p.w, a->p.h, a->p.a,
		a->p.height);
	desk_set_paint_origin(0, 0);
}

/* Once a frame: the sheet in the hand, and the sheets on the desk. */
static void take_tick(struct desk_server *server)
{
	double now = monotonic_sec();
	if (server->take.base >= 0) {
		struct desk_base *b = &server->bases[server->take.base];
		if (!server->take.turned && now >= server->take.turn_at) {
			/* and back round to the desk with it */
			server->take.turned = true;
			if (server->shelf_t > 0.5 && !(server->shelf_moving && server->shelf_to < 0.5)) {
				shelf_toggle(server);
			}
			/* the move is a spring, there in half its time: the sheet is
			 * over its place as the desk is, and goes down onto it */
			server->take.land_at = now + (server->shelf_moving ? server->shelf_dur * 0.5 : 0.0);
			/* down on the desk, and opening out: when it is put down,
			 * known from here */
			double bx = b->x + b->w * 0.5;
			double in = server->take.land_at + TAKE_DOWN - now;
			snd_in(server, DESK_SND_SHEET_DOWN, bx, 1.0, 0.0, in);
			snd_for_in(server, DESK_SND_PAPER_UNFOLD, bx, 0.9, 0.46, BASE_OPEN_S, in);
			paint_shelf(server);
		}
		double u = now - server->take.start;
		double land = server->take.land_at - server->take.start;
		if (server->take.turned && u >= land + TAKE_DOWN) {
			/* down: it is on the desk now, not in the hand */
			wlr_log(WLR_INFO, "the sheet for %s is put down, %.2f s after it was taken",
				b->app, u);
			if (server->take.sp.node) {
				wlr_scene_node_set_enabled(&server->take.sp.node->node, false);
			}
			server->take.sp.drawn = false;
			server->take.base = -1;
			server->take.idx = -1;
			b->state = BASE_OPENING;
			b->at = now;
			base_paint(server, b, 0.0);
			/* (heard as it goes down: asked for when you turned) */
		} else {
			struct take_args a;
			take_pose_at(server, u, &a.p);
			a.face = b->face;
			double r = 0.5 * hypot(a.p.w, a.p.h) * 1.06;
			double m = 90.0 + 80.0 * a.p.height;
			a.box[0] = a.p.cx - r - m;
			a.box[1] = a.p.cy - r - m;
			a.box[2] = (r + m) * 2.0;
			a.box[3] = (r + m) * 2.0;
			sprite_paint(server, &server->take.sp, server->layer_top, a.box, take_draw, &a);
		}
		server_schedule_frames(server);
	}
	for (int i = 0; i < DESK_BASES; i++) {
		struct desk_base *b = &server->bases[i];
		if (b->state == BASE_OPENING) {
			double u = (now - b->at) / BASE_OPEN_S;
			base_paint(server, b, fmin(u, 1.0));
			if (u >= 1.0) {
				b->state = BASE_WAITING;
				b->at = now;
				if (b->view) {
					base_hand_over(server, b);
				}
			}
			server_schedule_frames(server);
		} else if (b->state == BASE_GOING) {
			double u = (now - b->at) / BASE_GO_S;
			if (u >= 1.0 || !b->node) {
				base_free(b);
			} else {
				wlr_scene_buffer_set_opacity(b->node, (float)(1.0 - u * u));
				server_schedule_frames(server);
			}
		}
	}
}

/* Whether anything is still running in session sid: a process whose
 * session (the sixth field of /proc/<pid>/stat, after the name in
 * brackets) it is. */
static bool session_alive(pid_t sid)
{
	DIR *d = opendir("/proc");
	if (!d) {
		return true;   /* not to be known: waited out instead */
	}
	bool alive = false;
	struct dirent *e;
	while (!alive && (e = readdir(d))) {
		if (e->d_name[0] < '1' || e->d_name[0] > '9') {
			continue;
		}
		char path[300], line[512];
		snprintf(path, sizeof path, "/proc/%s/stat", e->d_name);
		FILE *f = fopen(path, "re");
		if (!f) {
			continue;
		}
		if (fgets(line, sizeof line, f)) {
			const char *p = strrchr(line, ')');
			int session = 0;
			if (p && sscanf(p + 1, " %*c %*d %*d %d", &session) == 1 && session == sid) {
				alive = true;
			}
		}
		fclose(f);
	}
	closedir(d);
	return alive;
}

/* Once a second: a program that has not sent its window has its sheet
 * screwed up and thrown away -- after BASE_WAIT_S, or as soon as nothing it
 * started is running any more (it quit, or handed the work to a program
 * already running, and no window is coming). */
static void base_timeouts(struct desk_server *server)
{
	double now = monotonic_sec(), waited_now = real_sec();
	for (int i = 0; i < DESK_BASES; i++) {
		struct desk_base *b = &server->bases[i];
		if (b->state != BASE_WAITING || b->view) {
			continue;
		}
		double waited = waited_now - b->taken_at;
		bool gone = waited >= BASE_GONE_S && b->sid > 0 && !session_alive(b->sid);
		if (waited < BASE_WAIT_S && !gone) {
			continue;
		}
		wlr_log(WLR_INFO, "no window came for %s%s: its sheet is thrown away", b->app,
			gone ? " (nothing of it is running)" : "");
		if (!server->conf.throw_closed) {
			/* not screwed up: it simply goes */
			b->state = BASE_GOING;
			b->at = now;
			server_schedule_frames(server);
			continue;
		}
		cairo_surface_t *snap = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, b->w, b->h);
		cairo_t *cr = cairo_create(snap);
		cairo_set_source_surface(cr, b->blank, 0, 0);
		cairo_paint(cr);
		cairo_destroy(cr);
		cairo_surface_flush(snap);
		double x = b->x, y = b->y;
		base_free(b);
		throw_paper(server, snap, x, y);
	}
}

/* The standard terminal (DESK_STD_TERMINAL), or the first terminal on
 * $PATH if no program says it is one. */
static void terminal_open(void)
{
	const struct desk_app_list *l = desk_apps_acquire();
	const struct desk_app *app = desk_apps_for_std(l, DESK_STD_TERMINAL);
	if (app) {
		desk_app_launch(app);
	}
	desk_apps_release(l);
	if (!app) {
		spawn(terminal_cmd());
	}
}

/* ---- the shell's own settings ----------------------------------------
 *
 * $XDG_CONFIG_HOME/desk/shell.conf, "key = value" a line, read at start
 * and again whenever it changes (looked at once a second). Shell Settings
 * writes it; so may anyone with an editor.
 *
 *   paper_for_all_windows = yes | no
 *       A program that draws its own frame and buttons (Firefox, GNOME's
 *       programs) is laid on the shell's paper anyway -- a margin of paper
 *       round it, its corner turned down, folded and thrown away like any
 *       other sheet -- or left as it draws itself. Yes unless it says no.
 *   paper.<program> = yes | no
 *       One program the other way: its desktop file id (with or without
 *       ".desktop"), its Wayland app id or its X class.
 *   throw_closed_windows = yes | no       a closed window is screwed up and
 *                                         thrown off the desk (yes), or goes
 *   corner_on_hover = yes | no            a corner turns up when the hand
 *                                         rests on its fold (yes), or only
 *                                         when it is clicked
 *   scale = auto | 1 | 1.25 | 1.5 | 2     how big things on the desk are:
 *                                         pixels to a point (DESK_SCALE,
 *                                         if set, comes first); a display
 *                                         tool's scale is written here
 *   motion = brisk | normal | gentle      how long everything that moves
 *                                         takes
 *   clock = 24h | 12h
 *   app.terminal = <desktop file id>      the standard terminal, and the
 *   app.browser, app.files, app.editor    other three of the drawer's
 *                                         things: what they open, instead
 *                                         of DEsk's choice (see apps.c)
 *   language = auto | en | ja | zh | ko | es | fr | de
 *                                         the desk's own words, and the
 *                                         names programs are filed under:
 *                                         auto follows the locale
 *   drawer_closes = yes | no              the drawer goes back in once
 *                                         something in it has been used
 *   take_sheet = yes | no                 a program taken out of ALL APPS
 *                                         has its sheet carried down to the
 *                                         desk, and its window comes onto it
 *   fold_into_drawer = yes | no           a window folded away goes into
 *                                         the drawer, not onto the desk
 *   sound = off | quiet | normal          the desk's sounds
 *   letter_sound = yes | no               a letter arriving in the post box
 *                                         is heard
 *   camera_bounce = normal | low | off    how far the camera goes past where
 *                                         it is looking and settles back, and
 *                                         how the head bobs walking up to the
 *                                         front door (off for anyone it makes
 *                                         feel unwell)
 *   lights_at_login = yes | no            the room starts dark at login and
 *                                         the lamp comes up as it is made
 *                                         (yes), or is lit at once
 *   screen_off = never | 1 | 2 | 5 | 10 | 15 | 30
 *                                         minutes nobody touches anything
 *                                         before the screen goes dark (10)
 *   lock_on_screen_off = yes | no         and the door comes down as it does
 *                                         (yes)
 *   tap_to_click = yes | no               a touchpad: a tap is a click (yes)
 *   natural_scrolling = yes | no          a touchpad: the page goes the way
 *                                         the fingers do (no)
 *   left_handed = yes | no                mice and touchpads: the buttons
 *                                         the other way round (no)
 *   pointer_speed = slow | normal | fast  or libinput's -1 to 1 (normal)
 *   keyboard_layout = system | <xkb>      the layout, as xkb names it (us,
 *                                         jp, de...); system: the one set
 *                                         for the machine (localectl)
 *   keyboard_second = none | <xkb>        a second layout to change to
 *   keyboard_switch = alt_shift | super_space | ctrl_shift | caps_lock
 *                                         the keys that change between them
 *   key_repeat = slow | normal | fast     how soon a held key repeats, and
 *                                         how fast */

static void shell_conf_path(char *out, size_t n)
{
	const char *cfg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (cfg && *cfg) {
		snprintf(out, n, "%s/desk/shell.conf", cfg);
	} else if (home && *home) {
		snprintf(out, n, "%s/.config/desk/shell.conf", home);
	} else {
		out[0] = '\0';
	}
}

/* The directories a file kept in is to go in, the first time anything is
 * kept there. */
static void conf_dirs_for(const char *path)
{
	char dir[512];
	snprintf(dir, sizeof dir, "%s", path);
	for (char *p = dir + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(dir, 0755);
			*p = '/';
		}
	}
}

static bool conf_bool(const char *v, bool *out)
{
	if (!strcasecmp(v, "yes") || !strcasecmp(v, "true") || !strcasecmp(v, "on")
		|| !strcmp(v, "1")) {
		*out = true;
		return true;
	}
	if (!strcasecmp(v, "no") || !strcasecmp(v, "false") || !strcasecmp(v, "off")
		|| !strcmp(v, "0")) {
		*out = false;
		return true;
	}
	return false;
}

static char *conf_trim(char *s)
{
	while (*s == ' ' || *s == '\t') {
		s++;
	}
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n'
			|| s[n - 1] == '\r')) {
		s[--n] = '\0';
	}
	return s;
}

static void shell_conf_defaults(struct shell_conf *c)
{
	for (int i = 0; i < DESK_STD_COUNT; i++) {
		desk_apps_set_std(i, "");
	}
	c->paper_all = true;
	c->throw_closed = true;
	c->corner_hover = true;
	c->scale = 0.0;
	c->motion = 1.0;
	c->bounce = 1.0;
	c->clock_12h = false;
	snprintf(c->language, sizeof c->language, "auto");
	c->drawer_closes = true;
	c->take_sheet = true;
	c->fold_drawer = false;
	c->sound = 1.0;
	c->letter_sound = true;
	c->lights_at_login = true;
	c->gfx = DESK_GFX_HIGH;
	c->gfx_own = false;
	c->bday_month = c->bday_day = 0;
	c->celebrations = true;
	c->weather = true;
	c->screen_off = 10;
	c->lock_screen_off = true;
	c->tap_click = true;
	c->natural_scroll = false;
	c->left_handed = false;
	c->pointer_speed = 0.0;
	c->kb_layout[0] = c->kb_second[0] = '\0';
	snprintf(c->kb_switch, sizeof c->kb_switch, "grp:alt_shift_toggle");
	c->repeat_rate = 25;
	c->repeat_delay = 600;
	c->n_apps = 0;
}

/* The settings that are not yes or no. False if the key is not one. */
static bool conf_other(struct shell_conf *c, const char *path, const char *key, const char *val)
{
	if (!strcmp(key, "graphics")) {
		static const char *const LEVEL[DESK_GFX_COUNT] = { "2d", "low", "medium", "high",
			"ultra" };
		int g = -1;
		for (int i = 0; i < DESK_GFX_COUNT; i++) {
			if (!strcasecmp(val, LEVEL[i])) {
				g = i;
			}
		}
		if (!strcasecmp(val, "2d_fallback") || !strcasecmp(val, "fallback")) {
			g = DESK_GFX_2D;
		}
		if (g >= 0) {
			c->gfx = g;
			c->gfx_own = c->reading_own;
		} else {
			wlr_log(WLR_ERROR, "%s: graphics is ultra, high, medium, low or 2d, not \"%s\"",
				path, val);
		}
		return true;
	}
	if (!strcmp(key, "scale")) {
		char *end = NULL;
		double v = strtod(val, &end);
		if (!strcasecmp(val, "auto")) {
			c->scale = 0.0;
		} else if (end != val && v > 0.0) {
			/* 1.5, or 150% */
			c->scale = end && *end == '%' ? v / 100.0 : v;
			c->scale = c->scale < 0.5 ? 0.5 : (c->scale > 4.0 ? 4.0 : c->scale);
		} else {
			wlr_log(WLR_ERROR, "%s: scale is auto or a number, not \"%s\"", path, val);
		}
		return true;
	}
	if (!strcmp(key, "camera_bounce")) {
		if (!strcasecmp(val, "normal")) {
			c->bounce = 1.0;
		} else if (!strcasecmp(val, "low")) {
			c->bounce = 0.4;
		} else if (!strcasecmp(val, "off") || !strcasecmp(val, "none")
			|| !strcasecmp(val, "no")) {
			c->bounce = 0.0;
		} else {
			wlr_log(WLR_ERROR, "%s: camera_bounce is normal, low or off, not \"%s\"", path, val);
		}
		return true;
	}
	if (!strcmp(key, "motion")) {
		if (!strcasecmp(val, "brisk")) {
			c->motion = 0.70;
		} else if (!strcasecmp(val, "gentle")) {
			c->motion = 1.50;
		} else if (!strcasecmp(val, "normal")) {
			c->motion = 1.0;
		} else {
			wlr_log(WLR_ERROR, "%s: motion is brisk, normal or gentle, not \"%s\"", path, val);
		}
		return true;
	}
	if (!strcmp(key, "sound")) {
		if (!strcasecmp(val, "off") || !strcasecmp(val, "no")) {
			c->sound = 0.0;
		} else if (!strcasecmp(val, "quiet")) {
			c->sound = 0.45;
		} else if (!strcasecmp(val, "normal") || !strcasecmp(val, "yes")) {
			c->sound = 1.0;
		} else {
			wlr_log(WLR_ERROR, "%s: sound is off, quiet or normal, not \"%s\"", path, val);
		}
		return true;
	}
	if (!strncmp(key, "app.", 4) && desk_std_parse(key + 4) >= 0) {
		desk_apps_set_std(desk_std_parse(key + 4), val);
		return true;
	}
	if (!strcmp(key, "language")) {
		static const char *const CODES[] = { "auto", "en", "ja", "zh", "ko", "es", "fr", "de" };
		bool known = false;
		for (size_t i = 0; i < sizeof CODES / sizeof CODES[0]; i++) {
			known = known || !strcasecmp(val, CODES[i]);
		}
		if (known) {
			snprintf(c->language, sizeof c->language, "%s", val);
			for (char *p = c->language; *p; p++) {
				*p = (char)tolower((unsigned char)*p);
			}
		} else {
			wlr_log(WLR_ERROR, "%s: language is auto, en, ja, zh, ko, es, fr or de, not \"%s\"",
				path, val);
		}
		return true;
	}
	if (!strcmp(key, "birthday")) {
		/* 10-08, as Shell Settings writes it, or 1990-10-08 as it did for
		 * a day (its year let go); nothing, or none, for none. Only the
		 * month and the day: the year is the user's own. */
		int y = 0, m = 0, d = 0;
		if (!*val || !strcasecmp(val, "none") || !strcasecmp(val, "no")) {
			c->bday_month = c->bday_day = 0;
		} else if ((sscanf(val, "%d-%d-%d", &y, &m, &d) == 3 && y >= 1000 && m >= 1 && m <= 12
			&& d >= 1 && d <= 31) || (sscanf(val, "%d-%d", &m, &d) == 2 && m >= 1 && m <= 12
			&& d >= 1 && d <= 31)) {
			c->bday_month = m;
			c->bday_day = d;
		} else {
			wlr_log(WLR_ERROR, "%s: birthday is MM-DD, not \"%s\"", path, val);
		}
		return true;
	}
	if (!strcmp(key, "clock")) {
		if (!strcasecmp(val, "12h") || !strcmp(val, "12")) {
			c->clock_12h = true;
		} else if (!strcasecmp(val, "24h") || !strcmp(val, "24")) {
			c->clock_12h = false;
		} else {
			wlr_log(WLR_ERROR, "%s: clock is 24h or 12h, not \"%s\"", path, val);
		}
		return true;
	}
	if (!strcmp(key, "pointer_speed")) {
		char *end = NULL;
		double v = strtod(val, &end);
		if (!strcasecmp(val, "slow")) {
			c->pointer_speed = -0.5;
		} else if (!strcasecmp(val, "normal")) {
			c->pointer_speed = 0.0;
		} else if (!strcasecmp(val, "fast")) {
			c->pointer_speed = 0.5;
		} else if (end != val && !*end && v >= -1.0 && v <= 1.0) {
			c->pointer_speed = v;
		} else {
			wlr_log(WLR_ERROR, "%s: pointer_speed is slow, normal, fast or -1 to 1, not \"%s\"",
				path, val);
		}
		return true;
	}
	if (!strcmp(key, "keyboard_layout") || !strcmp(key, "keyboard_second")) {
		bool first = key[9] == 'l';
		char *out = first ? c->kb_layout : c->kb_second;
		size_t n = first ? sizeof c->kb_layout : sizeof c->kb_second;
		/* an xkb layout's name: letters, digits, _ and - (and a variant
		 * in brackets, as us(intl)) */
		bool ok = strlen(val) < n;
		for (const char *q = val; ok && *q; q++) {
			ok = isalnum((unsigned char)*q) || strchr("_-()", *q);
		}
		if (!strcasecmp(val, first ? "system" : "none") || !*val) {
			out[0] = '\0';
		} else if (ok) {
			snprintf(out, n, "%s", val);
		} else {
			wlr_log(WLR_ERROR, "%s: %s is %s or an xkb layout, not \"%s\"", path, key,
				first ? "system" : "none", val);
		}
		return true;
	}
	if (!strcmp(key, "keyboard_switch")) {
		static const char *const SW[][2] = { { "alt_shift", "grp:alt_shift_toggle" },
			{ "super_space", "grp:win_space_toggle" }, { "ctrl_shift", "grp:ctrl_shift_toggle" },
			{ "caps_lock", "grp:caps_toggle" } };
		size_t i = 0;
		while (i < sizeof SW / sizeof SW[0] && strcasecmp(val, SW[i][0])) {
			i++;
		}
		if (i < sizeof SW / sizeof SW[0]) {
			snprintf(c->kb_switch, sizeof c->kb_switch, "%s", SW[i][1]);
		} else {
			wlr_log(WLR_ERROR, "%s: keyboard_switch is alt_shift, super_space, ctrl_shift or "
				"caps_lock, not \"%s\"", path, val);
		}
		return true;
	}
	if (!strcmp(key, "key_repeat")) {
		if (!strcasecmp(val, "slow")) {
			c->repeat_rate = 15;
			c->repeat_delay = 800;
		} else if (!strcasecmp(val, "normal")) {
			c->repeat_rate = 25;
			c->repeat_delay = 600;
		} else if (!strcasecmp(val, "fast")) {
			c->repeat_rate = 40;
			c->repeat_delay = 300;
		} else {
			wlr_log(WLR_ERROR, "%s: key_repeat is slow, normal or fast, not \"%s\"", path, val);
		}
		return true;
	}
	if (!strcmp(key, "screen_off")) {
		/* minutes; Shell Settings offers 1 to 30, an editor may say more */
		char *end = NULL;
		long v = strtol(val, &end, 10);
		if (!strcasecmp(val, "never")) {
			c->screen_off = 0;
		} else if (end != val && !*end && v >= 1 && v <= 24 * 60) {
			c->screen_off = (int)v;
		} else {
			wlr_log(WLR_ERROR, "%s: screen_off is never or minutes, not \"%s\"", path, val);
		}
		return true;
	}
	return false;
}

static void shell_conf_read_file(struct shell_conf *c, const char *path);

/* The machine's own defaults, under the user's: desk/shell.conf in the
 * first of $XDG_CONFIG_DIRS (/etc/xdg) that has one -- where the installer
 * writes the graphics it chose for the machine. */
static void shell_conf_system_path(char *out, size_t n)
{
	out[0] = '\0';
	const char *dirs = getenv("XDG_CONFIG_DIRS");
	if (!dirs || !*dirs) {
		dirs = "/etc/xdg";
	}
	while (*dirs) {
		size_t len = strcspn(dirs, ":");
		char path[512];
		if (len > 0 && (size_t)snprintf(path, sizeof path, "%.*s/desk/shell.conf", (int)len,
			dirs) < sizeof path && access(path, R_OK) == 0) {
			snprintf(out, n, "%s", path);
			return;
		}
		dirs += len + (dirs[len] == ':');
	}
}

static void shell_conf_load(struct shell_conf *c)
{
	shell_conf_defaults(c);
	char path[512];
	shell_conf_system_path(path, sizeof path);
	if (path[0]) {
		shell_conf_read_file(c, path);
	}
	shell_conf_path(path, sizeof path);
	if (path[0]) {
		c->reading_own = true;
		shell_conf_read_file(c, path);
		c->reading_own = false;
	}
}

static void shell_conf_read_file(struct shell_conf *c, const char *path)
{
	FILE *f = fopen(path, "re");
	if (!f) {
		return;
	}
	char line[512];
	while (fgets(line, sizeof line, f)) {
		char *s = conf_trim(line);
		if (!*s || *s == '#') {
			continue;
		}
		char *eq = strchr(s, '=');
		if (!eq) {
			continue;
		}
		*eq = '\0';
		char *key = conf_trim(s), *val = conf_trim(eq + 1);
		if (conf_other(c, path, key, val)) {
			continue;
		}
		bool b;
		if (!conf_bool(val, &b)) {
			/* (every setting left is a yes or a no, or is nothing at all) */
			wlr_log(WLR_ERROR, "%s: %s is yes or no, not \"%s\"", path, key, val);
			continue;
		}
		if (!strcmp(key, "paper_for_all_windows")) {
			c->paper_all = b;
		} else if (!strcmp(key, "throw_closed_windows")) {
			c->throw_closed = b;
		} else if (!strcmp(key, "corner_on_hover")) {
			c->corner_hover = b;
		} else if (!strcmp(key, "drawer_closes")) {
			c->drawer_closes = b;
		} else if (!strcmp(key, "take_sheet")) {
			c->take_sheet = b;
		} else if (!strcmp(key, "fold_into_drawer")) {
			c->fold_drawer = b;
		} else if (!strcmp(key, "letter_sound")) {
			c->letter_sound = b;
		} else if (!strcmp(key, "lights_at_login")) {
			c->lights_at_login = b;
		} else if (!strcmp(key, "celebrations")) {
			/* not on Shell Settings: the days are kept as a surprise, on
			 * their dates. This is for anyone who needs them not to be
			 * (the lamp going down, the show, the fireworks). */
			c->celebrations = b;
		} else if (!strcmp(key, "weather")) {
			c->weather = b;
		} else if (!strcmp(key, "lock_on_screen_off")) {
			c->lock_screen_off = b;
		} else if (!strcmp(key, "tap_to_click")) {
			c->tap_click = b;
		} else if (!strcmp(key, "natural_scrolling")) {
			c->natural_scroll = b;
		} else if (!strcmp(key, "left_handed")) {
			c->left_handed = b;
		} else if (!strncmp(key, "paper.", 6) && key[6]
			&& c->n_apps < (int)(sizeof c->apps / sizeof c->apps[0])) {
			snprintf(c->apps[c->n_apps].id, sizeof c->apps[0].id, "%s", key + 6);
			c->apps[c->n_apps].paper = b;
			c->n_apps++;
		} else {
			wlr_log(WLR_ERROR, "%s: nothing is called \"%s\"", path, key);
		}
	}
	fclose(f);
}

/* One setting written into shell.conf the way Shell Settings writes one:
 * its line changed, or added, and everything else left as it was. It is
 * read again from there like any other change (shell_conf_poll). */
static bool shell_conf_put(const char *key, const char *value)
{
	char path[512];
	shell_conf_path(path, sizeof path);
	if (!path[0]) {
		return false;
	}
	conf_dirs_for(path);
	char tmp[600];
	snprintf(tmp, sizeof tmp, "%s.new", path);
	FILE *in = fopen(path, "re");
	FILE *out = fopen(tmp, "we");
	if (!out) {
		if (in) {
			fclose(in);
		}
		wlr_log(WLR_ERROR, "cannot write %s", tmp);
		return false;
	}
	bool done = false;
	if (in) {
		char line[512];
		while (fgets(line, sizeof line, in)) {
			char look[512];
			snprintf(look, sizeof look, "%s", line);
			char *s = conf_trim(look);
			char *eq = *s == '#' ? NULL : strchr(s, '=');
			if (eq) {
				*eq = '\0';
			}
			if (eq && !strcmp(conf_trim(s), key)) {
				if (!done) {
					fprintf(out, "%s = %s\n", key, value);
					done = true;
				}
				continue;
			}
			fputs(line, out);
		}
		fclose(in);
	} else {
		fputs("# DEsk's shell settings. Shell Settings writes this; so may you.\n", out);
	}
	if (!done) {
		fprintf(out, "%s = %s\n", key, value);
	}
	bool ok = fflush(out) == 0 && !ferror(out);
	ok = fclose(out) == 0 && ok;
	if (!ok || rename(tmp, path) != 0) {
		wlr_log(WLR_ERROR, "cannot write %s", path);
		unlink(tmp);
		return false;
	}
	return true;
}

/* A display tool's scale: the size of a point, which is Shell Settings'
 * to say -- so it is said there, where Shell Settings shows it and the
 * next start finds it, and holds from now on as if Shell Settings had set
 * it. In hundredths, as a person would put it. */
static void shell_conf_put_scale(struct desk_server *server, double scale)
{
	double v = round(scale * 100.0) / 100.0;
	char word[32];
	snprintf(word, sizeof word, "%g", v);
	server->conf.scale = v;
	if (shell_conf_put("scale", word)) {
		wlr_log(WLR_INFO, "shell.conf: scale = %s, as a display tool asked", word);
	}
}

static void shell_apply_all(struct desk_server *server);
static void input_apply(struct desk_server *server);
static void desk_relayout(struct desk_server *server);
static void idle_rearm(struct desk_server *server);
static void om_publish(struct desk_server *server);

/* Everything with words on it drawn again in another language: the names
 * of programs read again in it, every kept picture let go, and the desk
 * set out again as for a new screen (from the traced room, kept). */
/* Every picture kept of the desk's things let go, to be drawn again: the
 * binders' sheets, and every sprite's key moved on so none matches. */

static void pictures_forget(struct desk_server *server)
{
	desk_binder_forget();
	book_kept_forget(server);
	struct desk_sprite *all[] = { &server->cal_sp, &server->agd_sp, &server->agd_mark_sp,
		&server->pb_sprite, &server->memo_sp };
	for (size_t i = 0; i < sizeof all / sizeof *all; i++) {
		all[i]->key ^= 0x9e3779b97f4a7c15ull;
	}
	for (int i = 0; i < SH_LAYERS; i++) {
		server->sh_sprite[i].key ^= 0x9e3779b97f4a7c15ull;
	}
	for (int i = 0; i < DESK_CCP_COUNT; i++) {
		server->cc_sprite[i].key ^= 0x9e3779b97f4a7c15ull;
	}
	for (int i = 0; i < DESK_OVL_COUNT; i++) {
		server->ovl[i].key ^= 0x9e3779b97f4a7c15ull;
	}
}

static void lang_switch(struct desk_server *server, enum desk_lang lang, const char *setting)
{
	desk_lang_set(lang);
	desk_apps_set_language(strcmp(setting, "auto") ? desk_lang_code(lang) : "");
	desk_apps_rescan();
	pictures_forget(server);
	desk_draw_forget_words();
	shell_apply_all(server);
	desk_relayout(server);
}

/* Another level of drawing (desk_set_gfx has it already): every kept
 * picture drawn again (their shadows gone, or back), the windows' own
 * shadows, and the room traced again at the level -- the room that was
 * up stays up until the new one is there, as for a new screen. */
static void gfx_switch(struct desk_server *server)
{
	static const char *const NAME[DESK_GFX_COUNT] = { "2D", "Low", "Medium", "High",
		"Ultra" };
	wlr_log(WLR_INFO, "graphics: %s now", NAME[desk_gfx_level()]);
	pictures_forget(server);
	desk_lock_forget();
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (view->scene_tree && !view->unmanaged) {
			view->shadow.w = view->shadow.h = 0;   /* drawn again at the next look */
			view_sync_shadow(view);
		}
	}
	desk_relayout(server);
}

/* How much is drawn: as chosen -- and with the screen drawn on the CPU,
 * 2D Fallback unless the user chose otherwise in their own file (Shell
 * Settings writes only what is changed there): the machine's default, the
 * installer's guess, was a guess at a GPU that is not drawing. */
static int gfx_wanted(const struct desk_server *server)
{
	const struct shell_conf *c = &server->conf;
	return server->on_cpu && !c->gfx_own ? DESK_GFX_2D : c->gfx;
}

/* Once a second: read again if it has changed since it was last read. */
static void shell_conf_poll(struct desk_server *server)
{
	char path[512];
	shell_conf_path(path, sizeof path);
	struct stat st;
	bool there = path[0] && stat(path, &st) == 0;
	struct shell_conf *c = &server->conf;
	if (c->loaded && there == c->there && (!there || (st.st_mtim.tv_sec == c->mtime
			&& st.st_mtim.tv_nsec == c->mtime_ns && st.st_size == c->size))) {
		return;
	}
	c->there = there;
	if (there) {
		c->mtime = st.st_mtim.tv_sec;
		c->mtime_ns = st.st_mtim.tv_nsec;
		c->size = st.st_size;
	}
	bool first = !c->loaded;
	enum desk_lang was = desk_lang_get();
	shell_conf_load(c);
	c->loaded = true;
	idle_rearm(server);
	desk_sound_set_level(c->sound);
	enum desk_lang lang = desk_lang_parse(c->language);
	g_bounce = c->bounce;
	/* how much is drawn: before the first room is traced, at the level it
	 * is to be traced at */
	int want = gfx_wanted(server);
	bool gfx_changed = !first && want != (int)desk_gfx_level();
	desk_set_gfx(want);
	if (first) {
		motion_set(motion_of(c));
		desk_set_clock_12h(c->clock_12h);
		/* before the first look at what is installed */
		desk_lang_set(lang);
		desk_apps_set_language(strcmp(c->language, "auto") ? desk_lang_code(lang) : "");
		char faces[512];
		desk_font_describe(faces, sizeof faces);
		wlr_log(WLR_INFO, "the desk speaks %s; text is set in %s", desk_lang_code(lang),
			faces);
	} else if (lang != was) {
		wlr_log(WLR_INFO, "the desk speaks %s now", desk_lang_code(lang));
		lang_switch(server, lang, c->language);
	} else {
		wlr_log(WLR_INFO, "shell settings read again: paper for all windows %s, %d by name",
			c->paper_all ? "yes" : "no", c->n_apps);
		shell_apply_all(server);
	}
	if (gfx_changed) {
		gfx_switch(server);
	}
	struct desk_occasion_conf oc = { c->bday_month, c->bday_day, c->celebrations, c->weather };
	desk_occasion_conf(&oc);
	if (!first && locked(server)) {
		paint_lock(server);
		server_schedule_frames(server);
	}
}

/* Is this program to be laid on paper when it draws its own frame? */
static bool view_wants_paper(struct desk_server *server, const struct desk_toplevel *view)
{
	const struct shell_conf *c = &server->conf;
	if (c->n_apps == 0) {
		return c->paper_all;
	}
	const char *names[4] = { NULL, NULL, NULL, NULL };
	char id[96] = "";
	if (view_app_id(view, id, sizeof id)) {
		names[0] = id;
	}
	if (view->xdg_toplevel) {
		names[1] = view->xdg_toplevel->app_id;
	}
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		names[2] = view->xsurface->class;
		names[3] = view->xsurface->instance;
	}
#endif
	for (int i = 0; i < c->n_apps; i++) {
		const char *want = c->apps[i].id;
		size_t wn = strlen(want);
		for (int k = 0; k < 4; k++) {
			const char *nm = names[k];
			if (!nm || !*nm) {
				continue;
			}
			if (!strcasecmp(nm, want)) {
				return c->apps[i].paper;
			}
			/* org.kde.kate.desktop is also org.kde.kate */
			size_t nn = strlen(nm);
			if (nn == wn + 8 && !strncasecmp(nm, want, wn) && !strcasecmp(nm + wn, ".desktop")) {
				return c->apps[i].paper;
			}
		}
	}
	return c->paper_all;
}

/* ---- whose frame a window has ----------------------------------------
 *
 * Ours, the sheet with its heading, for a program that asks for it; none
 * of ours for a splash screen, or for a program that draws its own frame
 * and buttons when the shell is told to leave those alone; and for a
 * program that draws its own and the shell is told to lay everything on
 * its paper (the default), a margin of paper round the program -- no
 * heading, the program has its own -- with the sheet's corner, its fold,
 * its shadow, and everything else a sheet does. */

static void view_repaint_chrome(struct desk_toplevel *view);
#ifdef DESK_HAVE_XWAYLAND
static bool xview_is_splash(const struct desk_server *server,
	const struct wlr_xwayland_surface *xs);
#endif

/* Has the surface said it is to have our frame, by KDE's protocol?
 *
 * GTK 3 does not speak xdg-decoration. It says whose frame a window is to
 * have by KDE's older server-decoration protocol, which is offered with
 * ours as what a window gets unless it says otherwise: a plain GTK 3
 * window -- Xfce's and MATE's programs, their dialogs -- then draws no
 * title bar of its own and comes onto our paper with its heading, like a
 * program that asks by xdg-decoration. A window with a title bar of its
 * own making (a header bar, its buttons in it) says it frames itself, and
 * keeps its frame. What a window says may change after it is on the desk,
 * and its frame is decided again when it does (kde_frame_again). */
static bool kde_server_frame(struct desk_server *server, struct wlr_surface *surface)
{
	struct desk_kde_deco *k;
	wl_list_for_each(k, &server->kde_decos, link) {
		if (k->deco->surface == surface) {
			return k->deco->mode == WLR_SERVER_DECORATION_MANAGER_MODE_SERVER;
		}
	}
	return false;
}

/* Does the program draw its own frame? */
static bool view_draws_own_frame(const struct desk_toplevel *view)
{
	if (view->xdg_toplevel) {
		return view->deco == NULL
			&& !kde_server_frame(view->server, view->xdg_toplevel->base->surface);
	}
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		return view->xsurface->decorations != WLR_XWAYLAND_SURFACE_DECORATIONS_ALL;
	}
#endif
	return false;
}

static void view_frame_decide(struct desk_toplevel *view, bool *bare, bool *own_frame)
{
	bool own = view_draws_own_frame(view);
	bool splash = false;
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		splash = xview_is_splash(view->server, view->xsurface);
	}
#endif
	*bare = splash || (own && !view_wants_paper(view->server, view));
	*own_frame = own && !*bare;
}

/* Told it is tiled on every side, a program that draws its own frame
 * leaves out its shadow and its round corners: on paper, the paper is its
 * edge. */
static void view_tell_tiled(struct desk_toplevel *view)
{
	if (view->xdg_toplevel && view->xdg_toplevel->base->initialized) {
		wlr_xdg_toplevel_set_tiled(view->xdg_toplevel, view->own_frame
			? WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT : WLR_EDGE_NONE);
	}
}

/* As it comes onto the desk: decided, nothing to move yet. */
static void view_frame_init(struct desk_toplevel *view)
{
	view_frame_decide(view, &view->bare, &view->own_frame);
	view_tell_tiled(view);
}

/* Decided again -- it has asked for our frame, or stopped; the shell's
 * settings have changed -- with the window proper left where it is on the
 * desk and the paper made again round it. */
static void view_frame_update(struct desk_toplevel *view)
{
	bool bare, own_frame;
	view_frame_decide(view, &bare, &own_frame);
	if (bare == view->bare && own_frame == view->own_frame) {
		return;
	}
	int l0, t0, r0, b0, l1, t1, r1, b1;
	view_insets(view, &l0, &t0, &r0, &b0);
	view->bare = bare;
	view->own_frame = own_frame;
	view_insets(view, &l1, &t1, &r1, &b1);
	view_tell_tiled(view);
	if (!view->scene_tree) {
		return;
	}
	wlr_scene_node_set_position(&view->scene_tree->node,
		view->scene_tree->node.x + l0 - l1, view->scene_tree->node.y + t0 - t1);
	view->chrome_key = 0;
	view_repaint_chrome(view);
	view_sync_shadow(view);
	view_send_position(view);
}

static void windows_keep_off_tray(struct desk_server *server);

static double ui_scale_for(struct desk_server *server, struct wlr_output *out);
static void overlay_update(struct desk_server *server, bool force);

static void shell_apply_all(struct desk_server *server)
{
	motion_set(motion_of(&server->conf));
	g_bounce = server->conf.bounce;
	desk_set_clock_12h(server->conf.clock_12h);
	overlay_update(server, true);
	/* a new size for things: the room is traced again for it, as for a
	 * new screen */
	struct wlr_output *out = first_output(server);
	if (out && server->desk_w > 8) {
		double scale = ui_scale_for(server, out);
		if (fabs(scale - desk_ui_scale()) > 1e-3) {
			wlr_log(WLR_INFO, "a point is %.2f pixels now", scale);
			desk_set_ui_scale(scale);
			desk_relayout(server);
			/* which the display tools show as the screen's scale */
			om_publish(server);
		}
	}
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (view->scene_tree && !view->unmanaged) {
			view_frame_update(view);
		}
	}
	/* a sheet that has grown a margin may now be over the tray */
	windows_keep_off_tray(server);
	input_apply(server);
	server_schedule_frames(server);
}

/* On a program laid on paper, is (dx from the right edge, ly from the
 * top) on the paper by the corner, or on the fold itself -- rather than on
 * the program, whose own buttons are just under it? */
static bool mount_on_fold(const struct desk_toplevel *view, double dx, double ly)
{
	return ly < DESK_MOUNT_TOP || dx < DESK_MOUNT || dx + ly <= view->curl_s + 4.0;
}

/* What a window is, for printing on the back of it: the application it
 * belongs to, found by what the window says of itself, and its icon -- or,
 * if no application owns up to it, the best name the window has. */
static cairo_surface_t *view_app(const struct desk_toplevel *view, char *name, size_t n)
{
	const char *app_id = NULL, *cls = NULL, *inst = NULL;
	if (view->xdg_toplevel) {
		app_id = view->xdg_toplevel->app_id;
	}
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		cls = view->xsurface->class;
		inst = view->xsurface->instance;
	}
#endif
	cairo_surface_t *icon = NULL;
	const struct desk_app_list *l = desk_apps_acquire();
	const struct desk_app *app = desk_apps_match(l, app_id, cls, inst);
	/* a script's window is called after its file (thing.py), and one that
	 * calls itself python3 says nothing: its title does better */
	char stem[128];
	if (desk_app_is_interpreter(app_id)) {
		app_id = NULL;
	} else if (desk_app_script_stem(app_id, stem, sizeof stem)) {
		app_id = stem;
	}
	if (desk_app_is_interpreter(cls)) {
		cls = NULL;
	}
	if (app) {
		snprintf(name, n, "%s", app->name);
		icon = desk_app_icon(app);
	} else if (app_id && *app_id) {
		/* org.example.Thing is Thing */
		const char *dot = strrchr(app_id, '.');
		snprintf(name, n, "%s", dot && dot[1] ? dot + 1 : app_id);
	} else if (cls && *cls) {
		char cstem[128];
		snprintf(name, n, "%s", desk_app_script_stem(cls, cstem, sizeof cstem) ? cstem : cls);
	} else {
		snprintf(name, n, "%s", view_title(view));
	}
	desk_apps_release(l);
	return icon;
}

static void view_fold_toggle(struct desk_toplevel *view)
{
	struct desk_server *server = view->server;
	if (view->sheet.phase != SHEET_IDLE) {
		sheet_begin_unfold(server, &view->sheet);
		return;
	}
	cairo_surface_t *src = view_snapshot(view);
	char name[128];
	cairo_surface_t *icon = view_app(view, name, sizeof name);
	bool had_keys = server->seat->keyboard_state.focused_surface == view_surface(view);
	sheet_begin_fold(server, &view->sheet, src,
		view->scene_tree->node.x, view->scene_tree->node.y,
		view->scene_tree, view, icon, name, view_screen(view));
	/* a sheet folded away is not the one being written on any more */
	if (had_keys) {
		focus_next(server);
	}
}

/* ---- what is in the drawer ---------------------------------------
 *
 * Whatever has been put there, in a row along the felt: programs, the
 * screwdriver, sheets folded away. A hand on one of them does what it is
 * for -- runs the program, opens Shell Settings, opens the sheet back out on
 * the desk -- and the drawer goes back in. Picked up and carried, a thing
 * goes elsewhere in the row; carried out over the desk, a program is taken
 * out of the drawer and a sheet is laid down there. A sheet folded on the
 * desk is carried into the drawer the same way, and the drawer comes out
 * for it if it is carried to the drawer's pull. The programs and the
 * screwdriver, in their order, are kept ($XDG_CONFIG_HOME/desk/drawer); the sheets go
 * when their windows do. */

static void drawer_path(char *out, size_t n)
{
	const char *cfg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (cfg && *cfg) {
		snprintf(out, n, "%s/desk/drawer", cfg);
	} else if (home && *home) {
		snprintf(out, n, "%s/.config/desk/drawer", home);
	} else {
		out[0] = '\0';
	}
}

static void drawer_save(struct desk_server *server)
{
	char path[512];
	drawer_path(path, sizeof path);
	if (!path[0]) {
		return;
	}
	conf_dirs_for(path);
	char tmp[600];
	snprintf(tmp, sizeof tmp, "%s.new", path);
	FILE *f = fopen(tmp, "we");
	if (!f) {
		wlr_log(WLR_ERROR, "cannot keep what is in the drawer in %s", tmp);
		return;
	}
	fprintf(f, "# DEsk's drawer (2). What is in it, in order: \"thing <terminal | browser |\n"
		"# files | editor>\" (DEsk's standard program for it, whichever that is),\n"
		"# \"app <desktop file id>\", or \"panel\" (the screwdriver: Shell Settings).\n");
	for (int i = 0; i < server->drw_n; i++) {
		const struct drawer_entry *e = &server->drw[i];
		if (e->kind == DRAWER_APP) {
			fprintf(f, "app %s\n", e->id);
		} else if (e->kind == DRAWER_STD) {
			fprintf(f, "thing %s\n", desk_std_name(e->std));
		} else if (e->kind == DRAWER_PANEL) {
			fprintf(f, "panel\n");
		}
	}
	bool ok = fflush(f) == 0 && !ferror(f);
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp, path) != 0) {
		wlr_log(WLR_ERROR, "cannot keep what is in the drawer in %s", path);
		unlink(tmp);
		return;
	}
	server->drw_default = false;
}

static void drawer_load(struct desk_server *server)
{
	server->drw_n = 0;
	server->drw_default = true;
	server->drw_gen = UINT_MAX;   /* matched to no list yet */
	server->drw_gap = -1;
	server->drw_hover = -1;
	server->drw_press = -1;
	server->drw_carry_from = -1;
	char path[512];
	drawer_path(path, sizeof path);
	FILE *f = path[0] ? fopen(path, "re") : NULL;
	if (!f) {
		return;
	}
	server->drw_default = false;
	/* one kept before there were standard programs: its first program of
	 * each kind was what that thing in the drawer opened, so that is what
	 * it is turned into (drawer_match) */
	server->drw_migrate = true;
	char line[256];
	while (fgets(line, sizeof line, f) && server->drw_n < DRAWER_MAX) {
		/* (DeskWM and [DE]sk: the names DEsk went by when it wrote some
		 * of these) */
		if (!strncmp(line, "# DEsk's drawer (2)", 19) || !strncmp(line, "# [DE]sk's drawer (2)", 21)
			|| !strncmp(line, "# DeskWM's drawer (2)", 21)) {
			server->drw_migrate = false;
		}
		char *s = line;
		while (*s == ' ' || *s == '\t') {
			s++;
		}
		size_t len = strlen(s);
		while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' '
				|| s[len - 1] == '\t')) {
			s[--len] = '\0';
		}
		struct drawer_entry e = { 0 };
		if (strcmp(s, "panel") == 0) {
			e.kind = DRAWER_PANEL;
			e.present = true;
		} else if (strncmp(s, "thing", 5) == 0 && (s[5] == ' ' || s[5] == '\t')) {
			const char *name = s + 6;
			while (*name == ' ' || *name == '\t') {
				name++;
			}
			e.std = desk_std_parse(name);
			if (e.std < 0) {
				continue;
			}
			e.kind = DRAWER_STD;
		} else if (strncmp(s, "app", 3) == 0 && (s[3] == ' ' || s[3] == '\t')) {
			const char *id = s + 4;
			while (*id == ' ' || *id == '\t') {
				id++;
			}
			if (!*id) {
				continue;
			}
			e.kind = DRAWER_APP;
			snprintf(e.id, sizeof e.id, "%s", id);
		} else {
			continue;
		}
		e.fit = e.fit_to = 1.0;
		server->drw[server->drw_n++] = e;
	}
	fclose(f);
}

static int drawer_shown(const struct desk_server *server)
{
	int m = 0;
	for (int i = 0; i < server->drw_n; i++) {
		m += server->drw[i].present;
	}
	return m;
}

/* Where each goes along the row, the place held open for something
 * carried skipped; now, or by sliding there. */
static void drawer_layout(struct desk_server *server, bool now)
{
	int shown = drawer_shown(server);
	int gap = server->drw_gap > shown ? shown : server->drw_gap;
	int m = shown + (gap >= 0 ? 1 : 0);
	double fit = desk_drawer_fit(m);
	int k = 0;
	for (int i = 0; i < server->drw_n; i++) {
		struct drawer_entry *e = &server->drw[i];
		if (!e->present) {
			continue;
		}
		int slot = gap >= 0 && k >= gap ? k + 1 : k;
		e->x_to = desk_drawer_slot_x(slot, m);
		e->fit_to = fit;
		if (now) {
			e->x = e->x_to;
			e->fit = e->fit_to;
		}
		k++;
	}
	server->drw_dirty = true;
	if (!now && !server->drw_sliding) {
		server->drw_sliding = true;
		server->drw_slid_at = monotonic_sec();
		server_schedule_frames(server);
	}
}

/* Matched again to the programs there are, when that list is new: which
 * are installed, and -- if nobody has filled the drawer yet -- what it
 * comes with, one of each of the four things there are programs for, and
 * the panel. */
static void drawer_match(struct desk_server *server)
{
	const struct desk_app_list *l = desk_apps_acquire();
	if (l->generation == server->drw_gen && !(server->drw_default && l->n > 0)) {
		desk_apps_release(l);
		return;
	}
	server->drw_gen = l->generation;
	if (server->drw_default && l->n > 0) {
		struct drawer_entry keep[DRAWER_MAX];
		int nk = 0;
		for (int i = 0; i < server->drw_n; i++) {
			if (server->drw[i].kind == DRAWER_SHEET) {
				keep[nk++] = server->drw[i];
			}
		}
		server->drw_n = 0;
		/* DEsk's standard programs, one a thing */
		for (int m = 0; m < DESK_STD_COUNT; m++) {
			server->drw[server->drw_n++] = (struct drawer_entry){ .kind = DRAWER_STD,
				.std = m, .fit = 1.0, .fit_to = 1.0 };
		}
		server->drw[server->drw_n++] = (struct drawer_entry){ .kind = DRAWER_PANEL,
			.present = true, .fit = 1.0, .fit_to = 1.0 };
		for (int i = 0; i < nk && server->drw_n < DRAWER_MAX; i++) {
			server->drw[server->drw_n++] = keep[i];
		}
		/* it is what it comes with until somebody changes it: not kept */
		server->drw_default = false;
	}
	if (server->drw_migrate && l->n > 0) {
		server->drw_migrate = false;
		for (int m = 0; m < DESK_STD_COUNT; m++) {
			for (int i = 0; i < server->drw_n; i++) {
				struct drawer_entry *e = &server->drw[i];
				const struct desk_app *app = e->kind == DRAWER_APP
					? desk_apps_find(l, e->id) : NULL;
				if (app && app->model == m) {
					wlr_log(WLR_INFO, "the drawer's %s is the standard one now, not %s",
						desk_std_name(m), e->id);
					e->kind = DRAWER_STD;
					e->std = m;
					e->id[0] = '\0';
					break;
				}
			}
		}
		drawer_save(server);
	}
	for (int i = 0; i < server->drw_n; i++) {
		struct drawer_entry *e = &server->drw[i];
		if (e->kind == DRAWER_APP) {
			e->present = desk_apps_find(l, e->id) != NULL;
		} else if (e->kind == DRAWER_STD) {
			e->present = desk_apps_for_std(l, e->std) != NULL;
		}
	}
	desk_apps_release(l);
	drawer_layout(server, true);
}

/* What entry i looks like lying there. The first program of a kind is the
 * thing itself; another of the same kind is a card, so the two can be told
 * apart. */
static void drawer_item_of(const struct desk_server *server, const struct desk_app_list *l,
	const struct drawer_entry *e, struct desk_drawer_item *it)
{
	memset(it, 0, sizeof *it);
	switch (e->kind) {
	case DRAWER_APP: {
		const struct desk_app *app = desk_apps_find(l, e->id);
		it->look = DESK_LOOK_CARD;
		if (!app) {
			snprintf(it->name, sizeof it->name, "%s", e->id);
			desk_initial_of(e->id, it->initial, sizeof it->initial);
			break;
		}
		bool first = app->model >= 0 && app->model < DESK_ICON_GEAR;
		/* the thing is the standard one, if that is in the drawer: this is
		 * a card */
		for (int i = 0; first && i < server->drw_n; i++) {
			const struct drawer_entry *o = &server->drw[i];
			first = !(o->kind == DRAWER_STD && o->present && o->std == app->model);
		}
		for (int i = 0; first && i < server->drw_n && &server->drw[i] != e; i++) {
			const struct drawer_entry *o = &server->drw[i];
			if (o->kind == DRAWER_APP && o->present) {
				const struct desk_app *oa = desk_apps_find(l, o->id);
				first = !(oa && oa->model == app->model);
			}
		}
		if (first) {
			it->look = app->model;
		} else {
			it->icon = desk_app_icon(app);
		}
		snprintf(it->name, sizeof it->name, "%s", app->name);
		snprintf(it->initial, sizeof it->initial, "%s", app->initial);
		it->seed = app->seed;
		break;
	}
	case DRAWER_STD: {
		const struct desk_app *app = desk_apps_for_std(l, e->std);
		it->look = e->std;
		if (app) {
			snprintf(it->name, sizeof it->name, "%s", app->name);
			snprintf(it->initial, sizeof it->initial, "%s", app->initial);
			it->seed = app->seed;
		} else {
			snprintf(it->name, sizeof it->name, "%s", desk_std_name(e->std));
			it->seed = (unsigned)e->std * 13u + 5u;
		}
		break;
	}
	case DRAWER_PANEL:
		it->look = DESK_ICON_GEAR;
		snprintf(it->name, sizeof it->name, "%s", TR("Shell Settings"));
		it->seed = 7;
		break;
	case DRAWER_SHEET:
		it->look = DESK_LOOK_PACKET;
		it->packet = e->packet;
		if (e->sheet) {
			snprintf(it->name, sizeof it->name, "%s", e->sheet->name);
			it->seed = e->sheet->seed;
		}
		break;
	}
}

/* Draw what lies in the drawer again, if it has changed: one buffer the
 * size of the felt, laid over the drawer sheet and moved with it. */
static void drawer_compose(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	struct desk_room_sheets sh;
	if (w < 8 || h < 8 || !desk_room_sheets(w, h, &sh)
		|| !desk_drawer_felt_box(w, h, server->drw_box)) {
		if (server->drw_node) {
			wlr_scene_node_set_enabled(&server->drw_node->node, false);
		}
		return;
	}
	/* nothing of it shows with the drawer in: it is drawn as it comes out */
	if (server->drawer_t <= 0.004 && !(server->drawer_moving && server->drawer_to > 0.5)) {
		return;
	}
	drawer_match(server);
	if (!server->drw_dirty && server->drw_node) {
		return;
	}
	server->drw_dirty = false;
	prof_begin(PROF_DOCK);
	struct desk_drawer_item items[DRAWER_MAX];
	double xs[DRAWER_MAX], fits[DRAWER_MAX];
	int n = 0, hover = -1;
	const struct desk_app_list *l = desk_apps_acquire();
	for (int i = 0; i < server->drw_n; i++) {
		const struct drawer_entry *e = &server->drw[i];
		if (!e->present) {
			continue;
		}
		if (i == server->drw_hover) {
			hover = n;
		}
		drawer_item_of(server, l, e, &items[n]);
		xs[n] = e->x;
		fits[n] = e->fit;
		n++;
	}
	/* as fine as the sheet is shown: magnified to the screen, and the
	 * screen's own pixels finer than its points */
	double zoom, pan, t;
	desk_room_view(server->drawer_t, &sh, &zoom, &pan, &t);
	int ow, oh;
	desk_overlay_size(w, h, &ow, &oh);
	double res = clampf_d((double)(int)(h * zoom + 0.5) / sh.h * ow / w, 1.0, 4.0);
	server->drw_res = res;
	struct desk_buf *buf = buf_create((int)ceil(server->drw_box[2] * res),
		(int)ceil(server->drw_box[3] * res));
	cairo_save(buf->cr);
	cairo_scale(buf->cr, res, res);
	cairo_translate(buf->cr, -server->drw_box[0], -server->drw_box[1]);
	desk_drawer_paint_items(buf->cr, w, h, items, n, xs, fits, hover);
	cairo_restore(buf->cr);
	desk_apps_release(l);
	scene_put(&server->drw_node, server->layer_drawer, buf, 0, 0,
		(void *)(uintptr_t)DESK_ROLE_DOCK);
	wlr_scene_node_raise_to_top(&server->drw_node->node);
	prof_end();
	room_layout(server);
}

static void drawer_hover_to(struct desk_server *server, int hover)
{
	if (hover == server->drw_hover) {
		return;
	}
	server->drw_hover = hover;
	server->drw_dirty = true;
	drawer_compose(server);
}

/* Which entry is under a point, or -1: by where they lie now, mid-slide
 * or not. */
static int drawer_entry_at(struct desk_server *server, double px, double py)
{
	double x;
	bool row;
	if (server->drawer_t < 0.35
		|| !desk_drawer_at(server->desk_w, server->desk_h, server->drawer_t, px, py, &x, &row)
		|| !row) {
		return -1;
	}
	double half = desk_drawer_pitch(drawer_shown(server)) * 0.5;
	int best = -1;
	for (int i = 0; i < server->drw_n; i++) {
		const struct drawer_entry *e = &server->drw[i];
		double d = fabs(x - e->x);
		if (e->present && d < half) {
			half = d;
			best = i;
		}
	}
	return best;
}

/* The place in the row a thing carried to x along it would go. */
static int drawer_gap_for(struct desk_server *server, double x)
{
	int shown = drawer_shown(server);
	int m = shown + 1;
	int k = (int)lround(x / desk_drawer_pitch(m) + (m - 1) * 0.5);
	return k < 0 ? 0 : (k > shown ? shown : k);
}

/* Put e in the row at place gap (counting only what is shown). */
static void drawer_insert(struct desk_server *server, const struct drawer_entry *e, int gap)
{
	if (server->drw_n >= DRAWER_MAX) {
		return;
	}
	int at = server->drw_n, k = 0;
	for (int i = 0; i < server->drw_n; i++) {
		if (!server->drw[i].present) {
			continue;
		}
		if (k == gap) {
			at = i;
			break;
		}
		k++;
	}
	memmove(&server->drw[at + 1], &server->drw[at],
		(size_t)(server->drw_n - at) * sizeof server->drw[0]);
	server->drw[at] = *e;
	server->drw_n++;
}

static void drawer_remove(struct desk_server *server, int i, struct drawer_entry *out)
{
	if (out) {
		*out = server->drw[i];
	} else if (server->drw[i].packet) {
		cairo_surface_destroy(server->drw[i].packet);
	}
	memmove(&server->drw[i], &server->drw[i + 1],
		(size_t)(server->drw_n - i - 1) * sizeof server->drw[0]);
	server->drw_n--;
	if (server->drw_hover == i) {
		server->drw_hover = -1;
	} else if (server->drw_hover > i) {
		server->drw_hover--;
	}
	/* and the one being pressed, which a window closing just now may
	 * have taken out from under the hand */
	if (server->drw_press == i) {
		server->drw_press = -1;
	} else if (server->drw_press > i) {
		server->drw_press--;
	}
}

static bool drawer_has_app(const struct desk_server *server, const char *id)
{
	for (int i = 0; i < server->drw_n; i++) {
		if (server->drw[i].kind == DRAWER_APP && strcmp(server->drw[i].id, id) == 0) {
			return true;
		}
	}
	const struct drawer_entry *c = &server->drw_carry;
	return server->drw_carrying && c->kind == DRAWER_APP && strcmp(c->id, id) == 0;
}

/* A program put in the drawer, after the programs already there -- or
 * taken out of it. Kept. */
static void drawer_keep_app(struct desk_server *server, const char *id, bool keep)
{
	/* what it comes with goes in first, or it would replace this later */
	drawer_match(server);
	if (keep) {
		if (drawer_has_app(server, id) || server->drw_n >= DRAWER_MAX) {
			return;
		}
		int at = 0;
		for (int i = 0; i < server->drw_n; i++) {
			if (server->drw[i].kind == DRAWER_APP || server->drw[i].kind == DRAWER_STD) {
				at = i + 1;
			}
		}
		struct drawer_entry e = { .kind = DRAWER_APP, .present = true, .fit = 1.0,
			.fit_to = 1.0 };
		snprintf(e.id, sizeof e.id, "%s", id);
		memmove(&server->drw[at + 1], &server->drw[at],
			(size_t)(server->drw_n - at) * sizeof server->drw[0]);
		server->drw[at] = e;
		server->drw_n++;
		wlr_log(WLR_INFO, "%s is put in the drawer", id);
	} else {
		for (int i = 0; i < server->drw_n; i++) {
			if (server->drw[i].kind == DRAWER_APP && strcmp(server->drw[i].id, id) == 0) {
				drawer_remove(server, i, NULL);
				wlr_log(WLR_INFO, "%s is taken out of the drawer", id);
				break;
			}
		}
	}
	drawer_layout(server, true);
	drawer_save(server);
}

/* In or out, from wherever it has got to. */
static void drawer_move_to(struct desk_server *server, double to)
{
	bool out = server->drawer_moving ? server->drawer_to > 0.5 : server->drawer_t > 0.5;
	if (out != (to > 0.5)) {
		drawer_toggle(server);
	}
}

static bool window_under(struct desk_server *server);

/* The packet that lies on the desk, not in the drawer, under the pointer:
 * one lying under a window is not, there, the window is. */
static struct desk_sheet *parked_sheet_at(struct desk_server *server, double px, double py)
{
	double x = px - server->layer_parked->node.x, y = py - server->layer_parked->node.y;
	bool covered = window_under(server);
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		struct desk_sheet *s = &view->sheet;
		if (!s->in_drawer && sheet_hit(s, x, y)
			&& !(covered && s->tree && s->tree->node.parent == server->layer_folded)) {
			return s;
		}
	}
	return NULL;
}

/* Sheet entry i out of the drawer, lying where it lay in it -- or, the
 * drawer shut, where it would have been put down on the desk -- to be
 * opened out from there. */
static void drawer_lift_sheet(struct desk_server *server, int i)
{
	struct desk_sheet *s = server->drw[i].sheet;
	double sx = 0.0, sy = 0.0;
	bool there = server->drawer_t > 0.05 && desk_drawer_point(server->desk_w,
		server->desk_h, server->drawer_t, server->drw[i].x, &sx, &sy);
	drawer_remove(server, i, NULL);
	drawer_layout(server, false);
	if (!s) {
		return;
	}
	s->in_drawer = false;
	s->slot = sheet_alloc_slot(server, first_output(server), s);
	int ow, oh, pad;
	desk_fold_extent(s->fold, 1.0, &ow, &oh, &pad);
	if (there) {
		s->x1 = sx - server->layer_parked->node.x - ow * 0.5;
		s->y1 = sy - server->layer_parked->node.y - oh * 0.5;
	} else {
		sheet_slot_xy(server, first_output(server), s->slot, ow, oh, &s->x1, &s->y1);
	}
	sheet_present(server, s, 1.0, s->x1, s->y1);
}

static int drawer_index_of(struct desk_server *server, const struct desk_sheet *sheet)
{
	for (int i = 0; i < server->drw_n; i++) {
		if (server->drw[i].kind == DRAWER_SHEET && server->drw[i].sheet == sheet) {
			return i;
		}
	}
	return -1;
}

/* A sheet folded straight into the drawer: at the end of the row. (The
 * drawer full, it is put down on the desk as any other.) */
static void drawer_put_in(struct desk_server *server, struct desk_sheet *sheet)
{
	if (server->drw_n >= DRAWER_MAX || drawer_index_of(server, sheet) >= 0 || !sheet->fold) {
		int ow, oh, pad;
		desk_fold_extent(sheet->fold, 1.0, &ow, &oh, &pad);
		sheet->slot = sheet_alloc_slot(server, first_output(server), sheet);
		sheet_slot_xy(server, first_output(server), sheet->slot, ow, oh,
			&sheet->x1, &sheet->y1);
		sheet_present(server, sheet, 1.0, sheet->x1, sheet->y1);
		return;
	}
	struct drawer_entry e = { .kind = DRAWER_SHEET, .sheet = sheet, .present = true,
		.fit = 1.0, .fit_to = 1.0 };
	e.packet = desk_fold_packet(sheet->fold);
	server->drw[server->drw_n++] = e;
	sheet->in_drawer = true;
	sheet->slot = -1;
	if (sheet->tree) {
		wlr_scene_node_set_enabled(&sheet->tree->node, false);
	}
	drawer_layout(server, true);
	drawer_compose(server);
}

/* Take hold of entry i, or of a packet on the desk: carried from here on,
 * under the hand, over everything. */
static void drawer_pick_up(struct desk_server *server)
{
	struct drawer_entry *c = &server->drw_carry;
	server->drw_gx = server->drw_gy = 0.5;
	server->drw_sprung = false;
	if (server->drw_press >= 0) {
		/* the place it leaves is held open, so nothing moves yet */
		int i = server->drw_press, k = 0;
		for (int j = 0; j < i; j++) {
			k += server->drw[j].present;
		}
		drawer_remove(server, i, c);
		server->drw_carry_from = k;
		server->drw_gap = k;
		server->drw_over = true;
		drawer_layout(server, false);
	} else {
		struct desk_sheet *s = server->drw_press_sheet;
		*c = (struct drawer_entry){ .kind = DRAWER_SHEET, .sheet = s, .present = true,
			.fit = 1.0, .fit_to = 1.0 };
		server->drw_carry_from = -1;
		server->drw_gap = -1;
		server->drw_over = false;
		/* held where the hand took hold of it */
		double lx = server->drw_px - server->layer_parked->node.x;
		double ly = server->drw_py - server->layer_parked->node.y;
		server->drw_gx = s->hit_w > 0 ? (lx - s->hit_x) / s->hit_w : 0.5;
		server->drw_gy = s->hit_h > 0 ? (ly - s->hit_y) / s->hit_h : 0.5;
	}
	/* off the felt; or a packet lifted off the desk */
	if (c->kind != DRAWER_SHEET || server->drw_carry_from >= 0) {
		desk_sound_play(DESK_SND_FELT_PICK, snd_pan(server, server->drw_px), 1.0, 0.0);
	} else {
		snd_at(server, DESK_SND_CORNER_LIFT, server->drw_px, 1.3);
	}
	server->drw_press = -1;
	server->drw_press_sheet = NULL;
	server->drw_carrying = true;
	server->drw_hover = -1;
	if (c->kind == DRAWER_SHEET && c->sheet && !c->packet) {
		c->packet = desk_fold_packet(c->sheet->fold);
	}
	/* the picture of it, held up */
	const struct desk_app_list *l = desk_apps_acquire();
	struct desk_drawer_item it;
	drawer_item_of(server, l, c, &it);
	double ax = 0.0, ay = 0.0;
	cairo_surface_t *pic = desk_drawer_item_picture(server->desk_w, server->desk_h, &it,
		&ax, &ay);
	desk_apps_release(l);
	if (pic) {
		int pw = cairo_image_surface_get_width(pic), ph = cairo_image_surface_get_height(pic);
		struct desk_buf *b = buf_create(pw, ph);
		cairo_set_operator(b->cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_surface(b->cr, pic, 0, 0);
		cairo_paint(b->cr);
		cairo_surface_destroy(pic);
		scene_put(&server->drw_carry_node, server->layer_drag, b, 0, 0, NULL);
		wlr_scene_buffer_set_opacity(server->drw_carry_node, 1.0f);
		server->drw_carry_ax = ax;
		server->drw_carry_ay = ay;
	}
	server->drw_poof_at = 0.0;
	server->drw_dirty = true;
	drawer_compose(server);
}

/* The carried thing under the hand: over the drawer, as it will lie in it;
 * a packet over the desk, as the packet it is. */
static void drawer_carry_show(struct desk_server *server)
{
	double cx = server->cursor->x, cy = server->cursor->y;
	struct drawer_entry *c = &server->drw_carry;
	struct desk_sheet *s = c->kind == DRAWER_SHEET ? c->sheet : NULL;
	bool packet = s && !server->drw_over && s->fold;
	if (packet) {
		int ow, oh, pad;
		desk_fold_extent(s->fold, 1.0, &ow, &oh, &pad);
		double x = cx - server->drw_gx * ow, y = cy - server->drw_gy * oh;
		if (!s->tree || s->tree->node.parent != server->layer_drag || !s->tree->node.enabled) {
			sheet_present(server, s, 1.0, x, y);
			wlr_scene_node_reparent(&s->tree->node, server->layer_drag);
		}
		wlr_scene_node_set_enabled(&s->tree->node, true);
		wlr_scene_node_set_position(&s->tree->node, (int)(x - pad), (int)(y - pad));
	} else if (s && s->tree) {
		wlr_scene_node_set_enabled(&s->tree->node, false);
	}
	if (server->drw_carry_node) {
		wlr_scene_node_set_enabled(&server->drw_carry_node->node, !packet);
		wlr_scene_node_set_position(&server->drw_carry_node->node,
			(int)(cx - server->drw_carry_ax), (int)(cy - server->drw_carry_ay));
		/* a program carried off over the desk is on its way out of it */
		bool leaving = !server->drw_over
			&& (c->kind == DRAWER_APP || c->kind == DRAWER_STD);
		wlr_scene_buffer_set_opacity(server->drw_carry_node, leaving ? 0.55f : 1.0f);
	}
}

static void drawer_carry_move(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	double cx = server->cursor->x, cy = server->cursor->y;
	struct drawer_entry *c = &server->drw_carry;
	/* a packet carried to the shut drawer's pull: it comes out for it
	 * (not for one only being moved about the desk with the right button) */
	if (c->kind == DRAWER_SHEET && !server->drw_sprung && server->drw_button != BTN_RIGHT
		&& !pointer_on_side(server) && cy >= h - desk_drawer_pull_h(w, h) - 6
		&& !(server->drawer_moving ? server->drawer_to > 0.5 : server->drawer_t > 0.5)) {
		server->drw_sprung = true;
		drawer_move_to(server, 1.0);
	}
	double x = 0.0;
	bool row;
	bool over = desk_drawer_at(w, h, server->drawer_t, cx, cy, &x, &row);
	int gap = -1;
	if (over) {
		gap = drawer_gap_for(server, x);
	} else if (c->kind == DRAWER_PANEL) {
		/* the screwdriver cannot be taken out: its place stays open for it */
		gap = server->drw_carry_from;
	}
	server->drw_over = over;
	if (gap != server->drw_gap) {
		server->drw_gap = gap;
		drawer_layout(server, false);
	}
	drawer_carry_show(server);
}

/* Let go of what is carried. */
static void drawer_drop(struct desk_server *server)
{
	struct drawer_entry c = server->drw_carry;
	int w = server->desk_w, h = server->desk_h;
	double cx = server->cursor->x, cy = server->cursor->y;
	double x = 0.0;
	bool row;
	bool over = server->drw_over
		&& desk_drawer_at(w, h, server->drawer_t, cx, cy, &x, &row);
	int gap = server->drw_gap;
	server->drw_carrying = false;
	server->drw_carry = (struct drawer_entry){ 0 };
	server->drw_gap = -1;
	bool keep = false;
	struct desk_sheet *s = c.kind == DRAWER_SHEET ? c.sheet : NULL;
	/* the drawer full, a packet off the desk does not go in: it is put down
	 * on the desk where it was let go, as any other */
	if (over && gap >= 0 && (!s || server->drw_n < DRAWER_MAX)) {
		/* into the drawer, where it was let go, and along to its place */
		snd_at(server, DESK_SND_FELT_DROP, cx, 1.0);
		c.x = x;
		drawer_insert(server, &c, gap);
		keep = c.kind != DRAWER_SHEET;
		if (s) {
			s->in_drawer = true;
			s->slot = -1;
			if (s->tree) {
				wlr_scene_node_set_enabled(&s->tree->node, false);
				wlr_scene_node_reparent(&s->tree->node, server->layer_parked);
			}
		}
		if (server->drw_carry_node) {
			wlr_scene_node_set_enabled(&server->drw_carry_node->node, false);
		}
	} else if (c.kind == DRAWER_PANEL) {
		/* back where it was */
		snd_at(server, DESK_SND_FELT_DROP, cx, 0.7);
		drawer_insert(server, &c, server->drw_carry_from >= 0 ? server->drw_carry_from : 0);
		if (server->drw_carry_node) {
			wlr_scene_node_set_enabled(&server->drw_carry_node->node, false);
		}
	} else if (c.kind == DRAWER_APP || c.kind == DRAWER_STD) {
		/* taken out of the drawer: it fades from the hand */
		server->drw_poof_at = monotonic_sec();
		keep = true;
		server_schedule_frames(server);
	} else if (s) {
		/* laid down where it was let go -- on the desk, or on the screen
		 * beside it the hand is over -- inside that screen's edges */
		if (c.packet) {
			cairo_surface_destroy(c.packet);
		}
		if (server->drw_carry_node) {
			wlr_scene_node_set_enabled(&server->drw_carry_node->node, false);
		}
		int ow, oh, pad;
		desk_fold_extent(s->fold, 1.0, &ow, &oh, &pad);
		struct wlr_output *out = screen_at(server, cx, cy);
		struct wlr_output *was = s->in_drawer ? NULL : sheet_screen(server, s);
		struct wlr_box wa = screen_area(server, out);
		double lx = cx - server->drw_gx * ow - server->layer_parked->node.x;
		double ly = cy - server->drw_gy * oh - server->layer_parked->node.y;
		lx = clampf_d(lx, wa.x, fmax(wa.x, wa.x + wa.width - ow));
		ly = clampf_d(ly, wa.y, fmax(wa.y, wa.y + wa.height - oh));
		s->in_drawer = false;
		/* a place along the front of the screen it is on now */
		if (s->slot < 0 || was != out) {
			s->slot = sheet_alloc_slot(server, out, s);
		}
		if (s->tree) {
			wlr_scene_node_reparent(&s->tree->node, server->layer_parked);
		}
		s->phase = SHEET_PARKED;
		s->x1 = lx;
		s->y1 = ly;
		sheet_present(server, s, 1.0, lx, ly);
		snd_at(server, DESK_SND_PAPER_SETTLE, cx, 1.0);
	}
	if (keep) {
		drawer_save(server);
	}
	drawer_layout(server, false);
	drawer_compose(server);
	server_schedule_frames(server);
}

/* A sheet is gone (its window has), or is being opened out by some other
 * way than the drawer: out of the drawer and out of the hand. */
static void drawer_forget(struct desk_server *server, struct desk_sheet *sheet)
{
	if (server->drw_press_sheet == sheet) {
		server->drw_press_sheet = NULL;
	}
	if (server->drw_carrying && server->drw_carry.sheet == sheet) {
		if (server->drw_carry.packet) {
			cairo_surface_destroy(server->drw_carry.packet);
		}
		server->drw_carry = (struct drawer_entry){ 0 };
		server->drw_carrying = false;
		server->drw_gap = -1;
		if (server->drw_carry_node) {
			wlr_scene_node_set_enabled(&server->drw_carry_node->node, false);
		}
		if (sheet->tree) {
			wlr_scene_node_reparent(&sheet->tree->node, server->layer_parked);
		}
		drawer_layout(server, false);
	}
	int i = drawer_index_of(server, sheet);
	if (i >= 0) {
		drawer_remove(server, i, NULL);
		drawer_layout(server, false);
	}
	sheet->in_drawer = false;
}

/* What a hand on entry i is for. */
static void drawer_activate(struct desk_server *server, int i)
{
	struct drawer_entry *e = &server->drw[i];
	snd_at(server, DESK_SND_FELT_PICK, server->cursor->x, 1.0);
	switch (e->kind) {
	case DRAWER_APP: {
		const struct desk_app_list *l = desk_apps_acquire();
		const struct desk_app *app = desk_apps_find(l, e->id);
		if (app) {
			wlr_log(WLR_INFO, "taking %s out of the drawer", app->id);
			desk_app_launch(app);
		}
		desk_apps_release(l);
		break;
	}
	case DRAWER_STD: {
		const struct desk_app_list *l = desk_apps_acquire();
		const struct desk_app *app = desk_apps_for_std(l, e->std);
		if (app) {
			wlr_log(WLR_INFO, "taking the %s, %s, out of the drawer", desk_std_name(e->std),
				app->id);
			desk_app_launch(app);
		}
		desk_apps_release(l);
		break;
	}
	case DRAWER_PANEL:
		/* the screwdriver: Shell Settings, whose window comes on the desk */
		wlr_log(WLR_INFO, "taking the screwdriver, Shell Settings, out of the drawer");
		settings_open();
		break;
	case DRAWER_SHEET: {
		struct desk_sheet *s = e->sheet;
		drawer_lift_sheet(server, i);
		if (s) {
			sheet_begin_unfold(server, s);
		}
		break;
	}
	}
	/* and the drawer goes back in: what came out of it is on the desk --
	 * unless it is to be left out (Shell Settings) */
	if (server->conf.drawer_closes) {
		drawer_move_to(server, 0.0);
	}
}

/* A hand put down: on something in the drawer, or on a packet lying on the
 * desk. Nothing happens until it is lifted or moved. The right button is
 * only for moving a folded sheet: anywhere on the desk, or into the drawer
 * if it is out, and a right click that does not move it does nothing. */
static bool drawer_press(struct desk_server *server, uint32_t button)
{
	if (server->drw_carrying || server->drw_press >= 0 || server->drw_press_sheet) {
		return true;   /* already holding something */
	}
	/* (a screen beside the desk's is in reach whatever the desk's is
	 * turned to) */
	if (!pointer_on_side(server) && (post_up(server) || console_up(server) || shelf_up(server))) {
		return false;
	}
	bool right = button == BTN_RIGHT;
	double cx = server->cursor->x, cy = server->cursor->y;
	if (server->drawer_t > 0.004) {
		int i = drawer_entry_at(server, cx, cy);
		if (i >= 0 && (!right || server->drw[i].kind == DRAWER_SHEET)) {
			server->drw_press = i;
			server->drw_px = cx;
			server->drw_py = cy;
			server->drw_button = button;
			return true;
		}
		if (cy >= drawer_line(server)) {
			return false;
		}
	}
	struct desk_sheet *s = parked_sheet_at(server, cx, cy);
	if (!s) {
		return false;
	}
	if (s->phase != SHEET_PARKED) {
		/* on its way down or up: it goes back the way it came */
		if (!right) {
			sheet_begin_unfold(server, s);
		}
		return true;
	}
	server->drw_press_sheet = s;
	server->drw_px = cx;
	server->drw_py = cy;
	server->drw_button = button;
	return true;
}

static bool drawer_motion(struct desk_server *server)
{
	if (server->drw_carrying) {
		drawer_carry_move(server);
		return true;
	}
	if (server->drw_press < 0 && !server->drw_press_sheet) {
		return false;
	}
	/* a little way is still a click: a hand is not a pin */
	if (hypot(server->cursor->x - server->drw_px, server->cursor->y - server->drw_py) < 6.0) {
		return true;
	}
	drawer_pick_up(server);
	drawer_carry_move(server);
	return true;
}

static bool drawer_release(struct desk_server *server)
{
	bool right = server->drw_button == BTN_RIGHT;
	server->drw_button = 0;
	if (server->drw_carrying) {
		drawer_drop(server);
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
		return true;
	}
	if (right && (server->drw_press >= 0 || server->drw_press_sheet)) {
		/* moved nowhere: a right click on a sheet is not a use of it */
		server->drw_press = -1;
		server->drw_press_sheet = NULL;
		return true;
	}
	if (server->drw_press >= 0) {
		int i = server->drw_press;
		server->drw_press = -1;
		if (i < server->drw_n) {
			drawer_activate(server, i);
		}
		return true;
	}
	if (server->drw_press_sheet) {
		struct desk_sheet *s = server->drw_press_sheet;
		server->drw_press_sheet = NULL;
		sheet_begin_unfold(server, s);
		drawer_move_to(server, 0.0);
		return true;
	}
	return false;
}

/* Once a frame: along the row to where they go, and a program taken out
 * fading from the hand. */
static void drawer_slide_tick(struct desk_server *server)
{
	double now = monotonic_sec();
	if (server->drw_poof_at > 0.0 && server->drw_carry_node) {
		double u = (now - server->drw_poof_at) / 0.22;
		if (u >= 1.0) {
			server->drw_poof_at = 0.0;
			wlr_scene_node_set_enabled(&server->drw_carry_node->node, false);
		} else {
			wlr_scene_buffer_set_opacity(server->drw_carry_node, (float)(0.55 * (1.0 - u)));
			server_schedule_frames(server);
		}
	}
	if (!server->drw_sliding) {
		return;
	}
	double dt = clampf_d(now - server->drw_slid_at, 0.0, 0.1);
	server->drw_slid_at = now;
	double k = gfx_flat() ? 1.0 : 1.0 - exp(-dt / 0.055);
	bool more = false;
	for (int i = 0; i < server->drw_n; i++) {
		struct drawer_entry *e = &server->drw[i];
		e->x += (e->x_to - e->x) * k;
		e->fit += (e->fit_to - e->fit) * k;
		if (fabs(e->x_to - e->x) > 0.003 || fabs(e->fit_to - e->fit) > 0.002) {
			more = true;
		} else {
			e->x = e->x_to;
			e->fit = e->fit_to;
		}
	}
	server->drw_sliding = more;
	server->drw_dirty = true;
	drawer_compose(server);
	if (more) {
		server_schedule_frames(server);
	}
}

/* Which of our windows a surface is the client of, if any. */
static struct desk_toplevel *view_of_surface(struct desk_server *server,
	struct wlr_surface *surface)
{
	if (!surface) {
		return NULL;
	}
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (view_surface(view) == surface) {
			return view;
		}
	}
	return NULL;
}

static void reset_cursor_mode(struct desk_server *server)
{
	server->cursor_mode = DESK_CUR_PASSTHROUGH;
	server->grabbed = NULL;
}

/* A video taken out of its page to float over everything -- Firefox's
 * and Chromium's picture-in-picture -- is known by its window's title
 * alone, as the browsers name it in DEsk's languages (compared without
 * case, spaces or hyphens): Wayland has no "keep this in front". */
static const char *const PIP_TITLES[] = {
	"pictureinpicture",          /* Firefox, Chromium */
	"ピクチャーインピクチャー",
	"画中画", "子母畫面",
	"pip모드", "화면속화면",
	"imagenenimagen", "imagendentrodeimagen", "imagensuperpuesta",
	"incrustationvidéo", "imagedansl’image", "imagedansl'image",
	"bildimbild",
};

static bool view_is_pip(const struct desk_toplevel *view)
{
	const char *t = view_title(view);
	char norm[96];
	size_t k = 0;
	for (const char *p = t; p && *p && k + 1 < sizeof norm; p++) {
		if (*p != ' ' && *p != '-') {
			norm[k++] = (char)tolower((unsigned char)*p);
		}
	}
	norm[k] = '\0';
	for (size_t i = 0; i < sizeof PIP_TITLES / sizeof PIP_TITLES[0]; i++) {
		if (strcmp(norm, PIP_TITLES[i]) == 0) {
			return true;
		}
	}
	return false;
}

/* What is kept in front goes back in front, after anything is put over
 * the windows -- the last of them to have been used on top. */
static void views_above_raise(struct desk_server *server)
{
	struct desk_toplevel *v;
	wl_list_for_each_reverse(v, &server->toplevels, link) {
		if (v->above && v->scene_tree && v->scene_tree->node.parent == server->layer_win) {
			wlr_scene_node_raise_to_top(&v->scene_tree->node);
		}
	}
}

/* Is it (still) one to keep in front? Asked when it comes, and whenever
 * its title changes. */
static void view_check_above(struct desk_toplevel *view)
{
	bool above = view_is_pip(view);
	if (above != view->above) {
		view->above = above;
		views_above_raise(view->server);
	}
}

static void focus_toplevel(struct desk_toplevel *toplevel)
{
	if (toplevel == NULL) {
		return;
	}
	struct desk_server *server = toplevel->server;
	if (server->lock_phase != 0) {
		return;   /* nothing gets the keys while the door is shut */
	}
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev = seat->keyboard_state.focused_surface;
	struct wlr_surface *surface = view_surface(toplevel);
	if (!surface || !toplevel->scene_tree) {
		return;
	}
	/* to the front first: a click on the one that has the keys already
	 * still brings it up over what was put down on it */
	wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	wl_list_remove(&toplevel->link);
	wl_list_insert(&server->toplevels, &toplevel->link);
	views_above_raise(server);
	if (!view_takes_keys(toplevel) || prev == surface) {
		/* raised; the keys stay where they were, or are here already */
		return;
	}
	struct desk_toplevel *was = view_of_surface(server, prev);
	if (was) {
		view_set_activated(was, false);
	}
	view_set_activated(toplevel, true);
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes,
			keyboard->num_keycodes, &keyboard->modifiers);
	} else {
		wlr_seat_keyboard_notify_enter(seat, surface, NULL, 0, NULL);
	}
	/* the title bar says which one has the keys */
	if (was) {
		view_repaint_chrome(was);
	}
	view_repaint_chrome(toplevel);
}

/* The window on top that can have the keys, when the one that had them
 * goes: or nobody, if nothing is left. A sheet folded away is not it. */
static void focus_next(struct desk_server *server)
{
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (view->sheet.phase == SHEET_IDLE && view->scene_tree && view_takes_keys(view)) {
			focus_toplevel(view);
			return;
		}
	}
	wlr_seat_keyboard_notify_clear_focus(server->seat);
}

/* The frame's buffer reaches this far past the paper: only far enough
 * for a hand reaching for the edge. (Its shadow is a node of its own.) */
#define CHROME_PAD 8

static bool chrome_accepts(struct wlr_scene_buffer *buffer, int sx, int sy)
{
	struct desk_toplevel *view = buffer->node.data;
	if (!view) {
		return false;
	}
	double lx = sx - CHROME_PAD;
	double ly = sy - CHROME_PAD;
	/* a little past the edge of the paper, where a hand reaches for it */
	if (lx < -6 || ly < -6 || lx > view->outer_w + 6 || ly > view->outer_h + 6) {
		return false;
	}
	if (lx >= DESK_BORDER && lx <= view->outer_w - DESK_BORDER &&
		ly >= DESK_TITLE_H && ly <= view->outer_h - DESK_BORDER) {
		return false;
	}
	return true;
}

static bool chrome_accepts_f(struct wlr_scene_buffer *buffer, double *sx, double *sy)
{
	return chrome_accepts(buffer, (int)*sx, (int)*sy);
}

static void view_paint_chrome_body(struct desk_toplevel *view, int inner_w, int inner_h)
{
	if (!view->scene_tree || !view->surface_tree) {
		return;
	}
	bool framed = view_framed(view);
	if (framed) {
		if (inner_w < 80) {
			inner_w = 80;
		}
		if (inner_h < 60) {
			inner_h = 60;
		}
	}
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	const char *title = view_title(view);
	struct wlr_surface *surface = view_surface(view);
	bool focused = view->server->seat->keyboard_state.focused_surface == surface;
	/* Where the window proper starts in the client's surface: a client
	 * that draws its own shadow draws it in a margin round the window,
	 * and the sheet is the window, not the margin. */
	struct wlr_box geo;
	view_geometry(view, &geo);
	if (geo.width <= 0 || geo.height <= 0) {
		geo = (struct wlr_box){ 0, 0, inner_w, inner_h };
	}
	view->own_shadow = view->xdg_toplevel && surface
		&& (geo.x > 0 || geo.y > 0 || surface->current.width > geo.width
			|| surface->current.height > geo.height);

	/* Drawn again only if what it shows has changed. A client commits a
	 * frame for every keystroke and every frame of a video, and the frame
	 * round it is the same every time. */
	uint64_t key = 14695981039346656037ull;
	int what[9] = { inner_w, inner_h, focused, framed, l, geo.x, geo.y, view->own_shadow,
		view->own_frame };
	key = key_add(key, what, sizeof what);
	key = key_add(key, title, strlen(title));
	if (view->chrome_key == key && (view->chrome || !framed)) {
		return;
	}
	view->chrome_key = key;
	view->outer_w = inner_w + l + r;
	view->outer_h = inner_h + t + b;
	/* The window proper at (l, t). The scene's node for an xdg surface
	 * already has its origin at the window geometry's corner, so the
	 * client's margin is not taken off again here: it was, and a program
	 * that draws its own shadow sat that far up and to the left of where
	 * its sheet was. (An X window has no margin.) */
	wlr_scene_node_set_position(&view->surface_tree->node, l, t);
	if (!view->sizing) {
		view_clip_rest(view, &geo);
	}
	if (!framed) {
		if (view->chrome) {
			wlr_scene_node_set_enabled(&view->chrome->node, false);
		}
		view_sync_shadow(view);
		return;
	}
	int bw = view->outer_w + CHROME_PAD * 2;
	int bh = view->outer_h + CHROME_PAD * 2;
	struct desk_buf *buffer = buf_create(bw, bh);
	if (view->own_frame) {
		desk_draw_window_mount(buffer->cr, CHROME_PAD, CHROME_PAD,
			view->outer_w, view->outer_h, title);
	} else {
		desk_draw_window_frame(buffer->cr, CHROME_PAD, CHROME_PAD,
			view->outer_w, view->outer_h, title, focused, 0, false);
	}
	scene_put(&view->chrome, view->scene_tree, buffer, -CHROME_PAD, -CHROME_PAD, view);
	/* The frame is a whole sheet of paper, the client lies on it: under
	 * the client, then, not over it -- it is made at the first commit that
	 * has a size, which is long after the client's own node, and a new
	 * node goes on top. */
	wlr_scene_node_place_below(&view->chrome->node, &view->surface_tree->node);
	wlr_scene_node_set_enabled(&view->chrome->node, true);
	view->chrome->point_accepts_input = chrome_accepts_f;
	view_sync_shadow(view);
}

static void view_paint_chrome(struct desk_toplevel *view, int inner_w, int inner_h)
{
	prof_begin(PROF_CHROME);
	view_paint_chrome_body(view, inner_w, inner_h);
	prof_end();
}

/* The frame again at the size the client has now: after a change of
 * focus, of title, or of whether there is to be a frame at all. */
static void view_sizing_place(struct desk_toplevel *view);

static void view_repaint_chrome(struct desk_toplevel *view)
{
	/* opening out or folding back: the frame and the program's drawing
	 * are away until the end, and view_unfold_finish paints the frame
	 * again once ua_on is clear */
	if (view->ua_on) {
		return;
	}
	struct wlr_surface *s = view_surface(view);
	if (!s || !view->scene_tree) {
		return;
	}
	/* the window proper: for a Wayland client its geometry, which leaves
	 * out any margin it draws a shadow in */
	int iw = s->current.width, ih = s->current.height;
	if (view->xdg_toplevel) {
		struct wlr_box geo;
		view_geometry(view, &geo);
		if (geo.width > 0 && geo.height > 0) {
			iw = geo.width;
			ih = geo.height;
		}
	}
	if (view->sizing) {
		/* the paper, not the program: it is where the pointer took it */
		iw = view->sz_x1 - view->sz_x0;
		ih = view->sz_y1 - view->sz_y0;
	}
	if (iw > 0 && ih > 0) {
		view_paint_chrome(view, iw, ih);
	}
	if (view->sizing) {
		view_sizing_place(view);
	}
}

/* The screen the desk is on: the one at the corner the desk is drawn from
 * -- the first there was, or the one a display tool put there -- else the
 * newest that is on. One turned off from a display tool is not where
 * anything is. */
static struct wlr_output *first_output(struct desk_server *server)
{
	if (wl_list_empty(&server->outputs)) {
		return NULL;
	}
	struct wlr_output *at = wlr_output_layout_output_at(server->output_layout, 0.0, 0.0);
	if (at) {
		return at;
	}
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (!o->off) {
			return o->wlr_output;
		}
	}
	o = wl_container_of(server->outputs.next, o, link);
	return o->wlr_output;
}

/* Where windows may lie: all of the screen but the tray along the back of
 * the desk -- console, clock, post box, the shelf's plate -- which
 * windows keep off the way they keep off any panel, and the shut drawer's
 * pull along the front. A window with the whole screen is the exception. */
static struct wlr_box work_area(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	struct wlr_output *out = first_output(server);
	if ((w < 8 || h < 8) && out) {
		w = out->width;
		h = out->height;
	}
	int top = desk_tray_bottom(w, h);
	int bottom = desk_drawer_pull_h(w, h);
	/* a screen too short for that still gets somewhere to put a window */
	if (h - top - bottom < 160) {
		top = h / 5;
		bottom = 0;
	}
	return (struct wlr_box){ 0, top, w, h - top - bottom };
}

/* ---- more than one screen -------------------------------------------
 *
 * The desk is on one screen: the one at the corner the desk is drawn from
 * (first_output). Any other screen is more desk beside it -- the same
 * boards, with nothing standing on them and no drawer (side_desks_paint)
 * -- for windows to be laid on. There, where a window may lie is all of
 * that screen; one opened out is as big as that screen, and one given the
 * whole screen has that one; a new window comes onto the screen the
 * pointer is on. Nothing the desk's own screen turns to -- the drawer, the
 * bookcase, the Control Panel, the post box -- moves what lies on the
 * others, or takes them out of reach; and a screen that goes puts what was
 * on it on one that is there. */

/* Is out a screen beside the desk's, rather than the desk's own? */
static bool screen_is_side(struct desk_server *server, const struct wlr_output *out)
{
	return out && out != first_output(server);
}

/* The screen a point is on, or the nearest one to it. */
static struct wlr_output *screen_at(struct desk_server *server, double x, double y)
{
	struct wlr_output *out = wlr_output_layout_output_at(server->output_layout, x, y);
	if (!out) {
		double cx, cy;
		wlr_output_layout_closest_point(server->output_layout, NULL, x, y, &cx, &cy);
		out = wlr_output_layout_output_at(server->output_layout, cx, cy);
	}
	return out ? out : first_output(server);
}

/* Is there a screen beside the desk's? */
static bool screens_beside(struct desk_server *server)
{
	struct wlr_output *desk = first_output(server);
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (o->wlr_output != desk && !o->off
			&& wlr_output_layout_get(server->output_layout, o->wlr_output)) {
			return true;
		}
	}
	return false;
}

/* Something of the desk's own screen drawn bigger than that screen -- the
 * room, the drawer, a camera move's film, the bookcase stepped up to --
 * laid over x, y, dw by dh in its layer (which is moved by lx, ly), src
 * being all of its picture: cut, when there is a screen beside the desk's,
 * to what of it is on the desk's screen, so that what is beside it is not
 * drawn over. */
static void desk_screen_lay(struct desk_server *server, struct wlr_scene_buffer *b,
	struct wlr_fbox src, double x, double y, double dw, double dh, int lx, int ly)
{
	if (screens_beside(server) && dw >= 1.0 && dh >= 1.0) {
		double sx0 = -lx, sy0 = -ly;
		double sx1 = sx0 + server->desk_w, sy1 = sy0 + server->desk_h;
		double x0 = fmax(x, sx0), y0 = fmax(y, sy0);
		double x1 = fmin(x + dw, sx1), y1 = fmin(y + dh, sy1);
		if (x1 - x0 >= 1.0 && y1 - y0 >= 1.0 && (x0 > x || y0 > y || x1 < x + dw
				|| y1 < y + dh)) {
			double kx = src.width / dw, ky = src.height / dh;
			struct wlr_fbox cut = { src.x + (x0 - x) * kx, src.y + (y0 - y) * ky,
				(x1 - x0) * kx, (y1 - y0) * ky };
			scene_source(b, &cut);
			wlr_scene_buffer_set_dest_size(b, (int)lround(x1 - x0), (int)lround(y1 - y0));
			wlr_scene_node_set_position(&b->node, (int)lround(x0), (int)lround(y0));
			return;
		}
	}
	scene_source(b, &src);
	wlr_scene_buffer_set_dest_size(b, (int)lround(dw), (int)lround(dh));
	wlr_scene_node_set_position(&b->node, (int)lround(x), (int)lround(y));
}

/* Is the pointer on a screen beside the desk's? */
static bool pointer_on_side(struct desk_server *server)
{
	struct wlr_output *at = wlr_output_layout_output_at(server->output_layout,
		server->cursor->x, server->cursor->y);
	return screen_is_side(server, at);
}

/* Where windows may lie on a screen, in the paper's coordinates (where a
 * window's node is put, in layer_win): on the desk's, the work area, which
 * goes wherever the desk goes; on one beside it, all of it, wherever the
 * drawer and the camera have moved the paper to (desk_follow). */
static struct wlr_box screen_area(struct desk_server *server, struct wlr_output *out)
{
	struct wlr_box box = { 0 };
	if (screen_is_side(server, out)) {
		wlr_output_layout_get_box(server->output_layout, out, &box);
		box.x -= server->desk_dx;
		box.y -= server->desk_dy + server->win_dy;
	}
	return wlr_box_empty(&box) ? work_area(server) : box;
}

/* How far the drawer and the camera have moved the paper on the desk off
 * its place (desk_follow): nothing to reach for until it is back. */
static bool paper_moved(const struct desk_server *server)
{
	return server->desk_dx != 0 || server->desk_dy != 0 || server->win_dy != 0;
}

/* The bare desk on each screen beside the desk's own (layer_side), cut
 * from the desk's room (desk_draw_side_desk): drawn for where that screen
 * is, how big, the level and the room -- again only when one of those has
 * changed, or whatever (force) -- as finely as the desk's own overlay is
 * drawn at the level, and as light as the lamp is up. A screen that is
 * not beside the desk any more loses its. */
static void side_desks_paint(struct desk_server *server, bool force)
{
	if (server->quitting) {
		return;
	}
	struct wlr_output *desk = first_output(server);
	int dw = server->desk_w, dh = server->desk_h;
	unsigned room = dw >= 8 && dh >= 8 ? desk_room_shown(dw, dh) : 0;
	int level = (int)desk_gfx_level();
	bool drew = false;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		struct wlr_box box = { 0 };
		if (o->wlr_output != desk && !o->off && o->wlr_output->enabled) {
			wlr_output_layout_get_box(server->output_layout, o->wlr_output, &box);
		}
		if (wlr_box_empty(&box) || dw < 8 || dh < 8) {
			if (o->side) {
				wlr_scene_node_destroy(&o->side->node);
				o->side = NULL;
				drew = true;
			}
			continue;
		}
		if (!force && o->side && o->side_room == room && o->side_level == level
			&& wlr_box_equal(&box, &o->side_box)) {
			continue;
		}
		int ow, oh;
		desk_ambient_size(dw, dh, &ow, &oh);
		double k = clampf_d((double)ow / dw, 0.25, 1.0);
		int bw = (int)ceil(box.width * k), bh = (int)ceil(box.height * k);
		struct desk_buf *buf = buf_create(bw, bh);
		cairo_scale(buf->cr, (double)bw / box.width, (double)bh / box.height);
		desk_draw_side_desk(buf->cr, box.x, box.y, box.width, box.height, dw, dh);
		scene_put(&o->side, server->layer_side, buf, box.x, box.y, NULL);
		if (o->side) {
			wlr_scene_buffer_set_dest_size(o->side, box.width, box.height);
			wlr_scene_buffer_set_opacity(o->side, (float)server->side_light);
		}
		o->side_box = box;
		o->side_room = room;
		o->side_level = level;
		drew = true;
	}
	if (drew) {
		server_schedule_frames(server);
	}
}

/* The lamp, as far up as it is (L, 0..1), on the bare desk beside the
 * desk's screen: dark at a login, lit as the desk is. */
static void side_desks_light(struct desk_server *server, double L)
{
	L = clampf_d(L, 0.0, 1.0);
	if (fabs(L - server->side_light) < 1e-4) {
		return;
	}
	server->side_light = L;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (o->side) {
			wlr_scene_buffer_set_opacity(o->side, (float)L);
		}
	}
}

static struct wlr_output *view_screen(struct desk_toplevel *view);
static void view_middle(struct desk_toplevel *view, double *x, double *y);
static struct wlr_box max_area(struct desk_server *server, struct wlr_output *out);
static void view_set_fullscreen(struct desk_toplevel *view, bool on);

/* A window whose middle is on no screen now -- its screen taken away, or
 * turned off -- is put on the nearest one that is there, all of it on it
 * where it fits; one that had all of its screen, or was opened out to it,
 * has the new one's. */
static void windows_onto_screens(struct desk_server *server)
{
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (!view->scene_tree || view->unmanaged) {
			continue;
		}
		struct wlr_scene_node *node = &view->scene_tree->node;
		double mx, my;
		view_middle(view, &mx, &my);
		if (wlr_output_layout_output_at(server->output_layout, mx, my)) {
			continue;
		}
		struct wlr_output *out = screen_at(server, mx, my);
		if (!out) {
			continue;
		}
		wlr_log(WLR_INFO, "a window off every screen comes onto %s: %s", out->name,
			view_title(view));
		if (view->fullscreen) {
			/* off the screen it had, and as it was before it took it */
			view_set_fullscreen(view, false);
			view_middle(view, &mx, &my);
			if (wlr_output_layout_output_at(server->output_layout, mx, my)) {
				continue;
			}
		}
		/* the nearest place on that screen, all of it on it where it fits
		 * -- in the paper's coordinates, which the area is in */
		double cx, cy;
		wlr_output_layout_closest_point(server->output_layout, out, mx, my, &cx, &cy);
		cx -= server->desk_dx;
		cy -= server->desk_dy + server->win_dy;
		struct wlr_box a = screen_area(server, out);
		int l, t, r, b;
		view_insets(view, &l, &t, &r, &b);
		/* one opened out is put back, later, at its own size */
		int w = view->maximized ? view->saved_inner_w + l + r : view->outer_w;
		int h = view->maximized ? view->saved_inner_h + t + b : view->outer_h;
		int x = (int)(cx - w * 0.5), y = (int)(cy - h * 0.5);
		x = x + w > a.x + a.width ? a.x + a.width - w : x;
		y = y + h > a.y + a.height ? a.y + a.height - h : y;
		x = x < a.x ? a.x : x;
		y = y < a.y ? a.y : y;
		if (view->maximized) {
			/* opened out to the screen it is on now, and to be put back
			 * there, on that screen too */
			struct wlr_box ma = max_area(server, out);
			view->saved_x = x;
			view->saved_y = y;
			wlr_scene_node_set_position(node, ma.x, ma.y);
			view_set_size(view, ma.width - l - r, ma.height - t - b);
		} else {
			wlr_scene_node_set_position(node, x, y);
		}
		/* folded, it opens out to where the window is now */
		if (view->sheet.phase != SHEET_IDLE) {
			view->sheet.x0 = node->x;
			view->sheet.y0 = node->y;
		}
		view_sync_shadow(view);
		view_send_position(view);
	}
	/* A sheet folded on a screen that has gone: along the front of the
	 * nearest one that is there. */
	wl_list_for_each(view, &server->toplevels, link) {
		struct desk_sheet *s = &view->sheet;
		if ((s->phase != SHEET_PARKED && s->phase != SHEET_FOLDING) || s->in_drawer
			|| s->to_drawer || !s->fold
			|| (s->tree && s->tree->node.parent == server->layer_drag)) {
			continue;
		}
		int ow, oh, pad;
		desk_fold_extent(s->fold, 1.0, &ow, &oh, &pad);
		double mx = s->x1 + ow * 0.5 + server->desk_dx;
		double my = s->y1 + oh * 0.5 + server->desk_dy + server->win_dy;
		if (wlr_output_layout_output_at(server->output_layout, mx, my)) {
			continue;
		}
		struct wlr_output *out = screen_at(server, mx, my);
		if (!out) {
			continue;
		}
		s->slot = sheet_alloc_slot(server, out, s);
		sheet_slot_xy(server, out, s->slot, ow, oh, &s->x1, &s->y1);
		if (s->phase == SHEET_PARKED) {
			sheet_present(server, s, 1.0, s->x1, s->y1);
		}
	}
}

static void screens_idle_cb(void *data)
{
	struct desk_server *server = data;
	server->screens_idle = NULL;
	side_desks_paint(server, false);
	windows_onto_screens(server);
	server_schedule_frames(server);
}

/* The screens set out some other way -- one plugged in, one taken away,
 * moved, made another size, turned off or on: once the change is over,
 * the bare desk beside the desk's screen, and the windows that were on a
 * screen that is not there now. */
static void layout_change(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, layout_change);
	(void)data;
	if (server->screens_idle || server->quitting) {
		return;
	}
	server->screens_idle = wl_event_loop_add_idle(wl_display_get_event_loop(server->wl_display),
		screens_idle_cb, server);
}

/* Where a window is on the screens: its middle -- or for one with the
 * whole of a screen, which has no middle of its own, that screen's corner
 * -- with the paper's move (desk_follow) added for one lying on the desk. */
static void view_middle(struct desk_toplevel *view, double *x, double *y)
{
	struct desk_server *server = view->server;
	const struct wlr_scene_node *node = &view->scene_tree->node;
	if (view->fullscreen) {
		*x = node->x + 1.0;
		*y = node->y + 1.0;
		return;
	}
	*x = node->x + view->outer_w * 0.5;
	*y = node->y + view->outer_h * 0.5;
	if (node->parent == server->layer_win) {
		*x += server->desk_dx;
		*y += server->desk_dy + server->win_dy;
	}
}

/* The screen a window is on: the one its middle is on. */
static struct wlr_output *view_screen(struct desk_toplevel *view)
{
	if (!view->scene_tree) {
		return first_output(view->server);
	}
	double x, y;
	view_middle(view, &x, &y);
	return screen_at(view->server, x, y);
}

/* What a maximised sheet on screen out covers: on the desk's, the work
 * area, and the drawer's pull under it too -- a strip of wood under a
 * window that fills the desk is a gap, not a handle anybody is reaching
 * for; on a screen beside it, all of that screen. */
static struct wlr_box max_area(struct desk_server *server, struct wlr_output *out)
{
	struct wlr_box wa = screen_area(server, out);
	int h = server->desk_h;
	if (!screen_is_side(server, out) && h >= 8) {
		wa.height = h - wa.y;
	}
	return wa;
}

/* A sheet may hang off the sides or the bottom, but its top edge -- the
 * title bar you move it by -- never goes up into the tray, nor off the
 * top of a screen beside the desk's. x is where across it its middle is
 * (the paper's coordinates, as y). */
static int work_clamp_y(struct desk_server *server, int x, int y)
{
	struct wlr_box wa = screen_area(server, screen_at(server, x + server->desk_dx,
		y + server->desk_dy + server->win_dy));
	return y < wa.y ? wa.y : y;
}

/* Where the whole screen goes in the room. A film or a game in front of
 * everything on the desk -- the drawer's handle, the sheets parked at its
 * edge -- or it is not the whole screen; the drawer pulled out is in front
 * of the game, as you would pull it out over anything; and the bookcase,
 * the Control Panel and the post box, the lock and the lamp going out are
 * in front of it still, being where you look up to from the desk. With
 * nothing on the whole screen, back where it was, over the windows. */
static void fullscreen_layer_order(struct desk_server *server)
{
	bool any = false;
	struct desk_toplevel *v;
	wl_list_for_each(v, &server->toplevels, link) {
		if (v->fullscreen && v->scene_tree) {
			any = true;
			break;
		}
	}
	bool drawer = server->drawer_moving || server->drawer_t > 0.004;
	struct wlr_scene_tree *over = any && !drawer ? server->layer_parked : server->layer_win;
	wlr_scene_node_place_above(&server->layer_full->node, &over->node);
	/* X11 menus over the game they belong to */
	wlr_scene_node_place_above(&server->layer_xover->node, &server->layer_full->node);
}

/* The whole screen: off the desk altogether, over everything on it, with
 * no frame and no shadow -- a film, a game, a slide show. And back to the
 * sheet it was, where it was, after. */
static void view_set_fullscreen(struct desk_toplevel *view, bool on)
{
	struct desk_server *server = view->server;
	/* the screen it is on, and the whole of that one */
	struct wlr_output *out = view_screen(view);
	if (!view->scene_tree || !out || on == view->fullscreen) {
		goto tell;
	}
	if (on) {
		int l, t, r, b;
		view_insets(view, &l, &t, &r, &b);
		struct wlr_box box;
		wlr_output_layout_get_box(server->output_layout, out, &box);
		if (wlr_box_empty(&box)) {
			box = (struct wlr_box){ 0, 0, out->width, out->height };
		}
		view->fs_x = view->scene_tree->node.x;
		view->fs_y = view->scene_tree->node.y;
		view->fs_inner_w = view->outer_w - l - r;
		view->fs_inner_h = view->outer_h - t - b;
		view->fullscreen = true;
		wlr_scene_node_reparent(&view->scene_tree->node, server->layer_full);
		wlr_scene_node_set_position(&view->scene_tree->node, box.x, box.y);
		view_repaint_chrome(view);
		view_sync_shadow(view);
		view_set_size(view, box.width, box.height);
	} else {
		view->fullscreen = false;
		wlr_scene_node_reparent(&view->scene_tree->node, server->layer_win);
		wlr_scene_node_set_position(&view->scene_tree->node, view->fs_x, view->fs_y);
		view_repaint_chrome(view);
		view_sync_shadow(view);
		view_set_size(view, view->fs_inner_w, view->fs_inner_h);
	}
	wlr_scene_node_raise_to_top(&view->scene_tree->node);
	fullscreen_layer_order(server);
tell:
#ifdef DESK_HAVE_XWAYLAND
	if (view->xsurface) {
		wlr_xwayland_surface_set_fullscreen(view->xsurface, view->fullscreen);
		return;
	}
#endif
	if (view->xdg_toplevel && view->xdg_toplevel->base->initialized) {
		wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel, view->fullscreen);
	}
}

static void view_toggle_max_now(struct desk_toplevel *view)
{
	struct wlr_output *out = view_screen(view);
	if (!out || !view->scene_tree || view->fullscreen) {
		return;
	}
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	if (!view->maximized) {
		view->saved_x = view->scene_tree->node.x;
		view->saved_y = view->scene_tree->node.y;
		view->saved_inner_w = view->outer_w - l - r;
		view->saved_inner_h = view->outer_h - t - b;
		/* all of the desk that is not the tray: down to the bottom edge,
		 * over the drawer's pull (the drawer still opens with Super+D); on
		 * a screen beside the desk's, all of it */
		struct wlr_box wa = max_area(view->server, out);
		int iw = wa.width - l - r;
		int ih = wa.height - t - b;
		if (iw < 200) {
			iw = 200;
		}
		if (ih < 120) {
			ih = 120;
		}
		wlr_scene_node_set_position(&view->scene_tree->node, wa.x, wa.y);
		view_sync_shadow(view);
		view_set_maximized(view, true);
		view_set_size(view, iw, ih);
		view->maximized = 1;
	} else {
		view_set_maximized(view, false);
		wlr_scene_node_set_position(&view->scene_tree->node, view->saved_x,
			work_clamp_y(view->server, view->saved_x + (view->saved_inner_w + l + r) / 2,
				view->saved_y));
		view_set_size(view, view->saved_inner_w, view->saved_inner_h);
		view_sync_shadow(view);
		view->maximized = 0;
	}
}

/* The tray is only known for certain once the room is traced, and moves
 * when the screen changes shape. Then: any window that has ended up on it
 * comes down off it, a maximised one fits what there is now, and X is told
 * where windows go (_NET_WORKAREA), for the programs that ask. */
static void windows_keep_off_tray(struct desk_server *server)
{
	struct wlr_box wa = work_area(server);
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (!view->scene_tree || view->fullscreen || view->unmanaged) {
			continue;
		}
		struct wlr_scene_node *node = &view->scene_tree->node;
		/* (one on a screen beside the desk's is clear of the tray, and
		 * opened out to fit that screen) */
		struct wlr_output *out = view_screen(view);
		if (view->maximized) {
			int l, t, r, b;
			view_insets(view, &l, &t, &r, &b);
			struct wlr_box ma = max_area(server, out);
			wlr_scene_node_set_position(node, ma.x, ma.y);
			view_set_size(view, ma.width - l - r, ma.height - t - b);
			view_sync_shadow(view);
		} else if (!screen_is_side(server, out) && node->y < wa.y) {
			wlr_scene_node_set_position(node, node->x, wa.y);
			view_sync_shadow(view);
			view_send_position(view);
		}
	}
#ifdef DESK_HAVE_XWAYLAND
	if (server->xwayland && server->xwayland->xwm) {
		wlr_xwayland_set_workareas(server->xwayland, &wa, 1);
	}
#endif
}

/* ---- the corner of a sheet ------------------------------------------
 *
 * The only controls a sheet has: its top right corner, turned down a
 * little, with Fold, Unfold and Discard printed on the back. Near it, the
 * corner lifts; pressing one of them turns the corner the way that
 * instruction would, and then does it. */

static void curl_sizes(const struct desk_toplevel *view, double *rest, double *open)
{
	desk_curl_sizes(view->server->desk_w, view->server->desk_h, rest, open);
	/* never more than a good part of a small sheet */
	double most = 0.46 * fmin(view->outer_w, view->outer_h);
	if (*open > most) {
		*open = most;
	}
	if (*rest > *open * 0.3) {
		*rest = *open * 0.3;
	}
}

static bool curl_wanted(const struct desk_toplevel *view)
{
	/* a sheet that draws its own frame has its own controls, where the
	 * corner would be */
	return view->scene_tree && view_framed(view) && view->sheet.phase == SHEET_IDLE
		&& !view->ua_on && view->outer_w > 60 && view->outer_h > 60;
}

static bool curl_accepts(struct wlr_scene_buffer *buffer, double *sx, double *sy)
{
	struct desk_toplevel *view = buffer->node.data;
	return view && desk_curl_contains(view->curl_box, view->curl_s, *sx, *sy);
}

static void view_curl_reset(struct desk_toplevel *view)
{
	if (view->curl_under) {
		cairo_surface_destroy(view->curl_under);
		view->curl_under = NULL;
	}
	view->curl = NULL;
	view->curl_s = 0.0;
	view->curl_v = 0.0;
	view->curl_at = 0.0;
	view->curl_near = false;
	view->curl_hover_at = 0.0;
	view->curl_hot = -1;
	view->curl_act = -1;
	view->curl_done = false;
	view->curl_key = 0;
}

/* The pixels of a scene buffer we drew ourselves, at its size on screen. */
static void paint_scene_buffer(cairo_t *cr, struct wlr_scene_buffer *sb, double x, double y)
{
	if (!sb || !sb->node.enabled || !sb->buffer || sb->buffer->impl != &cairo_buffer_impl) {
		return;
	}
	struct desk_buf *b = wl_container_of(sb->buffer, b, base);
	double bw = cairo_image_surface_get_width(b->surface);
	double bh = cairo_image_surface_get_height(b->surface);
	double dw = sb->dst_width > 0 ? sb->dst_width : bw;
	double dh = sb->dst_height > 0 ? sb->dst_height : bh;
	cairo_save(cr);
	cairo_translate(cr, x, y);
	cairo_scale(cr, dw / bw, dh / bh);
	cairo_set_source_surface(cr, b->surface, 0, 0);
	cairo_paint_with_alpha(cr, sb->opacity);
	cairo_restore(cr);
}

/* What is under a sheet's top right corner: the desk, and every sheet
 * lying beneath this one there -- its shadow, its paper, its own corner,
 * and, once the corner is lifted far enough to see it, what is written on
 * it. Put together once for a lift, and again only if the sheet moves;
 * reading back what another program drew is not a thing to do every frame. */
static cairo_surface_t *curl_underlay(struct desk_toplevel *view, bool full, double sz)
{
	struct desk_server *server = view->server;
	int cx = view->scene_tree->node.x + view->outer_w;
	int cy = view->scene_tree->node.y;
	int c = full ? (int)ceil(fmin(view->outer_w, view->outer_h) * 0.8 + 6.0)
		: (int)ceil(sz * 1.3 + 6.0);
	/* once more for each lift, so it shows what is there now; and back to
	 * the cheap one -- no other program's pixels -- when the corner is down */
	if (view->curl_under && view->curl_under_x == cx && view->curl_under_y == cy
		&& view->curl_under_c >= c && view->curl_under_full == full) {
		return view->curl_under;
	}
	if (view->curl_under) {
		cairo_surface_destroy(view->curl_under);
	}
	cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, c, c);
	cairo_t *cr = cairo_create(img);
	cairo_translate(cr, -(cx - c), -cy);
	/* in the windows' own layer, which goes up with the desk by just the
	 * drawer's lift, the room lies where it does with the drawer shut */
	desk_paint_room_at(cr, server->desk_w, server->desk_h, 0.0);
	/* the sheets beneath, bottom first: the list runs top to bottom */
	struct desk_toplevel *below[64];
	int n = 0;
	bool past = false;
	struct desk_toplevel *v;
	wl_list_for_each(v, &server->toplevels, link) {
		if (v == view) {
			past = true;
			continue;
		}
		if (!past || n >= 64 || !v->scene_tree || !v->scene_tree->node.enabled
			|| v->fullscreen || v->scene_tree->node.parent != view->scene_tree->node.parent) {
			continue;
		}
		int vx = v->scene_tree->node.x, vy = v->scene_tree->node.y;
		int pad = DESK_SHADOW + 40;
		if (vx - pad > cx || vx + v->outer_w + pad < cx - c || vy - pad > cy + c
			|| vy + v->outer_h + pad < cy) {
			continue;
		}
		below[n++] = v;
	}
	for (int i = n - 1; i >= 0; i--) {
		v = below[i];
		double tx = v->scene_tree->node.x, ty = v->scene_tree->node.y;
		if (v->shadow.node) {
			paint_scene_buffer(cr, v->shadow.node, tx + v->shadow.node->node.x,
				ty + v->shadow.node->node.y);
		}
		if (v->chrome) {
			paint_scene_buffer(cr, v->chrome, tx + v->chrome->node.x, ty + v->chrome->node.y);
		}
		if (full && v->surface_tree) {
			cairo_save(cr);
			cairo_rectangle(cr, tx, ty, v->outer_w, v->outer_h);
			cairo_clip(cr);
			paint_client(cr, view_surface(v), tx + v->surface_tree->node.x,
				ty + v->surface_tree->node.y);
			cairo_restore(cr);
		}
		if (v->curl) {
			paint_scene_buffer(cr, v->curl, tx + v->curl->node.x, ty + v->curl->node.y);
		}
	}
	cairo_destroy(cr);
	cairo_surface_flush(img);
	view->curl_under = img;
	view->curl_under_x = cx;
	view->curl_under_y = cy;
	view->curl_under_c = c;
	view->curl_under_full = full;
	return img;
}

/* Draw the corner again, if anything it shows has changed -- including
 * where on the desk it is, since what is under it shows where it was. */
static void view_curl_paint(struct desk_toplevel *view)
{
	if (!curl_wanted(view)) {
		if (view->curl) {
			wlr_scene_node_set_enabled(&view->curl->node, false);
		}
		return;
	}
	double rest, open;
	curl_sizes(view, &rest, &open);
	if (view->curl_s <= 0.0) {
		view->curl_s = rest;
	}
	double sz = clampf_d(view->curl_s, 0.0, fmin(view->outer_w, view->outer_h) * 0.8);
	double box = ceil(fmax(sz, 1.0) * 1.12 + 18.0);
	double lift = clampf_d((sz - rest) / fmax(open - rest, 1.0), 0.0, 1.3);
	int bx = view->scene_tree->node.x + view->outer_w - (int)box;
	int by = view->scene_tree->node.y;
	uint64_t key = 14695981039346656037ull;
	int what[6] = { (int)(sz * 4.0), view->curl_hot, view->maximized, bx, by, (int)box };
	key = key_add(key, what, sizeof what);
	if (view->curl && view->curl_key == key) {
		wlr_scene_node_set_enabled(&view->curl->node, true);
		return;
	}
	view->curl_key = key;
	view->curl_box = box;
	struct desk_buf *buf = buf_create((int)box, (int)box);
	/* where the paper was, what is under it */
	bool full = sz > rest * 1.6 || view->curl_near || view->curl_act >= 0;
	cairo_surface_t *under = curl_underlay(view, full, sz);
	if (under && sz > 0.5) {
		cairo_save(buf->cr);
		cairo_move_to(buf->cr, box - sz, 0);
		cairo_line_to(buf->cr, box, 0);
		cairo_line_to(buf->cr, box, sz);
		cairo_close_path(buf->cr);
		cairo_clip(buf->cr);
		cairo_set_source_surface(buf->cr, under, box - view->curl_under_c, 0);
		cairo_paint(buf->cr);
		cairo_restore(buf->cr);
	}
	desk_draw_curl(buf->cr, box, sz, open, lift, view->curl_hot, view->maximized != 0,
		view_title(view));
	scene_put(&view->curl, view->scene_tree, buf, view->outer_w - (int)box, 0, view);
	view->curl->point_accepts_input = curl_accepts;
	wlr_scene_node_set_enabled(&view->curl->node, true);
}

static void view_toggle_max(struct desk_toplevel *view);

/* Where the pointer is, for every sheet: near the corner of the one it is
 * over, or near none. */
static void curl_track(struct desk_server *server, struct desk_toplevel *under,
	double px, double py)
{
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		bool near = view->curl_near;
		bool on_fold = false;
		int hot = -1;
		if (view == under && curl_wanted(view) && view->curl_act < 0) {
			double rest, open;
			curl_sizes(view, &rest, &open);
			double lx = px - view->scene_tree->node.x;
			double ly = py - view->scene_tree->node.y;
			double dx = view->outer_w - lx;
			/* On the fold itself -- not merely near the corner, where the
			 * pointer passes on its way to everything at the top right of
			 * a program -- the corner stirs; held there it turns up
			 * (curls_tick), and a click turns it up at once. Turned up,
			 * it stays so while the pointer is anywhere on its back. */
			double zone = rest * 2.6;
			on_fold = dx >= -6.0 && dx <= zone && ly >= -6.0 && ly <= zone
				&& (!view->own_frame || mount_on_fold(view, dx, ly));
			if (near) {
				near = dx >= -6.0 && dx <= open * 1.30 && ly >= -6.0 && ly <= open * 1.30;
			}
			if (near && view->curl) {
				hot = desk_curl_hit(view->curl_box, view->curl_s, open,
					lx - (view->outer_w - view->curl_box), ly);
			}
		} else {
			near = false;
		}
		if (!near && on_fold) {
			if (view->curl_hover_at <= 0.0) {
				view->curl_hover_at = real_sec();
				server_schedule_frames(server);
			}
		} else {
			view->curl_hover_at = 0.0;
		}
		if (near != view->curl_near || hot != view->curl_hot) {
			view->curl_near = near;
			view->curl_hot = hot;
			view_curl_paint(view);
			server_schedule_frames(server);
		}
	}
}

/* A click on the fold: turned up at once, without the wait. */
static void curl_turn_up(struct desk_toplevel *view)
{
	if (!view->curl_near && view->scene_tree) {
		snd_at(view->server, DESK_SND_CORNER_LIFT,
			view->scene_tree->node.x + view->outer_w, 1.0);
	}
	view->curl_near = true;
	view->curl_hover_at = 0.0;
	server_schedule_frames(view->server);
}

/* One of the instructions on the back, pressed: the corner is turned the
 * way that instruction goes, and then it is done. */
static void curl_press(struct desk_toplevel *view, int item)
{
	if (view->scene_tree) {
		snd_at(view->server, DESK_SND_CORNER_TAP,
			view->scene_tree->node.x + view->outer_w, 1.0);
	}
	view->curl_act = item;
	view->curl_act_at = monotonic_sec();
	view->curl_done = false;
	view->curl_hot = item;
	server_schedule_frames(view->server);
}

/* Once a frame: every corner goes where it is going, on a spring soft
 * enough to be paper -- it goes a little past and comes back. */
static void curls_tick(struct desk_server *server)
{
	double now = monotonic_sec();
	bool more = false;
	struct desk_toplevel *view, *tmp;
	wl_list_for_each_safe(view, tmp, &server->toplevels, link) {
		if (!curl_wanted(view)) {
			if (view->curl) {
				wlr_scene_node_set_enabled(&view->curl->node, false);
			}
			continue;
		}
		double rest, open;
		curl_sizes(view, &rest, &open);
		/* held on the fold long enough: turned up */
		if (!view->curl_near && view->curl_hover_at > 0.0) {
			if (real_sec() - view->curl_hover_at >= 0.5 && view->server->conf.corner_hover) {
				snd_at(server, DESK_SND_CORNER_LIFT,
					view->scene_tree->node.x + view->outer_w, 1.0);
				view->curl_near = true;
				view->curl_hover_at = 0.0;
			} else {
				more = true;
			}
		}
		/* the pointer resting on the fold stirs it, so it can be seen to be
		 * something that lifts */
		double to = view->curl_near ? open : (view->curl_hover_at > 0.0 ? rest * 1.7 : rest);
		if (view->curl_act >= 0) {
			double e = now - view->curl_act_at;
			switch (view->curl_act) {
			case DESK_CURL_FOLD:
				/* turned further, as if to fold the sheet in -- then it is */
				to = open * 1.55;
				if (e > 0.20) {
					view->curl_act = -1;
					view->curl_near = false;
					view->curl_hot = -1;
					view->curl_s = rest;
					view->curl_v = 0.0;
					view_fold_toggle(view);
					continue;
				}
				break;
			case DESK_CURL_UNFOLD:
				/* laid flat, and the sheet opens out */
				to = rest * 0.15;
				if (e > 0.16) {
					view->curl_act = -1;
					view->curl_near = false;
					view->curl_hot = -1;
					view_toggle_max(view);
					to = rest;
				}
				break;
			default:
				/* peeled right back, the way a sheet is torn off a pad --
				 * and if the program keeps it (it asks about unsaved work)
				 * the corner goes back down */
				to = open * 1.9;
				if (e > 0.22 && !view->curl_done) {
					view->curl_done = true;
					view_close(view);
					/* it may be gone now; the list is walked safely */
				}
				if (e > 1.10) {
					view->curl_act = -1;
					view->curl_hot = -1;
					to = view->curl_near ? open : rest;
				}
				break;
			}
			more = true;
		}
		double dt = view->curl_at > 0.0 ? clampf_d(now - view->curl_at, 0.0, 0.05) : 0.0;
		view->curl_at = now;
		if (view->curl_s <= 0.0) {
			view->curl_s = rest;
		}
		const double k = 240.0, c = 2.0 * 0.60 * 15.49;
		double acc = k * (to - view->curl_s) - c * view->curl_v;
		view->curl_v += acc * dt;
		view->curl_s += view->curl_v * dt;
		if (gfx_flat()) {
			view->curl_s = to;
			view->curl_v = 0.0;
		}
		if (fabs(to - view->curl_s) < 0.15 && fabs(view->curl_v) < 2.0) {
			view->curl_s = to;
			view->curl_v = 0.0;
		} else {
			more = true;
		}
		/* the words move with the flap, under a pointer that may not */
		if (view->curl_near && view->curl_act < 0 && view->curl_box > 0.0) {
			view->curl_hot = desk_curl_hit(view->curl_box, view->curl_s, open,
				server->cursor->x - view->scene_tree->node.x
					- (view->outer_w - view->curl_box),
				server->cursor->y - view->scene_tree->node.y);
		}
		view_curl_paint(view);
	}
	if (more) {
		server_schedule_frames(server);
	}
}

/* ---- throwing a sheet away -------------------------------------------
 *
 * A sheet that goes -- discarded from its corner, closed by a key, or put
 * away by its own program -- is screwed up where it lay and thrown off the
 * nearer side of the desk: it goes up as it goes, so it grows a little and
 * its shadow falls further behind it, and it turns over in the air. */

#define THROW_CRUMPLE 0.42

struct desk_throw {
	struct wl_list link;
	struct desk_crumple *c;
	struct wlr_scene_buffer *node;
	double x0, y0;       /* the sheet's top left, as it lay */
	double cx, cy;       /* and its middle, where the ball ends up */
	double start;
	double dir;          /* -1 off the left, +1 off the right */
	double dist, flight, peak, vy, spin;
	double drawn_at;
	double cost;         /* what drawing it last took */
};

static void throw_free(struct desk_throw *th)
{
	wl_list_remove(&th->link);
	if (th->node) {
		wlr_scene_node_destroy(&th->node->node);
	}
	desk_crumple_free(th->c);
	free(th);
}

/* Called as a window goes, while what it last drew is still there to read. */
static void view_throw_away(struct desk_toplevel *view)
{
	struct desk_server *server = view->server;
	struct wlr_surface *s = view_surface(view);
	if (!server->conf.throw_closed || server->quitting || !view->scene_tree || !view->scene_tree->node.enabled
		|| view->unmanaged || view->sheet.phase != SHEET_IDLE
		|| view->outer_w < 60 || view->outer_h < 60 || !s || !s->buffer) {
		return;
	}
	prof_begin(PROF_THROW);
	cairo_surface_t *snap = view_snapshot(view);
	if (cairo_surface_status(snap) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(snap);
		prof_end();
		return;
	}
	throw_paper(server, snap, view->scene_tree->node.x, view->scene_tree->node.y);
	prof_end();
}

/* Screw a sheet up where it lies -- snap is what it looks like, its top
 * left at (x0, y0) on the desk -- and throw it off the nearer side. Takes
 * snap. */
static void throw_paper(struct desk_server *server, cairo_surface_t *snap, double x0,
	double y0)
{
	struct desk_throw *th = calloc(1, sizeof *th);
	if (!th) {
		cairo_surface_destroy(snap);
		return;
	}
	static unsigned seed;
	seed++;
	th->c = desk_crumple_new(snap, seed * 2654435761u);
	int sw = cairo_image_surface_get_width(snap), sh = cairo_image_surface_get_height(snap);
	cairo_surface_destroy(snap);
	if (!th->c) {
		free(th);
		return;
	}
	th->x0 = x0;
	th->y0 = y0;
	th->cx = th->x0 + sw * 0.5;
	th->cy = th->y0 + sh * 0.5;
	th->start = monotonic_sec();
	/* off whichever side is nearer: nobody throws across the whole desk */
	th->dir = th->cx < server->desk_w * 0.5 ? -1.0 : 1.0;
	double R = desk_crumple_radius(th->c);
	th->dist = (th->dir < 0 ? th->cx : server->desk_w - th->cx) + R * 1.6 + 80.0;
	th->flight = clampf_d(0.34 + th->dist / 2600.0, 0.40, 0.80);
	th->peak = R * 0.9 + 60.0;
	double h = (double)((seed * 40503u) % 1000u) / 1000.0;
	th->vy = -40.0 - 120.0 * h;
	th->spin = th->dir * (5.0 + 3.0 * h);
	wl_list_insert(&server->throws, &th->link);
	snd_at(server, DESK_SND_PAPER_CRUMPLE, th->cx, 1.0);
	/* and all that follows is known now: thrown, and landing off the
	 * edge of the desk */
	snd_for_in(server, DESK_SND_PAPER_TOSS, th->cx, 1.0, 0.42, th->flight, THROW_CRUMPLE);
	desk_sound_play_in(DESK_SND_PAPER_LAND, th->dir * 0.9, 1.0, 0.0,
		real_secs(THROW_CRUMPLE + th->flight) + SND_SEEN);
	server_schedule_frames(server);
}

static void throws_tick_body(struct desk_server *server);

static void throws_tick(struct desk_server *server)
{
	if (wl_list_empty(&server->throws)) {
		return;
	}
	prof_begin(PROF_THROW);
	throws_tick_body(server);
	prof_end();
}

static void throws_tick_body(struct desk_server *server)
{
	double now = monotonic_sec();
	bool more = false;
	struct desk_throw *th, *tmp;
	wl_list_for_each_safe(th, tmp, &server->throws, link) {
		double t = now - th->start;
		struct desk_buf *buf;
		int bx, by;
		int dw = 0, dh = 0;
		if (t < THROW_CRUMPLE) {
			/* Screwed up where it lay: thirty times a second, which is
			 * plenty for paper being crushed -- and, once it has begun
			 * to buckle and nobody can read it, drawn at a size the
			 * compositor scales up, since every facet is a picture
			 * turned and stretched, and that costs by the pixel. Not at
			 * Ultra, which is the picture whole however it moves: drawn
			 * full size, and every frame where a frame's worth of it
			 * leaves room for the rest (a big sheet is 25 ms and more on
			 * a laptop, and goes on at thirty). */
			more = true;
			bool ultra = desk_gfx_level() == DESK_GFX_ULTRA;
			double every = ultra && th->cost < 0.010 ? 0.0 : 0.030;
			if (th->node && now - th->drawn_at < every) {
				continue;
			}
			th->drawn_at = now;
			double u = t / THROW_CRUMPLE;
			double x0, y0, x1, y1;
			desk_crumple_extent(th->c, u, &x0, &y0, &x1, &y1);
			double bw = x1 - x0, bh = y1 - y0;
			double k = u < 0.06 || ultra ? 1.0
				: clampf_d(sqrt(150000.0 / fmax(bw * bh, 1.0)), 0.4, 1.0);
			/* and at Low, smaller again (motion_res) */
			k *= motion_res();
			int w = (int)ceil(bw * k), h = (int)ceil(bh * k);
			buf = buf_create(w > 1 ? w : 1, h > 1 ? h : 1);
			cairo_scale(buf->cr, k, k);
			cairo_translate(buf->cr, -x0, -y0);
			desk_crumple_draw(buf->cr, th->c, u);
			th->cost = monotonic_sec() - now;
			bx = (int)floor(th->x0 + x0);
			by = (int)floor(th->y0 + y0);
			dw = (int)ceil(bw);
			dh = (int)ceil(bh);
		} else {
			/* and thrown */
			double p = (t - THROW_CRUMPLE) / th->flight;
			if (p >= 1.0) {
				/* landed, off the edge of the desk (heard: asked for
				 * when it was thrown) */
				throw_free(th);
				continue;
			}
			/* away from the hand quickly, and then just going */
			double s = p < 0.2 ? p * p / 0.4 : p - 0.1;
			s /= 0.9;
			double z = th->peak * sin(3.14159265358979 * p * 0.85);
			double scale = 1.0 + z / 700.0;
			double x = th->cx + th->dir * th->dist * s;
			double y = th->cy + th->vy * (t - THROW_CRUMPLE);
			double R = desk_crumple_radius(th->c) * scale;
			int m = (int)ceil(R * 1.30 + z * 0.65 + 34.0);
			double k = motion_res();
			int pm = (int)ceil(m * 2 * k);
			buf = buf_create(pm, pm);
			if (pm != m * 2) {
				cairo_scale(buf->cr, (double)pm / (m * 2), (double)pm / (m * 2));
			}
			desk_crumple_ball(buf->cr, th->c, m, m, scale,
				th->spin * (t - THROW_CRUMPLE), z);
			bx = (int)floor(x) - m;
			by = (int)floor(y) - m;
			dw = dh = m * 2;
		}
		scene_put(&th->node, server->layer_parked, buf, bx, by, NULL);
		wlr_scene_buffer_set_dest_size(th->node, dw, dh);
		wlr_scene_node_raise_to_top(&th->node->node);
		more = true;
	}
	if (more) {
		server_schedule_frames(server);
	}
}

/* ---- making a sheet bigger or smaller ---------------------------------
 *
 * The rest of every sheet is folded under it at the edges. Pull an edge
 * and the paper comes out from under, round a turn of paper at the edge,
 * with the creases it was folded with; push it and the paper goes back
 * under the same way. The paper follows the hand at once; the program
 * draws into the new size when it can, and until then what it has drawn
 * stays put against the far edge, trimmed to the paper. */

/* The size of what the program has drawn: for an X program what it has
 * committed, not what it has been asked for. */
static void view_drawn_size(struct desk_toplevel *view, struct wlr_box *geo, int *w, int *h)
{
	view_geometry(view, geo);
	*w = geo->width;
	*h = geo->height;
	struct wlr_surface *s = view_surface(view);
	if (view->xsurface && s) {
		*w = s->current.width;
		*h = s->current.height;
		geo->x = geo->y = 0;
	}
}

static void view_sizing_place(struct desk_toplevel *view)
{
	if (!view->sizing || !view->scene_tree || !view->surface_tree) {
		return;
	}
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	struct wlr_box geo;
	int cw, ch;
	view_drawn_size(view, &geo, &cw, &ch);
	int nx = view->scene_tree->node.x, ny = view->scene_tree->node.y;
	/* against the edge that is not moving -- or, opened out or folded
	 * back, where it was until the program has redrawn it */
	int cx = (view->sz_edges & WLR_EDGE_LEFT) ? view->sz_x1 - cw : view->sz_x0;
	int cy = (view->sz_edges & WLR_EDGE_TOP) ? view->sz_y1 - ch : view->sz_y0;
	if (view->sz_hold) {
		if (cw == view->sz_x1 - view->sz_x0 && ch == view->sz_y1 - view->sz_y0) {
			view->sz_hold = false;
		} else {
			cx = view->sz_hx;
			cy = view->sz_hy;
		}
	}
	/* (the scene's node is at the window geometry's corner already) */
	wlr_scene_node_set_position(&view->surface_tree->node, cx - nx, cy - ny);
	/* trimmed to the paper: past its edge it is folded under */
	int vx0 = cx > view->sz_x0 ? cx : view->sz_x0;
	int vy0 = cy > view->sz_y0 ? cy : view->sz_y0;
	int vx1 = cx + cw < view->sz_x1 ? cx + cw : view->sz_x1;
	int vy1 = cy + ch < view->sz_y1 ? cy + ch : view->sz_y1;
	if (vx1 <= vx0 || vy1 <= vy0) {
		vx1 = vx0 + 1;
		vy1 = vy0 + 1;
	}
	if (vx0 == cx && vy0 == cy && vx1 == cx + cw && vy1 == cy + ch) {
		view_clip_rest(view, &geo);
	} else {
		struct wlr_box clip = { geo.x + vx0 - cx, geo.y + vy0 - cy, vx1 - vx0, vy1 - vy0 };
		struct wlr_scene_node *content = view_content_node(view);
		if (content) {
			wlr_scene_subsurface_tree_set_clip(content, &clip);
		}
	}

	/* and over it all, the creases and the turn at the edge */
	int ow = view->outer_w, oh = view->outer_h;
	double c0x = vx0 - nx, c0y = vy0 - ny, c1x = vx1 - nx, c1y = vy1 - ny;
	/* where the program's drawing reaches the paper's edge, no paper has
	 * come out on that side */
	if (vx0 <= view->sz_x0) {
		c0x = 0;
	}
	if (vy0 <= view->sz_y0) {
		c0y = 0;
	}
	if (vx1 >= view->sz_x1) {
		c1x = ow;
	}
	if (vy1 >= view->sz_y1) {
		c1y = oh;
	}
	/* opened out: the four creases it came out along, as it did */
	bool opened = view->sz_hold && view->sz_opened;
	if (opened) {
		c0x = view->sz_was.x - nx;
		c0y = view->sz_was.y - ny;
		c1x = c0x + view->sz_was.width;
		c1y = c0y + view->sz_was.height;
	}
	uint64_t key = 14695981039346656037ull;
	int what[9] = { ow, oh, (int)c0x, (int)c0y, (int)c1x, (int)c1y, (int)view->sz_edges,
		(int)(view->sz_lip * 12.0 + 0.5), opened };
	key = key_add(key, what, sizeof what);
	if (view->sz_over && view->sz_key == key) {
		return;
	}
	view->sz_key = key;
	const int m = 24;
	/* drawn again every time the hand moves: drawn smaller and stretched */
	int fw = ow + m * 2, fh = oh + m * 2;
	double k = motion_res();
	int pw = (int)ceil(fw * k), ph = (int)ceil(fh * k);
	struct desk_buf *buf = buf_create(pw, ph);
	if (pw != fw || ph != fh) {
		cairo_scale(buf->cr, (double)pw / fw, (double)ph / fh);
	}
	cairo_translate(buf->cr, m, m);
	if (opened) {
		desk_draw_unfolded_creases(buf->cr, ow, oh, c0x, c0y, c1x, c1y);
	} else {
		desk_draw_sizing(buf->cr, ow, oh, c0x, c0y, c1x, c1y, view->sz_edges, view->sz_lip,
			view->sz_seed);
	}
	scene_put(&view->sz_over, view->scene_tree, buf, -m, -m, NULL);
	wlr_scene_buffer_set_dest_size(view->sz_over, fw, fh);
	view->sz_over->point_accepts_input = shadow_accepts;
	wlr_scene_node_place_above(&view->sz_over->node, &view->surface_tree->node);
	wlr_scene_node_set_enabled(&view->sz_over->node, true);
}

static void view_sizing_apply(struct desk_toplevel *view);

/* Done: the program has caught up, or will not. The sheet is the size of
 * what it drew again. */
static void view_sizing_stop(struct desk_toplevel *view)
{
	if (!view->sizing) {
		return;
	}
	if (view->scene_tree) {
		view_sizing_apply(view);
	}
	view->sizing = false;
	view->sz_hold = false;
	view->sz_opened = false;
	if (view->sz_over) {
		wlr_scene_node_destroy(&view->sz_over->node);
		view->sz_over = NULL;
	}
	if (!view->scene_tree || !view->surface_tree) {
		return;
	}
	struct wlr_box rgeo;
	view_geometry(view, &rgeo);
	view_clip_rest(view, &rgeo);
	/* where the paper's top left is now, the program's drawing starts */
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	struct wlr_box geo;
	int cw, ch;
	view_drawn_size(view, &geo, &cw, &ch);
	int x = (view->sz_edges & WLR_EDGE_LEFT) ? view->sz_x1 - cw : view->sz_x0;
	int y = (view->sz_edges & WLR_EDGE_TOP) ? view->sz_y1 - ch : view->sz_y0;
	wlr_scene_node_set_position(&view->scene_tree->node, x - l, y - t);
	view->chrome_key = 0;
	view_repaint_chrome(view);
	view_sync_shadow(view);
	view_send_position(view);
}

static void view_sizing_move(struct desk_toplevel *view, int x0, int y0, int x1, int y1,
	unsigned edges)
{
	struct desk_server *server = view->server;
	double now = monotonic_sec();
	if (!view->sizing) {
		view->sizing = true;
		view->sz_lip = 0.0;
		view->sz_at = now;
		view->sz_seed = (unsigned)(now * 1000.0);
		view->sz_x0 = x0;
		view->sz_y0 = y0;
		view->sz_x1 = x1;
		view->sz_y1 = y1;
	}
	view->sz_hold = false;
	view->sz_opened = false;
	/* how hard it is being pulled: how far the edge went since last time */
	int moved = abs(x0 - view->sz_x0) + abs(x1 - view->sz_x1)
		+ abs(y0 - view->sz_y0) + abs(y1 - view->sz_y1);
	double dt = fmax(now - view->sz_at, 0.008);
	view->sz_lip_to = clampf_d(0.50 + moved / dt / 1600.0, 0.50, 1.0);
	view->sz_at = now;
	view->sz_released = 0.0;
	view->sz_edges = edges;
	view->sz_x0 = x0;
	view->sz_y0 = y0;
	view->sz_x1 = x1;
	view->sz_y1 = y1;
	/* The pointer can report a hundred times a second; the paper is drawn
	 * and the program asked once a frame, with wherever it has got to. */
	view->sz_dirty = true;
	server_schedule_frames(server);
}

static void view_sizing_apply(struct desk_toplevel *view)
{
	if (!view->sz_dirty) {
		return;
	}
	view->sz_dirty = false;
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	wlr_scene_node_set_position(&view->scene_tree->node, view->sz_x0 - l, view->sz_y0 - t);
	view_set_size(view, view->sz_x1 - view->sz_x0, view->sz_y1 - view->sz_y0);
	view_repaint_chrome(view);
	view_sync_shadow(view);
}

static void sizing_tick(struct desk_server *server)
{
	double now = monotonic_sec();
	bool more = false;
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (!view->sizing) {
			continue;
		}
		view_sizing_apply(view);
		double dt = view->sz_tick_at > 0.0 ? clampf_d(now - view->sz_tick_at, 0.0, 0.05) : 0.016;
		view->sz_tick_at = now;
		double to = view->sz_lip_to;
		if (view->sz_released > 0.0) {
			to = 0.0;
		} else if (now - view->sz_at > 0.15) {
			/* held still: the turn relaxes, but the edge is still in hand */
			to = 0.45;
		}
		view->sz_lip += (to - view->sz_lip) * (gfx_flat() ? 1.0 : clampf_d(dt * 12.0, 0.0, 1.0));
		if (view->sz_released > 0.0 && view->sz_lip < 0.02) {
			struct wlr_box geo;
			int cw, ch;
			view_drawn_size(view, &geo, &cw, &ch);
			bool caught_up = cw == view->sz_x1 - view->sz_x0 && ch == view->sz_y1 - view->sz_y0;
			/* a program asked to fill the desk may take a while about it */
			double wait = view->sz_hold ? 1.5 : 0.6;
			if (caught_up || real_sec() - view->sz_released > wait) {
				view_sizing_stop(view);
				server_schedule_frames(server);
				continue;
			}
		}
		view_sizing_place(view);
		more = true;
	}
	if (more) {
		server_schedule_frames(server);
	}
}

/* The pointer's shape for where it is on a sheet's edge, WLR_EDGE_ bits. */
static void cursor_for_edges(struct desk_server *server, unsigned e)
{
	const char *name = "default";
	if (e == (WLR_EDGE_TOP | WLR_EDGE_LEFT)) {
		name = "top_left_corner";
	} else if (e == (WLR_EDGE_TOP | WLR_EDGE_RIGHT)) {
		name = "top_right_corner";
	} else if (e == (WLR_EDGE_BOTTOM | WLR_EDGE_LEFT)) {
		name = "bottom_left_corner";
	} else if (e == (WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT)) {
		name = "bottom_right_corner";
	} else if (e & WLR_EDGE_TOP) {
		name = "top_side";
	} else if (e & WLR_EDGE_BOTTOM) {
		name = "bottom_side";
	} else if (e & WLR_EDGE_LEFT) {
		name = "left_side";
	} else if (e & WLR_EDGE_RIGHT) {
		name = "right_side";
	}
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, name);
}

/* ---- opening a sheet out to the desk, and folding it back ------------ */

static void ua_live(struct desk_toplevel *view, bool on)
{
	if (view->chrome) {
		wlr_scene_node_set_enabled(&view->chrome->node, on && view_framed(view));
	}
	if (view->surface_tree) {
		wlr_scene_node_set_enabled(&view->surface_tree->node, on);
	}
	/* the corner comes back where the sheet's corner is now, which is for
	 * view_curl_paint to say */
	if (view->curl && !on) {
		wlr_scene_node_set_enabled(&view->curl->node, false);
	}
	if (view->shadow.node) {
		wlr_scene_node_set_enabled(&view->shadow.node->node, on && !gfx_flat());
	}
}

static void view_unfold_cancel(struct desk_toplevel *view)
{
	if (!view->ua_on) {
		return;
	}
	view->ua_on = false;
	if (view->ua_base) {
		wlr_scene_node_destroy(&view->ua_base->node);
		view->ua_base = NULL;
	}
	for (int i = 0; i < 4; i++) {
		if (view->ua_edge[i]) {
			wlr_scene_node_destroy(&view->ua_edge[i]->node);
			view->ua_edge[i] = NULL;
		}
	}
	if (view->ua_img) {
		cairo_surface_destroy(view->ua_img);
		view->ua_img = NULL;
	}
	ua_live(view, true);
}

#define UA_PHASE_DUR 0.50

/* The part of the sheet that is flat through a phase -- inside the creases
 * this phase opens or closes, less the edges that lift -- and the edges. */
static void ua_phase_setup(struct desk_toplevel *view, int phase)
{
	struct desk_server *server = view->server;
	struct wlr_box from = view->ua_from, to = view->ua_to;
	bool sides = (view->ua_dir > 0) == (phase == 0);
	/* the rectangle inside this phase's creases, and outside them */
	struct wlr_box in, out;
	if (view->ua_dir > 0) {
		in = phase == 0 ? from : (struct wlr_box){ to.x, from.y, to.width, from.height };
		out = phase == 0 ? (struct wlr_box){ to.x, from.y, to.width, from.height } : to;
	} else {
		in = phase == 0 ? (struct wlr_box){ from.x, to.y, from.width, to.height } : to;
		out = phase == 0 ? from : (struct wlr_box){ from.x, to.y, from.width, to.height };
	}
	struct wlr_box flat = in;
	for (int i = 0; i < 4; i++) {
		struct desk_unfold_edge *e = &view->ua_e[i];
		memset(e, 0, sizeof *e);
		e->side = i;
		e->sheet = e->panel = view->ua_img;
		e->sheet_x = e->panel_x = view->ua_img_box.x;
		e->sheet_y = e->panel_y = view->ua_img_box.y;
		e->vx = server->desk_w * 0.5;
		e->vy = server->desk_h * 0.5;
		e->dist = server->desk_h * 1.05;
		double across;
		if (i == 0) {
			e->crease = in.x + in.width;
			e->len = out.x + out.width - e->crease;
		} else if (i == 1) {
			e->crease = in.x;
			e->len = in.x - out.x;
		} else if (i == 2) {
			e->crease = in.y + in.height;
			e->len = out.y + out.height - e->crease;
		} else {
			e->crease = in.y;
			e->len = in.y - out.y;
		}
		if (i < 2) {
			e->t0 = in.y;
			e->t1 = in.y + in.height;
			across = in.width;
		} else {
			e->t0 = in.x;
			e->t1 = in.x + in.width;
			across = in.height;
		}
		/* how much of the sheet by the crease comes up with it: more for a
		 * longer panel, never much of a small sheet */
		e->bend = floor(fmin(fmin(e->len * 0.34 + 36.0, 170.0), across * 0.28));
		view->ua_e_on[i] = ((i < 2) == sides) && e->len >= 12.0 && e->bend >= 12.0;
		if (!view->ua_e_on[i]) {
			if (view->ua_edge[i]) {
				wlr_scene_node_set_enabled(&view->ua_edge[i]->node, false);
			}
			continue;
		}
		if (i == 0) {
			flat.width -= (int)e->bend;
		} else if (i == 1) {
			flat.x += (int)e->bend;
			flat.width -= (int)e->bend;
		} else if (i == 2) {
			flat.height -= (int)e->bend;
		} else {
			flat.y += (int)e->bend;
			flat.height -= (int)e->bend;
		}
	}
	/* The shadow under what is lying flat; what is in the air throws its
	 * own. The sheet's node is moved there -- its frame and its program's
	 * drawing are away until the end -- without the shadow swinging. */
	wlr_scene_node_set_position(&view->scene_tree->node, in.x, in.y);
	view->outer_w = in.width;
	view->outer_h = in.height;
	view->shadow.settled = 0;
	view_sync_shadow(view);
	/* the flat part: a window onto the picture of the sheet */
	if (view->ua_base && flat.width > 0 && flat.height > 0) {
		struct wlr_fbox src = { flat.x - view->ua_img_box.x, flat.y - view->ua_img_box.y,
			flat.width, flat.height };
		scene_source(view->ua_base, &src);
		wlr_scene_buffer_set_dest_size(view->ua_base, flat.width, flat.height);
		wlr_scene_node_set_position(&view->ua_base->node, flat.x - in.x, flat.y - in.y);
		wlr_scene_node_set_enabled(&view->ua_base->node, true);
	} else if (view->ua_base) {
		wlr_scene_node_set_enabled(&view->ua_base->node, false);
	}
	view->ua_phase = phase;
	view->ua_start = monotonic_sec();
}

static bool ua_phase_empty(const struct desk_toplevel *view)
{
	for (int i = 0; i < 4; i++) {
		if (view->ua_e_on[i]) {
			return false;
		}
	}
	return true;
}

static void ua_paint(struct desk_toplevel *view, double u)
{
	int nx = view->scene_tree->node.x, ny = view->scene_tree->node.y;
	for (int i = 0; i < 4; i++) {
		if (!view->ua_e_on[i]) {
			continue;
		}
		const struct desk_unfold_edge *e = &view->ua_e[i];
		double x0, y0, x1, y1;
		desk_unfold_edge_extent(e, u, &x0, &y0, &x1, &y1);
		int bx = (int)floor(x0), by = (int)floor(y0);
		int bw = (int)ceil(x1) - bx, bh = (int)ceil(y1) - by;
		if (bw < 1 || bh < 1) {
			continue;
		}
		/* in motion the whole time: drawn smaller and stretched */
		double k = motion_res();
		int pw = (int)ceil(bw * k), ph = (int)ceil(bh * k);
		struct desk_buf *buf = buf_create(pw, ph);
		if (pw != bw || ph != bh) {
			cairo_scale(buf->cr, (double)pw / bw, (double)ph / bh);
		}
		cairo_translate(buf->cr, -bx, -by);
		desk_draw_unfold_edge(buf->cr, e, u);
		scene_put(&view->ua_edge[i], view->scene_tree, buf, bx - nx, by - ny, NULL);
		wlr_scene_buffer_set_dest_size(view->ua_edge[i], bw, bh);
		view->ua_edge[i]->point_accepts_input = shadow_accepts;
		wlr_scene_node_set_enabled(&view->ua_edge[i]->node, true);
		wlr_scene_node_raise_to_top(&view->ua_edge[i]->node);
	}
}

static void view_unfold_finish(struct desk_toplevel *view)
{
	struct desk_server *server = view->server;
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	struct wlr_box to = view->ua_to, from = view->ua_from;
	int dir = view->ua_dir;
	view_unfold_cancel(view);
	/* the program is asked for the new size now, and its drawing stays
	 * where it was on the paper until it has redrawn it */
	wlr_scene_node_set_position(&view->scene_tree->node, to.x, to.y);
	view->outer_w = to.width;
	view->outer_h = to.height;
	view->sizing = true;
	view->sz_x0 = to.x + l;
	view->sz_y0 = to.y + t;
	view->sz_x1 = to.x + to.width - r;
	view->sz_y1 = to.y + to.height - b;
	view->sz_edges = 0;
	view->sz_hold = true;
	view->sz_hx = from.x + l;
	view->sz_hy = from.y + t;
	view->sz_opened = dir > 0;
	view->sz_was = from;
	view->sz_lip = view->sz_lip_to = 0.0;
	view->sz_at = monotonic_sec();
	view->sz_released = real_sec();   /* waiting on a program: real time */
	view->sz_seed = (unsigned)(view->sz_at * 1000.0);
	view->sz_dirty = false;
	view->chrome_key = 0;
	view->shadow.settled = 0;
	view_set_maximized(view, dir > 0);
	view_set_size(view, view->sz_x1 - view->sz_x0, view->sz_y1 - view->sz_y0);
	view_repaint_chrome(view);
	view_sync_shadow(view);
	view_curl_paint(view);
	server_schedule_frames(server);
}

static void unfold_tick(struct desk_server *server)
{
	double now = monotonic_sec();
	bool more = false;
	struct desk_toplevel *view, *tmp;
	wl_list_for_each_safe(view, tmp, &server->toplevels, link) {
		if (!view->ua_on) {
			continue;
		}
		double t = (now - view->ua_start) / UA_PHASE_DUR;
		while (t >= 1.0 || ua_phase_empty(view)) {
			if (view->ua_phase == 1) {
				view_unfold_finish(view);
				break;
			}
			ua_phase_setup(view, 1);
			t = 0.0;
		}
		if (!view->ua_on) {
			continue;
		}
		double u = view->ua_dir > 0 ? t : 1.0 - t;
		ua_paint(view, clampf_d(u, 0.0, 1.0));
		more = true;
	}
	if (more) {
		server_schedule_frames(server);
	}
}

static bool box_within(struct wlr_box in, struct wlr_box out)
{
	return in.x >= out.x && in.y >= out.y && in.x + in.width <= out.x + out.width
		&& in.y + in.height <= out.y + out.height;
}

/* The green instruction on the back of the corner, a program asking, or a
 * key: open the sheet out to the desk, or fold it back to what it was. */
static void view_toggle_max(struct desk_toplevel *view)
{
	struct desk_server *server = view->server;
	if (!view->scene_tree || view->fullscreen || view->ua_on) {
		return;
	}
	if (!view_framed(view) || view->sheet.phase != SHEET_IDLE || !first_output(server)) {
		view_toggle_max_now(view);
		return;
	}
	view_sizing_stop(view);
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	struct wlr_box cur = { view->scene_tree->node.x, view->scene_tree->node.y,
		view->outer_w, view->outer_h };
	struct wlr_box from, to;
	int dir;
	if (!view->maximized) {
		dir = 1;
		from = cur;
		/* as view_toggle_max_now has it: over the drawer's pull, or all of
		 * a screen beside the desk's */
		to = max_area(server, view_screen(view));
	} else {
		dir = -1;
		from = cur;
		int sw = view->saved_inner_w + l + r;
		to = (struct wlr_box){ view->saved_x,
			work_clamp_y(server, view->saved_x + sw / 2, view->saved_y),
			sw, view->saved_inner_h + t + b };
	}
	/* Only a sheet that is opening out, or folding back into itself, can
	 * be shown doing it; anything else just goes there. */
	if (!(dir > 0 ? box_within(from, to) : box_within(to, from))) {
		view_toggle_max_now(view);
		return;
	}
	if (dir > 0) {
		view->saved_x = cur.x;
		view->saved_y = cur.y;
		view->saved_inner_w = cur.width - l - r;
		view->saved_inner_h = cur.height - t - b;
		view->maximized = 1;
	} else {
		view->maximized = 0;
	}
	view->ua_dir = dir;
	view->ua_from = from;
	view->ua_to = to;

	/* The picture everything is drawn from. Opening out: plain paper the
	 * size of the desk with the sheet as it is laid on it, and the creases
	 * where it was folded under. Folding back: the sheet as it is. */
	struct wlr_box big = dir > 0 ? to : from;
	struct desk_buf *buf = buf_create(big.width, big.height);
	cairo_surface_t *snap = view_snapshot(view);
	if (dir > 0) {
		/* the same paper the sheet's frame will be, so nothing changes
		 * under it when the program takes over */
		desk_draw_blank_sheet(buf->cr, 0, 0, big.width, big.height, view_title(view));
		cairo_set_source_surface(buf->cr, snap, from.x - big.x, from.y - big.y);
		cairo_paint(buf->cr);
		double x0 = from.x - big.x, y0 = from.y - big.y;
		desk_draw_unfolded_creases(buf->cr, big.width, big.height,
			x0, y0, x0 + from.width, y0 + from.height);
	} else {
		cairo_set_source_surface(buf->cr, snap, 0, 0);
		cairo_paint(buf->cr);
	}
	cairo_surface_destroy(snap);
	cairo_surface_flush(buf->surface);
	view->ua_img = cairo_surface_reference(buf->surface);
	view->ua_img_box = big;
	scene_put(&view->ua_base, view->scene_tree, buf, 0, 0, NULL);
	view->ua_base->point_accepts_input = shadow_accepts;

	view->ua_on = true;
	ua_live(view, false);
	ua_phase_setup(view, 0);
	ua_paint(view, dir > 0 ? 0.0 : 1.0);
	server_schedule_frames(server);
}

/* Position the two room sheets for the current drawer position. Both are
 * baked; this only moves and scales nodes, so it is free per frame. */
static void room_layout(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	struct desk_room_sheets sh;
	if (w < 8 || h < 8 || !desk_room_sheets(w, h, &sh)) {
		return;
	}
	double zoom, pan, t;
	desk_room_view(server->drawer_t, &sh, &zoom, &pan, &t);
	int dw = (int)(w * zoom + 0.5), dh = (int)(h * zoom + 0.5);
	int ox = (w - dw) / 2, oy = (int)((h - dh) / 2 + pan * h);
	double scale = (double)dh / sh.h;

	/* The desk keeps its size and slides up out of the drawer's way. */
	int lx = server->desk_dx, ly = server->desk_dy;
	if (server->wallpaper) {
		desk_screen_lay(server, server->wallpaper,
			(struct wlr_fbox){ 0.0, 0.0, (double)sh.w, (double)sh.h }, ox, oy, dw, dh, lx, ly);
	}
	if (server->drawer_sheet) {
		double dy, clip;
		desk_room_drawer_offset(h, server->drawer_t, scale, oy,
			sh.dr_v0, sh.dr_v1, sh.h, &dy, &clip);
		/* Everything above the line is still inside the desk. */
		double ny = oy + dy;
		double cut = clampf_d((clip - ny) / fmax(scale, 1e-6), 0.0, (double)sh.h);
		struct wlr_fbox src = { 0.0, cut, (double)sh.w, (double)sh.h - cut };
		if (src.height < 1.0 || server->drawer_t <= 0.004) {
			wlr_scene_node_set_enabled(&server->drawer_sheet->node, false);
		} else {
			wlr_scene_node_set_enabled(&server->drawer_sheet->node, true);
			desk_screen_lay(server, server->drawer_sheet, src, ox,
				(int)(ny + cut * scale + 0.5), dw, (int)(src.height * scale + 0.5), lx, ly);
		}
		/* what lies in it goes with it, cut at the same line */
		if (server->drw_node) {
			const double *b = server->drw_box;
			double top = ny + b[1] * scale;
			double dcut = clampf_d((clip - top) / fmax(scale, 1e-6), 0.0, b[3]);
			if (server->drawer_t <= 0.004 || b[3] - dcut < 1.0) {
				wlr_scene_node_set_enabled(&server->drw_node->node, false);
			} else {
				double r = server->drw_res > 0.0 ? server->drw_res : 1.0;
				struct wlr_fbox isrc = { 0.0, dcut * r, b[2] * r, (b[3] - dcut) * r };
				wlr_scene_node_set_enabled(&server->drw_node->node, true);
				desk_screen_lay(server, server->drw_node, isrc,
					(int)(ox + b[0] * scale + 0.5), (int)(top + dcut * scale + 0.5),
					(int)(b[2] * scale + 0.5), (int)((b[3] - dcut) * scale + 0.5), lx, ly);
			}
		}
	}
}

/* Where everything over the desk goes for the drawer where it is now:
 * the sprites on the desk follow it by however far it has moved since they
 * were drawn -- a whole number of pixels, because the desk only ever
 * slides, so a moved sprite is exactly the sprite drawn there -- and the
 * shade is a flat colour for the GPU, not a picture to upload. */
static void overlay_place(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	int ox = 0, oy = 0;
	double clip = 0.0;
	if (!server->ovl_under || !desk_desk_place(w, h, server->drawer_t, &ox, &oy, &clip)) {
		return;
	}
	int dx = ox - server->ovl_ox, dy = oy - server->ovl_oy;
	for (int i = 0; i < DESK_OVL_COUNT; i++) {
		struct desk_sprite *sp = &server->ovl[i];
		if (!sp->node) {
			continue;
		}
		bool moves = desk_overlay_on_desk(i);
		wlr_scene_node_set_position(&sp->node->node,
			sp->x + (moves ? dx : 0), sp->y + (moves ? dy : 0));
	}
	float rgb[3];
	double a = desk_desk_shade(server->drawer_t, rgb);
	/* A rectangle ends on a whole pixel and the line it stands for does
	 * not, so the row the line falls in is a rectangle of its own, shaded
	 * by how much of it is above the line -- which is what cairo's edge
	 * did when this was painted. */
	double line = fmin(clip, (double)h);
	int full = line > 0.0 ? (int)floor(line) : 0;
	double part = line > 0.0 ? line - full : 0.0;
	bool on = a > 0.0 && line > 0.0;
	if (on && full > 0) {
		const float color[4] = { rgb[0] * (float)a, rgb[1] * (float)a,
			rgb[2] * (float)a, (float)a };
		wlr_scene_rect_set_color(server->shade, color);
		wlr_scene_rect_set_size(server->shade, w, full);
	}
	wlr_scene_node_set_enabled(&server->shade->node, on && full > 0);
	if (on && part > 0.002) {
		float ea = (float)(a * part);
		const float color[4] = { rgb[0] * ea, rgb[1] * ea, rgb[2] * ea, ea };
		wlr_scene_rect_set_color(server->shade_edge, color);
		wlr_scene_rect_set_size(server->shade_edge, w, 1);
		wlr_scene_node_set_position(&server->shade_edge->node, 0, full);
	}
	wlr_scene_node_set_enabled(&server->shade_edge->node, on && part > 0.002);
}

struct ovl_args {
	int which;
	time_t now;
	int unread;
};

static void ovl_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct ovl_args *a = data;
	desk_draw_overlay(cr, a->which, w, h, a->now, a->unread);
}

/* One thing over the desk, drawn again and cropped to what it covers. */
static void sprite_draw(struct desk_server *server, int i, time_t now, int unread)
{
	struct ovl_args a = { i, now, unread };
	double box[4];
	bool boxed = desk_overlay_box(i, server->desk_w, server->desk_h, box);
	sprite_paint(server, &server->ovl[i],
		desk_overlay_shaded(i) ? server->ovl_under : server->ovl_over,
		boxed ? box : NULL, ovl_draw, &a);
}

/* Everything over the desk that needs drawing again, drawn again. force
 * is for a new room or a new size, when every sprite is stale. */
static void overlay_update(struct desk_server *server, bool force)
{
	int w = server->desk_w, h = server->desk_h;
	if (w < 8 || h < 8) {
		return;
	}
	if (!server->ovl_under) {
		server->ovl_under = wlr_scene_tree_create(server->layer_desk);
		const float none[4] = { 0, 0, 0, 0 };
		server->shade = wlr_scene_rect_create(server->layer_desk, 0, 0, none);
		wlr_scene_node_set_enabled(&server->shade->node, false);
		server->shade_edge = wlr_scene_rect_create(server->layer_desk, 0, 0, none);
		wlr_scene_node_set_enabled(&server->shade_edge->node, false);
		server->ovl_over = wlr_scene_tree_create(server->layer_desk);
	}
	int ox = 0, oy = 0;
	double clip;
	if (!desk_desk_place(w, h, 0.0, &ox, &oy, &clip)) {
		/* no room yet: nothing is placed, and the next paint forces */
		for (int i = 0; i < DESK_OVL_COUNT; i++) {
			server->ovl[i].drawn = false;
			if (server->ovl[i].node) {
				wlr_scene_node_set_enabled(&server->ovl[i].node->node, false);
			}
		}
		server->ovl_placed = false;
		return;
	}
	/* stale if the desk has moved, or if it is another room: a bake that
	 * has just landed, or the last one kept up while the next is made */
	struct desk_room_sheets sh;
	const void *room = desk_room_sheets(w, h, &sh) ? (const void *)sh.background : NULL;
	if (ox != server->ovl_ox || oy != server->ovl_oy || !server->ovl_placed
		|| room != server->ovl_room) {
		force = true;
	}
	server->ovl_room = room;
	server->ovl_ox = ox;
	server->ovl_oy = oy;
	server->ovl_placed = true;

	time_t now = time(NULL);
	int unread = post_unread();
	for (int i = 0; i < DESK_OVL_COUNT; i++) {
		uint64_t key = desk_overlay_key(i, now, unread);
		if (!force && server->ovl[i].drawn && key == server->ovl[i].key) {
			continue;
		}
		server->ovl[i].key = key;
		sprite_draw(server, i, now, unread);
	}
	overlay_place(server);
}

static void desktop_paint_body(struct desk_server *server)
{
	int w = server->desk_w;
	int h = server->desk_h;
	if (w < 8 || h < 8) {
		return;
	}
	/* the lamp what is drawn over the room is lit by: the room's own, as
	 * the room for this screen has it (the shadows follow on the next
	 * frame, paper_tick) */
	desk_light_desk(w, h);
	struct desk_room_sheets sh;
	if (desk_room_sheets(w, h, &sh)) {
		/* The room is one traced scene, delivered as two sheets. */
		struct desk_buf *bg = buf_create(sh.w, sh.h);
		cairo_set_source_surface(bg->cr, sh.background, 0, 0);
		cairo_paint(bg->cr);
		scene_put(&server->wallpaper, server->layer_bg, bg, 0, 0, NULL);

		struct desk_buf *dr = buf_create(sh.w, sh.h);
		cairo_set_source_surface(dr->cr, sh.drawer, 0, 0);
		cairo_paint(dr->cr);
		scene_put(&server->drawer_sheet, server->layer_drawer, dr, 0, 0, NULL);
		room_layout(server);
		/* and what lies in it, over it: a new room is a new felt */
		server->drw_dirty = true;
		drawer_compose(server);
	} else if (!desk_room_shown(w, h)) {
		/* No room at all yet, not even another size's: the bare boards,
		 * under the dark of the lamp not yet lit, drawn a quarter of the
		 * screen and stretched. Drawn whole they were the slowest thing on
		 * the way up -- 0.3 s here, three on a machine of 2010 -- for a
		 * picture that a room read back from the disk replaces a moment
		 * later. */
		int pw = w / 4 > 16 ? w / 4 : 16, ph = h / 4 > 16 ? h / 4 : 16;
		struct desk_buf *wp = buf_create(pw, ph);
		desk_draw_room_placeholder(wp->cr, pw, ph);
		scene_put(&server->wallpaper, server->layer_bg, wp, 0, 0, NULL);
		scene_source(server->wallpaper, NULL);
		wlr_scene_buffer_set_dest_size(server->wallpaper, w, h);
	} else {
		struct desk_buf *wp = buf_create(w, h);
		desk_draw_room(wp->cr, w, h, server->drawer_t);
		scene_put(&server->wallpaper, server->layer_bg, wp, 0, 0, NULL);
		scene_source(server->wallpaper, NULL);
		wlr_scene_buffer_set_dest_size(server->wallpaper, w, h);
	}

	/* The gadgets live on the desk; only what glows behind their glass is
	 * drawn over the bake -- and only what has changed, which after a new
	 * room or a new size is all of it. This is called four times a second
	 * while a room is baking, and the sprites have no business being drawn
	 * again for that. */
	overlay_update(server, false);

	console_dim_apply(server);
	/* and the bare desk beside it, cut from this room */
	side_desks_paint(server, false);
}

static void desktop_paint(struct desk_server *server)
{
	prof_begin(PROF_DESK);
	desktop_paint_body(server);
	prof_end();
	server->wait_room = desk_room_shown(server->desk_w, server->desk_h);
	server->wait_w = server->desk_w;
	server->wait_h = server->desk_h;
	server->wait_drawer = server->drawer_t;
}

/* whether the desk painted while the room is traced would come out any
 * different now (wait_*) */
static bool desktop_waiting_stale(struct desk_server *server)
{
	return server->wait_w != server->desk_w || server->wait_h != server->desk_h
		|| server->wait_drawer != server->drawer_t
		|| server->wait_room != desk_room_shown(server->desk_w, server->desk_h);
}

static void desktop_tick_body(struct desk_server *server)
{
	int w = server->desk_w;
	int h = server->desk_h;
	if (w < 8 || h < 8) {
		return;
	}
	/* The room is baked; only the glowing faces change with the clock, and
	 * only the ones whose faces have actually changed are drawn. */
	overlay_update(server, false);
}

static void desktop_tick(struct desk_server *server)
{
	prof_begin(PROF_DESK);
	desktop_tick_body(server);
	prof_end();
}

#ifdef DESK_HAVE_POST
/* Something arrived, or something was taken out. Redraw whatever of the
 * post box is showing. */
static void post_changed(void *data)
{
	struct desk_server *server = data;
	const struct desk_note *notes;
	int n = desk_post_letters(&notes);
	wlr_log(WLR_INFO, "post: %d letter%s, %d unread%s", n, n == 1 ? "" : "s",
		desk_post_unread(), n ? "" : " (box empty)");
	/* one that has not been here before: heard coming through the slot */
	unsigned newest = 0;
	for (int i = 0; i < n; i++) {
		newest = notes[i].id > newest ? notes[i].id : newest;
	}
	if (server->post_seen_set && newest > server->post_seen && server->conf.letter_sound) {
		desk_sound_play(DESK_SND_LETTER_ARRIVE, 0.55, 1.0, 0.0);
	}
	if (!server->post_seen_set || newest > server->post_seen) {
		server->post_seen = newest;
	}
	server->post_seen_set = true;
	/* With the box open, the letters standing in it are what is in it
	 * now: never turned past the last of them, the hand over whichever is
	 * under it now, and drawn again. */
	if (post_up(server)) {
		const struct desk_note *in = NULL;
		int k = post_count(&in);
		double top = k > 0 ? k - 1 : 0;
		if (server->pb_pos_to > top) {
			server->pb_pos_to = top;
		}
		if (server->pb_pos > top) {
			server->pb_pos = top;
			server->snd_letter = (int)lround(server->pb_pos);
		}
		server->pb_hover = server->pb_t >= 0.9995
			? desk_post_hit(server->desk_w, server->desk_h, k, server->pb_pos,
				server->cursor->x, server->cursor->y)
			: -1;
		paint_post(server);
	}
	desktop_tick(server);
	server_schedule_frames(server);
}
#endif

/* The desk is a different size now. Everything that was built for the old
 * one goes, the room starts baking again for the new one, and until it
 * lands the desk paints flat -- the same as a first run, because that is
 * what it is. */
static void desk_relayout(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	if (w < 8 || h < 8) {
		return;
	}
	wlr_log(WLR_INFO, "desk is %dx%d now; rebuilding", w, h);
	book_kept_forget(server);

	/* the traced moves, which are one strip of frames at one size */
	for (int i = 0; i < DESK_VIEW_COUNT; i++) {
		if (server->film_a[i]) {
			wlr_scene_node_destroy(&server->film_a[i]->node);
			server->film_a[i] = NULL;
		}
		if (server->film_b[i]) {
			wlr_scene_node_destroy(&server->film_b[i]->node);
			server->film_b[i] = NULL;
		}
		server->film_frames[i] = 0;
	}
	/* the traced sheets behind the live overlays: their own size guards
	 * will notice, but drop them here so nothing is drawn at the old size
	 * even once */
	if (server->cc_base) {
		wlr_scene_node_destroy(&server->cc_base->node);
		server->cc_base = NULL;
		server->cc_base_w = server->cc_base_h = 0;
	}
	if (server->pb_base) {
		wlr_scene_node_destroy(&server->pb_base->node);
		server->pb_base = NULL;
		server->pb_base_w = server->pb_base_h = 0;
	}

	/* the letters lying on the desk move with the desk */
	papers_place(server, w, h);

	server->icons_shown = false;
	desk_scene_warm(w, h, true);
	desktop_paint(server);
	paint_shelf(server);
	paint_console(server);
	paint_post(server);
	windows_keep_off_tray(server);
	server_schedule_frames(server);
}

static int resize_cb(void *data)
{
	struct desk_server *server = data;
	if (server->resize_w == server->desk_w && server->resize_h == server->desk_h) {
		return 0;
	}
	server->desk_w = server->resize_w;
	server->desk_h = server->resize_h;
	desk_relayout(server);
	return 0;
}

/* Called for every size the backend offers. Only the one it settles on
 * ever reaches the ray tracer. */
static void desk_resize_to(struct desk_server *server, int w, int h)
{
	if (w < 8 || h < 8) {
		return;
	}
	server->resize_w = w;
	server->resize_h = h;
	if (w == server->desk_w && h == server->desk_h) {
		return;
	}
	if (server->resize_timer) {
		wl_event_source_timer_update(server->resize_timer, 350);
	}
}

/* What the first look at a view would otherwise stop to make: the case
 * with its labels printed, the console's traced sheet, the post box's, and
 * the three films of the camera moves. A game loads its assets behind the
 * title screen; this makes them on the idle ticks after the room is up,
 * one a tick so that no single tick is the long one, and leaves them
 * hidden until they are looked at. Each view's as soon as that view is
 * there: on a machine that traces on its processor the bookcase is
 * minutes ahead of the other two. */
static void views_prewarm(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	if (w != server->warm_w || h != server->warm_h) {
		server->warm_w = w;
		server->warm_h = h;
		server->warm_step = 0;
	}
	if (!server->icons_shown || shelf_up(server) || console_up(server) || post_up(server)) {
		return;
	}
	if (server->warm_step > 7) {
		/* The films are traced after the views (draw.c), and may come long
		 * after the steps below went past them: each is taken up on the
		 * tick after it comes, not on the first move to its view. */
		struct wlr_scene_tree *const layer[DESK_VIEW_COUNT] = {
			[DESK_VIEW_SHELF] = server->layer_shelf,
			[DESK_VIEW_CONSOLE] = server->layer_console,
			[DESK_VIEW_POST] = server->layer_post,
		};
		for (int v = 0; v < DESK_VIEW_COUNT; v++) {
			if (!server->film_a[v]) {
				film_ensure(server, v, layer[v]);
				if (server->film_a[v]) {
					return;   /* one a tick */
				}
			}
		}
		return;
	}
	/* a view at a time, the bookcase first, each waiting for its view to
	 * be there; and each view's film before its sheet, the order a first
	 * look makes them in, which is the order they stack in */
	static const signed char NEEDS[8] = { DESK_VIEW_SHELF, DESK_VIEW_SHELF, DESK_VIEW_SHELF,
		DESK_VIEW_CONSOLE, DESK_VIEW_CONSOLE, DESK_VIEW_POST, DESK_VIEW_POST, -1 };
	int need = NEEDS[server->warm_step < 0 ? 0 : server->warm_step];
	if (need >= 0 && !desk_scene_view_ready(w, h, need)) {
		return;
	}
	switch (server->warm_step) {
	case 0:
		if (!desk_shelf_prewarm(w, h)) {
			return;
		}
		break;
	case 1:
		film_ensure(server, DESK_VIEW_SHELF, server->layer_shelf);
		break;
	case 2: {
		/* and the case itself, as the shelf view will first put it up */
		if (!server->sh_live) {
			server->sh_live = wlr_scene_tree_create(server->layer_shelf);
			for (int k = 0; k < SH_LAYERS; k++) {
				server->sh_layer[k] = wlr_scene_tree_create(server->sh_live);
			}
			wlr_scene_node_set_enabled(&server->sh_live->node, false);
		}
		const struct desk_app_list *l = desk_apps_acquire();
		unsigned gen = l->generation;
		desk_apps_release(l);
		struct sh_args a = { server, -1, -1, -1, 0, 0.0, NULL, time(NULL) };
		uint64_t key = key_add(14695981039346656037ull, &gen, sizeof gen);
		key = key_add(key, &a.taken, sizeof a.taken);
		double full[4] = { 0.0, 0.0, (double)w, (double)h };
		server->sh_w = w;
		server->sh_h = h;
		sh_put(server, SH_CASE, key, false, full, sh_case_draw, &a);
		break;
	}
	case 3:
		film_ensure(server, DESK_VIEW_CONSOLE, server->layer_console);
		break;
	case 4:
		if (!server->cc_base || server->cc_base_w != w || server->cc_base_h != h) {
			struct desk_buf *bb = buf_create(w, h);
			desk_draw_console_base(bb->cr, w, h);
			scene_put(&server->cc_base, server->layer_console, bb, 0, 0, NULL);
			wlr_scene_node_set_enabled(&server->cc_base->node, false);
			server->cc_base_w = w;
			server->cc_base_h = h;
		}
		break;
	case 5:
		film_ensure(server, DESK_VIEW_POST, server->layer_post);
		break;
	case 6:
		if (!server->pb_base || server->pb_base_w != w || server->pb_base_h != h) {
			struct desk_buf *bb = buf_create(w, h);
			desk_draw_post(bb->cr, w, h, NULL, 0, -1, 0.0, 0.0, NULL);
			scene_put(&server->pb_base, server->layer_post, bb, 0, 0, NULL);
			wlr_scene_node_set_enabled(&server->pb_base->node, false);
			server->pb_base_w = w;
			server->pb_base_h = h;
		}
		break;
	default:
		break;
	}
	server->warm_step++;
}

/* ---- the clock ------------------------------------------------------
 *
 * Three keys under its display, left to right POWER, VOLUME and DAY. A
 * key shows its page; pressed again, or left alone a while with the hand
 * gone, the clock goes back to the time. The keys light as a hand comes
 * near, and the one under it more. */

#define CLOCK_IDLE_S 12.0

static void clock_repaint(struct desk_server *server)
{
	struct desk_sprite *sp = &server->ovl[DESK_OVL_CLOCK];
	if (!server->ovl_placed) {
		return;
	}
	time_t now = time(NULL);
	uint64_t key = desk_clock_key(now);
	if (sp->drawn && sp->key == key) {
		return;
	}
	sp->key = key;
	sprite_draw(server, DESK_OVL_CLOCK, now, post_unread());
	overlay_place(server);
}

/* Everything the face shows, handed to the painter, and painted if it
 * comes out different. */
static void clock_publish(struct desk_server *server)
{
	struct desk_clock_state st;
	memset(&st, 0, sizeof st);
	st.mode = server->clk.mode;
	st.wake = server->clk.wake;
	for (int i = 0; i < DESK_CLOCK_KEYS; i++) {
		st.glow[i] = server->clk.glow[i];
	}
	st.pressed = server->clk.pressed;
	struct desk_meters m;
	memset(&m, 0, sizeof m);
	desk_meters_read_volume(&m);
	st.have_volume = m.have_volume;
	st.volume = m.volume > 1.0 ? 1.0 : m.volume;
	st.muted = m.muted;
	st.supply = server->clk.supply;
	st.avg_w = server->clk.n_all > 0 ? server->clk.sum_all / server->clk.n_all : -1.0;
	st.hour_wh = -1.0;
	if (server->clk.hist_n > 0) {
		double sum = 0.0;
		for (int i = 0; i < server->clk.hist_n; i++) {
			sum += server->clk.hist[i];
		}
		/* a second at a time: watt seconds, in watt hours */
		st.hour_wh = sum / 3600.0;
		st.hour_span = server->clk.hist_n / 3600.0;
	}
	st.now = monotonic_sec();
	st.boot = server->boot_on ? server->boot_p : -1.0;
	st.boot_stage = server->boot_stage;
	st.media_hot = server->clk.media_hot;
	st.media_down = server->clk.media_down;
	st.camera = desk_camera_state();
	st.mic = desk_mic_state();
	st.show = server->show;
	st.show_t = server->show ? real_sec() - server->show_t0 : 0.0;
	{
		time_t ot = desk_occasion_now();
		struct tm otm;
		localtime_r(&ot, &otm);
		st.year = otm.tm_year + 1900;
	}
#ifdef DESK_HAVE_POST
	/* the input method is shown in the tray (its own item), not here */
	st.ime[0] = '\0';
	desk_media_read(&st.media, st.now);
#endif
	desk_clock_set(&st);
	clock_repaint(server);
	/* and back when it will next come out different by itself */
	if (server->clk.timer) {
		int ms = desk_clock_next_ms();
		wl_event_source_timer_update(server->clk.timer, ms < 0 ? 0 : ms > 0 ? ms : 1);
	}
}

static int clock_timer_cb(void *data)
{
	clock_publish(data);
	return 0;
}

static void clock_changed_cb(void *data)
{
	clock_publish(data);
}

/* the weather has come in: the door out in it */
static void occasion_changed_cb(void *data)
{
	struct desk_server *server = data;
	if (locked(server)) {
		paint_lock(server);
		server_schedule_frames(server);
	}
}

static void clock_animate(struct desk_server *server)
{
	if (!server->clk.animating) {
		server->clk.animating = true;
		server->clk.anim_at = monotonic_sec();
	}
	server_schedule_frames(server);
}

/* Once a frame while the light is still changing: the keys brighten
 * quickly and fade a little slower, the way a lamp does. */
static void clock_tick_frame(struct desk_server *server)
{
	if (!server->clk.animating) {
		return;
	}
	double now = monotonic_sec();
	double dt = clampf_d(now - server->clk.anim_at, 0.0, 0.05);
	server->clk.anim_at = now;
	bool moving = false;
	double *v = &server->clk.wake, to = server->clk.wake_to;
	double k = gfx_flat() ? 1.0 : 1.0 - exp(-dt / (to > *v ? 0.07 : 0.22));
	*v += (to - *v) * k;
	if (fabs(to - *v) < 0.01) {
		*v = to;
	} else {
		moving = true;
	}
	for (int i = 0; i < DESK_CLOCK_KEYS; i++) {
		double target = server->clk.hot == i ? 1.0 : 0.0;
		double *g = &server->clk.glow[i];
		double kg = gfx_flat() ? 1.0 : 1.0 - exp(-dt / (target > *g ? 0.05 : 0.18));
		*g += (target - *g) * kg;
		if (fabs(target - *g) < 0.01) {
			*g = target;
		} else {
			moving = true;
		}
	}
	clock_publish(server);
	if (moving) {
		server_schedule_frames(server);
	} else {
		server->clk.animating = false;
	}
}

static void clock_set_mode(struct desk_server *server, int mode)
{
	if (server->clk.mode == mode) {
		return;
	}
	server->clk.mode = mode;
	clock_publish(server);
}

static void clock_set_volume(struct desk_server *server, double v)
{
	v = clampf_d(v, 0.0, 1.0);
	if (desk_meters_set_volume(v)) {
		/* the console's knob is the same mixer */
		server->cc_vol = v;
		clock_publish(server);
	}
}

/* Where the pointer is, for the clock: how lit its keys are, and which one
 * is under it. True when it is over the clock. */
static bool clock_track(struct desk_server *server, bool can_touch)
{
	struct desk_clock_pick pk;
	bool have = can_touch && desk_clock_pick(server->desk_w, server->desk_h,
		server->cursor->x, server->cursor->y, server->drawer_t, &pk);
	if (!have) {
		memset(&pk, 0, sizeof pk);
		pk.key = -1;
		pk.button = -1;
	}
	if (pk.near != server->clk.near) {
		server->clk.near = pk.near;
		server->clk.wake_to = pk.near ? 1.0 : 0.0;
		if (!pk.near) {
			server->clk.away_at = real_sec();
		}
		clock_animate(server);
	}
	if (pk.key != server->clk.hot) {
		server->clk.hot = pk.key;
		clock_animate(server);
	}
	int button = pk.button == DESK_MEDIA_RAISE ? -1 : pk.button;
	if (button != server->clk.media_hot) {
		server->clk.media_hot = button;
		clock_publish(server);
	}
	return pk.on;
}

static void clock_press(struct desk_server *server, uint32_t button, bool *taken)
{
	*taken = false;
	struct desk_clock_pick pk;
	if (!desk_clock_pick(server->desk_w, server->desk_h, server->cursor->x, server->cursor->y,
			server->drawer_t, &pk) || !pk.on) {
		return;
	}
	*taken = true;
	if (pk.key >= 0) {
		if (button == BTN_LEFT) {
			server->clk.pressed = pk.key;
			snd_at(server, DESK_SND_KEY_PRESS, server->cursor->x, 1.0);
			clock_publish(server);
		}
		return;
	}
	if (server->clk.mode == DESK_CLOCK_MEDIA && button == BTN_LEFT) {
		if (pk.button >= 0) {
			/* it acts when let go over itself, as a key does */
			snd_at(server, DESK_SND_KEY_PRESS, server->cursor->x, 0.8);
			server->clk.media_down = pk.button;
			clock_publish(server);
		} else if (pk.speaker) {
			if (desk_meters_set_muted(2)) {
				clock_publish(server);
			}
		} else if (pk.bar) {
			server->clk.vol_drag = true;
			clock_set_volume(server, pk.bar_value);
		}
	}
}

/* A button let go: a key held and let go over itself turns its page up,
 * or back to the time if its page is up already. */
static bool clock_release(struct desk_server *server)
{
	if (server->clk.vol_drag) {
		server->clk.vol_drag = false;
		return true;
	}
	if (server->clk.media_down >= 0) {
		int which = server->clk.media_down;
		server->clk.media_down = -1;
		snd_at(server, DESK_SND_KEY_RELEASE, server->cursor->x, 0.8);
		struct desk_clock_pick pk;
		if (desk_clock_pick(server->desk_w, server->desk_h, server->cursor->x,
				server->cursor->y, server->drawer_t, &pk) && pk.button == which) {
#ifdef DESK_HAVE_POST
			desk_media_command(which);
#endif
		}
		clock_publish(server);
		return true;
	}
	if (server->clk.pressed < 0) {
		return false;
	}
	int key = server->clk.pressed;
	server->clk.pressed = -1;
	snd_at(server, DESK_SND_KEY_RELEASE, server->cursor->x, 1.0);
	struct desk_clock_pick pk;
	if (desk_clock_pick(server->desk_w, server->desk_h, server->cursor->x, server->cursor->y,
			server->drawer_t, &pk) && pk.key == key) {
		int page = desk_clock_key_mode(key);
		clock_set_mode(server, server->clk.mode == page ? DESK_CLOCK_TIME : page);
	}
	clock_publish(server);
	return true;
}

static bool clock_drag(struct desk_server *server)
{
	if (!server->clk.vol_drag) {
		return false;
	}
	double v;
	if (desk_clock_bar_value(server->desk_w, server->desk_h, server->cursor->x,
			server->cursor->y, server->drawer_t, &v)) {
		clock_set_volume(server, v);
	}
	return true;
}

/* A wheel over the clock: the volume, on the time and media pages. */
static bool clock_axis(struct desk_server *server, const struct wlr_pointer_axis_event *event)
{
	struct desk_clock_pick pk;
	if (!desk_clock_pick(server->desk_w, server->desk_h, server->cursor->x, server->cursor->y,
			server->drawer_t, &pk) || !pk.on) {
		return false;
	}
	double d = event->delta;
	if (d == 0.0 && event->delta_discrete != 0) {
		d = (double)event->delta_discrete / 120.0 * 15.0;
	}
	if (event->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL
		|| (server->clk.mode != DESK_CLOCK_TIME && server->clk.mode != DESK_CLOCK_MEDIA)) {
		return true;
	}
	server->clk.vol_scroll += d;
	int notches = 0;
	while (server->clk.vol_scroll >= 15.0) {
		notches--;
		server->clk.vol_scroll -= 15.0;
	}
	while (server->clk.vol_scroll <= -15.0) {
		notches++;
		server->clk.vol_scroll += 15.0;
	}
	if (notches) {
		struct desk_meters m;
		memset(&m, 0, sizeof m);
		desk_meters_read_volume(&m);
		if (m.have_volume) {
			clock_set_volume(server, round((m.volume + notches * 0.02) * 50.0) / 50.0);
		}
	}
	return true;
}

/* Once a tick: what the machine is drawing, the input method, the tray
 * and what is playing, and back to the time when nobody has been near it
 * a while. The tick comes ten times a second while the shelf is up; the
 * record of what was drawn is a second at a time all the same. */
static void clock_tick(struct desk_server *server)
{
	struct desk_supply *s = &server->clk.supply;
	if (real_sec() - server->clk.supply_at >= 0.95) {
		server->clk.supply_at = real_sec();
		desk_supply_read(s);
		if (s->draw_w >= 0.0) {
			server->clk.hist[server->clk.hist_at] = (float)s->draw_w;
			server->clk.hist_at = (server->clk.hist_at + 1) % 3600;
			if (server->clk.hist_n < 3600) {
				server->clk.hist_n++;
			}
			server->clk.sum_all += s->draw_w;
			server->clk.n_all++;
		}
	}
#ifdef DESK_HAVE_POST
	desk_ime_tick();
	desk_tray_tick();
	desk_media_tick();
#endif
	if (server->clk.mode != DESK_CLOCK_TIME && !server->clk.near && !server->clk.vol_drag
		&& real_sec() - server->clk.away_at > CLOCK_IDLE_S) {
		server->clk.mode = DESK_CLOCK_TIME;
	}
	desk_occasion_tick();
	show_midnight_check(server);
	clock_publish(server);
}

/* ---- the tray: slips under the Control Panel -------------------------
 *
 * Each thing a program has put in the tray is a slip tucked under the
 * front of the Control Panel, and the one under the pointer slides out a
 * little to meet it. A click is the program's own (Activate: most bring
 * their window up), the middle button its second thing; the right one
 * pulls the slip out, and the card it is stuck on comes out with it, with
 * the program's menu on it. A program that is nothing but a menu has its
 * card come out whichever button it is. */

#ifdef DESK_HAVE_POST

/* the one thing on the card of a program that has no menu of its own */
#define CARD_ACTIVATE INT32_MIN

static double *slip_lift(struct desk_server *server, unsigned id, bool make)
{
	for (int i = 0; i < server->slp.n_lift; i++) {
		if (server->slp.lift_id[i] == id) {
			return &server->slp.lift[i];
		}
	}
	if (!make) {
		return NULL;
	}
	/* a free place, or the place of one that is back where it lies */
	int at = server->slp.n_lift < DESK_SLIPS_MAX ? server->slp.n_lift++ : -1;
	for (int i = 0; at < 0 && i < server->slp.n_lift; i++) {
		if (server->slp.lift[i] <= 0.0) {
			at = i;
		}
	}
	if (at < 0) {
		return NULL;
	}
	server->slp.lift_id[at] = id;
	server->slp.lift[at] = 0.0;
	return &server->slp.lift[at];
}

/* How a slip happens to lie and what paper it is: its Id's, so it lies the
 * same way every time the program puts it there. */
static unsigned slip_seed(const struct desk_tray_item *it)
{
	unsigned h = 2166136261u;
	for (const char *p = it->name_id; *p; p++) {
		h = (h ^ (unsigned char)*p) * 16777619u;
	}
	return h;
}

static int slip_index(unsigned id)
{
	const struct desk_tray_item *items = NULL;
	int n = desk_tray_items(&items);
	for (int i = 0; i < n && i < DESK_SLIPS_MAX; i++) {
		if (items[i].id == id) {
			return i;
		}
	}
	return -1;
}

static void card_paint(struct desk_server *server);

/* The slips as the tray has them, handed to the painter, and painted if
 * they come out different. */
/* The first line of s, as much of it as fits, cut between characters: a
 * slip has room for a line, and a character cut in half is a string cairo
 * refuses -- and refuses everything drawn after it on the same card. */
static void utf8_line(char *out, size_t n, const char *s)
{
	size_t i = 0;
	if (n == 0) {
		return;
	}
	while (s && s[i] && s[i] != '\n' && s[i] != '\r' && i + 1 < n) {
		i++;
	}
	if (s && s[i] && s[i] != '\n' && s[i] != '\r') {
		while (i > 0 && ((unsigned char)s[i] & 0xc0) == 0x80) {
			i--;
		}
	}
	if (s) {
		memcpy(out, s, i);
	}
	out[i] = '\0';
	while (i > 0 && out[i - 1] == ' ') {
		out[--i] = '\0';
	}
}

static void slips_publish(struct desk_server *server)
{
	const struct desk_tray_item *items = NULL;
	int n = desk_tray_items(&items);
	struct desk_slips_state st;
	memset(&st, 0, sizeof st);
	st.hot = st.pulled = -1;
	for (int i = 0; i < n && st.n < DESK_SLIPS_MAX; i++) {
		struct desk_slip *sl = &st.slips[st.n];
		sl->icon = items[i].icon;
		utf8_line(sl->title, sizeof sl->title, items[i].title);
		sl->seed = slip_seed(&items[i]);
		sl->attention = items[i].attention;
		sl->passive = items[i].passive;
		const double *lift = slip_lift(server, items[i].id, false);
		sl->lift = lift ? *lift : 0.0;
		if (items[i].id == server->slp.hot) {
			st.hot = st.n;
		}
		if (items[i].id == server->slp.card) {
			st.pulled = st.n;
		}
		st.n++;
	}
	/* never slid further than there are slips to show */
	int fit = desk_slips_fit(server->desk_w, server->desk_h);
	double most = fit > 0 && st.n > fit ? (double)(st.n - fit) : 0.0;
	server->slp.scroll_to = clampf_d(server->slp.scroll_to, 0.0, most);
	server->slp.scroll = clampf_d(server->slp.scroll, 0.0, most);
	st.scroll = server->slp.scroll;
	/* the tray's band is deeper with slips in it than without */
	static int had = -1;
	bool changed_band = had >= 0 && (had > 0) != (st.n > 0);
	had = st.n;
	desk_slips_set(&st);
	if (changed_band) {
		windows_keep_off_tray(server);
	}
	struct desk_sprite *sp = &server->ovl[DESK_OVL_SLIPS];
	if (server->ovl_placed) {
		uint64_t key = desk_slips_key();
		if (!sp->drawn || sp->key != key) {
			sp->key = key;
			sprite_draw(server, DESK_OVL_SLIPS, time(NULL), post_unread());
			overlay_place(server);
		}
	}
	card_paint(server);
}

/* What is on the card now: the entries of whichever submenu it has been
 * taken down to -- or, for a program with no menu of its own, the one
 * thing any of them can be asked to do. */
static bool card_build(struct desk_server *server, struct desk_card *c)
{
	memset(c, 0, sizeof *c);
	c->hot = -1;
	c->slip = slip_index(server->slp.card);
	if (c->slip < 0) {
		return false;
	}
	const struct desk_tray_item *items = NULL;
	desk_tray_items(&items);
	utf8_line(c->title, sizeof c->title, items[c->slip].title);
	int depth = server->slp.depth;
	if (depth > 0) {
		c->back = true;
		utf8_line(c->title, sizeof c->title, server->slp.path_label[depth - 1]);
	}
	const struct desk_tray_menu_entry *e = NULL;
	int state = DESK_TRAY_MENU_NONE;
	int n = desk_tray_menu(depth > 0 ? server->slp.path[depth - 1] : 0, &e, &state);
	if (state == DESK_TRAY_MENU_LOADING) {
		c->loading = true;
	} else if (state == DESK_TRAY_MENU_NONE) {
		c->n = 1;
		c->e[0].id = CARD_ACTIVATE;
		snprintf(c->e[0].label, sizeof c->e[0].label, "%s", TR("Open"));
		c->e[0].enabled = true;
		c->e[0].state = -1;
	} else {
		for (int i = 0; i < n && c->n < DESK_CARD_MAX; i++) {
			struct desk_card_entry *ce = &c->e[c->n++];
			ce->id = e[i].id;
			memcpy(ce->label, e[i].label, sizeof ce->label);
			ce->enabled = e[i].enabled;
			ce->separator = e[i].separator;
			ce->submenu = e[i].submenu;
			ce->toggle = e[i].toggle;
			ce->state = e[i].state;
			ce->icon = e[i].icon;
		}
	}
	c->hot = server->slp.hot_entry < c->n ? server->slp.hot_entry : -1;
	c->top = server->slp.top < c->n ? server->slp.top : 0;
	return true;
}

static uint64_t card_key(const struct desk_server *server, const struct desk_card *c)
{
	uint64_t h = key_add(14695981039346656037ull, &c->slip, sizeof c->slip);
	int top[8] = { c->n, c->hot, c->top, c->back, c->loading,
		(int)lround(server->slp.pull * 2000.0), server->desk_w, server->desk_h };
	h = key_add(h, top, sizeof top);
	h = key_add(h, c->title, strlen(c->title));
	for (int i = 0; i < c->n; i++) {
		const struct desk_card_entry *e = &c->e[i];
		int f[6] = { e->id, e->enabled, e->separator, e->submenu, e->toggle, e->state };
		h = key_add(h, f, sizeof f);
		h = key_add(h, e->label, strlen(e->label));
		uintptr_t pi = (uintptr_t)e->icon;
		h = key_add(h, &pi, sizeof pi);
	}
	uint64_t slips = desk_slips_key();
	h = key_add(h, &slips, sizeof slips);
	double t = server->drawer_t;
	return key_add(h, &t, sizeof t);
}

/* The card as it is now, where it is now. */
static void card_paint(struct desk_server *server)
{
	if (!server->slp.card) {
		if (server->slp.node) {
			wlr_scene_node_set_enabled(&server->slp.node->node, false);
		}
		return;
	}
	struct desk_card c;
	struct desk_card_place pl;
	if (!card_build(server, &c) || !desk_card_place(server->desk_w, server->desk_h,
			server->drawer_t, &c, &pl)) {
		card_gone(server);
		return;
	}
	uint64_t key = card_key(server, &c);
	if (server->slp.node && server->slp.node->node.enabled && server->slp.key == key) {
		return;
	}
	server->slp.key = key;
	double box[4];
	desk_card_box(&pl, server->slp.pull, box);
	struct desk_buf *buf = buf_create((int)box[2], (int)box[3]);
	cairo_translate(buf->cr, -box[0], -box[1]);
	desk_draw_card(buf->cr, server->desk_w, server->desk_h, server->drawer_t, &c, &pl,
		server->slp.pull);
	/* over the sheets on the desk: it has been pulled out to be read */
	scene_put(&server->slp.node, server->layer_parked, buf, (int)box[0], (int)box[1], NULL);
	server->slp.node->point_accepts_input = shadow_accepts;
	wlr_scene_node_raise_to_top(&server->slp.node->node);
	wlr_scene_node_set_enabled(&server->slp.node->node, true);
}

static void card_pull(struct desk_server *server, double to)
{
	desk_sound_play(DESK_SND_CARD_SLIDE, -0.45, to > 0.5 ? 1.0 : 0.7, to > 0.5 ? 0.0 : 1.2);
	server->slp.pull_from = server->slp.pull;
	server->slp.pull_to = to;
	server->slp.pull_start = monotonic_sec();
	server->slp.pulling = true;
	server_schedule_frames(server);
}

static void card_open(struct desk_server *server, unsigned id)
{
	if (server->slp.card == id) {
		if (server->slp.pull_to < 0.5) {
			card_pull(server, 1.0);
		}
		return;
	}
	card_gone(server);
	server->slp.card = id;
	server->slp.depth = 0;
	server->slp.hot_entry = -1;
	server->slp.top = 0;
	server->slp.card_scroll = 0.0;
	server->slp.pull = 0.0;
	desk_tray_menu_open(id);
	card_pull(server, 1.0);
	slips_publish(server);
}

/* Slid back in under the machine; what the program is told is told when
 * it is out of sight, so what is on it does not change on the way. */
static void card_put_back(struct desk_server *server)
{
	if (server->slp.card && server->slp.pull_to > 0.5) {
		card_pull(server, 0.0);
	}
}

/* Gone at once: the view is changing, or the program has gone. */
static void card_gone(struct desk_server *server)
{
	if (!server->slp.card) {
		return;
	}
	desk_tray_menu_close();
	server->slp.card = 0;
	server->slp.pull = server->slp.pull_to = 0.0;
	server->slp.pulling = false;
	if (server->slp.node) {
		wlr_scene_node_set_enabled(&server->slp.node->node, false);
	}
	slips_publish(server);
}

static bool card_out(const struct desk_server *server)
{
	return server->slp.card && server->slp.pull_to > 0.5;
}

/* Where the card is under the pointer: an entry, -2 its head, -1 the card
 * but nothing on it, -3 not the card. */
static int card_under(struct desk_server *server, struct desk_card *c)
{
	struct desk_card_place pl;
	if (!card_build(server, c) || !desk_card_place(server->desk_w, server->desk_h,
			server->drawer_t, c, &pl)) {
		return -3;
	}
	return desk_card_hit(c, &pl, server->cursor->x, server->cursor->y);
}

static void card_hot(struct desk_server *server, int hot)
{
	if (hot != server->slp.hot_entry) {
		server->slp.hot_entry = hot;
		card_paint(server);
	}
}

/* An entry chosen: a submenu is gone down into, anything else is done and
 * the card put back. */
static void card_choose(struct desk_server *server, const struct desk_card *c, int i)
{
	if (i == -2) {
		if (server->slp.depth > 0) {
			server->slp.depth--;
			server->slp.hot_entry = -1;
			server->slp.top = 0;
			card_paint(server);
		} else {
			/* the program's own name: it comes, as it does for a click */
			desk_tray_click(server->slp.card, 0, (int)server->cursor->x,
				(int)server->cursor->y);
			card_put_back(server);
		}
		return;
	}
	if (i < 0 || i >= c->n || !c->e[i].enabled || c->e[i].separator) {
		return;
	}
	const struct desk_card_entry *e = &c->e[i];
	if (e->id == CARD_ACTIVATE) {
		desk_tray_click(server->slp.card, 0, (int)server->cursor->x, (int)server->cursor->y);
		card_put_back(server);
		return;
	}
	if (e->submenu && server->slp.depth < (int)(sizeof server->slp.path / sizeof server->slp.path[0])) {
		int d = server->slp.depth++;
		server->slp.path[d] = e->id;
		snprintf(server->slp.path_label[d], sizeof server->slp.path_label[d], "%s", e->label);
		server->slp.hot_entry = -1;
		server->slp.top = 0;
		desk_tray_menu_show(e->id);
		card_paint(server);
		return;
	}
	desk_tray_menu_click(e->id);
	card_put_back(server);
}

/* Once a frame while something moves: the slips sliding out to the
 * pointer and back, and a card on its way out or in. */
static void slips_tick_frame(struct desk_server *server)
{
	double now = monotonic_sec();
	bool moved = false, more = false;
	if (server->slp.animating) {
		double dt = clampf_d(now - server->slp.anim_at, 0.0, 0.05);
		server->slp.anim_at = now;
		for (int i = 0; i < server->slp.n_lift; i++) {
			double to = server->slp.lift_id[i] == server->slp.hot ? 1.0 : 0.0;
			double *v = &server->slp.lift[i];
			double k = gfx_flat() ? 1.0 : 1.0 - exp(-dt / (to > *v ? 0.055 : 0.11));
			*v += (to - *v) * k;
			if (fabs(to - *v) < 0.01) {
				*v = to;
			} else {
				more = true;
			}
		}
		if (fabs(server->slp.scroll_to - server->slp.scroll) > 0.0) {
			double k = gfx_flat() ? 1.0 : 1.0 - exp(-dt / 0.07);
			server->slp.scroll += (server->slp.scroll_to - server->slp.scroll) * k;
			if (fabs(server->slp.scroll_to - server->slp.scroll) < 0.004) {
				server->slp.scroll = server->slp.scroll_to;
			} else {
				more = true;
			}
		}
		moved = true;
		server->slp.animating = more;
	}
	if (server->slp.pulling) {
		bool out = server->slp.pull_to > server->slp.pull_from;
		double u = clampf_d((now - server->slp.pull_start) / (out ? 0.34 : 0.24), 0.0, 1.0);
		server->slp.pull = server->slp.pull_from
			+ (server->slp.pull_to - server->slp.pull_from) * drawer_ease(u);
		if (u >= 1.0) {
			server->slp.pull = server->slp.pull_to;
			server->slp.pulling = false;
			if (server->slp.pull_to <= 0.0) {
				card_gone(server);
			}
		} else {
			more = true;
		}
		moved = true;
	}
	if (moved) {
		slips_publish(server);
	}
	if (more) {
		server_schedule_frames(server);
	}
}

/* Where the pointer is, for the slips. True when it is on one. */
static bool slips_track(struct desk_server *server, bool can_touch)
{
	unsigned hot = 0;
	int i = can_touch ? desk_slip_at(server->desk_w, server->desk_h, server->cursor->x,
		server->cursor->y, server->drawer_t) : -1;
	if (i >= 0) {
		const struct desk_tray_item *items = NULL;
		if (i < desk_tray_items(&items)) {
			hot = items[i].id;
		}
	}
	if (hot != server->slp.hot) {
		server->slp.hot = hot;
		if (hot) {
			slip_lift(server, hot, true);
		}
		if (!server->slp.animating) {
			server->slp.animating = true;
			server->slp.anim_at = monotonic_sec();
		}
		server_schedule_frames(server);
	}
	return hot != 0;
}

static void slips_press(struct desk_server *server, uint32_t button, bool *taken)
{
	*taken = false;
	int i = desk_slip_at(server->desk_w, server->desk_h, server->cursor->x, server->cursor->y,
		server->drawer_t);
	const struct desk_tray_item *items = NULL;
	if (i < 0 || i >= desk_tray_items(&items)) {
		return;
	}
	*taken = true;
	const struct desk_tray_item *it = &items[i];
	if (button == BTN_RIGHT || (button == BTN_LEFT && it->is_menu)) {
		card_open(server, it->id);
	} else if (button == BTN_LEFT || button == BTN_MIDDLE) {
		desk_tray_click(it->id, button == BTN_MIDDLE ? 1 : 0, (int)server->cursor->x,
			(int)server->cursor->y);
	}
}

/* A button pressed with the card out: it is the card's, whatever it is
 * over. Off the card it goes back in -- unless it is another slip's card
 * being asked for. */
static bool card_press(struct desk_server *server, uint32_t button)
{
	if (!card_out(server)) {
		return false;
	}
	struct desk_card c;
	int hit = card_under(server, &c);
	if (hit == -3) {
		bool slip;
		card_put_back(server);
		slips_press(server, button, &slip);
		return true;
	}
	if (button == BTN_LEFT) {
		card_choose(server, &c, hit);
	}
	return true;
}

/* The pointer over the card: the entry under it. True when it is the
 * card's, and nothing under it is to hear of it. */
static bool card_motion(struct desk_server *server)
{
	if (!card_out(server)) {
		return false;
	}
	struct desk_card c;
	int hit = card_under(server, &c);
	card_hot(server, hit >= 0 || hit == -2 ? hit : -1);
	return hit != -3;
}

static bool card_axis(struct desk_server *server, const struct wlr_pointer_axis_event *event)
{
	if (!card_out(server)) {
		return false;
	}
	struct desk_card c;
	struct desk_card_place pl;
	if (card_under(server, &c) == -3) {
		return false;
	}
	if (event->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL
		|| !desk_card_place(server->desk_w, server->desk_h, server->drawer_t, &c, &pl)) {
		return true;
	}
	/* by the notch, as everything else the wheel turns: a trackpad's flick
	 * comes as dozens of small events */
	double d = event->delta;
	if (d == 0.0 && event->delta_discrete != 0) {
		d = (double)event->delta_discrete / 120.0 * DESK_NOTCH;
	}
	server->slp.card_scroll += d;
	int step = 0;
	while (server->slp.card_scroll >= DESK_NOTCH) {
		step++;
		server->slp.card_scroll -= DESK_NOTCH;
	}
	while (server->slp.card_scroll <= -DESK_NOTCH) {
		step--;
		server->slp.card_scroll += DESK_NOTCH;
	}
	if (!step) {
		return true;
	}
	int top = server->slp.top + step;
	if (top > c.n - pl.rows) {
		top = c.n - pl.rows;
	}
	if (top < 0) {
		top = 0;
	}
	if (top != server->slp.top) {
		server->slp.top = top;
		card_paint(server);
	}
	return true;
}

static bool slips_axis(struct desk_server *server, const struct wlr_pointer_axis_event *event)
{
	int i = desk_slip_at(server->desk_w, server->desk_h, server->cursor->x, server->cursor->y,
		server->drawer_t);
	const struct desk_tray_item *items = NULL;
	int n = desk_tray_items(&items);
	if (i < 0 || i >= n) {
		return false;
	}
	int steps = event->delta_discrete ? event->delta_discrete
		: (int)lround(event->delta / 15.0 * 120.0);
	/* More slips than there is room for, and no card out: the wheel slides
	 * the row along, a slip a notch. Only then -- with all of them in
	 * sight, it is the program's, as it always was. */
	int fit = desk_slips_fit(server->desk_w, server->desk_h);
	if (n > DESK_SLIPS_MAX) {
		n = DESK_SLIPS_MAX;
	}
	if (fit > 0 && n > fit && !card_out(server)) {
		server->slp.wheel += steps;
		int notches = server->slp.wheel / 120;
		server->slp.wheel -= notches * 120;
		if (notches) {
			double to = clampf_d(round(server->slp.scroll_to) + notches, 0.0, (double)(n - fit));
			if (to != server->slp.scroll_to) {
				server->slp.scroll_to = to;
				if (!server->slp.animating) {
					server->slp.anim_at = monotonic_sec();
					server->slp.animating = true;
				}
				/* the one that was under the pointer goes with the row */
				server->slp.hot = 0;
				server_schedule_frames(server);
			}
		}
		return true;
	}
	if (steps) {
		desk_tray_scroll(items[i].id, steps,
			event->orientation == WL_POINTER_AXIS_HORIZONTAL_SCROLL);
	}
	return true;
}

/* The keys, with the card out: up and down it, into a submenu and back
 * out, choose, and put it back. */
static bool card_keys(struct desk_server *server, xkb_keysym_t sym)
{
	if (!card_out(server)) {
		return false;
	}
	struct desk_card c;
	struct desk_card_place pl;
	if (!card_build(server, &c) || !desk_card_place(server->desk_w, server->desk_h,
			server->drawer_t, &c, &pl)) {
		return false;
	}
	switch (sym) {
	case XKB_KEY_Escape:
		card_put_back(server);
		return true;
	case XKB_KEY_Left:
	case XKB_KEY_BackSpace:
		if (server->slp.depth > 0) {
			card_choose(server, &c, -2);
		}
		return true;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
	case XKB_KEY_space:
	case XKB_KEY_Right:
		if (c.hot >= 0 && (sym != XKB_KEY_Right || c.e[c.hot].submenu)) {
			card_choose(server, &c, c.hot);
		}
		return true;
	case XKB_KEY_Up:
	case XKB_KEY_Down: {
		int step = sym == XKB_KEY_Down ? 1 : -1;
		int i = c.hot;
		for (int n = 0; n < c.n; n++) {
			i = i < 0 ? (step > 0 ? 0 : c.n - 1) : (i + step + c.n) % c.n;
			if (!c.e[i].separator && c.e[i].enabled) {
				break;
			}
		}
		if (i < 0 || i >= c.n) {
			return true;
		}
		/* scrolled so it is in sight */
		if (i < server->slp.top) {
			server->slp.top = i;
		} else if (i >= server->slp.top + pl.rows) {
			server->slp.top = i - pl.rows + 1;
		}
		server->slp.hot_entry = -1;
		card_hot(server, i);
		return true;
	}
	default:
		return false;
	}
}

static void slips_bus_changed(void *data)
{
	struct desk_server *server = data;
	clock_publish(server);
	slips_publish(server);
}

static void media_changed(void *data)
{
	clock_publish(data);
}

#else

static void slips_tick_frame(struct desk_server *server)
{
	(void)server;
}

static bool slips_track(struct desk_server *server, bool can_touch)
{
	(void)server;
	(void)can_touch;
	return false;
}

static void slips_press(struct desk_server *server, uint32_t button, bool *taken)
{
	(void)server;
	(void)button;
	*taken = false;
}

static bool card_press(struct desk_server *server, uint32_t button)
{
	(void)server;
	(void)button;
	return false;
}

static bool card_motion(struct desk_server *server)
{
	(void)server;
	return false;
}

static bool card_axis(struct desk_server *server, const struct wlr_pointer_axis_event *event)
{
	(void)server;
	(void)event;
	return false;
}

static bool slips_axis(struct desk_server *server, const struct wlr_pointer_axis_event *event)
{
	(void)server;
	(void)event;
	return false;
}

static bool card_keys(struct desk_server *server, xkb_keysym_t sym)
{
	(void)server;
	(void)sym;
	return false;
}

static void card_gone(struct desk_server *server)
{
	(void)server;
}

#endif

/* ---- drawn on the CPU --------------------------------------------------
 *
 * A virtual machine without 3D, a GPU without its driver: the screen is put
 * together by the processor -- pixman, or GL that is a rasterizer running on
 * it (llvmpipe and the like) -- and everything the desk draws is drawn by the
 * processor too. Then it draws in 2D Fallback unless the user has chosen a
 * level of their own (gfx_wanted), says so in a letter once in a while, and
 * tells Shell Settings, which says it beside Graphics. The way things are
 * drawn is the same either way: 2D is the desk with its effects off, not a
 * second desk drawn some other way. DESK_ON_CPU=1 (or 0) says outright. */

static void render_check(struct desk_server *server)
{
	char name[128] = "";
	bool cpu = false;
	if (wlr_renderer_is_pixman(server->renderer)) {
		cpu = true;
		snprintf(name, sizeof name, "pixman");
	}
#ifdef DESK_HAVE_GPU
	else if (wlr_renderer_is_gles2(server->renderer)) {
		struct wlr_egl *egl = wlr_gles2_renderer_get_egl(server->renderer);
		if (egl && desk_gl_renderer_of(wlr_egl_get_display(egl), wlr_egl_get_context(egl),
				name, sizeof name)) {
			cpu = desk_gl_name_software(name);
		}
	}
#endif
	const char *say = getenv("DESK_ON_CPU");
	if (say && *say) {
		cpu = strcmp(say, "0") != 0;
	}
	server->on_cpu = cpu;
	snprintf(server->render_name, sizeof server->render_name, "%s", name);
	if (cpu) {
		wlr_log(WLR_INFO, "the screen is drawn on the CPU (%s): the desk draws in %s",
			name[0] ? name : "?", gfx_wanted(server) == DESK_GFX_2D
			? "2D Fallback" : "the level chosen for it");
	} else if (name[0]) {
		wlr_log(WLR_INFO, "the screen is drawn by %s", name);
	}
}

/* For Shell Settings: $XDG_RUNTIME_DIR/<the Wayland display>.render, read
 * by settings.c render_read. */
static char g_render_file[512];

static void render_publish(struct desk_server *server, const char *socket)
{
	const char *run = getenv("XDG_RUNTIME_DIR");
	if (!run || !*run || !socket || !*socket || strchr(socket, '/')
		|| (size_t)snprintf(g_render_file, sizeof g_render_file, "%s/%s.render", run,
			socket) >= sizeof g_render_file) {
		g_render_file[0] = '\0';
		return;
	}
	char tmp[600];
	snprintf(tmp, sizeof tmp, "%s.new", g_render_file);
	FILE *f = fopen(tmp, "we");
	if (!f) {
		g_render_file[0] = '\0';
		return;
	}
	fprintf(f, "# how the desk's screen is drawn, for Shell Settings\n"
		"on_cpu = %s\nrenderer = %s\n", server->on_cpu ? "yes" : "no", server->render_name);
	bool ok = fclose(f) == 0;
	if (!ok || rename(tmp, g_render_file) != 0) {
		unlink(tmp);
		g_render_file[0] = '\0';
	}
}

static void render_unpublish(void)
{
	if (g_render_file[0]) {
		unlink(g_render_file);
		g_render_file[0] = '\0';
	}
}

/* Shell Settings, from its desktop file, or by name, or from beside this. */
static void settings_open(void)
{
	const struct desk_app_list *l = desk_apps_acquire();
	const struct desk_app *app = desk_apps_find(l, "desk-settings.desktop");
	if (app) {
		desk_app_launch(app);
	}
	desk_apps_release(l);
	if (app) {
		return;
	}
	if (on_path("desk-settings")) {
		spawn("desk-settings");
		return;
	}
	char exe[512];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
	exe[n > 0 ? n : 0] = '\0';
	char *slash = strrchr(exe, '/');
	if (slash && (size_t)(slash - exe) + sizeof "/desk-settings" < sizeof exe) {
		memcpy(slash, "/desk-settings", sizeof "/desk-settings");
		if (access(exe, X_OK) == 0) {
			char *const argv[] = { exe, NULL };
			spawn_argv(argv);
		}
	}
}

/* A letter to say the screen is drawn on the CPU: at the end of a login,
 * when the lamp is up and the desk is there to read it on -- and not every
 * login: once, again a week on if it still is, and again if what draws it
 * has changed ($XDG_STATE_HOME/desk/cpu-letter). */
static void cpu_letter(struct desk_server *server)
{
	if (!server->on_cpu) {
		return;
	}
	char path[512];
	const char *st = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
	if (st && *st == '/') {
		snprintf(path, sizeof path, "%s/desk/cpu-letter", st);
	} else if (home && *home) {
		snprintf(path, sizeof path, "%s/.local/state/desk/cpu-letter", home);
	} else {
		path[0] = '\0';
	}
	time_t now = time(NULL);
	if (path[0]) {
		FILE *f = fopen(path, "re");
		if (f) {
			long long when = 0;
			char was[128] = "";
			if (fscanf(f, "%lld %127[^\n]", &when, was) >= 1 && now - (time_t)when < 7 * 86400
				&& now >= (time_t)when && !strcmp(was, server->render_name)) {
				fclose(f);
				return;
			}
			fclose(f);
		}
	}
#ifdef DESK_HAVE_POST
	struct desk_note note = { 0 };
	snprintf(note.summary, sizeof note.summary, "%s", TR("Running on the CPU"));
	snprintf(note.body, sizeof note.body, "%s", desk_gfx_level() == DESK_GFX_2D
		? TR("The screen is being drawn by the processor, not a graphics card, so the desk "
			"draws in 2D Fallback. Check the graphics drivers -- or, in a virtual "
			"machine, its settings (3D acceleration).")
		: TR("The screen is being drawn by the processor, not a graphics card. Check the "
			"graphics drivers -- or, in a virtual machine, its settings (3D "
			"acceleration)."));
	snprintf(note.desktop, sizeof note.desktop, "desk-settings");
	note.n_actions = 1;
	snprintf(note.action_key[0], sizeof note.action_key[0], "desk-settings");
	snprintf(note.action_label[0], sizeof note.action_label[0], "%s", TR("Shell Settings"));
	desk_post_own(&note);
	wlr_log(WLR_INFO, "a letter: the screen is drawn on the CPU");
#endif
	if (path[0]) {
		conf_dirs_for(path);
		FILE *f = fopen(path, "we");
		if (f) {
			fprintf(f, "%lld %s\n", (long long)now, server->render_name);
			fclose(f);
		}
	}
}

/* What a letter of the desk's own offers to do (desk_post_own), done here;
 * and you turn back to the desk, where what it opened comes. */
static void own_letter_action(struct desk_server *server, unsigned id, const char *key)
{
	if (!strcmp(key, "desk-settings")) {
		settings_open();
	}
#ifdef DESK_HAVE_POST
	desk_post_dismiss(id);
#else
	(void)id;
#endif
	if (post_up(server)) {
		post_toggle(server);
	}
}

/* ---- the lamp coming up at login --------------------------------------
 *
 * The room starts dark, the way a room is before the light is put on. The
 * clock on the back of the desk, which lights itself, counts in red how
 * far the room has been made -- the desk's two sheets, then the bookcase,
 * the console and the post box, then the pictures made ahead of the first
 * look at each -- and the lamp comes up with the desk: the pool under it
 * first and warm, the corners last, over boot_light_s(). It does not wait
 * for the rest: they go on being made with the light on, the clock still
 * counting them, and at the end it says READY for a moment and goes back
 * to being a clock. Nothing waits on any of it: the desk answers the whole
 * time.
 *
 * The lamp used to take 3.6 s however ready the room was, and on a machine
 * slow to make the views stay dark up to twelve more after the desk was
 * there: every login waited on it, the room on the disk or not. */

#define BOOT_LIGHT_S 1.0     /* the lamp comes up over this, once the desk is there */
#define BOOT_RATE 1.6        /* and the count goes no faster than this a second */
#define BOOT_HOLD_S 0.8      /* READY, before the clock is a clock again */
#define BOOT_WARM_S 0.12     /* the pictures made ahead, one this often */

/* At Low, half as long: a full-screen change every frame is what an old
 * machine can least afford */
static double boot_light_s(void)
{
	return desk_gfx_level() <= DESK_GFX_LOW ? BOOT_LIGHT_S * 0.5 : BOOT_LIGHT_S;
}

/* The room has finished baking, or been read back: put it up in place of
 * the holding gradient the first frames were drawn with -- and now the tray
 * is where it really is, not where it was guessed to be. */
static void room_landed(struct desk_server *server)
{
	if (!server->icons_shown) {
		wlr_log(WLR_INFO, "the desk is up (%.1f s)", real_sec() - g_start_real);
		/* what the first window's paper is cut from, made meanwhile */
		desk_warm_tiles();
	}
	server->icons_shown = true;
	desktop_paint(server);
	windows_keep_off_tray(server);
	server_schedule_frames(server);
}

/* The letter left in the middle of the desk while its room is made for the
 * first time (desk_scene_first_run): over the dark, where the clock counts,
 * until the lamp is up -- then on the desk, under the windows -- and gone
 * when the room is all there. Not something to click: the desk under it
 * answers as if it were not there. Drawn once, at the screen's own
 * resolution, not the overlay's: at 2D the overlay is the room's 640 wide,
 * and a letter that small is hard to read. */
static void first_letter_tick(struct desk_server *server, double light)
{
	struct desk_sprite *sp = &server->first_letter;
	int w = server->desk_w, h = server->desk_h;
	bool want = server->boot_on && desk_scene_first_run() && w >= 64 && h >= 64;
	if (!want) {
		if (server->first_letter_up && sp->node) {
			wlr_scene_node_set_enabled(&sp->node->node, false);
			wlr_log(WLR_INFO, "the first start's letter is put away");
		}
		server->first_letter_up = false;
		return;
	}
	double box[4];
	uint64_t key = ((uint64_t)w << 32) ^ (uint64_t)h ^ ((uint64_t)desk_lang_get() << 60);
	if ((!sp->drawn || sp->key != key) && desk_first_letter_box(w, h, box)) {
		sp->key = key;
		int bx = (int)box[0], by = (int)box[1], bw = (int)box[2], bh = (int)box[3];
		struct desk_buf *b = buf_create(bw, bh);
		cairo_save(b->cr);
		cairo_translate(b->cr, -bx, -by);
		desk_draw_first_letter(b->cr, w, h);
		cairo_restore(b->cr);
		struct wlr_scene_tree *at = sp->node ? sp->node->node.parent : server->layer_top;
		scene_put(&sp->node, at, b, bx, by, NULL);
		wlr_scene_buffer_set_dest_size(sp->node, bw, bh);
		sp->node->point_accepts_input = shadow_accepts;
		sp->drawn = true;
		if (!server->first_letter_up) {
			wlr_log(WLR_INFO, "a first start: a letter in the middle of the desk says so");
		}
	}
	if (!sp->node || !sp->drawn) {
		return;
	}
	server->first_letter_up = true;
	/* over the dark while there is dark; on the desk once the lamp is up */
	struct wlr_scene_tree *in = light < 0.999 ? server->layer_top : server->layer_desk;
	if (sp->node->node.parent != in) {
		wlr_scene_node_reparent(&sp->node->node, in);
	}
	wlr_scene_node_set_enabled(&sp->node->node, true);
	wlr_scene_node_raise_to_top(&sp->node->node);
}

static void boot_end(struct desk_server *server)
{
	server->boot_on = false;
	first_letter_tick(server, 1.0);
	if (server->boot_night) {
		wlr_scene_node_set_enabled(&server->boot_night->node, false);
	}
	if (server->boot_glow.node) {
		wlr_scene_node_set_enabled(&server->boot_glow.node->node, false);
	}
	clock_publish(server);
	server_schedule_frames(server);
	wlr_log(WLR_INFO, "the lamp is up (%.1f s, %.1f s since the start)",
		real_sec() - server->boot_real0, real_sec() - g_start_real);
	/* a day kept: and now the clock says so */
	show_for_today(server);
	/* and on the CPU, a letter to say so */
	cpu_letter(server);
}

struct boot_glow_args {
	int w, h;
};

static void boot_glow_draw(cairo_t *cr, int w, int h, void *data)
{
	(void)data;
	desk_draw_clock_glow(cr, w, h);
}

/* The dark over everything, as the lamp is (L: 1 for none of it), and
 * what of the clock shines through it; face changes whenever the clock
 * would come out different. */
static void night_paint(struct desk_server *server, double L, uint64_t face)
{
	int w = server->desk_w, h = server->desk_h;
	bool dark = L < 0.999;
	side_desks_light(server, dark ? L : 1.0);
	if (dark) {
		/* all gradient: a quarter of the screen, stretched */
		int nw = w / 4 > 16 ? w / 4 : 16, nh = h / 4 > 16 ? h / 4 : 16;
		int q = (int)lround(L * 1000.0);
		if (!server->boot_night || q != server->boot_night_q || nw != server->boot_night_w
			|| nh != server->boot_night_h) {
			struct desk_buf *buf = buf_create(nw, nh);
			cairo_save(buf->cr);
			cairo_scale(buf->cr, (double)nw / w, (double)nh / h);
			desk_draw_night(buf->cr, w, h, L);
			cairo_restore(buf->cr);
			scene_put(&server->boot_night, server->layer_top, buf, 0, 0, NULL);
			if (server->boot_night) {
				/* it is the dark in the room, not something you can click */
				server->boot_night->point_accepts_input = shadow_accepts;
			}
			server->boot_night_q = q;
			server->boot_night_w = nw;
			server->boot_night_h = nh;
		}
		if (server->boot_night) {
			wlr_scene_buffer_set_dest_size(server->boot_night, w, h);
			wlr_scene_node_set_enabled(&server->boot_night->node, true);
			wlr_scene_node_raise_to_top(&server->boot_night->node);
		}
	} else if (server->boot_night) {
		wlr_scene_node_set_enabled(&server->boot_night->node, false);
	}
	/* and what of the clock shines through the dark */
	double box[4];
	struct desk_sprite *sp = &server->boot_glow;
	if (dark && desk_clock_glow_box(w, h, box)) {
		uint64_t key = face ^ ((uint64_t)w << 16) ^ (uint64_t)h;
		if (!sp->drawn || sp->key != key) {
			sp->key = key;
			struct boot_glow_args a = { w, h };
			sprite_paint(server, sp, server->layer_top, box, boot_glow_draw, &a);
		}
		if (sp->node) {
			double a = desk_night_alpha_at(w, h, L, box[0] + box[2] * 0.5,
				box[1] + box[3] * 0.5);
			wlr_scene_buffer_set_opacity(sp->node, (float)clampf_d(a, 0.0, 1.0));
			wlr_scene_node_set_enabled(&sp->node->node, true);
			wlr_scene_node_raise_to_top(&sp->node->node);
		}
	} else if (sp->node) {
		wlr_scene_node_set_enabled(&sp->node->node, false);
	}
}

/* The dark, and the clock over it, as the lamp is now. */
static void boot_paint(struct desk_server *server)
{
	/* the clock, counting */
	int shown = (int)floor(server->boot_p * 100.0 + 1e-6) * 8 + server->boot_stage;
	if (shown != server->boot_shown) {
		server->boot_shown = shown;
		clock_publish(server);
	}
	night_paint(server, server->boot_light, (uint64_t)shown << 32);
}

/* the lamp brought down for a show, and the clock through it */
static void show_paint_night(struct desk_server *server, double L)
{
	night_paint(server, L, desk_clock_key(time(NULL)) | 1u);
}

static void boot_tick(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	if (!server->boot_on || w < 8 || h < 8) {
		return;
	}
	double now = monotonic_sec();
	if (server->boot_t0 <= 0.0) {
		server->boot_t0 = now;
		server->boot_at = now;
		server->boot_real0 = real_sec();
	}
	double dt = clampf_d(now - server->boot_at, 0.0, 0.1);
	server->boot_at = now;
	/* how far the room really is -- and whether there is any desk to light
	 * meanwhile: a first look traced small (room_preview), on a machine
	 * that traces on its processor */
	bool desk = desk_scene_ready(w, h);
	bool shown = desk || desk_room_shown(w, h) != 0;
	if (desk && !server->icons_shown) {
		/* up the moment it is there, not on the next idle tick: the bare
		 * boards would show through the dark */
		room_landed(server);
	}
	bool views = desk && desk_scene_views_ready(w, h);
	/* each view as it comes, for the log: on a machine that traces on its
	 * processor the bookcase is minutes ahead of the post box */
	for (int v = 0; desk && v < DESK_VIEW_COUNT; v++) {
		static const char *const NAME[DESK_VIEW_COUNT] = { "the bookcase",
			"the Control Panel", "the post box" };
		if (!(server->views_logged & (1u << v)) && desk_scene_view_ready(w, h, v)) {
			server->views_logged |= 1u << v;
			wlr_log(WLR_INFO, "%s is up (%.1f s since the start)", NAME[v],
				real_sec() - g_start_real);
		}
	}
	int view = -1;
	/* the desk's own count says nothing while a GPU is still being got
	 * ready to trace it -- seconds, sometimes -- so it creeps meanwhile, a
	 * little and never past a third of its share, rather than sit at 0 */
	double dp = desk ? 1.0 : fmax(desk_scene_progress(),
		0.30 * (1.0 - exp(-(now - server->boot_t0) / 5.0)));
	double vp = views ? 1.0 : desk_scene_views_progress(&view);
	double wp = !views ? 0.0 : server->warm_step > 7 ? 1.0 : server->warm_step / 8.0;
	double real = 0.45 * dp + 0.40 * vp + 0.15 * wp;
	(void)view;
	if (desk && server->boot_desk_at <= 0.0) {
		server->boot_desk_at = real_sec();
	}
	/* the pictures made ahead of a first look, quicker than the idle tick
	 * makes them while there is a count waiting on them -- each view's as
	 * it comes (views_prewarm waits for the one it needs) */
	if (desk && server->warm_step <= 7 && now - server->boot_warm_at >= BOOT_WARM_S) {
		server->boot_warm_at = now;
		views_prewarm(server);
	}
	/* the count: toward how far it really is, no quicker than the lamp may
	 * come up, and no jumps */
	double ramp = server->boot_lamp ? (now - server->boot_t0) / boot_light_s() : 1.0;
	double target = fmin(real, ramp);
	if (target > server->boot_p) {
		server->boot_p = fmin(target, server->boot_p + dt * BOOT_RATE);
	}
	/* the lamp, as far as the count -- and all the way once the desk is
	 * there, whatever is still being made */
	if (shown) {
		server->boot_floor = fmin(1.0, server->boot_floor + dt / boot_light_s());
	}
	/* what the clock says is being made: as far as it has counted, which is
	 * never further than the work has really got -- the desk, the three
	 * views a third of their share each, the lamp, and ready */
	double p = server->boot_p;
	server->boot_stage = p < 0.45 ? 0 : p < 0.583 ? 1 : p < 0.717 ? 2 : p < 0.85 ? 3
		: p < 0.999 ? 4 : 5;
	/* With no desk yet at all (its first run at this size) the room stays
	 * dark whatever the lamp is set to, and the clock -- where it will
	 * stand -- is what says how far along it is. */
	double L = server->boot_lamp || !desk ? desk_boot_light(server->boot_p) : 1.0;
	L = fmax(L, server->boot_floor);
	server->boot_light = L;
	if (L >= 0.999 && !server->boot_lit) {
		server->boot_lit = true;
		wlr_log(WLR_INFO, "the light is on (%.1f s since the start)", real_sec() - g_start_real);
	}
	if (server->boot_p >= 0.999 && real >= 0.999) {
		if (server->boot_done_at <= 0.0) {
			server->boot_done_at = now;
		}
		if (now - server->boot_done_at >= BOOT_HOLD_S) {
			boot_end(server);
			return;
		}
	}
	boot_paint(server);
	first_letter_tick(server, L);
	/* Frame after frame only while the lamp is coming up. Waiting on the
	 * room, nothing moves but the count, which the tick comes back for
	 * (tick_cb): a frame every frame meant the dark over the whole screen
	 * put together sixty times a second, on the CPU the room was being
	 * read back or traced with -- on an old machine, most of it. */
	if (shown && L < 0.999) {
		server_schedule_frames(server);
	}
}

static int tick_cb(void *data)
{
	struct desk_server *server = data;
	prof_report(real_sec());
	/* the lamp's count, while nothing else is moving (boot_tick) */
	if (server->boot_on) {
		server_schedule_frames(server);
	}
	/* The room is ray traced on a worker. Check first, so that the tick it
	 * lands on is the tick that puts it up rather than the one after. */
	if (!server->icons_shown && desk_scene_ready(server->desk_w, server->desk_h)) {
		room_landed(server);
	} else if (!server->icons_shown) {
		/* Still tracing. Ask again every tick: a resize that landed while
		 * the tracer was busy is turned away, and nothing else would ever
		 * come back for it -- the desk would stay a bare plank for good. */
		desk_scene_warm(server->desk_w, server->desk_h, true);
		/* How far along it is is the clock's to say (desktop_tick); the
		 * desk under it is the same picture until there is a room to
		 * draw, or another (wait_*). */
		if (desktop_waiting_stale(server)) {
			desktop_paint(server);
			server_schedule_frames(server);
		}
	}
	desktop_tick(server);
	views_prewarm(server);
	clock_tick(server);
	base_timeouts(server);
	shell_conf_poll(server);
	/* The clock and the calendar live on the bookcase shelf now, and the
	 * shelf is only repainted when something about it changes -- and while
	 * you are looking at it, ten times a second. The seconds hand is
	 * thrown at each mark and rings about it before it settles, and at one
	 * frame a second there is no throw and no ring, only a picture of a
	 * clock that differs from the last picture of a clock. */
	bool shelf = shelf_up(server);
	if (shelf) {
		paint_shelf(server);
		server_schedule_frames(server);
		/* the book's stills for the next turn, while it lies still */
		book_ahead(server);
	}
	if (server->tick) {
		int ms = 1000;
		if (!server->icons_shown || server->boot_on) {
			ms = 400;   /* asking after the room, while it is being made */
		} else if (shelf) {
			ms = 100;
		}
		wl_event_source_timer_update(server->tick, ms);
	}
	return 0;
}

static void begin_interactive(struct desk_toplevel *toplevel, enum desk_cursor_mode mode, uint32_t edges)
{
	struct desk_server *server = toplevel->server;
	if (!toplevel->scene_tree || toplevel->fullscreen) {
		return;
	}
	server->grabbed = toplevel;
	server->cursor_mode = mode;
	if (mode == DESK_CUR_MOVE) {
		server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
		server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
	} else {
		struct wlr_box geo;
		int l, t, r, b;
		view_geometry(toplevel, &geo);
		view_insets(toplevel, &l, &t, &r, &b);
		/* the window proper lies at the insets: its surface is placed so */
		double border_x = (toplevel->scene_tree->node.x + l) +
			((edges & WLR_EDGE_RIGHT) ? geo.width : 0);
		double border_y = (toplevel->scene_tree->node.y + t) +
			((edges & WLR_EDGE_BOTTOM) ? geo.height : 0);
		server->grab_x = server->cursor->x - border_x;
		server->grab_y = server->cursor->y - border_y;
		server->grab_geobox = geo;
		server->grab_geobox.x = toplevel->scene_tree->node.x + l;
		server->grab_geobox.y = toplevel->scene_tree->node.y + t;
		server->resize_edges = edges;
		cursor_for_edges(server, edges);
	}
}

static struct desk_toplevel *view_at(struct desk_server *server, double lx, double ly,
	struct wlr_surface **surface, double *sx, double *sy)
{
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
	if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
	if (scene_surface) {
		*surface = scene_surface->surface;
	} else {
		*surface = NULL;
	}
	struct wlr_scene_tree *tree = node->parent;
	while (tree != NULL && tree->node.data == NULL) {
		tree = tree->node.parent;
	}
	if (tree && tree->node.data && (uintptr_t)tree->node.data != DESK_ROLE_DOCK &&
		(uintptr_t)tree->node.data != DESK_ROLE_SHEET) {
		return tree->node.data;
	}
	return NULL;
}

/* Is it a window under the pointer -- a frame, a client, an X menu --
 * rather than the desk? The desk and everything on it lie under the
 * windows, and a click on a sheet of paper lying over the post box is a
 * click on the paper. */
static bool pointer_over(struct desk_server *server, struct wlr_scene_tree *tree)
{
	if (!tree) {
		return false;
	}
	double sx, sy;
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node,
		server->cursor->x, server->cursor->y, &sx, &sy);
	for (; node; node = node->parent ? &node->parent->node : NULL) {
		if (node == &tree->node) {
			return true;
		}
	}
	return false;
}

static bool window_under(struct desk_server *server)
{
	return pointer_over(server, server->layer_win) || pointer_over(server, server->layer_full)
		|| pointer_over(server, server->layer_xover);
}

/* A game may occupy the fullscreen layer above the whole desk. On an
 * explicit application switch, return it to its sheet before raising the
 * next app, or the new window would remain hidden under the game. */
static void desk_switch_application(struct desk_server *server)
{
	struct desk_toplevel *current = view_of_surface(server,
		server->seat->keyboard_state.focused_surface);
	if (!current && !wl_list_empty(&server->toplevels)) {
		current = wl_container_of(server->toplevels.next, current, link);
	}
	struct desk_toplevel *next;
	wl_list_for_each_reverse(next, &server->toplevels, link) {
		if (next != current && next->scene_tree && !next->unmanaged
			&& next->sheet.phase == SHEET_IDLE && view_takes_keys(next)) {
			if (current && current->fullscreen) {
				view_set_fullscreen(current, false);
			}
			focus_toplevel(next);
			return;
		}
	}
	/* nothing else to go to: a game alone on the whole screen goes back on
	 * its sheet, and the desk is there */
	if (current && current->fullscreen) {
		view_set_fullscreen(current, false);
	}
}

static bool handle_keybinding(struct desk_server *server, xkb_keysym_t sym, uint32_t mods)
{
	/* Once the lamp starts going out there is nothing to press, nothing to
	 * turn away to, and no key that takes it back. That is what the key
	 * and the glass were for. */
	if (console_dying(server)) {
		return true;
	}
	if ((mods & (WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO)) && sym == XKB_KEY_Escape) {
		end_begin(server, END_LOG_OUT);
		return true;
	}
	/* Alt+Tab should be a compositor shortcut even when a game has focus. */
	if ((mods & WLR_MODIFIER_ALT) && sym == XKB_KEY_Tab) {
		desk_switch_application(server);
		return true;
	}
	if (card_keys(server, sym)) {
		return true;
	}
	if (post_up(server)) {
		if (sym == XKB_KEY_Escape) {
			post_toggle(server);
			return true;
		}
	}
	if (console_up(server)) {
		if (sym == XKB_KEY_Escape) {
			console_toggle(server);
			return true;
		}
	}
	if (shelf_up(server) && cal_up(server)) {
		/* the calendar in your hand: put back, or turned a page */
		if (sym == XKB_KEY_Escape) {
			cal_close(server);
			return true;
		}
		if (sym == XKB_KEY_Left || sym == XKB_KEY_Up || sym == XKB_KEY_Page_Up) {
			cal_turn_page(server, -1);
			return true;
		}
		if (sym == XKB_KEY_Right || sym == XKB_KEY_Down || sym == XKB_KEY_Page_Down) {
			cal_turn_page(server, 1);
			return true;
		}
		/* and the sections by their initials */
		int v = sym == XKB_KEY_y ? DESK_CAL_YEAR : sym == XKB_KEY_m ? DESK_CAL_MONTH
			: sym == XKB_KEY_w ? DESK_CAL_WEEK : sym == XKB_KEY_d ? DESK_CAL_DAY : -1;
		if (v >= 0) {
			cal_switch(server, v);
			return true;
		}
	}
	if (shelf_up(server)) {
		if (sym == XKB_KEY_Escape) {
			/* the binder goes back on the shelf before you sit down */
			if (server->shelf_binder >= 0) {
				binder_close(server);
			} else {
				shelf_toggle(server);
			}
			return true;
		}
		if (server->shelf_binder >= 0) {
			int fan = desk_binder_fan();
			if (sym == XKB_KEY_Right || sym == XKB_KEY_Down) {
				binder_riffle(server, 1);
				return true;
			}
			if (sym == XKB_KEY_Left || sym == XKB_KEY_Up) {
				binder_riffle(server, -1);
				return true;
			}
			if (sym == XKB_KEY_Page_Down) {
				binder_riffle(server, fan);
				return true;
			}
			if (sym == XKB_KEY_Page_Up) {
				binder_riffle(server, -fan);
				return true;
			}
		}
	}
	if (mods & WLR_MODIFIER_LOGO) {
		switch (sym) {
		case XKB_KEY_Return:
			terminal_open();
			return true;
		case XKB_KEY_l:
		case XKB_KEY_L:
			lock_request(server);
			return true;
		case XKB_KEY_q:
		case XKB_KEY_Q:
			if (!wl_list_empty(&server->toplevels)) {
				struct desk_toplevel *view = wl_container_of(server->toplevels.next, view, link);
				view_close(view);
			}
			return true;
		case XKB_KEY_Tab:
			desk_switch_application(server);
			return true;
		case XKB_KEY_f:
		case XKB_KEY_F:
			/* Super+F gives games without a fullscreen setting one anyway. */
			if (!shelf_up(server) && !console_up(server) && !post_up(server)) {
				struct desk_toplevel *v = view_of_surface(server,
					server->seat->keyboard_state.focused_surface);
				if (!v && !wl_list_empty(&server->toplevels)) {
					v = wl_container_of(server->toplevels.next, v, link);
				}
				if (v && v->scene_tree && v->sheet.phase == SHEET_IDLE && !v->unmanaged) {
					view_set_fullscreen(v, !v->fullscreen);
				}
			}
			return true;
		case XKB_KEY_m:
		case XKB_KEY_M:
			if (!wl_list_empty(&server->toplevels)) {
				struct desk_toplevel *view = view_of_surface(server,
					server->seat->keyboard_state.focused_surface);
				if (!view) {
					view = wl_container_of(server->toplevels.next, view, link);
				}
				if (view->fullscreen) {
					view_set_fullscreen(view, false);
				}
				view_fold_toggle(view);
			}
			return true;
		case XKB_KEY_d:
		case XKB_KEY_D:
			if (!shelf_up(server)) {
				drawer_toggle(server);
			}
			return true;
		case XKB_KEY_a:
		case XKB_KEY_A:
			if (!console_up(server) && !post_up(server)) {
				shelf_toggle(server);
			}
			return true;
		case XKB_KEY_c:
		case XKB_KEY_C:
			if (!shelf_up(server) && !post_up(server)) {
				console_toggle(server);
			}
			return true;
		case XKB_KEY_n:
		case XKB_KEY_N:
			if (!shelf_up(server) && !console_up(server)) {
				post_toggle(server);
			}
			return true;
		default:
			break;
		}
	}
	if ((mods & WLR_MODIFIER_ALT) && sym == XKB_KEY_F4) {
		if (!wl_list_empty(&server->toplevels)) {
			struct desk_toplevel *view = wl_container_of(server->toplevels.next, view, link);
			view_close(view);
		}
		return true;
	}
	if ((mods & WLR_MODIFIER_ALT) && sym == XKB_KEY_F1) {
		desk_switch_application(server);
		return true;
	}
	return false;
}

/* ---- the screen lock: the front door ----------------------------------
 *
 * Super+L, or logind (loginctl lock-session, the machine going to sleep):
 * the desk is behind a front door (desk_draw_lock). Nothing behind it
 * gets a key or the pointer -- no window, no shortcut but the VT keys and
 * a laptop's brightness keys -- until the password is right (PAM, on a
 * thread, lock.c). Then the lever turns, the door swings in on the room,
 * and you go in: the room is the view of the bookcase from inside, and
 * once it fills the screen the door is gone and you sit down at the desk,
 * the bookcase's own camera move (shelf_toggle). Locked from somewhere
 * that move does not start from (the Control Panel, the post box, the
 * drawer out, a first run with no bookcase yet), the door fades away on
 * where you were instead. */

enum { LOCK_NONE = 0, LOCK_SHUT, LOCK_CHECK, LOCK_OPEN };

#define LOCK_FADE_S 0.35     /* coming down over the desk */
#define LOCK_WALK_S 1.10     /* and walking up to the door */
#define LOCK_FAIL_S 1.60     /* told no: the red slot, and what it says */
#define LOCK_CARET_MS 550
#define LOCK_LEAN_S 0.45     /* leaning in to the key */
#define LOCK_TRY 0.10        /* how far it goes round before it is known */
#define LOCK_TRIED_S 0.70    /* after Return, by when the key has been tried */
#define LOCK_TURN_S 0.32     /* let in: the key, a quarter turn */
#define LOCK_BACK0 0.30      /* leaning back out, from here */
#define LOCK_BACK1 0.95      /* to here */
#define LOCK_SWING0 0.40     /* the door, from here */
#define LOCK_SWING1 1.50     /* to here */
#define LOCK_PUSH0 0.95      /* and through it, from here */
#define LOCK_PUSH1 2.55      /* to here */
/* and locking from in the room, the same backwards: out of the room, the
 * door shut behind you, the key turned back */
#define LOCK_OUT_WALK 1.55   /* back out through the doorway */
#define LOCK_OUT_SHUT0 1.00  /* the door, from here */
#define LOCK_OUT_SHUT1 2.00  /* to here */
#define LOCK_OUT_LEAN 1.90   /* leaning in to the key */
#define LOCK_OUT_TURN 2.40   /* turning it back */
#define LOCK_OUT_BACK 2.80   /* and leaning back out */
#define LOCK_OUT_END 3.45
#define LOCK_FILM_MAX_S 3.0  /* how long the turn to the bookcase may take */

enum { LOCK_FROM_ELSEWHERE = 0, LOCK_FROM_DESK, LOCK_FROM_SHELF, LOCK_FROM_CONSOLE };
enum { LOCK_COME_NOW = 0, LOCK_COME_WALK, LOCK_COME_FILM, LOCK_COME_OUT };

static bool locked(const struct desk_server *server)
{
	return server->lock_phase != LOCK_NONE;
}

static double lock_ease(double t)
{
	t = clampf_d(t, 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}

struct lock_args {
	struct desk_lock_look look;
};

static void lock_draw(cairo_t *cr, int w, int h, void *data)
{
	const struct lock_args *a = data;
	desk_draw_lock(cr, w, h, &a->look);
}

static void lock_look(const struct desk_server *server, struct desk_lock_look *lk)
{
	memset(lk, 0, sizeof *lk);
	double now = monotonic_sec();
	lk->name = server->lock_name;
	lk->picture = server->lock_picture;
	lk->typed = server->lock_typed;
	lk->caret = server->lock_caret_on && server->lock_phase == LOCK_SHUT;
	lk->checking = server->lock_phase == LOCK_CHECK;
	lk->caps = server->lock_caps;
	lk->now_s = now;
	lk->steady = 1.0 - g_bounce;
	if (server->lock_walk) {
		lk->back = 1.0 - lock_ease((now - server->lock_at) / LOCK_WALK_S);
	}
	/* Return: lean in to the key, and try it */
	if (server->lock_check_at > 0.0) {
		double t = now - server->lock_check_at, l0 = server->lock_lean0;
		lk->lean = l0 + (1.0 - l0) * lock_ease(t / LOCK_LEAN_S);
		lk->turn = LOCK_TRY * lock_ease((t - LOCK_LEAN_S * 0.75) / 0.22);
	}
	if (server->lock_phase == LOCK_OPEN) {
		/* it goes round; stand back */
		double t = now - server->lock_open_at;
		lk->turn += (1.0 - lk->turn) * lock_ease(t / LOCK_TURN_S);
		lk->lean *= 1.0 - lock_ease((t - LOCK_BACK0) / (LOCK_BACK1 - LOCK_BACK0));
	} else if (server->lock_fail_at > 0.0 && !server->lock_refused
		&& now >= server->lock_fail_at) {
		/* it will not go: it is jerked against the wards and comes back,
		 * and you stand back to read why */
		double t = now - server->lock_fail_at;
		lk->fail = clampf_d(t / LOCK_FAIL_S, 0.0, 0.999);
		lk->turn *= 1.0 - lock_ease(t / 0.30);
		lk->turn += 0.07 * sin(t * 46.0) * exp(-t * 5.5);
		lk->lean *= 1.0 - lock_ease((t - 0.22) / 0.50);
	}
	if (server->lock_leaving) {
		/* letting yourself in, backwards */
		double t = now - server->lock_leave_at;
		lk->push = 1.0 - lock_ease(t / LOCK_OUT_WALK);
		lk->open = 1.0 - lock_ease((t - LOCK_OUT_SHUT0) / (LOCK_OUT_SHUT1 - LOCK_OUT_SHUT0));
		lk->lean = lock_ease((t - LOCK_OUT_LEAN) / LOCK_LEAN_S)
			* (1.0 - lock_ease((t - LOCK_OUT_BACK) / (LOCK_OUT_END - LOCK_OUT_BACK)));
		lk->turn = 1.0 - lock_ease((t - LOCK_OUT_TURN) / LOCK_TURN_S);
		lk->inside = server->lock_inside;
		lk->caret = false;
		lk->typed = 0;
	}
	lk->inside_view = server->lock_from == LOCK_FROM_CONSOLE ? DESK_VIEW_CONSOLE
		: DESK_VIEW_SHELF;
	/* what it is like out there: the hour, the weather, the day */
	desk_occasion_mood(&lk->mood);
	if (server->lock_phase == LOCK_OPEN) {
		double t = now - server->lock_open_at;
		lk->open = lock_ease((t - LOCK_SWING0) / (LOCK_SWING1 - LOCK_SWING0));
		lk->push = server->lock_inside
			? lock_ease((t - LOCK_PUSH0) / (LOCK_PUSH1 - LOCK_PUSH0)) : 0.0;
		lk->inside = server->lock_inside;
		lk->checking = false;
	}
}

/* How often what moves at the door is drawn: thirty times a second, and
 * fifteen on Low; at Ultra every frame. In 2D it does not move at all
 * (paint_lock_fx). */
static double lock_fx_period(const struct desk_server *server)
{
	(void)server;
	enum desk_gfx g = desk_gfx_level();
	return g == DESK_GFX_ULTRA ? 0.0 : g <= DESK_GFX_LOW ? 1.0 / 15.0 : 1.0 / 30.0;
}

/* what moves out at the door, on its own sheet over it */
static void paint_lock_fx(struct desk_server *server, const struct desk_lock_look *lk,
	double op)
{
	int w = server->desk_w, h = server->desk_h;
	if (!desk_lock_fx_wanted(&lk->mood) || lk->push >= 0.999 || server->lock_film
		|| gfx_flat()) {
		if (server->lock_fx) {
			wlr_scene_node_set_enabled(&server->lock_fx->node, false);
		}
		return;
	}
	int ow, oh;
	desk_ambient_size(w, h, &ow, &oh);
	struct desk_buf *buf = buf_create(ow, oh);
	cairo_save(buf->cr);
	cairo_scale(buf->cr, (double)ow / w, (double)oh / h);
	desk_draw_lock_fx(buf->cr, w, h, lk);
	cairo_restore(buf->cr);
	scene_put(&server->lock_fx, server->layer_lock, buf, 0, 0, NULL);
	if (server->lock_fx) {
		wlr_scene_buffer_set_dest_size(server->lock_fx, w, h);
		server->lock_fx->point_accepts_input = shadow_accepts;
		wlr_scene_node_raise_to_top(&server->lock_fx->node);
		wlr_scene_buffer_set_opacity(server->lock_fx, (float)op);
		wlr_scene_node_set_enabled(&server->lock_fx->node, true);
	}
	server->lock_fx_at = real_sec();
}

static void paint_lock(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	if (!locked(server) || w < 8 || h < 8) {
		return;
	}
	if (server->lock_film) {
		/* turning to the bookcase first: the door is not down yet, but
		 * nothing behind it has the keys any more */
		wlr_scene_node_set_enabled(&server->layer_lock->node, false);
		return;
	}
	/* black over every output, under the door, so that a second screen
	 * shows nothing of what is behind it either */
	struct wlr_box all;
	wlr_output_layout_get_box(server->output_layout, NULL, &all);
	if (!server->lock_cover) {
		const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		server->lock_cover = wlr_scene_rect_create(server->layer_lock, 1, 1, black);
	}
	wlr_scene_rect_set_size(server->lock_cover, all.width > w ? all.width : w,
		all.height > h ? all.height : h);
	wlr_scene_node_set_position(&server->lock_cover->node, all.x < 0 ? all.x : 0,
		all.y < 0 ? all.y : 0);
	struct lock_args a;
	lock_look(server, &a.look);
	int ow, oh;
	desk_ambient_size(w, h, &ow, &oh);
	struct desk_buf *buf = buf_create(ow, oh);
	cairo_save(buf->cr);
	cairo_scale(buf->cr, (double)ow / w, (double)oh / h);
	lock_draw(buf->cr, w, h, &a);
	cairo_restore(buf->cr);
	scene_put(&server->lock_node, server->layer_lock, buf, 0, 0, NULL);
	if (server->lock_node) {
		wlr_scene_buffer_set_dest_size(server->lock_node, w, h);
		wlr_scene_node_raise_to_top(&server->lock_node->node);
	}
	/* coming down, it is seen through; going, it fades off where you were
	 * when there is no going in */
	double now = monotonic_sec(), op = 1.0;
	if (server->lock_fade_in) {
		op = clampf_d((now - server->lock_at) / LOCK_FADE_S, 0.0, 1.0);
	}
	if (server->lock_phase == LOCK_OPEN && !server->lock_inside) {
		op = 1.0 - clampf_d((now - server->lock_open_at - LOCK_SWING1 + 0.15) / LOCK_FADE_S,
			0.0, 1.0);
	}
	if (server->lock_node) {
		wlr_scene_buffer_set_opacity(server->lock_node, (float)op);
	}
	paint_lock_fx(server, &a.look, op);
	wlr_scene_node_set_enabled(&server->lock_cover->node, op >= 0.999);
	wlr_scene_node_set_enabled(&server->layer_lock->node, true);
}

static int lock_caret_cb(void *data)
{
	struct desk_server *server = data;
	/* dark, there is nobody to blink at: it starts again on waking */
	if (server->lock_phase != LOCK_SHUT || server->dark) {
		return 0;
	}
	server->lock_caret_on = !server->lock_caret_on;
	paint_lock(server);
	server_schedule_frames(server);
	wl_event_source_timer_update(server->lock_timer, LOCK_CARET_MS);
	return 0;
}

/* zeroed in a way the compiler may not leave out */
static void lock_zero(void *p, size_t n)
{
	volatile unsigned char *q = p;
	while (n--) {
		*q++ = 0;
	}
}

static void lock_wipe(struct desk_server *server)
{
	lock_zero(server->lock_pass, sizeof server->lock_pass);
	server->lock_len = 0;
}

/* What the room looks like from in front of the bookcase, and from in
 * front of the Control Panel: the pictures the door opens on to, and is
 * walked out of. */
static cairo_surface_t *lock_snap_shelf(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h, ow, oh;
	desk_ambient_size(w, h, &ow, &oh);
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ow, oh);
	cairo_t *c = cairo_create(s);
	cairo_scale(c, (double)ow / w, (double)oh / h);
	desk_draw_shelf(c, w, h, -1, 0.0, server->shelf_binder);
	desk_draw_shelf_gadgets(c, w, h, time(NULL));
	cairo_destroy(c);
	return s;
}

static cairo_surface_t *lock_snap_console(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h, ow, oh;
	desk_ambient_size(w, h, &ow, &oh);
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ow, oh);
	cairo_t *c = cairo_create(s);
	cairo_scale(c, (double)ow / w, (double)oh / h);
	struct desk_console_state st;
	console_state(server, &st);
	desk_draw_console(c, w, h, &st, -1);
	cairo_destroy(c);
	return s;
}

/* Locked. how: at once (logind: the machine may be going to sleep); walking
 * up to the door from a step away; turning to the bookcase first and then
 * going out (lock_tick); or straight out of the room, which inside shows
 * as it is now. from says where you were, to come back to. */
static void lock_begin(struct desk_server *server, int how, cairo_surface_t *inside, int from)
{
	if (locked(server)) {
		if (inside) {
			cairo_surface_destroy(inside);
		}
		return;
	}
	wlr_log(WLR_INFO, "lock: locked");
	double now = monotonic_sec();
	server->lock_phase = LOCK_SHUT;
	server->lock_at = now;
	server->lock_fade_in = how == LOCK_COME_WALK;
	server->lock_walk = how == LOCK_COME_WALK;
	server->lock_film = how == LOCK_COME_FILM;
	server->lock_leaving = how == LOCK_COME_OUT;
	server->lock_leave_at = now;
	server->lock_from = from;
	if (server->lock_inside) {
		cairo_surface_destroy(server->lock_inside);
	}
	server->lock_inside = inside;
	server->lock_fail_at = 0.0;
	server->lock_check_at = 0.0;
	server->lock_lean0 = 0.0;
	server->lock_refused = false;
	server->lock_typed = 0;
	server->lock_caret_on = true;
	lock_wipe(server);
	if (!server->lock_name[0]) {
		char login[64];
		desk_lock_user(login, sizeof login, server->lock_name, sizeof server->lock_name);
	}
	if (!server->lock_picture && !server->lock_picture_tried) {
		server->lock_picture_tried = true;
		server->lock_picture = desk_lock_picture();
	}
	/* nobody behind the door has the keys or the pointer any more */
	reset_cursor_mode(server);
	server->cc_grab = -1;
	wlr_seat_keyboard_notify_clear_focus(server->seat);
	wlr_seat_pointer_notify_clear_focus(server->seat);
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	server->lock_hand = false;
	if (!server->lock_timer) {
		server->lock_timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), lock_caret_cb, server);
	}
	if (server->lock_timer) {
		wl_event_source_timer_update(server->lock_timer, LOCK_CARET_MS);
	}
	if (how == LOCK_COME_NOW || how == LOCK_COME_WALK) {
		desk_sound_play(DESK_SND_LOCK_CLUNK, 0.0, 0.5, 0.0);
	}
	desk_lock_hint(true);
	paint_lock(server);
	server_schedule_frames(server);
	/* the door, lit with nobody at it, goes dark sooner than the desk */
	idle_rearm(server);
}

/* Out of the room: the door swings to, the key goes back round. */
static void lock_go_out(struct desk_server *server, cairo_surface_t *inside)
{
	double now = monotonic_sec();
	if (server->lock_inside) {
		cairo_surface_destroy(server->lock_inside);
	}
	server->lock_inside = inside;
	server->lock_film = false;
	server->lock_leaving = true;
	server->lock_leave_at = now;
	desk_sound_play_in(DESK_SND_POST_CLOSE, 0.1, 0.8, 0.0, real_secs(LOCK_OUT_SHUT1 - 0.06));
	desk_sound_play_in(DESK_SND_LOCK_CLUNK, 0.3, 0.9, 0.0,
		real_secs(LOCK_OUT_TURN + LOCK_TURN_S * 0.85));
}

/* Super+L, or the LOCK button: lock, the way you let yourself in run
 * backwards -- from the desk, turn to the bookcase and go out of the room
 * by the front door; from in front of the bookcase or the Control Panel,
 * go out from there. Anywhere else, the door comes down over it and you
 * walk up to it. */
static void lock_request(struct desk_server *server)
{
	if (locked(server)) {
		return;
	}
	if (desk_gfx_level() <= DESK_GFX_LOW) {
		/* Low and 2D: no walk out to it, traced as it goes -- the door,
		 * at once */
		lock_begin(server, LOCK_COME_NOW, NULL, LOCK_FROM_ELSEWHERE);
		return;
	}
	bool views = desk_scene_views_ready(server->desk_w, server->desk_h);
	if (views && console_up(server) && !server->cc_moving && server->cc_t > 0.999
		&& !console_dying(server)) {
		cairo_surface_t *in = lock_snap_console(server);
		/* going out: the bolt's clunk comes when the key turns
		 * (lock_go_out), not once more now */
		lock_begin(server, LOCK_COME_OUT, NULL, LOCK_FROM_CONSOLE);
		lock_go_out(server, in);
		paint_lock(server);
		return;
	}
	if (views && shelf_up(server) && !server->shelf_moving && server->shelf_t > 0.999) {
		cairo_surface_t *in = lock_snap_shelf(server);
		lock_begin(server, LOCK_COME_OUT, NULL, LOCK_FROM_SHELF);
		lock_go_out(server, in);
		paint_lock(server);
		return;
	}
	if (views && !shelf_up(server) && !console_up(server) && !post_up(server)
		&& server->drawer_t < 0.004) {
		shelf_toggle(server);
		if (server->shelf_moving || server->shelf_t > 0.999) {
			lock_begin(server, LOCK_COME_FILM, NULL, LOCK_FROM_DESK);
			return;
		}
	}
	lock_begin(server, LOCK_COME_WALK, NULL, LOCK_FROM_ELSEWHERE);
}

/* The door is gone: you are in. */
static void lock_finish(struct desk_server *server)
{
	bool into_shelf = server->lock_inside != NULL && server->lock_from_desk;
	server->lock_phase = LOCK_NONE;
	server->lock_film = false;
	server->lock_leaving = false;
	lock_wipe(server);
	if (server->lock_inside) {
		cairo_surface_destroy(server->lock_inside);
		server->lock_inside = NULL;
	}
	if (into_shelf) {
		/* standing in the room, looking at the bookcase, as the last of
		 * the door showed it: and now sit down at the desk */
		server->shelf_from = server->shelf_to = server->shelf_t = 1.0;
		server->shelf_moving = 0;
		paint_shelf(server);
	}
	wlr_scene_node_set_enabled(&server->layer_lock->node, false);
	if (server->lock_fx) {
		wlr_scene_node_set_enabled(&server->lock_fx->node, false);
	}
	desk_lock_forget();
	desk_lock_hint(false);
	if (into_shelf) {
		shelf_toggle(server);
	}
	/* let in on a day kept: the clock says so */
	show_for_today(server);
	if (!wl_list_empty(&server->toplevels)) {
		struct desk_toplevel *top = wl_container_of(server->toplevels.next, top, link);
		focus_toplevel(top);
	}
	server_schedule_frames(server);
	wlr_log(WLR_INFO, "lock: in");
}

/* Let in: the key goes round, you stand back as the door swings in, and
 * through it into the room -- once the key has been seen tried. */
static void lock_open(struct desk_server *server)
{
	if (!locked(server) || server->lock_phase == LOCK_OPEN) {
		return;
	}
	double now = monotonic_sec();
	server->lock_phase = LOCK_OPEN;
	server->lock_refused = false;
	server->lock_fail_at = 0.0;
	server->lock_open_at = now;
	if (server->lock_check_at > 0.0 && server->lock_check_at + LOCK_TRIED_S > now) {
		server->lock_open_at = server->lock_check_at + LOCK_TRIED_S;
	}
	double at = server->lock_open_at;
	lock_wipe(server);
	int w = server->desk_w, h = server->desk_h;
	/* the room to go into: where you went out of it if you did -- the
	 * desk by way of the bookcase, the bookcase, the Control Panel -- and
	 * otherwise, the bookcase if its camera can sit you down at the desk
	 * from there, or where you already were */
	int from = server->lock_from;
	if (from == LOCK_FROM_ELSEWHERE) {
		from = !shelf_up(server) && !console_up(server) && !post_up(server)
			&& server->drawer_t < 0.004 ? LOCK_FROM_DESK
			: shelf_up(server) && server->shelf_t > 0.999 ? LOCK_FROM_SHELF
			: console_up(server) && server->cc_t > 0.999 ? LOCK_FROM_CONSOLE
			: LOCK_FROM_ELSEWHERE;
	}
	server->lock_film = false;
	server->lock_leaving = false;
	server->lock_from = from;
	server->lock_from_desk = from == LOCK_FROM_DESK;
	if (server->lock_inside) {
		cairo_surface_destroy(server->lock_inside);
		server->lock_inside = NULL;
	}
	if (desk_scene_views_ready(w, h)) {
		if (from == LOCK_FROM_DESK || from == LOCK_FROM_SHELF) {
			server->lock_inside = lock_snap_shelf(server);
		} else if (from == LOCK_FROM_CONSOLE && console_up(server)) {
			server->lock_inside = lock_snap_console(server);
		}
	}
	desk_sound_play_in(DESK_SND_LOCK_CLUNK, 0.3, 0.9, 0.0,
		real_secs(at - now + LOCK_TURN_S * 0.85));
	desk_sound_play_in(DESK_SND_POST_OPEN, 0.1, 0.8, 0.0, real_secs(at - now + LOCK_SWING0));
	paint_lock(server);
	server_schedule_frames(server);
}

static void lock_checked(bool ok, void *data)
{
	struct desk_server *server = data;
	if (server->lock_phase != LOCK_CHECK) {
		return;
	}
	if (ok) {
		wlr_log(WLR_INFO, "lock: the password is right");
		lock_open(server);
		return;
	}
	wlr_log(WLR_INFO, "lock: the password is wrong");
	/* told at once, perhaps; shown once the key has been seen tried
	 * (lock_tick) */
	double now = monotonic_sec();
	server->lock_refused = true;
	server->lock_fail_at = now;
	if (server->lock_check_at + LOCK_TRIED_S > now) {
		server->lock_fail_at = server->lock_check_at + LOCK_TRIED_S;
	}
	paint_lock(server);
	server_schedule_frames(server);
}

/* The key will not go round: back to waiting, leaning back out. */
static void lock_refuse(struct desk_server *server)
{
	server->lock_refused = false;
	server->lock_phase = LOCK_SHUT;
	server->lock_typed = 0;
	server->lock_caret_on = true;
	desk_sound_play(DESK_SND_TOGGLE, 0.3, 0.8, 0.6);
	desk_sound_play_in(DESK_SND_KEY_IN, 0.3, 0.5, 0.8, real_secs(0.08));
	if (server->lock_timer) {
		wl_event_source_timer_update(server->lock_timer, LOCK_CARET_MS);
	}
}

/* Return, or the key clicked: lean in to it, try it, and ask. */
static void lock_submit(struct desk_server *server)
{
	if (server->lock_phase != LOCK_SHUT || server->lock_film || server->lock_leaving) {
		return;
	}
	double now = monotonic_sec();
	struct desk_lock_look lk;
	lock_look(server, &lk);
	server->lock_lean0 = clampf_d(lk.lean, 0.0, 1.0);
	server->lock_phase = LOCK_CHECK;
	server->lock_check_at = now;
	if (server->lock_hand) {
		server->lock_hand = false;
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
	server->lock_fail_at = 0.0;
	server->lock_refused = false;
	desk_sound_play_in(DESK_SND_KEY_IN, 0.3, 0.55, 0.0, real_secs(LOCK_LEAN_S * 0.75));
	if (!desk_lock_check(wl_display_get_event_loop(server->wl_display),
			server->lock_pass, lock_checked, server)) {
		lock_wipe(server);
		lock_checked(false, server);
		return;
	}
	lock_wipe(server);
	paint_lock(server);
	server_schedule_frames(server);
}

static void lock_key(struct desk_server *server, struct xkb_state *xkb, uint32_t keycode,
	uint32_t sym, uint32_t mods, bool released)
{
	bool caps = (mods & WLR_MODIFIER_CAPS) != 0;
	if (caps != server->lock_caps) {
		server->lock_caps = caps;
		paint_lock(server);
		server_schedule_frames(server);
	}
	if (released || server->lock_phase != LOCK_SHUT || server->lock_film
		|| server->lock_leaving) {
		return;   /* still going out of the room: the key is not in your hand */
	}
	char utf8[8] = "";
	xkb_state_key_get_utf8(xkb, keycode + 8, utf8, sizeof utf8);
	switch (sym) {
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		lock_submit(server);
		return;
	case XKB_KEY_BackSpace:
		if (server->lock_len > 0) {
			server->lock_len = utf8_prev(server->lock_pass, server->lock_len);
			server->lock_pass[server->lock_len] = '\0';
			server->lock_typed = server->lock_typed > 0 ? server->lock_typed - 1 : 0;
		}
		break;
	case XKB_KEY_Escape:
		lock_wipe(server);
		server->lock_typed = 0;
		break;
	default:
		if ((mods & WLR_MODIFIER_CTRL) && (sym == XKB_KEY_u || sym == XKB_KEY_U)) {
			lock_wipe(server);
			server->lock_typed = 0;
			break;
		}
		if ((unsigned char)utf8[0] < 0x20 || utf8[0] == 0x7f
			|| (mods & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO))) {
			return;
		}
		size_t k = strlen(utf8);
		if (server->lock_len + (int)k >= (int)sizeof server->lock_pass - 1) {
			return;
		}
		memcpy(server->lock_pass + server->lock_len, utf8, k);
		server->lock_len += (int)k;
		server->lock_pass[server->lock_len] = '\0';
		server->lock_typed++;
		desk_sound_play(DESK_SND_KEY_PRESS, 0.0, 0.25, 1.3);
		break;
	}
	server->lock_caret_on = true;
	if (server->lock_timer) {
		wl_event_source_timer_update(server->lock_timer, LOCK_CARET_MS);
	}
	paint_lock(server);
	server_schedule_frames(server);
}

/* Once a frame while something about the door is moving. */
static void lock_tick(struct desk_server *server)
{
	if (!locked(server)) {
		return;
	}
	double now = monotonic_sec();
	bool moving = false;
	if (server->lock_fade_in) {
		if (now - server->lock_at >= LOCK_FADE_S) {
			server->lock_fade_in = false;
		}
		moving = true;
	}
	if (server->lock_walk) {
		if (now - server->lock_at >= LOCK_WALK_S) {
			server->lock_walk = false;
		}
		moving = true;
	}
	if (server->lock_film) {
		if (server->shelf_t > 0.999 && !server->shelf_moving) {
			/* in front of the bookcase: now out of the room */
			lock_go_out(server, lock_snap_shelf(server));
		} else if (now - server->lock_at > LOCK_FILM_MAX_S) {
			/* it never got there: the door comes down over it instead */
			server->lock_film = false;
			server->lock_from = LOCK_FROM_ELSEWHERE;
			server->lock_walk = server->lock_fade_in = true;
			server->lock_at = now;
		}
		moving = true;
	}
	if (server->lock_leaving) {
		if (now - server->lock_leave_at >= LOCK_OUT_END) {
			server->lock_leaving = false;
			server->lock_caret_on = true;
			if (server->lock_inside) {
				cairo_surface_destroy(server->lock_inside);
				server->lock_inside = NULL;
			}
		}
		moving = true;
	}
	if (server->lock_refused && now >= server->lock_fail_at) {
		lock_refuse(server);
	}
	if (server->lock_fail_at > 0.0 && !server->lock_refused) {
		if (now - server->lock_fail_at >= LOCK_FAIL_S) {
			/* stood back from the door again, as it was */
			server->lock_fail_at = 0.0;
			server->lock_check_at = 0.0;
		}
		moving = true;
	}
	if (server->lock_phase == LOCK_CHECK) {
		moving = true;   /* the light running along the slot */
	}
	if (desk_lock_settling()) {
		moving = true;   /* the door, finely, as soon as it is done */
	}
	if (server->lock_phase == LOCK_OPEN) {
		double end = server->lock_inside ? LOCK_PUSH1 : LOCK_SWING1 + 0.15 + LOCK_FADE_S;
		if (now - server->lock_open_at >= end) {
			lock_finish(server);
			return;
		}
		moving = true;
	}
	if (moving) {
		paint_lock(server);
		server_schedule_frames(server);
	} else if (server->lock_phase != LOCK_NONE) {
		/* only the rain, the snow, the lights: their sheet, thirty
		 * times a second (lock_fx_period), the door left as it is */
		struct desk_lock_look lk;
		lock_look(server, &lk);
		if (desk_lock_fx_wanted(&lk.mood) && !gfx_flat()) {
			if (real_sec() - server->lock_fx_at >= lock_fx_period(server)) {
				double op = server->lock_node ? server->lock_node->opacity : 1.0;
				paint_lock_fx(server, &lk, op);
			}
			server_schedule_frames(server);
		}
	}
}

static void lock_cb(void *data)
{
	/* the machine may be about to sleep: down at once, no fading */
	lock_begin(data, LOCK_COME_NOW, NULL, LOCK_FROM_ELSEWHERE);
}

static void unlock_cb(void *data)
{
	struct desk_server *server = data;
	if (locked(server)) {
		lock_open(server);
	}
}

/* ---- the screen going dark ---------------------------------------------
 *
 * Nobody has touched a key, the pointer or the screen for a while
 * (shell.conf screen_off; ten minutes unless it says otherwise): the front
 * door comes down at once -- unless lock_on_screen_off says not to -- and
 * the screen goes dark. Anything touched brings it back, and does nothing
 * else: a click on the black is not a click on whatever was under it, and
 * the key that woke it is not typed at the door. A program playing a film
 * holds it up while its window is out to be seen (idle-inhibit); programs
 * that do something of their own after a while, swayidle, are told when
 * anybody last touched anything (ext-idle-notify); and wlopm turns the
 * screens off and on through the same machinery (wlr-output-power-
 * management). DESK_IDLE_TEST_S gives the wait in seconds, for trying it.
 *
 * Dark is the screen turned off: the output disabled, no frames, nothing
 * drawn. Nested, the screen is a window on another desktop, which is not
 * ours to turn off -- the X11 backend would take the window away, and with
 * it the pointer that is to wake it -- so it is covered black, shown once,
 * and nothing more is drawn into it until somebody comes back. */

#define IDLE_AT_DOOR_MS 60000   /* the door, lit, with nobody at it */
#define WAKE_BLIND_S 0.8        /* woken: a press before this is not for anything */

/* How long nobody touching anything takes to turn the screen off; 0 never. */
static int idle_ms(const struct desk_server *server)
{
	const char *test = getenv("DESK_IDLE_TEST_S");
	int ms = test && atoi(test) > 0 ? atoi(test) * 1000 : server->conf.screen_off * 60000;
	/* the door need not stand lit long for nobody */
	if (ms > IDLE_AT_DOOR_MS && locked(server)) {
		ms = IDLE_AT_DOOR_MS;
	}
	return ms;
}

/* Has every screen that is on gone dark? */
static bool screen_all_dark(struct desk_server *server)
{
	bool any = false;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (!o->off && !o->dark) {
			return false;
		}
		any |= o->dark;
	}
	return any;
}

/* The timer set for when the wait runs out, counted from the last touch;
 * none while it is dark already, or never to be. */
static void idle_rearm(struct desk_server *server)
{
	if (!server->idle_timer) {
		return;
	}
	int ms = idle_ms(server);
	if (ms <= 0 || screen_all_dark(server)) {
		wl_event_source_timer_update(server->idle_timer, 0);
		return;
	}
	double left = server->idle_at + ms / 1000.0 - real_sec();
	wl_event_source_timer_update(server->idle_timer,
		left > 0.001 ? (int)ceil(left * 1000.0) : 1);
}

/* Is a program holding the screen up: a window that asked, out on the desk
 * to be seen -- not folded away, not in the drawer, not behind the door? */
static bool idle_held(struct desk_server *server)
{
	if (locked(server)) {
		return false;
	}
	struct desk_idle_hold *hold;
	wl_list_for_each(hold, &server->idle_holds, link) {
		struct wlr_surface *s = wlr_surface_get_root_surface(hold->inhibitor->surface);
		if (!s || !s->mapped) {
			continue;
		}
		struct desk_toplevel *view = view_of_surface(server, s);
		if (!view || view->sheet.phase == SHEET_IDLE) {
			return true;
		}
	}
	return false;
}

static void idle_hold_update(struct desk_server *server)
{
	bool held = idle_held(server);
	if (held == server->idle_held) {
		return;
	}
	server->idle_held = held;
	if (server->idle_notifier) {
		wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, held);
	}
	wlr_log(WLR_INFO, "screen: %s", held ? "kept lit for a program (idle inhibitor)"
		: "no program keeping it lit any more");
	if (!held) {
		/* let go of: counted from now, as if somebody had just looked up */
		server->idle_at = real_sec();
		idle_rearm(server);
	}
}

static void idle_hold_gone(struct wl_listener *listener, void *data)
{
	struct desk_idle_hold *hold = wl_container_of(listener, hold, destroy);
	struct desk_server *server = hold->server;
	(void)data;
	wl_list_remove(&hold->destroy.link);
	wl_list_remove(&hold->map.link);
	wl_list_remove(&hold->unmap.link);
	wl_list_remove(&hold->link);
	free(hold);
	idle_hold_update(server);
}

static void idle_hold_map(struct wl_listener *listener, void *data)
{
	struct desk_idle_hold *hold = wl_container_of(listener, hold, map);
	(void)data;
	idle_hold_update(hold->server);
}

static void idle_hold_unmap(struct wl_listener *listener, void *data)
{
	struct desk_idle_hold *hold = wl_container_of(listener, hold, unmap);
	(void)data;
	idle_hold_update(hold->server);
}

static void server_new_idle_hold(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, new_idle_hold);
	struct wlr_idle_inhibitor_v1 *inhibitor = data;
	struct desk_idle_hold *hold = calloc(1, sizeof *hold);
	if (!hold) {
		return;
	}
	hold->server = server;
	hold->inhibitor = inhibitor;
	hold->destroy.notify = idle_hold_gone;
	wl_signal_add(&inhibitor->events.destroy, &hold->destroy);
	hold->map.notify = idle_hold_map;
	wl_signal_add(&inhibitor->surface->events.map, &hold->map);
	hold->unmap.notify = idle_hold_unmap;
	wl_signal_add(&inhibitor->surface->events.unmap, &hold->unmap);
	wl_list_insert(&server->idle_holds, &hold->link);
	idle_hold_update(server);
}

/* Is this screen a window on another desktop? */
static bool output_is_window(struct wlr_output *out)
{
#if WLR_HAS_X11_BACKEND
	if (wlr_output_is_x11(out)) {
		return true;
	}
#endif
	return wlr_output_is_wl(out);
}

static bool output_power(struct wlr_output *out, bool on, struct wlr_output_mode *mode,
	int w, int h, int refresh)
{
	struct wlr_output_state st;
	wlr_output_state_init(&st);
	wlr_output_state_set_enabled(&st, on);
	if (on && mode) {
		wlr_output_state_set_mode(&st, mode);
	} else if (on && w > 0 && h > 0) {
		wlr_output_state_set_custom_mode(&st, w, h, refresh);
	}
	bool ok = wlr_output_commit_state(out, &st);
	wlr_output_state_finish(&st);
	return ok;
}

/* One screen dark, or lit again in the mode it was in. */
static void output_set_dark(struct desk_output *o, bool dark)
{
	struct desk_server *server = o->server;
	struct wlr_output *out = o->wlr_output;
	if (o->dark == dark || o->off) {
		return;
	}
	if (output_is_window(out)) {
		if (dark) {
			struct wlr_box box;
			wlr_output_layout_get_box(server->output_layout, out, &box);
			const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			o->dark_cover = wlr_scene_rect_create(server->layer_dark,
				box.width > 0 ? box.width : out->width,
				box.height > 0 ? box.height : out->height, black);
			if (o->dark_cover) {
				wlr_scene_node_set_position(&o->dark_cover->node, box.x, box.y);
			}
		} else if (o->dark_cover) {
			wlr_scene_node_destroy(&o->dark_cover->node);
			o->dark_cover = NULL;
		}
		o->dark = dark;
		o->dark_shown = false;
		wlr_output_schedule_frame(out);
		return;
	}
	if (dark) {
		o->dark_mode = out->current_mode;
		o->dark_w = out->width;
		o->dark_h = out->height;
		o->dark_refresh = out->refresh;
		if (!output_power(out, false, NULL, 0, 0, 0)) {
			wlr_log(WLR_ERROR, "screen: %s would not turn off", out->name);
			return;
		}
		o->dark = true;
		return;
	}
	/* lit again whatever happens: a screen that will not come back must
	 * not leave every key and click after it swallowed as waking it */
	o->dark = false;
	if (!output_power(out, true, o->dark_mode, o->dark_w, o->dark_h, o->dark_refresh)
		&& !output_power(out, true, NULL, 0, 0, 0)) {
		wlr_log(WLR_ERROR, "screen: %s would not turn on again", out->name);
	}
	wlr_output_schedule_frame(out);
}

static void screen_dark_sync(struct desk_server *server)
{
	server->dark = false;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		server->dark |= o->dark;
	}
}

/* Nobody here: the door down first, if it is to come down, and then dark. */
static void screen_go_dark(struct desk_server *server)
{
	double idle = real_sec() - server->idle_at;
	bool lock = server->conf.lock_screen_off && !locked(server);
	if (lock) {
		lock_begin(server, LOCK_COME_NOW, NULL, LOCK_FROM_ELSEWHERE);
	}
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		output_set_dark(o, true);
	}
	screen_dark_sync(server);
	idle_rearm(server);
	wlr_log(WLR_INFO, "screen: nobody has touched anything for %.0f s: dark%s", idle,
		lock ? ", the door locked first" : "");
}

/* Somebody back: every screen lit again, the door's caret going. */
static void screen_wake(struct desk_server *server)
{
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		output_set_dark(o, false);
	}
	screen_dark_sync(server);
	server->idle_at = real_sec();
	idle_rearm(server);
	if (locked(server)) {
		server->lock_caret_on = true;
		if (server->lock_timer) {
			wl_event_source_timer_update(server->lock_timer, LOCK_CARET_MS);
		}
		paint_lock(server);
	}
	server_schedule_frames(server);
}

static int idle_cb(void *data)
{
	struct desk_server *server = data;
	int ms = idle_ms(server);
	if (ms <= 0 || screen_all_dark(server)) {
		return 0;
	}
	double idle = real_sec() - server->idle_at;
	if (idle * 1000.0 < ms - 5) {
		/* touched since the timer was set: set it again from then */
		idle_rearm(server);
		return 0;
	}
	if (idle_held(server)) {
		/* a film playing is somebody watching it */
		server->idle_at = real_sec();
		idle_rearm(server);
		return 0;
	}
	screen_go_dark(server);
	return 0;
}

/* Somebody at the desk: a key, the pointer, the screen touched. Counts
 * against the screen going dark, and programs that ask are told. When it
 * had gone dark, this brings it back, and a press does nothing else: false,
 * and the caller drops it -- this one, and any other press until the screen
 * has had time to be seen (a monitor takes a moment to light, and a hand
 * that nudged the mouse to wake it is often already clicking). The pointer
 * moving goes where it goes either way. */
static bool input_seen(struct desk_server *server, const char *what)
{
	double now = real_sec();
	server->idle_at = now;
	if (server->idle_notifier) {
		wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
	}
	if (server->dark) {
		wlr_log(WLR_INFO, "screen: woken by %s", what);
		screen_wake(server);
		server->wake_at = now;
		return false;
	}
	return now - server->wake_at >= WAKE_BLIND_S;
}

/* The touch screen: nothing on the desk takes a finger yet, but one on the
 * glass is somebody there. */
static void server_touch_down(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, touch_down);
	(void)data;
	input_seen(server, "a touch");
}

static void server_touch_motion(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, touch_motion);
	(void)data;
	input_seen(server, "a touch");
}

/* wlopm, or swayidle through it: a screen off, or on again. */
static void power_set_mode(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, power_set_mode);
	const struct wlr_output_power_v1_set_mode_event *ev = data;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (o->wlr_output != ev->output) {
			continue;
		}
		bool off = ev->mode == ZWLR_OUTPUT_POWER_V1_MODE_OFF;
		wlr_log(WLR_INFO, "screen: %s turned %s by a program", ev->output->name,
			off ? "off" : "on");
		output_set_dark(o, off);
		screen_dark_sync(server);
		if (!off) {
			server->idle_at = real_sec();
			idle_rearm(server);
			server_schedule_frames(server);
		}
		return;
	}
}

/* ---- the screens, as display tools see them ----------------------------
 *
 * wlr-randr, kanshi, wdisplays (wlr-output-management): each screen's
 * modes and the one it is in, where it is, which way up, whether it is on,
 * and its scale -- which on DEsk is the size of a point, the same as
 * shell.conf's scale, and is written there: the desk is laid out in the
 * screen's own pixels, and a program's window is the size it draws itself
 * either way. A screen's own scale stays 1. What is asked is tried on the
 * backend first and then done all at once; the desk is then set out again
 * for the new size or point as for a new screen. Screens are placed with
 * the desk's own at the corner it is drawn from, and one at least stays
 * on: with every screen off there would be nothing to turn one on again
 * from. What a tool has done is kept, and done again the next time the
 * same screens are there (displays.conf, below). */

static struct desk_output *desk_output_of(struct desk_server *server, struct wlr_output *out)
{
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (o->wlr_output == out) {
			return o;
		}
	}
	return NULL;
}

static void om_publish(struct desk_server *server)
{
	if (!server->om || server->quitting) {
		return;
	}
	struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
	if (!config) {
		return;
	}
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		struct wlr_output_configuration_head_v1 *head =
			wlr_output_configuration_head_v1_create(config, o->wlr_output);
		if (!head) {
			continue;
		}
		/* dark is not off: it is still there, in the mode it was in */
		head->state.enabled = !o->off;
		if (o->dark && !output_is_window(o->wlr_output)) {
			head->state.mode = o->dark_mode;
			head->state.custom_mode.width = o->dark_w;
			head->state.custom_mode.height = o->dark_h;
			head->state.custom_mode.refresh = o->dark_refresh;
		}
		head->state.scale = (float)desk_ui_scale();
		struct wlr_box box;
		wlr_output_layout_get_box(server->output_layout, o->wlr_output, &box);
		if (!wlr_box_empty(&box)) {
			head->state.x = box.x;
			head->state.y = box.y;
		}
	}
	wlr_output_manager_v1_set_configuration(server->om, config);
}

/* The head for the screen the desk will be on: the one put at the corner,
 * or else the one furthest left (and then up) of those left on. */
static struct wlr_output_configuration_head_v1 *om_desk_head(
	struct wlr_output_configuration_v1 *config)
{
	struct wlr_output_configuration_head_v1 *head, *best = NULL;
	wl_list_for_each(head, &config->heads, link) {
		const struct wlr_output_head_v1_state *s = &head->state;
		if (!s->enabled) {
			continue;
		}
		if (s->x == 0 && s->y == 0) {
			return head;
		}
		if (!best || s->x < best->state.x
			|| (s->x == best->state.x && s->y < best->state.y)) {
			best = head;
		}
	}
	return best;
}

static void om_log(const struct wlr_output_configuration_v1 *config, const char *who,
	const char *what, bool ok)
{
	const struct wlr_output_configuration_head_v1 *head;
	wl_list_for_each(head, &config->heads, link) {
		const struct wlr_output_head_v1_state *s = &head->state;
		if (!s->enabled) {
			wlr_log(WLR_INFO, "%s: %s %s: off: %s", who, what, s->output->name,
				ok ? "done" : "refused");
			continue;
		}
		int w = s->mode ? s->mode->width : s->custom_mode.width;
		int h = s->mode ? s->mode->height : s->custom_mode.height;
		int r = s->mode ? s->mode->refresh : s->custom_mode.refresh;
		wlr_log(WLR_INFO, "%s: %s %s: %dx%d@%.3f Hz at %d,%d, scale %.2f, "
			"transform %d: %s", who, what, s->output->name, w, h, r / 1000.0, s->x, s->y,
			s->scale, (int)s->transform, ok ? "done" : "refused");
	}
}

/* Tried, and done if it is to be (apply): what a display tool asks, or
 * what displays.conf has kept (who says which, for the log). */
static bool om_try(struct desk_server *server, struct wlr_output_configuration_v1 *config,
	bool apply, const char *who)
{
	const char *what = apply ? "apply" : "test";
	struct wlr_output_configuration_head_v1 *desk = om_desk_head(config);
	if (!desk) {
		wlr_log(WLR_INFO, "%s: %s refused: it would leave no screen on", who, what);
		return false;
	}
	double scale = desk->state.scale;
	if (scale < 0.5 || scale > 4.0) {
		wlr_log(WLR_INFO, "%s: %s refused: the desk is drawn for a point of "
			"0.5 to 4 pixels, not %.2f", who, what, scale);
		return false;
	}
	const char *env = getenv("DESK_SCALE");
	if (env && *env && atof(env) > 0.0 && fabs(scale - desk_ui_scale()) > 1e-3) {
		wlr_log(WLR_INFO, "%s: %s refused: DESK_SCALE holds the size of a point",
			who, what);
		return false;
	}
	size_t n = 0;
	struct wlr_backend_output_state *states =
		wlr_output_configuration_v1_build_state(config, &n);
	if (!states) {
		return false;
	}
	for (size_t i = 0; i < n; i++) {
		/* the scale is the desk's point, not the screen's */
		states[i].base.committed &= ~(uint32_t)WLR_OUTPUT_STATE_SCALE;
	}
	if (apply && server->dark) {
		/* a screen set up anew is somebody at it */
		screen_wake(server);
	}
	bool ok = wlr_backend_test(server->backend, states, n);
	if (ok && apply) {
		ok = wlr_backend_commit(server->backend, states, n);
	}
	for (size_t i = 0; i < n; i++) {
		wlr_output_state_finish(&states[i].base);
	}
	free(states);
	om_log(config, who, what, ok);
	if (!ok || !apply) {
		return ok;
	}

	int dx = desk->state.x, dy = desk->state.y;
	struct wlr_output_configuration_head_v1 *head;
	wl_list_for_each(head, &config->heads, link) {
		struct desk_output *o = desk_output_of(server, head->state.output);
		if (!o) {
			continue;
		}
		struct wlr_output *out = o->wlr_output;
		if (!head->state.enabled) {
			o->off = true;
			wlr_output_layout_remove(server->output_layout, out);
			continue;
		}
		o->off = false;
		bool was = wlr_output_layout_get(server->output_layout, out) != NULL;
		struct wlr_output_layout_output *lo = wlr_output_layout_add(server->output_layout,
			out, head->state.x - dx, head->state.y - dy);
		if (!was && lo) {
			/* back on: its piece of the scene, which went with it */
			struct wlr_scene_output *so =
				wlr_scene_get_scene_output(server->scene, out);
			if (!so) {
				so = wlr_scene_output_create(server->scene, out);
			}
			if (so) {
				wlr_scene_output_layout_add_output(server->scene_layout, lo, so);
			}
		}
		wlr_output_schedule_frame(out);
	}

	/* and the desk set out again, for its screen's new size or point --
	 * a new point being Shell Settings' scale from now on */
	if (fabs(scale - desk_ui_scale()) > 1e-3) {
		shell_conf_put_scale(server, scale);
	}
	struct wlr_output *out = first_output(server);
	double point = ui_scale_for(server, out);
	bool rescale = fabs(point - desk_ui_scale()) > 1e-3;
	if (rescale) {
		wlr_log(WLR_INFO, "a point is %.2f pixels now, as a display tool asked", point);
		desk_set_ui_scale(point);
	}
	int w, h;
	wlr_output_effective_resolution(out, &w, &h);
	if (w != server->desk_w || h != server->desk_h) {
		desk_resize_to(server, w, h);
	} else if (rescale) {
		desk_relayout(server);
	}
	return true;
}

static void displays_keep(struct desk_server *server);

static void om_apply_cb(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, om_apply);
	struct wlr_output_configuration_v1 *config = data;
	if (om_try(server, config, true, "display tool")) {
		wlr_output_configuration_v1_send_succeeded(config);
		/* and kept, for the next time these screens are there */
		displays_keep(server);
	} else {
		wlr_output_configuration_v1_send_failed(config);
	}
	wlr_output_configuration_v1_destroy(config);
	/* what the screens are now, whichever it was */
	om_publish(server);
}

static void om_test_cb(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, om_test);
	struct wlr_output_configuration_v1 *config = data;
	if (om_try(server, config, false, "display tool")) {
		wlr_output_configuration_v1_send_succeeded(config);
	} else {
		wlr_output_configuration_v1_send_failed(config);
	}
	wlr_output_configuration_v1_destroy(config);
}

/* ---- the screens as they were left -----------------------------------
 *
 * $XDG_CONFIG_HOME/desk/displays.conf: how a display tool last set out
 * each set of screens that have been plugged in together -- each screen's
 * mode, where it is, which way up, whether it is on -- written when a tool
 * has done something, and done again whenever the same screens are there:
 * at the start, and when one is plugged in or taken away. A screen is
 * known by its maker, model and serial number, or by the connector it is
 * on where it gives no serial number (or two give the same one). The size
 * of a point is not kept here: it is shell.conf's scale. Only real screens
 * are set out from it -- a window on another desktop is that desktop's to
 * size, and is not kept either -- and not when the command line has said
 * what size the screen is to be (--size). */

#define DISP_SCREENS 8    /* in a set, at most */
#define DISP_SETS 16      /* sets kept, the newest last */

struct disp_screen {
	char id[192];
	struct wlr_output *out;     /* the screen it is now, when it is there */
	bool enabled;
	bool have_mode, custom;
	int w, h, refresh;          /* refresh in mHz; 0 for whatever it does */
	bool have_pos;
	int x, y;                   /* the desk's screen at 0,0 */
	int transform;              /* enum wl_output_transform */
};

struct disp_set {
	int n;
	struct disp_screen s[DISP_SCREENS];
};

/* as wlr-randr has them */
static const char *const DISP_TRANSFORM[] = { "normal", "90", "180", "270", "flipped",
	"flipped-90", "flipped-180", "flipped-270" };

static void displays_path(char *out, size_t n)
{
	const char *cfg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (cfg && *cfg) {
		snprintf(out, n, "%s/desk/displays.conf", cfg);
	} else if (home && *home) {
		snprintf(out, n, "%s/.config/desk/displays.conf", home);
	} else {
		out[0] = '\0';
	}
}

static bool str_same(const char *a, const char *b)
{
	return !strcmp(a ? a : "", b ? b : "");
}

/* What a screen is known by (see above). */
static void disp_name(struct desk_server *server, struct wlr_output *out, char *id, size_t n)
{
	bool own = out->serial && *out->serial;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		const struct wlr_output *other = o->wlr_output;
		if (own && other != out && str_same(other->serial, out->serial)
			&& str_same(other->make, out->make) && str_same(other->model, out->model)) {
			own = false;
		}
	}
	if (own) {
		snprintf(id, n, "%s / %s / %s", out->make ? out->make : "",
			out->model ? out->model : "", out->serial);
	} else {
		snprintf(id, n, "%s", out->name);
	}
	/* a line of the file each */
	for (char *p = id; *p; p++) {
		if ((unsigned char)*p < ' ') {
			*p = ' ';
		}
	}
}

static int disp_by_name(const void *a, const void *b)
{
	return strcmp(((const struct disp_screen *)a)->id, ((const struct disp_screen *)b)->id);
}

/* The screens there are now, as they are set out, in the order of their
 * names. False when they are not to be kept or set out from the file: one
 * of them a window on another desktop, or more of them than a set holds. */
static bool displays_now(struct desk_server *server, struct disp_set *set)
{
	set->n = 0;
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		struct wlr_output *out = o->wlr_output;
		if (output_is_window(out) || set->n == DISP_SCREENS) {
			return false;
		}
		struct disp_screen *d = &set->s[set->n++];
		memset(d, 0, sizeof *d);
		disp_name(server, out, d->id, sizeof d->id);
		d->out = out;
		d->enabled = !o->off;
		if (!d->enabled) {
			continue;
		}
		/* dark is not off: it is in the mode it was in */
		const struct wlr_output_mode *mode = o->dark ? o->dark_mode : out->current_mode;
		d->have_mode = true;
		d->custom = mode == NULL;
		d->w = mode ? mode->width : o->dark ? o->dark_w : out->width;
		d->h = mode ? mode->height : o->dark ? o->dark_h : out->height;
		d->refresh = mode ? mode->refresh : o->dark ? o->dark_refresh : out->refresh;
		struct wlr_box box;
		wlr_output_layout_get_box(server->output_layout, out, &box);
		d->have_pos = !wlr_box_empty(&box);
		d->x = box.x;
		d->y = box.y;
		d->transform = (int)out->transform;
	}
	qsort(set->s, (size_t)set->n, sizeof set->s[0], disp_by_name);
	return set->n > 0;
}

static const struct disp_screen *disp_find(const struct disp_set *set, const char *id)
{
	for (int i = 0; i < set->n; i++) {
		if (!strcmp(set->s[i].id, id)) {
			return &set->s[i];
		}
	}
	return NULL;
}

/* The same screens, in whatever order? */
static bool disp_same_set(const struct disp_set *a, const struct disp_set *b)
{
	if (a->n != b->n) {
		return false;
	}
	bool used[DISP_SCREENS] = { false };
	for (int i = 0; i < a->n; i++) {
		int k = 0;
		while (k < b->n && (used[k] || strcmp(a->s[i].id, b->s[k].id))) {
			k++;
		}
		if (k == b->n) {
			return false;
		}
		used[k] = true;
	}
	return true;
}

/* Every set the file keeps, up to max, in its order; how many. */
static int displays_read(struct disp_set *sets, int max)
{
	char path[512];
	displays_path(path, sizeof path);
	FILE *f = path[0] ? fopen(path, "re") : NULL;
	if (!f) {
		return 0;
	}
	int n = 0;
	struct disp_set *set = NULL;
	struct disp_screen *d = NULL;
	char line[512];
	while (fgets(line, sizeof line, f)) {
		char *s = conf_trim(line);
		char *eq = *s == '#' ? NULL : strchr(s, '=');
		if (!eq) {
			continue;
		}
		*eq = '\0';
		char *key = conf_trim(s), *val = conf_trim(eq + 1);
		if (!strcmp(key, "set")) {
			if (n == max) {
				break;
			}
			set = &sets[n++];
			set->n = 0;
			d = NULL;
		} else if (!set) {
			continue;
		} else if (!strcmp(key, "screen")) {
			d = set->n < DISP_SCREENS ? &set->s[set->n++] : NULL;
			if (d) {
				memset(d, 0, sizeof *d);
				d->enabled = true;
				snprintf(d->id, sizeof d->id, "%s", val);
			}
		} else if (!d) {
			continue;
		} else if (!strcmp(key, "enabled")) {
			conf_bool(val, &d->enabled);
		} else if (!strcmp(key, "mode")) {
			double hz = 0.0;
			int got = sscanf(val, "%dx%d@%lf", &d->w, &d->h, &hz);
			d->have_mode = got >= 2 && d->w > 0 && d->h > 0;
			d->refresh = got == 3 && hz > 0.0 ? (int)lround(hz * 1000.0) : 0;
			d->custom = strstr(val, "custom") != NULL;
		} else if (!strcmp(key, "position")) {
			d->have_pos = sscanf(val, "%d,%d", &d->x, &d->y) == 2;
		} else if (!strcmp(key, "transform")) {
			for (int i = 0; i < 8; i++) {
				if (!strcasecmp(val, DISP_TRANSFORM[i])) {
					d->transform = i;
				}
			}
		}
	}
	fclose(f);
	return n;
}

static void disp_write_set(FILE *f, const struct disp_set *set)
{
	fputs("\nset =", f);
	for (int i = 0; i < set->n; i++) {
		fprintf(f, "%s %s", i ? " +" : "", set->s[i].id);
	}
	fputc('\n', f);
	for (int i = 0; i < set->n; i++) {
		const struct disp_screen *d = &set->s[i];
		fprintf(f, "screen = %s\nenabled = %s\n", d->id, d->enabled ? "yes" : "no");
		if (d->have_mode) {
			fprintf(f, "mode = %dx%d@%.3f%s\n", d->w, d->h, d->refresh / 1000.0,
				d->custom ? " custom" : "");
		}
		if (d->have_pos) {
			fprintf(f, "position = %d,%d\n", d->x, d->y);
		}
		if (d->enabled) {
			fprintf(f, "transform = %s\n", DISP_TRANSFORM[d->transform & 7]);
		}
	}
}

/* After a display tool has done something: how the screens are set out
 * now, kept for this set of them in place of what was kept for it. */
static void displays_keep(struct desk_server *server)
{
	struct disp_set now;
	char path[512];
	displays_path(path, sizeof path);
	if (!path[0] || !displays_now(server, &now)) {
		return;
	}
	struct disp_set *sets = calloc(DISP_SETS, sizeof *sets);
	if (!sets) {
		return;
	}
	int n = displays_read(sets, DISP_SETS);
	conf_dirs_for(path);
	char tmp[600];
	snprintf(tmp, sizeof tmp, "%s.new", path);
	FILE *f = fopen(tmp, "we");
	if (!f) {
		wlr_log(WLR_ERROR, "cannot keep how the screens are set out in %s", tmp);
		free(sets);
		return;
	}
	fputs("# DEsk's screens, as a display tool last set them out (wdisplays, Xfce's\n"
		"# Display settings, wlr-randr), for each set of screens plugged in together.\n"
		"# DEsk writes this when a tool has changed something, and sets the screens\n"
		"# out by it again whenever the same ones are there. The size of a point,\n"
		"# which the tools call the scale, is shell.conf's scale.\n"
		"#\n"
		"#   set = <screen> + <screen> ...     a set, and then each screen in it:\n"
		"#   screen = <make> / <model> / <serial>, or the connector it is on (DP-1)\n"
		"#   enabled = yes | no\n"
		"#   mode = <width>x<height>@<Hz> [custom]\n"
		"#   position = <x>,<y>                in pixels, the desk's screen at 0,0\n"
		"#   transform = normal | 90 | 180 | 270 | flipped | flipped-90 | flipped-180\n"
		"#               | flipped-270\n", f);
	/* the other sets as they were, the oldest let go once there are too
	 * many, and this one last */
	int others = 0;
	for (int i = 0; i < n; i++) {
		others += !disp_same_set(&sets[i], &now);
	}
	int drop = others - (DISP_SETS - 1);
	for (int i = 0; i < n; i++) {
		if (disp_same_set(&sets[i], &now)) {
			continue;
		}
		if (drop > 0) {
			drop--;
			continue;
		}
		disp_write_set(f, &sets[i]);
	}
	disp_write_set(f, &now);
	free(sets);
	bool ok = fflush(f) == 0 && !ferror(f);
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp, path) != 0) {
		wlr_log(WLR_ERROR, "cannot keep how the screens are set out in %s", path);
		unlink(tmp);
		return;
	}
	wlr_log(WLR_INFO, "displays.conf: kept for this set of %d screen%s", now.n,
		now.n == 1 ? "" : "s");
}

/* The screen's own mode for what was kept: the same size, and the refresh
 * nearest to it. None if it has none that size. */
static struct wlr_output_mode *disp_mode(struct wlr_output *out, const struct disp_screen *d)
{
	struct wlr_output_mode *m, *best = NULL;
	wl_list_for_each(m, &out->modes, link) {
		if (m->width == d->w && m->height == d->h && (!best
				|| abs(m->refresh - d->refresh) < abs(best->refresh - d->refresh))) {
			best = m;
		}
	}
	return best;
}

/* The screens set out as displays.conf keeps them for this set of them,
 * if it keeps them and they are not so already. */
static void displays_put_back(struct desk_server *server)
{
	struct disp_set now;
	if (server->quitting || server->size_set || !displays_now(server, &now)) {
		return;
	}
	struct disp_set *sets = calloc(DISP_SETS, sizeof *sets);
	if (!sets) {
		return;
	}
	int n = displays_read(sets, DISP_SETS);
	const struct disp_set *kept = NULL;
	for (int i = n - 1; i >= 0 && !kept; i--) {
		if (disp_same_set(&sets[i], &now)) {
			kept = &sets[i];
		}
	}
	struct wlr_output_configuration_v1 *config = kept ? wlr_output_configuration_v1_create()
		: NULL;
	bool differ = false;
	for (int i = 0; config && i < now.n; i++) {
		const struct disp_screen *is = &now.s[i];
		const struct disp_screen *was = disp_find(kept, is->id);
		struct wlr_output_configuration_head_v1 *head =
			wlr_output_configuration_head_v1_create(config, is->out);
		if (!was || !head) {
			wlr_output_configuration_v1_destroy(config);
			config = NULL;
			break;
		}
		struct wlr_output_head_v1_state *st = &head->state;
		st->enabled = was->enabled;
		st->scale = (float)desk_ui_scale();
		differ |= was->enabled != is->enabled;
		if (!was->enabled) {
			continue;
		}
		if (was->have_mode) {
			struct wlr_output_mode *mode = was->custom ? NULL : disp_mode(is->out, was);
			int w = mode ? mode->width : was->w, h = mode ? mode->height : was->h;
			int r = mode ? mode->refresh : was->refresh;
			st->mode = mode;
			st->custom_mode.width = w;
			st->custom_mode.height = h;
			st->custom_mode.refresh = r;
			differ |= !is->have_mode || is->w != w || is->h != h
				|| (r != 0 && is->refresh != r) || is->custom != (mode == NULL);
		}
		st->x = was->have_pos ? was->x : is->x;
		st->y = was->have_pos ? was->y : is->y;
		differ |= st->x != is->x || st->y != is->y || !is->have_pos;
		st->transform = (enum wl_output_transform)was->transform;
		differ |= was->transform != is->transform;
	}
	free(sets);
	if (!config) {
		return;
	}
	if (differ) {
		wlr_log(WLR_INFO, "displays.conf: this set of %d screen%s set out as kept",
			now.n, now.n == 1 ? "" : "s");
		om_try(server, config, true, "displays.conf");
		om_publish(server);
	}
	wlr_output_configuration_v1_destroy(config);
}

static void displays_idle_cb(void *data)
{
	struct desk_server *server = data;
	server->disp_idle = NULL;
	displays_put_back(server);
}

/* The screens have changed -- the first of them have come, one has been
 * plugged in or taken away: once those coming at the same time have all
 * come, they are set out as they were kept. */
static void displays_changed(struct desk_server *server)
{
	if (server->disp_idle || server->quitting) {
		return;
	}
	server->disp_idle = wl_event_loop_add_idle(wl_display_get_event_loop(server->wl_display),
		displays_idle_cb, server);
}

/* ---- a day kept: the shows ----
 *
 * On a birthday, at Christmas and over the New Year, coming in at the
 * front door -- and starting the desk up -- the lamp is brought down a
 * little and the clock says so, and does a little more: plays the birthday
 * tune on its buzzer in time with a metronome, Jingle Bells on bells, and
 * at the New Year a few fireworks far off. At the stroke of the New Year,
 * wherever you are, fireworks: their whistles and bangs, and their light
 * coming in. About twenty seconds; Esc stops it. The tunes are old enough
 * to be anybody's. */
struct show_note {
	double at;         /* seconds into it */
	int snd;
	double semis;      /* above the sound's own note */
	double gain, pan;
};
struct show_burst {
	double at;         /* when it bursts */
	double x;          /* where across the sky, 0..1 */
	double r, g, b;
	double power;
};
#define SHOW_NOTES 192
#define SHOW_BURSTS 24
#define SHOW_S 20.0          /* how long */
#define SHOW_DIM 0.55        /* the lamp, while it is on */
static struct show_note g_show_notes[SHOW_NOTES];
static int g_show_n;
static struct show_burst g_show_bursts[SHOW_BURSTS];
static int g_show_nb;
static uint64_t g_show_dice = 0x9E3779B97F4A7C15ull;

static double show_dice(void)
{
	g_show_dice ^= g_show_dice << 13;
	g_show_dice ^= g_show_dice >> 7;
	g_show_dice ^= g_show_dice << 17;
	return (double)(g_show_dice >> 11) * (1.0 / 9007199254740992.0);
}

static void show_note(double at, int snd, double semis, double gain, double pan)
{
	if (g_show_n < SHOW_NOTES) {
		g_show_notes[g_show_n++] = (struct show_note){ at, snd, semis, gain, pan };
	}
}

/* Happy Birthday to You, on the clock's buzzer, and the metronome under it:
 * a bar of it alone to count in, the first of each bar the louder */
static void score_birthday(void)
{
	const double beat = DESK_SHOW_BEAT;
	static const struct { double at, semis; } M[] = {
		{ 2.0, -5 }, { 2.75, -5 }, { 3, -3 }, { 4, -5 }, { 5, 0 }, { 6, -1 },
		{ 8.0, -5 }, { 8.75, -5 }, { 9, -3 }, { 10, -5 }, { 11, 2 }, { 12, 0 },
		{ 14.0, -5 }, { 14.75, -5 }, { 15, 7 }, { 16, 4 }, { 17, 0 }, { 18, -1 }, { 19, -3 },
		{ 20.0, 5 }, { 20.75, 5 }, { 21, 4 }, { 22, 0 }, { 23, 2 }, { 24, 0 },
	};
	for (int i = 0; i <= 26; i++) {
		bool down = i % 3 == 0;
		show_note(i * beat, down ? DESK_SND_TOCK : DESK_SND_TICK, 0, down ? 0.55 : 0.40, 0.3);
	}
	for (size_t i = 0; i < sizeof M / sizeof M[0]; i++) {
		show_note(M[i].at * beat, DESK_SND_BEEP, M[i].semis, 0.85, -0.1);
	}
	static const double F[4] = { 0, 4, 7, 12 };
	for (int i = 0; i < 4; i++) {
		show_note(27.0 * beat + i * 0.10, DESK_SND_BEEP, F[i], 0.65, -0.1);
	}
}

/* Jingle Bells, the chorus with its last line, on bells, the sleigh bells
 * shaken on the beat */
static void score_xmas(void)
{
	const double beat = 0.44;
	static const struct { double at, semis; } M[] = {
		{ 0, 4 }, { 1, 4 }, { 2, 4 }, { 4, 4 }, { 5, 4 }, { 6, 4 },
		{ 8, 4 }, { 9, 7 }, { 10, 0 }, { 11.5, 2 }, { 12, 4 },
		{ 16, 5 }, { 17, 5 }, { 18, 5 }, { 19.5, 5 }, { 20, 5 }, { 21, 4 }, { 22, 4 },
		{ 23, 4 }, { 23.5, 4 }, { 24, 7 }, { 25, 7 }, { 26, 5 }, { 27, 2 }, { 28, 0 },
	};
	for (size_t i = 0; i < sizeof M / sizeof M[0]; i++) {
		show_note(M[i].at * beat, DESK_SND_BELL, M[i].semis - 12, 0.75, -0.05);
	}
	/* the last chord, and the bells shaken through it all */
	static const double C[3] = { 4, 7, 12 };
	for (int i = 0; i < 3; i++) {
		show_note(28.0 * beat + 0.02 * i, DESK_SND_BELL, C[i] - 12, 0.45, 0.1 * i);
	}
	for (int i = 0; i < 31; i++) {
		show_note(i * beat + 0.01, DESK_SND_JINGLE, 0, i % 2 ? 0.30 : 0.45, 0.35);
	}
}

static void show_burst(double at, double x, double power)
{
	static const double PAL[6][3] = { { 1.0, 0.72, 0.30 }, { 1.0, 0.32, 0.26 },
		{ 0.38, 1.0, 0.48 }, { 0.42, 0.58, 1.0 }, { 0.92, 0.46, 1.0 }, { 1.0, 0.98, 0.92 } };
	if (g_show_nb >= SHOW_BURSTS) {
		return;
	}
	const double *c = PAL[(int)(show_dice() * 6.0) % 6];
	g_show_bursts[g_show_nb++] = (struct show_burst){ at, x, c[0], c[1], c[2], power };
}

/* a firework: up with a whistle, the bang, its stars crackling down */
static void firework(double at, double power, bool whistle)
{
	double pan = show_dice() * 1.6 - 0.8;
	double x = 0.5 + pan * 0.55;
	if (whistle) {
		show_note(at - 1.15, DESK_SND_FW_WHISTLE, show_dice() * 2.0 - 1.0, 0.45 * power, pan);
	}
	show_note(at, DESK_SND_FW_BOOM, show_dice() * 2.0 - 1.5, 0.95 * power, pan);
	if (show_dice() < 0.75) {
		show_note(at + 0.22, DESK_SND_FW_CRACKLE, show_dice() * 2.0, 0.40 * power, pan);
	}
	show_burst(at, x, power);
}

/* a party: the poppers going off in the corners */
static void party(void)
{
	show_note(0.10, DESK_SND_POPPER, 0.0, 0.75, -0.8);
	show_note(0.32, DESK_SND_POPPER, 0.6, 0.75, 0.8);
}

/* the New Year, coming in: a party, bells up an arpeggio, and fireworks
 * far off */
static void score_newyear(void)
{
	party();
	static const double A[4] = { 0, 4, 7, 12 };
	for (int i = 0; i < 4; i++) {
		show_note(2.3 + i * 0.22, DESK_SND_BELL, A[i] - 12, 0.6, 0.0);
	}
	static const double T[5] = { 4.2, 7.2, 9.9, 12.9, 15.8 };
	for (int i = 0; i < 5; i++) {
		firework(T[i], 0.45, false);
	}
}

/* the stroke of the New Year: a party, and the sky full of fireworks */
static void score_midnight(void)
{
	party();
	static const double T[] = { 2.4, 3.6, 4.3, 6.1, 7.6, 8.3, 10.1, 11.7, 12.5, 14.4, 15.1,
		15.6, 16.8 };
	for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
		firework(T[i], i % 4 == 3 ? 0.6 : 1.0, true);
	}
}

static void fw_paint(struct desk_server *server, double t);
static void confetti_paint(struct desk_server *server, double t);

static void show_stop(struct desk_server *server, bool skipped)
{
	if (!server->show) {
		return;
	}
	for (int i = 0; i < server->show_nh; i++) {
		desk_sound_cancel(server->show_handles[i]);
		desk_sound_stop(server->show_handles[i], skipped ? 0.25 : 0.8);
	}
	server->show_nh = 0;
	server->show = DESK_SHOW_NONE;
	if (!server->boot_on) {
		show_paint_night(server, 1.0);
	}
	if (server->fw_node) {
		wlr_scene_node_set_enabled(&server->fw_node->node, false);
	}
	if (server->confetti_node) {
		wlr_scene_node_set_enabled(&server->confetti_node->node, false);
	}
	clock_publish(server);
	server_schedule_frames(server);
	wlr_log(WLR_INFO, "show: %s", skipped ? "stopped" : "over");
}

static void show_start(struct desk_server *server, int kind)
{
	if (server->show) {
		show_stop(server, true);
	}
	g_show_n = g_show_nb = 0;
	g_show_dice ^= (uint64_t)time(NULL) * 0x2545F4914F6CDD1Dull;
	switch (kind) {
	case DESK_SHOW_BIRTHDAY:
		score_birthday();
		break;
	case DESK_SHOW_XMAS:
		score_xmas();
		break;
	case DESK_SHOW_NEWYEAR:
		score_newyear();
		break;
	default:
		score_midnight();
		break;
	}
	/* in the order they are to be asked for */
	for (int i = 1; i < g_show_n; i++) {
		struct show_note n = g_show_notes[i];
		int j = i - 1;
		while (j >= 0 && g_show_notes[j].at > n.at) {
			g_show_notes[j + 1] = g_show_notes[j];
			j--;
		}
		g_show_notes[j + 1] = n;
	}
	server->show = kind;
	/* real time: a tune keeps its own tempo, however fast things move */
	server->show_t0 = real_sec();
	server->show_next = 0;
	server->show_nh = 0;
	clock_publish(server);
	server_schedule_frames(server);
	static const char *const NAME[] = { "", "birthday", "christmas", "new year", "midnight" };
	wlr_log(WLR_INFO, "show: %s", NAME[kind]);
}

/* whatever today is being kept as, if it is */
static void show_for_today(struct desk_server *server)
{
	unsigned o = desk_occasion_today();
	int kind = o & DESK_OCC_BIRTHDAY ? DESK_SHOW_BIRTHDAY
		: o & DESK_OCC_NEWYEAR ? DESK_SHOW_NEWYEAR
		: o & DESK_OCC_XMAS ? DESK_SHOW_XMAS : DESK_SHOW_NONE;
	if (kind) {
		show_start(server, kind);
	}
}

/* Once a frame while one is on: the notes coming due asked for (a little
 * ahead, so they are heard on time), the lamp, the fireworks' light. */
static void show_tick(struct desk_server *server)
{
	if (!server->show) {
		return;
	}
	double t = real_sec() - server->show_t0;
	while (server->show_next < g_show_n && g_show_notes[server->show_next].at <= t + 0.45) {
		const struct show_note *n = &g_show_notes[server->show_next++];
		int hd = desk_sound_play_exact(n->snd, n->pan, n->gain, pow(2.0, n->semis / 12.0),
			fmax(n->at - t, 0.0));
		if (!hd) {
			continue;
		}
		if (server->show_nh == SHOW_HANDLES) {
			memmove(server->show_handles, server->show_handles + 1,
				(SHOW_HANDLES - 1) * sizeof server->show_handles[0]);
			server->show_nh--;
		}
		server->show_handles[server->show_nh++] = hd;
	}
	/* in 2D the clock and the tune only: no lamp going down over the
	 * desk, no light of the fireworks, no confetti, nothing laid over it */
	if (!locked(server) && !server->boot_on && !gfx_flat()) {
		double down = clampf_d(t / 1.4, 0.0, 1.0), up = clampf_d((SHOW_S - t) / 1.6, 0.0, 1.0);
		show_paint_night(server, 1.0 - (1.0 - SHOW_DIM) * lock_ease(fmin(down, up)));
	}
	if (g_show_nb > 0 && !gfx_flat()) {
		fw_paint(server, t);
	}
	if ((server->show == DESK_SHOW_NEWYEAR || server->show == DESK_SHOW_MIDNIGHT)
		&& !gfx_flat()) {
		confetti_paint(server, t);
	}
	if (t >= SHOW_S) {
		show_stop(server, false);
		return;
	}
	server_schedule_frames(server);
}

/* The light of the fireworks, coming in over everything -- the door too:
 * added to what is under it, not laid over it. A sheet an eighth of the
 * screen's size, stretched, the light in its colour and hardly any alpha:
 * put over the rest, it (nearly) adds. Not none at all -- pixman passes
 * over a pixel with no alpha in it without a look at its colour. */
static void fw_paint(struct desk_server *server, double t)
{
	int w = server->desk_w, h = server->desk_h;
	double I[SHOW_BURSTS], any = 0.0;
	for (int b = 0; b < g_show_nb; b++) {
		double dt = t - g_show_bursts[b].at;
		I[b] = 0.0;
		if (dt < 0.0 || dt > 3.2) {
			continue;
		}
		double rise = dt < 0.04 ? dt / 0.04 : 1.0;
		double flick = 0.82 + 0.18 * sin(dt * 47.0 + b * 1.3) * sin(dt * 31.0 + b);
		I[b] = g_show_bursts[b].power * rise * (exp(-dt * 3.2) + 0.28 * exp(-dt * 1.0)) * flick;
		any += I[b];
	}
	if (any < 0.004 || w < 8 || h < 8) {
		if (server->fw_node) {
			wlr_scene_node_set_enabled(&server->fw_node->node, false);
		}
		return;
	}
	int gw = w / 8 + 1, gh = h / 8 + 1;
	struct desk_buf *buf = buf_create(gw, gh);
	cairo_surface_flush(buf->surface);
	unsigned char *px = cairo_image_surface_get_data(buf->surface);
	int stride = cairo_image_surface_get_stride(buf->surface);
	double R2 = 0.85 * w * 0.85 * w;
	for (int y = 0; y < gh; y++) {
		uint32_t *row = (uint32_t *)(void *)(px + (size_t)y * stride);
		double sy = (y + 0.5) * 8.0 + 0.30 * h;
		for (int x = 0; x < gw; x++) {
			double sx = (x + 0.5) * 8.0, r = 0.0, g = 0.0, bl = 0.0;
			for (int b = 0; b < g_show_nb; b++) {
				if (I[b] <= 0.0) {
					continue;
				}
				double dx = sx - g_show_bursts[b].x * w;
				double f = I[b] / (1.0 + (dx * dx + sy * sy) / R2 * 1.4);
				r += g_show_bursts[b].r * f;
				g += g_show_bursts[b].g * f;
				bl += g_show_bursts[b].b * f;
			}
			/* a little: light in through a window, not a floodlight */
			int R = (int)fmin(255.0, r * 0.46 * 255.0), G = (int)fmin(255.0, g * 0.46 * 255.0);
			int B = (int)fmin(255.0, bl * 0.46 * 255.0);
			int m = R > G ? (R > B ? R : B) : (G > B ? G : B);
			uint32_t A = m > 0 ? (uint32_t)(1 + m / 6) : 0u;
			row[x] = (A << 24) | ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B;
		}
	}
	cairo_surface_mark_dirty(buf->surface);
	scene_put(&server->fw_node, server->layer_glow, buf, 0, 0, NULL);
	if (server->fw_node) {
		wlr_scene_buffer_set_dest_size(server->fw_node, w, h);
		server->fw_node->point_accepts_input = shadow_accepts;
		wlr_scene_node_set_enabled(&server->fw_node->node, true);
		wlr_scene_node_set_enabled(&server->layer_glow->node, true);
	}
}

/* the poppers' confetti, over everything while it is coming down */
static void confetti_paint(struct desk_server *server, double t)
{
	int w = server->desk_w, h = server->desk_h;
	if (t > 7.6 || w < 8 || h < 8) {
		if (server->confetti_node) {
			wlr_scene_node_set_enabled(&server->confetti_node->node, false);
		}
		return;
	}
	int ow, oh;
	desk_ambient_size(w, h, &ow, &oh);
	struct desk_buf *buf = buf_create(ow, oh);
	cairo_save(buf->cr);
	cairo_scale(buf->cr, (double)ow / w, (double)oh / h);
	bool any = desk_draw_poppers(buf->cr, w, h, t);
	cairo_restore(buf->cr);
	scene_put(&server->confetti_node, server->layer_glow, buf, 0, 0, NULL);
	if (server->confetti_node) {
		wlr_scene_buffer_set_dest_size(server->confetti_node, w, h);
		server->confetti_node->point_accepts_input = shadow_accepts;
		wlr_scene_node_raise_to_top(&server->confetti_node->node);
		wlr_scene_node_set_enabled(&server->confetti_node->node, any);
		wlr_scene_node_set_enabled(&server->layer_glow->node, true);
	}
}

/* the stroke of the New Year: looked for once a second */
static void show_midnight_check(struct desk_server *server)
{
	time_t now = desk_occasion_now();
	struct tm tm;
	localtime_r(&now, &tm);
	int ymd = (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
	int was = server->occ_ymd;
	server->occ_ymd = ymd;
	if (was && was % 10000 == 1231 && tm.tm_mon == 0 && tm.tm_mday == 1
		&& (desk_occasion_today() & DESK_OCC_NEWYEAR)) {
		show_start(server, DESK_SHOW_MIDNIGHT);
	}
	if (was && was != ymd && locked(server)) {
		/* a new day at the door: dressed for it, or not any more */
		paint_lock(server);
		server_schedule_frames(server);
	}
}

/* A picture of the screen -- or of the sheet in front, paper and all --
 * with grim, by desk-screenshot (which files it and posts a letter). */
static void screenshot_take(struct desk_server *server, bool window)
{
	char cmd[160] = "desk-screenshot";
	if (window && !wl_list_empty(&server->toplevels)) {
		struct desk_toplevel *v = wl_container_of(server->toplevels.next, v, link);
		if (v->scene_tree && v->scene_tree->node.enabled && v->outer_w > 0 && v->outer_h > 0) {
			snprintf(cmd, sizeof cmd, "desk-screenshot --geometry '%d,%d %dx%d'",
				v->scene_tree->node.x, v->scene_tree->node.y, v->outer_w, v->outer_h);
		}
	}
	wlr_log(WLR_INFO, "screenshot: %s", cmd);
	spawn(cmd);
}

/* A program asks for one of its windows to be brought to the front -- with
 * a token it got from whatever started it, or from itself. A window folded
 * away is unfolded for it, as clicking its packet would. */
static void xdg_request_activate(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, request_activate);
	const struct wlr_xdg_activation_v1_request_activate_event *ev = data;
	struct desk_toplevel *view = view_of_surface(server, ev->surface);
	if (!view || !view->scene_tree || server->lock_phase != 0) {
		return;
	}
	if (view->sheet.phase != SHEET_IDLE) {
		sheet_begin_unfold(server, &view->sheet);
	}
	focus_toplevel(view);
	server_schedule_frames(server);
}

static bool ti_mods_in(struct desk_server *server, const struct wlr_keyboard_modifiers *mods);

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data)
{
	struct desk_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
	(void)data;
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	if (ti_mods_in(keyboard->server, &keyboard->wlr_keyboard->modifiers)) {
		return;   /* behind a key the input method is answering for */
	}
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->wlr_keyboard->modifiers);
}

/* An input method's own window, by its name -- a Wayland app_id, an X
 * window's class: IBus's panel (ibus-ui-gtk3: the candidates, the
 * switcher), Mozc's renderer (its candidates where they are not IBus's),
 * fcitx's candidates. */
static bool ime_window_name(const char *name)
{
	static const char *const names[] = { "ibus-ui-gtk3", "mozc_renderer", "fcitx" };
	for (size_t i = 0; name && i < sizeof names / sizeof *names; i++) {
		if (strcasecmp(name, names[i]) == 0) {
			return true;
		}
	}
	return false;
}

/* ---- typing into programs' windows, through the input method --------
 *
 * A program on Wayland that wants an input method says so with text-input
 * (GTK, Qt), and the compositor is the go-between: the keys go to the input
 * method first -- IBus, by way of the same input context DEsk types its
 * own notes through (ime.c) -- and only what it does not keep goes on to
 * the window, in order; what it writes comes back to the window as the
 * text being written (preedit) and then the text written (commit). The
 * compositor knows where the window is, so the input method is told where
 * on the screen the cursor is, and Mozc's candidates come up under it --
 * a program talking to IBus directly can only say where in its window,
 * and they came up in the corner of the screen. And the input method's
 * mode, あ or A as 半角/全角 goes, comes back with it, for the clock. */

struct desk_ti {
	struct desk_server *server;
	struct wlr_text_input_v3 *ti;
	struct wl_list link;
	struct wl_listener enable, commit, disable, destroy;
};

enum { TIK_IDLE = 0, TIK_ASKED, TIK_ASKED_UP, TIK_DOWN, TIK_TAKEN };

static bool ti_same_client(struct wlr_text_input_v3 *ti, struct wlr_surface *s)
{
	return s && ti->resource && wl_resource_get_client(ti->resource)
		== wl_resource_get_client(s->resource);
}

static void ti_forward(struct desk_server *server, uint32_t keycode, uint32_t state)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	uint32_t ms = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
	wlr_seat_keyboard_notify_key(server->seat, ms, keycode, state);
}

static void ti_commit_text(const char *text, void *user)
{
	struct desk_ti *t = user;
	wlr_text_input_v3_send_preedit_string(t->ti, NULL, 0, 0);
	wlr_text_input_v3_send_commit_string(t->ti, text);
	wlr_text_input_v3_send_done(t->ti);
}

static void ti_preedit(const char *text, int cursor, bool visible, void *user)
{
	struct desk_ti *t = user;
	if (!visible || !text || !*text) {
		wlr_text_input_v3_send_preedit_string(t->ti, NULL, 0, 0);
	} else {
		/* the cursor comes as characters, and goes as bytes */
		int b = 0;
		for (int c = 0; text[b] && c < cursor; c++) {
			b++;
			while ((text[b] & 0xc0) == 0x80) {
				b++;
			}
		}
		wlr_text_input_v3_send_preedit_string(t->ti, text, b, b);
	}
	wlr_text_input_v3_send_done(t->ti);
}

/* what is at the front of the line goes to the window, up to the first key
 * the input method has not answered for yet */
static void ti_flush(struct desk_server *server)
{
	while (server->ti_q_n > 0) {
		struct ti_ev * e = &server->ti_q[server->ti_q_head];
		if (e->waiting) {
			break;
		}
		if (e->mods_only) {
			struct wlr_keyboard_modifiers m = e->mods;
			wlr_seat_keyboard_notify_modifiers(server->seat, &m);
		} else if (e->code < sizeof server->ti_keys) {
			uint8_t *k = &server->ti_keys[e->code];
			if (e->pressed) {
				if (e->taken) {
					*k = TIK_TAKEN;
				} else {
					ti_forward(server, e->code, WL_KEYBOARD_KEY_STATE_PRESSED);
					*k = TIK_DOWN;
				}
			} else {
				/* let go: unless the input method kept it when it went down */
				if (*k != TIK_TAKEN) {
					ti_forward(server, e->code, WL_KEYBOARD_KEY_STATE_RELEASED);
				}
				*k = TIK_IDLE;
			}
		}
		server->ti_q_head = (server->ti_q_head + 1) % 64;
		server->ti_q_n--;
	}
	if (server->ti_timer) {
		wl_event_source_timer_update(server->ti_timer, server->ti_q_n > 0 ? 800 : 0);
	}
}

static void ti_push(struct desk_server *server, bool mods_only, bool pressed, bool waiting,
	uint32_t code, const struct wlr_keyboard_modifiers *mods)
{
	if (server->ti_q_n >= 64) {
		/* a line that long is an input method gone quiet: let it all go */
		for (int i = 0; i < server->ti_q_n; i++) {
			server->ti_q[(server->ti_q_head + i) % 64].waiting = 0;
		}
		ti_flush(server);
	}
	struct ti_ev * e = &server->ti_q[(server->ti_q_head + server->ti_q_n) % 64];
	memset(e, 0, sizeof *e);
	e->mods_only = mods_only;
	e->pressed = pressed;
	e->waiting = waiting;
	e->code = code;
	if (mods) {
		e->mods = *mods;
	}
	server->ti_q_n++;
}

/* the input method has not answered in a long while: the keys go on as
 * they are */
static int ti_timeout(void *data)
{
	struct desk_server *server = data;
	for (int i = 0; i < server->ti_q_n; i++) {
		server->ti_q[(server->ti_q_head + i) % 64].waiting = 0;
	}
	ti_flush(server);
	return 0;
}

static void ti_key(uint32_t keysym, uint32_t keycode, uint32_t mods, bool taken, bool answer,
	void *user)
{
	(void)keysym;
	(void)mods;
	struct desk_ti *t = user;
	struct desk_server *server = t->server;
	if (!answer) {
		/* the input method giving a key back, to be typed as it is */
		ti_push(server, false, true, false, keycode, NULL);
		ti_push(server, false, false, false, keycode, NULL);
		ti_flush(server);
		return;
	}
	for (int i = 0; i < server->ti_q_n; i++) {
		struct ti_ev * e = &server->ti_q[(server->ti_q_head + i) % 64];
		if (e->waiting && !e->mods_only && e->pressed && e->code == keycode) {
			e->waiting = 0;
			e->taken = taken;
			break;
		}
	}
	ti_flush(server);
}

struct ti_find {
	struct wlr_surface *surface;
	int x, y;
	bool found;
};

static void ti_find_buffer(struct wlr_scene_buffer *b, int sx, int sy, void *data)
{
	struct ti_find *f = data;
	struct wlr_scene_surface *ss = f->found ? NULL : wlr_scene_surface_try_from_buffer(b);
	if (ss && ss->surface == f->surface) {
		f->x = sx;
		f->y = sy;
		f->found = true;
	}
}

static void ime_window_place(struct desk_toplevel *view);

/* where the cursor is, on the screen the input method's windows go on */
static void ti_cursor(struct desk_ti *t)
{
	struct desk_server *server = t->server;
	if (desk_ime_typing_owner() != t || !t->ti->focused_surface) {
		return;
	}
	struct ti_find f = { t->ti->focused_surface, 0, 0, false };
	wlr_scene_node_for_each_buffer(&server->scene->tree.node, ti_find_buffer, &f);
	if (!f.found) {
		return;
	}
	struct wlr_box r = t->ti->current.cursor_rectangle;
	if (!(t->ti->active_features & WLR_TEXT_INPUT_V3_FEATURE_CURSOR_RECTANGLE)) {
		r = (struct wlr_box){ 0, 0, 1, 18 };
	}
	server->ime_at = (struct wlr_box){ f.x + r.x, f.y + r.y, r.width > 0 ? r.width : 1,
		r.height > 0 ? r.height : 18 };
	desk_ime_typing_cursor(server->ime_at.x, server->ime_at.y, server->ime_at.width,
		server->ime_at.height);
	/* and what the input method has up goes with it, as it would on X */
	for (size_t i = 0; i < sizeof server->ime_win / sizeof *server->ime_win; i++) {
		struct desk_toplevel *v = server->ime_win[i];
		if (v && v->scene_tree->node.enabled) {
			ime_window_place(v);
		}
	}
}

static void ti_attach(struct desk_ti *t)
{
	struct desk_server *server = t->server;
	void *owner = desk_ime_typing_owner();
	if (owner == t) {
		return;
	}
	if (owner) {
		return;   /* DEsk's own notes have it */
	}
	const struct desk_ime_typing ty = { ti_commit_text, ti_preedit, NULL, ti_key, t };
	desk_ime_typing_begin(&ty);
	server->ti_ime = t;
	memset(server->ti_keys, 0, sizeof server->ti_keys);
	server->ti_q_n = 0;
	ti_cursor(t);
}

static void ti_detach(struct desk_server *server)
{
	if (server->ti_ime && desk_ime_typing_owner() == server->ti_ime) {
		desk_ime_typing_end();
	}
	server->ti_ime = NULL;
	server->ime_at = (struct wlr_box){ 0 };
	memset(server->ti_keys, 0, sizeof server->ti_keys);
	server->ti_q_n = 0;
	if (server->ti_timer) {
		wl_event_source_timer_update(server->ti_timer, 0);
	}
}

static void ti_on_enable(struct wl_listener *listener, void *data)
{
	(void)data;
	struct desk_ti *t = wl_container_of(listener, t, enable);
	if (t->ti->focused_surface
		&& t->ti->focused_surface == t->server->seat->keyboard_state.focused_surface) {
		ti_attach(t);
	}
}

static void ti_on_commit(struct wl_listener *listener, void *data)
{
	(void)data;
	struct desk_ti *t = wl_container_of(listener, t, commit);
	if (t->ti->current_enabled && t->ti->focused_surface
		&& t->ti->focused_surface == t->server->seat->keyboard_state.focused_surface) {
		ti_attach(t);
		ti_cursor(t);
	}
}

static void ti_on_disable(struct wl_listener *listener, void *data)
{
	(void)data;
	struct desk_ti *t = wl_container_of(listener, t, disable);
	if (t->server->ti_ime == t) {
		ti_detach(t->server);
	}
}

static void ti_on_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct desk_ti *t = wl_container_of(listener, t, destroy);
	if (t->server->ti_ime == t) {
		ti_detach(t->server);
	}
	wl_list_remove(&t->enable.link);
	wl_list_remove(&t->commit.link);
	wl_list_remove(&t->disable.link);
	wl_list_remove(&t->destroy.link);
	wl_list_remove(&t->link);
	free(t);
}

static void server_new_text_input(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, new_text_input);
	struct wlr_text_input_v3 *ti = data;
	struct desk_ti *t = calloc(1, sizeof *t);
	if (!t) {
		return;
	}
	t->server = server;
	t->ti = ti;
	t->enable.notify = ti_on_enable;
	wl_signal_add(&ti->events.enable, &t->enable);
	t->commit.notify = ti_on_commit;
	wl_signal_add(&ti->events.commit, &t->commit);
	t->disable.notify = ti_on_disable;
	wl_signal_add(&ti->events.disable, &t->disable);
	t->destroy.notify = ti_on_destroy;
	wl_signal_add(&ti->events.destroy, &t->destroy);
	wl_list_insert(&server->text_inputs, &t->link);
	struct wlr_surface *focus = server->seat->keyboard_state.focused_surface;
	if (ti_same_client(ti, focus)) {
		wlr_text_input_v3_send_enter(ti, focus);
	}
}

/* Pointer capture follows keyboard focus too, so switching apps releases a game. */
static void desk_constraint_refresh(struct desk_server *server);

/* the keyboard has gone to another surface: the text inputs follow it */
static void seat_kb_focus_change(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, kb_focus_change);
	struct wlr_seat_keyboard_focus_change_event *ev = data;
	desk_constraint_refresh(server);
	if (server->ti_ime && server->ti_ime->ti->focused_surface != ev->new_surface) {
		ti_detach(server);
	}
	struct desk_ti *t;
	wl_list_for_each(t, &server->text_inputs, link) {
		if (t->ti->focused_surface && t->ti->focused_surface != ev->new_surface) {
			wlr_text_input_v3_send_leave(t->ti);
		}
		if (!t->ti->focused_surface && ti_same_client(t->ti, ev->new_surface)) {
			wlr_text_input_v3_send_enter(t->ti, ev->new_surface);
		}
	}
}

/* A key for the window in front: to the input method first, if the window
 * is typing through one. True if it is in hand (the window gets it, or
 * not, when the input method has answered). */
static bool ti_key_in(struct desk_server *server, uint32_t code, bool pressed, uint32_t sym,
	uint32_t mods)
{
	struct desk_ti *t = server->ti_ime;
	struct wlr_surface *focus = server->seat->keyboard_state.focused_surface;
	if (!t || !t->ti->current_enabled || t->ti->focused_surface != focus) {
		t = NULL;
		struct desk_ti *x;
		wl_list_for_each(x, &server->text_inputs, link) {
			if (x->ti->current_enabled && focus && x->ti->focused_surface == focus) {
				t = x;
			}
		}
		if (!t) {
			return false;
		}
	}
	if (desk_ime_typing_owner() != t) {
		if (desk_ime_typing_owner()) {
			return false;
		}
		ti_attach(t);
	}
	if (code >= sizeof server->ti_keys) {
		return false;
	}
	if (!server->ti_timer) {
		server->ti_timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
			ti_timeout, server);
	}
	if (!pressed) {
		desk_ime_typing_key(sym, code, mods, true);
		ti_push(server, false, false, false, code, NULL);
	} else {
		bool asked = desk_ime_typing_key(sym, code, mods, false);
		ti_push(server, false, true, asked, code, NULL);
	}
	ti_flush(server);
	return true;
}

/* modifiers changing: in line behind any key still with the input method */
static bool ti_mods_in(struct desk_server *server, const struct wlr_keyboard_modifiers *mods)
{
	if (server->ti_q_n == 0) {
		return false;
	}
	ti_push(server, true, false, false, 0, mods);
	ti_flush(server);
	return true;
}

/* ---- Ctrl+Alt+Del: for when something has gone wrong ----
 *
 * Twice running (each within CAD_GAP_S of the last), the desk's own parts
 * are set right again (cad_reset); three times, the session ends, as LOG
 * OUT on the Control Panel ends it: the windows asked to close first, and
 * one that will not holds it up until it is asked again. Counted as they
 * come, at the door too, and never passed on to a window; acted on once no
 * other has come for CAD_GAP_S (the third at once). desk-ctl does the same
 * from a terminal or another VT, by signal (cad_signal). */
#define CAD_GAP_S 0.8

/* Everything the desk itself has open or in hand put away, every picture it
 * keeps drawn again, the desk laid out afresh, its settings read again and
 * the keyboard and the pointer set up again. The windows are left as they
 * are, and a locked desk stays locked: only the door is drawn again. */
static void cad_reset(struct desk_server *server)
{
	wlr_log(WLR_INFO, "the desk set right again");
	show_stop(server, true);
	reset_cursor_mode(server);
	server->cc_grab = -1;
	cal_drop(server);
	memo_drop(server);
	agd_drop(server);
	binder_close(server);
	if (post_up(server)) {
		post_toggle(server);
	}
	if (console_up(server)) {
		console_toggle(server);
	}
	if (shelf_up(server)) {
		shelf_toggle(server);
	}
	drawer_move_to(server, 0.0);
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (view->scene_tree && !view->unmanaged) {
			view->shadow.w = view->shadow.h = 0;
			view_sync_shadow(view);
		}
	}
	pictures_forget(server);
	desk_lock_forget();
	/* shell.conf read again, as if it had changed */
	server->conf.loaded = false;
	shell_conf_poll(server);
	shell_apply_all(server);
	input_apply(server);
	desk_relayout(server);
	if (locked(server)) {
		paint_lock(server);
	} else if (!wl_list_empty(&server->toplevels)) {
		struct desk_toplevel *top = wl_container_of(server->toplevels.next, top, link);
		focus_toplevel(top);
	}
	desk_sound_play(DESK_SND_PAPER_SETTLE, 0.0, 0.8, 0.0);
	server_schedule_frames(server);
}

static int cad_cb(void *data)
{
	struct desk_server *server = data;
	int n = server->cad_n;
	server->cad_n = 0;
	if (n == 2) {
		wlr_log(WLR_INFO, "ctrl+alt+del twice");
		cad_reset(server);
	}
	return 0;
}

static void cad_press(struct desk_server *server)
{
	double now = real_sec();
	server->cad_n = now - server->cad_at <= CAD_GAP_S ? server->cad_n + 1 : 1;
	server->cad_at = now;
	desk_sound_play(DESK_SND_KEY_PRESS, 0.0, 0.7, 0.85 + 0.15 * server->cad_n);
	if (server->cad_n >= 3) {
		server->cad_n = 0;
		if (server->cad_timer) {
			wl_event_source_timer_update(server->cad_timer, 0);
		}
		wlr_log(WLR_INFO, "ctrl+alt+del three times: the session ends");
		end_begin(server, END_LOG_OUT);
		return;
	}
	if (!server->cad_timer) {
		server->cad_timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), cad_cb, server);
	}
	if (server->cad_timer) {
		wl_event_source_timer_update(server->cad_timer, (int)(CAD_GAP_S * 1000.0));
	}
}

/* desk-ctl's signals, USR1 for a reset and USR2 for the end of the session,
 * and TERM, HUP and INT -- logind ending the session for a restart or a
 * power-off, ^C on a desk in a window -- which end the desk the way a log
 * out finally does, at once (the session is going whatever the windows
 * say), so that what is in hand is still kept (cal_drop, memo_drop) and
 * the programs are let go in order. Written down a pipe by the handler --
 * all a handler may safely do -- and acted on by the loop. A handler does
 * not outlive an exec, so the programs started from here get the signals'
 * usual meaning back; nor is any of them blocked, which an exec would hand
 * down. */
static int g_sig_pipe[2] = { -1, -1 };
static pid_t g_sig_pid;

static void sig_note(int sig)
{
	int saved = errno;
	if (getpid() != g_sig_pid) {
		/* a child of the desk's, not yet into its program: the signal is
		 * its own, and does what it would without the handler */
		signal(sig, SIG_DFL);
		raise(sig);
		errno = saved;
		return;
	}
	unsigned char b = (unsigned char)sig;
	ssize_t r = write(g_sig_pipe[1], &b, 1);
	(void)r;
	errno = saved;
}

static int cad_signal(int fd, uint32_t mask, void *data)
{
	(void)mask;
	struct desk_server *server = data;
	unsigned char b;
	while (read(fd, &b, 1) == 1) {
		if (b == SIGUSR1) {
			wlr_log(WLR_INFO, "desk-ctl reset");
			cad_reset(server);
		} else if (b == SIGUSR2) {
			wlr_log(WLR_INFO, "desk-ctl logout: the session ends");
			end_begin(server, END_LOG_OUT);
		} else if (b == SIGTERM || b == SIGHUP || b == SIGINT) {
			wlr_log(WLR_INFO, "%s: the desk ends",
				b == SIGTERM ? "SIGTERM" : b == SIGHUP ? "SIGHUP" : "SIGINT");
			wl_display_terminate(server->wl_display);
		}
	}
	return 0;
}

static void cad_signals(struct desk_server *server)
{
	if (pipe(g_sig_pipe) != 0) {
		return;
	}
	for (int i = 0; i < 2; i++) {
		fcntl(g_sig_pipe[i], F_SETFD, FD_CLOEXEC);
		fcntl(g_sig_pipe[i], F_SETFL, fcntl(g_sig_pipe[i], F_GETFL) | O_NONBLOCK);
	}
	g_sig_pid = getpid();
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = sig_note;
	sa.sa_flags = SA_RESTART;
	sigemptyset(&sa.sa_mask);
	const int sigs[] = { SIGUSR1, SIGUSR2, SIGTERM, SIGHUP, SIGINT };
	for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++) {
		/* an ending the desk was started ignoring (nohup, a shell's
		 * background) stays ignored: that was asked for */
		struct sigaction was;
		if (sigs[i] != SIGUSR1 && sigs[i] != SIGUSR2
			&& sigaction(sigs[i], NULL, &was) == 0 && was.sa_handler == SIG_IGN) {
			continue;
		}
		sigaction(sigs[i], &sa, NULL);
	}
	wl_event_loop_add_fd(wl_display_get_event_loop(server->wl_display), g_sig_pipe[0],
		WL_EVENT_READABLE, cad_signal, server);
}

static void keyboard_handle_key(struct wl_listener *listener, void *data)
{
	struct desk_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct desk_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	struct wlr_seat *seat = server->seat;
	uint32_t keycode = event->keycode + 8;
	/* a key that wakes the screen, or comes before it can have been seen,
	 * is the waking's, down and up */
	if (!input_seen(server, "a key")) {
		server->wake_key = event->state == WL_KEYBOARD_KEY_STATE_PRESSED ? keycode : 0;
		return;
	}
	if (server->wake_key == keycode && event->state == WL_KEYBOARD_KEY_STATE_RELEASED) {
		server->wake_key = 0;
		return;
	}
	desk_sound_awake();   /* somebody at the desk: the speakers kept ready */
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);
	/* the key's first keysym (NoSymbol for a key without one): what the
	 * lock, the show, the screenshot, the brightness, volume and media keys
	 * and the VTs go by */
	xkb_keysym_t sym0 = nsyms > 0 ? syms[0] : XKB_KEY_NoSymbol;
	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	/* Ctrl+Alt+F1 and so on: another VT, whatever is going on here, the
	 * screen lock included */
	if (sym0 >= XKB_KEY_XF86Switch_VT_1 && sym0 <= XKB_KEY_XF86Switch_VT_12) {
		if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED && server->session) {
			wlr_session_change_vt(server->session,
				(unsigned)(sym0 - XKB_KEY_XF86Switch_VT_1 + 1));
		}
		return;
	}
	/* Esc stops a show, whatever has the keys -- and its letting go is the
	 * show's too */
	if (sym0 == XKB_KEY_Escape) {
		bool down = event->state == WL_KEYBOARD_KEY_STATE_PRESSED;
		if (server->show && down
			&& !(modifiers & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO))) {
			show_stop(server, true);
			server->show_esc = true;
			return;
		}
		if (server->show_esc && !down) {
			server->show_esc = false;
			return;
		}
	}
	/* Ctrl+Alt+Del: counted, at the door too, and nobody else's */
	if ((sym0 == XKB_KEY_Delete || sym0 == XKB_KEY_KP_Delete)
		&& (modifiers & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
			== (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT)) {
		if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
			cad_press(server);
		}
		return;
	}
	/* the lock takes every key, one with no keysym too: only a laptop's
	 * brightness keys work even at the door */
	if (locked(server) && sym0 != XKB_KEY_XF86MonBrightnessUp
		&& sym0 != XKB_KEY_XF86MonBrightnessDown) {
		lock_key(server, keyboard->wlr_keyboard->xkb_state, event->keycode, sym0, modifiers,
			event->state == WL_KEYBOARD_KEY_STATE_RELEASED);
		return;
	}
	/* Print takes a picture of the screen, Alt+Print of the window in front,
	 * whatever is in your hand */
	if (sym0 == XKB_KEY_Print || sym0 == XKB_KEY_Sys_Req) {
		/* (with Alt held, most keymaps call the key SysRq) */
		if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
			screenshot_take(server, (modifiers & WLR_MODIFIER_ALT) != 0);
		}
		return;
	}
	/* a laptop's brightness keys work whatever is in your hand */
	if (sym0 == XKB_KEY_XF86MonBrightnessUp || sym0 == XKB_KEY_XF86MonBrightnessDown) {
		if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
			screen_step(server, sym0 == XKB_KEY_XF86MonBrightnessUp ? 1 : -1);
		}
		return;
	}
	/* The keyboard's sound and player keys: under Wayland only the desk
	 * sees them, so they are the desk's, and do what the clock's volume
	 * and its MEDIA keys do -- the mixer and the player are borrowed. */
	if (sym0 == XKB_KEY_XF86AudioRaiseVolume || sym0 == XKB_KEY_XF86AudioLowerVolume
		|| sym0 == XKB_KEY_XF86AudioMute) {
		if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
			if (sym0 == XKB_KEY_XF86AudioMute) {
				if (desk_meters_set_muted(2)) {
					clock_publish(server);
				}
			} else {
				struct desk_meters m;
				desk_meters_read_volume(&m);
				if (m.have_volume) {
					clock_set_volume(server, m.volume
						+ (sym0 == XKB_KEY_XF86AudioRaiseVolume ? 0.05 : -0.05));
				}
			}
		}
		return;
	}
#ifdef DESK_HAVE_POST
	if (sym0 == XKB_KEY_XF86AudioPlay || sym0 == XKB_KEY_XF86AudioPause
		|| sym0 == XKB_KEY_XF86AudioNext || sym0 == XKB_KEY_XF86AudioPrev) {
		if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
			desk_media_command(sym0 == XKB_KEY_XF86AudioNext ? DESK_MEDIA_NEXT
				: sym0 == XKB_KEY_XF86AudioPrev ? DESK_MEDIA_PREV : DESK_MEDIA_PLAY_PAUSE);
		}
		return;
	}
#endif
	if (agd_has_keys(server) && shelf_up(server)) {
		/* the binder of notes is in: the keys are its */
		agd_key(server, keyboard->wlr_keyboard->xkb_state, event->keycode,
			sym0, modifiers,
			event->state == WL_KEYBOARD_KEY_STATE_RELEASED);
		return;
	}
	if (book_has_keys(server)) {
		/* the book is open: what is typed is written on its line */
		book_key(server, keyboard->wlr_keyboard->xkb_state, event->keycode,
			sym0, modifiers,
			event->state == WL_KEYBOARD_KEY_STATE_RELEASED);
		return;
	}
	if (memo_has_keys(server)) {
		/* the memo pad in your hand: what is typed is written on it */
		memo_key(server, keyboard->wlr_keyboard->xkb_state, event->keycode,
			sym0, modifiers,
			event->state == WL_KEYBOARD_KEY_STATE_RELEASED);
		return;
	}
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (int i = 0; i < nsyms; i++) {
			handled = handle_keybinding(server, syms[i], modifiers);
			if (handled) {
				break;
			}
		}
	}
	if (!handled) {
		wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
		if (ti_key_in(server, event->keycode, event->state == WL_KEYBOARD_KEY_STATE_PRESSED,
				sym0, modifiers)) {
			return;   /* the input method's first; the window's when it answers */
		}
		wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
	}
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data)
{
	struct desk_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
	(void)data;
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

/* The keyboard as the machine is set up to have it. Left to itself
 * xkbcommon takes US, and on a Japanese keyboard that is more than the
 * odd symbol in the wrong place: the keys an input method is switched
 * with -- 半角/全角, 変換, 無変換, カタカナひらがな -- come out as an
 * accent and nothing, and there is no getting from A to あ. So: what the
 * environment says (XKB_DEFAULT_*, which xkbcommon reads itself), else
 * Debian's /etc/default/keyboard, else what systemd-localed wrote for X.
 * The strings point into a buffer that outlives the call. */
static char g_kb_buf[4][128];

static void kb_value(const char *line, const char *key, char *out, size_t n)
{
	size_t k = strlen(key);
	while (*line == ' ' || *line == '\t') {
		line++;
	}
	if (strncmp(line, key, k) || line[k] != '=') {
		return;
	}
	const char *v = line + k + 1;
	size_t i = 0;
	for (; *v && *v != '\n' && i + 1 < n; v++) {
		if (*v != '"' && *v != '\'') {
			out[i++] = *v;
		}
	}
	out[i] = '\0';
}

static bool kb_names(struct xkb_rule_names *names)
{
	if (getenv("XKB_DEFAULT_LAYOUT") && *getenv("XKB_DEFAULT_LAYOUT")) {
		return false;
	}
	memset(g_kb_buf, 0, sizeof g_kb_buf);
	char line[512];
	FILE *f = fopen("/etc/default/keyboard", "re");
	if (f) {
		while (fgets(line, sizeof line, f)) {
			kb_value(line, "XKBMODEL", g_kb_buf[0], sizeof g_kb_buf[0]);
			kb_value(line, "XKBLAYOUT", g_kb_buf[1], sizeof g_kb_buf[1]);
			kb_value(line, "XKBVARIANT", g_kb_buf[2], sizeof g_kb_buf[2]);
			kb_value(line, "XKBOPTIONS", g_kb_buf[3], sizeof g_kb_buf[3]);
		}
		fclose(f);
	}
	if (!g_kb_buf[1][0] && (f = fopen("/etc/X11/xorg.conf.d/00-keyboard.conf", "re"))) {
		static const char *const OPT[4] = { "XkbModel", "XkbLayout", "XkbVariant", "XkbOptions" };
		while (fgets(line, sizeof line, f)) {
			for (int i = 0; i < 4; i++) {
				char pat[32];
				snprintf(pat, sizeof pat, "\"%s\"", OPT[i]);
				char *at = strstr(line, pat);
				if (!at) {
					continue;
				}
				char *q0 = strchr(at + strlen(pat), '"'), *q1 = q0 ? strchr(q0 + 1, '"') : NULL;
				if (q0 && q1 && (size_t)(q1 - q0 - 1) < sizeof g_kb_buf[i]) {
					memcpy(g_kb_buf[i], q0 + 1, (size_t)(q1 - q0 - 1));
					g_kb_buf[i][q1 - q0 - 1] = '\0';
				}
			}
		}
		fclose(f);
	}
	if (!g_kb_buf[1][0]) {
		return false;
	}
	memset(names, 0, sizeof *names);
	names->model = g_kb_buf[0][0] ? g_kb_buf[0] : NULL;
	names->layout = g_kb_buf[1];
	names->variant = g_kb_buf[2][0] ? g_kb_buf[2] : NULL;
	names->options = g_kb_buf[3][0] ? g_kb_buf[3] : NULL;
	return true;
}

/* The keymap the keyboards are given: the layout Shell Settings names, or
 * the machine's (kb_names), and a second one with the keys that change to
 * it. desc says which, for knowing when it has changed. NULL if even the
 * default will not compile. */
static struct xkb_keymap *keyboard_keymap(const struct shell_conf *c, char *desc, size_t n)
{
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!context) {
		return NULL;
	}
	struct xkb_rule_names names;
	bool have = kb_names(&names);
	static char layout[80], variant[80], options[300];
	if (c->kb_layout[0] || c->kb_second[0]) {
		if (!have) {
			memset(&names, 0, sizeof names);
		}
		/* the machine's layout goes on being the first unless one is named */
		const char *base = c->kb_layout[0] ? c->kb_layout : have ? names.layout : "us";
		const char *base_var = c->kb_layout[0] ? "" : have && names.variant ? names.variant : "";
		const char *sys_opt = have && names.options ? names.options : "";
		if (c->kb_second[0]) {
			snprintf(layout, sizeof layout, "%s,%s", base, c->kb_second);
			snprintf(variant, sizeof variant, "%s,", base_var);
			snprintf(options, sizeof options, "%s%s%s", sys_opt, *sys_opt ? "," : "",
				c->kb_switch);
			names.options = options;
		} else {
			snprintf(layout, sizeof layout, "%s", base);
			snprintf(variant, sizeof variant, "%s", base_var);
		}
		names.layout = layout;
		names.variant = variant[0] && strcmp(variant, ",") ? variant : NULL;
		have = true;
	}
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, have ? &names : NULL,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!keymap && have) {
		/* a layout this xkbcommon does not know: the default, rather than none */
		wlr_log(WLR_ERROR, "keyboard: no layout \"%s\" here; using the default", names.layout);
		keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
		snprintf(desc, n, "the default");
	} else if (have) {
		snprintf(desc, n, "%s%s%s%s%s (%s)", names.layout, names.variant ? " " : "",
			names.variant ? names.variant : "", names.options ? ", " : "",
			names.options ? names.options : "", names.model ? names.model : "default model");
	} else {
		snprintf(desc, n, "the default");
	}
	xkb_context_unref(context);
	return keymap;
}

static void server_new_keyboard(struct desk_server *server, struct wlr_input_device *device)
{
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);
	struct desk_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	if (!keyboard) {
		return;
	}
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;
	char desc[sizeof server->kb_applied];
	struct xkb_keymap *keymap = keyboard_keymap(&server->conf, desc, sizeof desc);
	if (keymap) {
		wlr_keyboard_set_keymap(wlr_keyboard, keymap);
		xkb_keymap_unref(keymap);
	}
	if (strcmp(desc, server->kb_applied)) {
		wlr_log(WLR_INFO, "keyboard: %s", desc);
		snprintf(server->kb_applied, sizeof server->kb_applied, "%s", desc);
	}
	wlr_keyboard_set_repeat_info(wlr_keyboard, server->conf.repeat_rate,
		server->conf.repeat_delay);
	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);
	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
	wl_list_insert(&server->keyboards, &keyboard->link);
}

/* A mouse or touchpad set up as shell.conf says. Only libinput's devices
 * can be (the nested backends' pointer is the host's to set). */
static void pointer_configure(struct desk_server *server, struct wlr_input_device *device)
{
#ifdef DESK_HAVE_LIBINPUT
	if (!wlr_input_device_is_libinput(device)) {
		return;
	}
	struct libinput_device *d = wlr_libinput_get_device_handle(device);
	if (!d) {
		return;
	}
	const struct shell_conf *c = &server->conf;
	/* a touchpad is what can be tapped */
	bool pad = libinput_device_config_tap_get_finger_count(d) > 0;
	if (pad) {
		libinput_device_config_tap_set_enabled(d,
			c->tap_click ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);
	}
	if (libinput_device_config_scroll_has_natural_scroll(d)) {
		libinput_device_config_scroll_set_natural_scroll_enabled(d, pad && c->natural_scroll);
	}
	if (libinput_device_config_accel_is_available(d)) {
		libinput_device_config_accel_set_speed(d, c->pointer_speed);
	}
	if (libinput_device_config_left_handed_is_available(d)) {
		libinput_device_config_left_handed_set(d, c->left_handed);
	}
	wlr_log(WLR_INFO, "pointer: %s (%s): %s%sspeed %.2f%s", device->name ? device->name : "?",
		pad ? "touchpad" : "mouse", pad ? (c->tap_click ? "tap to click, " : "no tapping, ") : "",
		pad && c->natural_scroll ? "natural scrolling, " : "", c->pointer_speed,
		c->left_handed ? ", left-handed" : "");
#else
	(void)server;
	(void)device;
#endif
}

static void pointer_destroy(struct wl_listener *listener, void *data)
{
	struct desk_pointer *p = wl_container_of(listener, p, destroy);
	(void)data;
	wl_list_remove(&p->link);
	wl_list_remove(&p->destroy.link);
	free(p);
}

/* A device that belongs to one screen -- a touch screen, or the pointer
 * of one window of a nested desk -- has its place on that screen, not
 * spread over all of them: once the screen it names is there. */
static void input_map_to_output(struct desk_server *server, struct wlr_input_device *device)
{
	const char *name = NULL;
	if (device->type == WLR_INPUT_DEVICE_POINTER) {
		name = wlr_pointer_from_input_device(device)->output_name;
	} else if (device->type == WLR_INPUT_DEVICE_TOUCH) {
		name = wlr_touch_from_input_device(device)->output_name;
	}
	if (!name) {
		return;
	}
	struct desk_output *o;
	wl_list_for_each(o, &server->outputs, link) {
		if (!strcmp(o->wlr_output->name, name)) {
			wlr_cursor_map_input_to_output(server->cursor, device, o->wlr_output);
			return;
		}
	}
}

static void server_new_pointer(struct desk_server *server, struct wlr_input_device *device)
{
	wlr_cursor_attach_input_device(server->cursor, device);
	input_map_to_output(server, device);
	struct desk_pointer *p = calloc(1, sizeof *p);
	if (!p) {
		return;
	}
	p->server = server;
	p->device = device;
	p->destroy.notify = pointer_destroy;
	wl_signal_add(&device->events.destroy, &p->destroy);
	wl_list_insert(&server->pointers, &p->link);
	pointer_configure(server, device);
}

/* Shell Settings changed something: the keyboards and pointers already
 * here are set up again -- a new keymap only if the layouts changed. */
static void input_apply(struct desk_server *server)
{
	char desc[sizeof server->kb_applied];
	struct xkb_keymap *keymap = NULL;
	struct desk_keyboard *kb;
	wl_list_for_each(kb, &server->keyboards, link) {
		if (!keymap) {
			keymap = keyboard_keymap(&server->conf, desc, sizeof desc);
			if (!keymap || !strcmp(desc, server->kb_applied)) {
				break;
			}
			wlr_log(WLR_INFO, "keyboard: %s", desc);
			snprintf(server->kb_applied, sizeof server->kb_applied, "%s", desc);
		}
		wlr_keyboard_set_keymap(kb->wlr_keyboard, keymap);
	}
	if (keymap) {
		xkb_keymap_unref(keymap);
	}
	wl_list_for_each(kb, &server->keyboards, link) {
		wlr_keyboard_set_repeat_info(kb->wlr_keyboard, server->conf.repeat_rate,
			server->conf.repeat_delay);
	}
	struct desk_pointer *p;
	wl_list_for_each(p, &server->pointers, link) {
		pointer_configure(server, p->device);
	}
}

static void server_new_input(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		server_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(server, device);
		break;
	case WLR_INPUT_DEVICE_TOUCH:
		/* only so a finger on the glass counts as somebody here */
		wlr_cursor_attach_input_device(server->cursor, device);
		input_map_to_output(server, device);
		break;
	default:
		break;
	}
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

/* ---- Game pointer support ------------------------------------------------
 * This intentionally handles only the focused surface. On keyboard focus
 * changes and pointer leave, a lock is relinquished. No game receives raw
 * mouse events while another program has focus.
 */
/* A game letting go of the pointer it held still may say where over its
 * window it would like it to be: there. */
static void constraint_hint_warp(struct desk_server *server, struct wlr_pointer_constraint_v1 *c)
{
	if (c->type != WLR_POINTER_CONSTRAINT_V1_LOCKED || !c->current.cursor_hint.enabled
		|| !c->surface || !c->surface->mapped) {
		return;
	}
	struct desk_toplevel *view = view_of_surface(server, c->surface);
	struct wlr_scene_node *node = view ? view_content_node(view) : NULL;
	int lx, ly;
	if (!node || !wlr_scene_node_coords(node, &lx, &ly)) {
		return;
	}
	double hx = c->current.cursor_hint.x, hy = c->current.cursor_hint.y;
	wlr_cursor_warp(server->cursor, NULL, lx + hx, ly + hy);
	wlr_seat_pointer_warp(server->seat, hx, hy);
	wlr_log(WLR_INFO, "a game let go of the pointer, at %.0f,%.0f on its window", hx, hy);
}

static void desk_constraint_set(struct desk_server *server,
	struct wlr_pointer_constraint_v1 *next)
{
	struct wlr_pointer_constraint_v1 *old = server->active_constraint;
	if (next == old) {
		return;
	}
	server->active_constraint = NULL;
	if (old) {
		constraint_hint_warp(server, old);
		/* A one-shot constraint can be destroyed by deactivation. */
		wlr_pointer_constraint_v1_send_deactivated(old);
	}
	if (next) {
		server->active_constraint = next;
		/* a pointer that says where it is moves from where the cursor
		 * is now */
		server->abs_x = server->cursor->x;
		server->abs_y = server->cursor->y;
		server->abs_seen = true;
		wlr_pointer_constraint_v1_send_activated(next);
		if (next->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
			wlr_cursor_unset_image(server->cursor);
		}
	} else {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
}

static void desk_constraint_refresh(struct desk_server *server)
{
	struct wlr_surface *under = server->seat->pointer_state.focused_surface;
	struct wlr_surface *keys = server->seat->keyboard_state.focused_surface;
	struct wlr_pointer_constraint_v1 *want = NULL;
	/* and none while you are looking up from the game: at the bookcase, the
	 * Control Panel, the post box, or into the drawer, the hand is yours */
	bool away = shelf_up(server) || console_up(server) || post_up(server)
		|| server->drawer_moving || server->drawer_t > 0.004;
	if (!locked(server) && !away && under && keys == under
		&& server->cursor_mode == DESK_CUR_PASSTHROUGH && server->pointer_constraints) {
		want = wlr_pointer_constraints_v1_constraint_for_surface(
			server->pointer_constraints, under, server->seat);
	}
	desk_constraint_set(server, want);
}

static void desk_constraint_destroy(struct wl_listener *listener, void *data)
{
	(void)data;
	struct desk_game_constraint *entry = wl_container_of(listener, entry, destroy);
	if (entry->server->active_constraint == entry->constraint) {
		/* (a game lets go by taking its hold away) */
		constraint_hint_warp(entry->server, entry->constraint);
		entry->server->active_constraint = NULL;
		wlr_cursor_set_xcursor(entry->server->cursor, entry->server->cursor_mgr, "default");
	}
	wl_list_remove(&entry->destroy.link);
	free(entry);
}

static void desk_new_pointer_constraint(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, new_pointer_constraint);
	struct wlr_pointer_constraint_v1 *constraint = data;
	struct desk_game_constraint *entry = calloc(1, sizeof *entry);
	if (!entry) {
		return;
	}
	entry->server = server;
	entry->constraint = constraint;
	entry->destroy.notify = desk_constraint_destroy;
	wl_signal_add(&constraint->events.destroy, &entry->destroy);
	desk_constraint_refresh(server);
}

static void seat_request_cursor(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;
	if (server->active_constraint && server->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
		return;  /* hide the hardware cursor while the game has it locked */
	}
	if (focused_client == event->seat_client) {
		wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
	}
}

static void seat_pointer_focus_change(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, pointer_focus_change);
	struct wlr_seat_pointer_focus_change_event *event = data;
	desk_constraint_refresh(server);
	if (event->new_surface == NULL && !server->active_constraint) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
}

static void seat_request_set_selection(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

/* What was last selected, for a middle click to paste -- the X habit, which
 * Wayland programs keep as well, and which Xwayland carries across. */
static void seat_request_set_primary_selection(struct wl_listener *listener, void *data)
{
	struct desk_server *server =
		wl_container_of(listener, server, request_set_primary_selection);
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

/* A drag starts only if we say so, and we say so if the button it started
 * with really is held down in that client: otherwise nothing is dragged at
 * all, which is how it was until now. */
static void seat_request_start_drag(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, request_start_drag);
	struct wlr_seat_request_start_drag_event *event = data;
	if (wlr_seat_validate_pointer_grab_serial(server->seat, event->origin, event->serial)) {
		wlr_seat_start_pointer_drag(server->seat, event->drag, event->serial);
		return;
	}
	wlr_data_source_destroy(event->drag->source);
}

static void drag_icon_gone(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, drag_icon_gone);
	(void)data;
	wl_list_remove(&listener->link);
	wl_list_init(&listener->link);
	server->drag_icon = NULL;
}

/* What is being dragged, if the client gave it a picture: under the pointer,
 * over everything, until it is dropped. */
static void seat_start_drag(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, start_drag);
	struct wlr_drag *drag = data;
	if (!drag->icon || server->drag_icon) {
		return;
	}
	server->drag_icon = wlr_scene_drag_icon_create(server->layer_drag, drag->icon);
	if (!server->drag_icon) {
		return;
	}
	wl_signal_add(&server->drag_icon->node.events.destroy, &server->drag_icon_gone);
	wlr_scene_node_set_position(&server->drag_icon->node,
		(int)server->cursor->x, (int)server->cursor->y);
}

static void process_cursor_move(struct desk_server *server)
{
	struct desk_toplevel *toplevel = server->grabbed;
	if (!toplevel->scene_tree) {
		return;
	}
	int x = (int)(server->cursor->x - server->grab_x);
	wlr_scene_node_set_position(&toplevel->scene_tree->node, x,
		work_clamp_y(server, x + toplevel->outer_w / 2,
			(int)(server->cursor->y - server->grab_y)));
	view_sync_shadow(toplevel);
	view_send_position(toplevel);
}

static void process_cursor_resize(struct desk_server *server)
{
	struct desk_toplevel *toplevel = server->grabbed;
	if (!toplevel->scene_tree) {
		return;
	}
	double border_x = server->cursor->x - server->grab_x;
	double border_y = server->cursor->y - server->grab_y;
	int new_left = server->grab_geobox.x;
	int new_right = server->grab_geobox.x + server->grab_geobox.width;
	int new_top = server->grab_geobox.y;
	int new_bottom = server->grab_geobox.y + server->grab_geobox.height;
	if (server->resize_edges & WLR_EDGE_TOP) {
		new_top = (int)border_y;
		/* the content's top: the frame's title bar is above it */
		int l, t, r, b;
		view_insets(server->grabbed, &l, &t, &r, &b);
		int lowest = screen_area(server, view_screen(server->grabbed)).y + t;
		if (new_top < lowest) {
			new_top = lowest;
		}
		if (new_top >= new_bottom) {
			new_top = new_bottom - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_BOTTOM) {
		new_bottom = (int)border_y;
		if (new_bottom <= new_top) {
			new_bottom = new_top + 1;
		}
	}
	if (server->resize_edges & WLR_EDGE_LEFT) {
		new_left = (int)border_x;
		if (new_left >= new_right) {
			new_left = new_right - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_RIGHT) {
		new_right = (int)border_x;
		if (new_right <= new_left) {
			new_right = new_left + 1;
		}
	}
	if (view_framed(toplevel)) {
		view_sizing_move(toplevel, new_left, new_top, new_right, new_bottom,
			server->resize_edges);
		return;
	}
	int l, t, r, b;
	view_insets(toplevel, &l, &t, &r, &b);
	wlr_scene_node_set_position(&toplevel->scene_tree->node, new_left - l, new_top - t);
	view_sync_shadow(toplevel);
	view_set_size(toplevel, new_right - new_left, new_bottom - new_top);
}

static void pointer_over_paper(struct desk_server *server, uint32_t time);
static void paper_press(struct desk_server *server, struct wlr_pointer_button_event *event);

/* The pointer gone over to a screen beside the desk's: whatever on the
 * desk's own screen it was over -- a thing on the desk, a binder, a
 * control, a letter -- is let go of. */
static void desk_hover_off(struct desk_server *server)
{
	drawer_hover_to(server, -1);
	clock_track(server, false);
	slips_track(server, false);
	if (shelf_up(server) && server->shelf_binder < 0) {
		hover_set(server, -1);
	}
	if (console_up(server) && server->cc_hover >= 0) {
		server->cc_hover = -1;
		paint_console(server);
		server_schedule_frames(server);
	}
	if (post_up(server) && server->pb_hover >= 0) {
		server->pb_hover = -1;
		paint_post(server);
		server_schedule_frames(server);
	}
}

static void process_cursor_motion(struct desk_server *server, uint32_t time)
{
	desk_sound_awake();
	if (locked(server)) {
		/* at the door: nothing behind it sees the pointer; the key can
		 * be taken hold of */
		wlr_seat_pointer_clear_focus(server->seat);
		bool hand = server->lock_phase == LOCK_SHUT && desk_lock_key_at(server->desk_w,
			server->desk_h, server->cursor->x, server->cursor->y);
		if (hand != server->lock_hand) {
			server->lock_hand = hand;
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, hand ? "pointer" : "default");
		}
		return;
	}
	if (server->drag_icon) {
		wlr_scene_node_set_position(&server->drag_icon->node,
			(int)server->cursor->x, (int)server->cursor->y);
	}
	/* something from the drawer, or a packet off the desk, in the hand */
	if (drawer_motion(server)) {
		wlr_seat_pointer_clear_focus(server->seat);
		if (server->drw_carrying) {
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "fleur");
		} else {
			cursor_for_edges(server, 0);
		}
		return;
	}
	if (server->cursor_mode == DESK_CUR_MOVE) {
		process_cursor_move(server);
		return;
	}
	if (server->cursor_mode == DESK_CUR_RESIZE) {
		process_cursor_resize(server);
		return;
	}
	/* a letter in the hand */
	if (paper_drag(server)) {
		wlr_seat_pointer_clear_focus(server->seat);
		if (server->pp_moved) {
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "fleur");
		}
		return;
	}
	/* a hand on the clock's volume bar keeps hold of it */
	if (clock_drag(server)) {
		return;
	}
	/* the card pulled out from under the Control Panel is over it all */
	if (card_motion(server)) {
		cursor_for_edges(server, 0);
		wlr_seat_pointer_clear_focus(server->seat);
		return;
	}
	/* On a screen beside the desk's there is only paper: in reach whatever
	 * the desk's own screen is turned to, once the drawer and the camera
	 * have left it where it lies. What the hand was over on the desk is
	 * let go of. */
	if (pointer_on_side(server)) {
		desk_hover_off(server);
		if (paper_moved(server)) {
			wlr_seat_pointer_clear_focus(server->seat);
			cursor_for_edges(server, 0);
			return;
		}
		pointer_over_paper(server, time);
		return;
	}

	if (post_up(server)) {
		if (server->pb_t < 0.9995) {
			return;
		}
		const struct desk_note *notes = NULL;
		int n = post_count(&notes);
		int hv = desk_post_hit(server->desk_w, server->desk_h, n, server->pb_pos,
			server->cursor->x, server->cursor->y);
		if (hv != server->pb_hover) {
			server->pb_hover = hv;
			paint_post(server);
			server_schedule_frames(server);
		}
		pbc_track(server);
		return;
	}
	if (console_up(server)) {
		if (server->cc_grab >= 0) {
			console_drag(server);
			return;
		}
		if (server->cc_t < 0.9995) {
			return;
		}
		int hv = desk_console_hit(server->desk_w, server->desk_h,
			server->cursor->x, server->cursor->y);
		if (hv != server->cc_hover) {
			server->cc_hover = hv;
			paint_console(server);
			server_schedule_frames(server);
		}
		return;
	}
	if (shelf_up(server)) {
		if (server->shelf_t < 0.9995) {
			return;
		}
		if (memo_up(server)) {
			return;   /* the memo pad in your hand: the shelf is behind it */
		}
		if (cal_up(server)) {
			/* on the binder, what of it; else which edge of the page the
			 * pointer is at: it stands up; or which tab: it is drawn out a
			 * little */
			bool on_binder = false;
			if (server->agd_drag) {
				/* drawn along the ruler */
				agd_drag_to(server, server->cursor->x, server->cursor->y);
				return;
			}
			if (agd_up(server)) {
				agd_hover_at(server, server->cursor->x, server->cursor->y);
				on_binder = server->agd_hover != DESK_AGENDA_HIT_NONE;
			}
			double cx = server->cursor->x, cy = server->cursor->y;
			cal_unaside(server, &cx, &cy);
			struct desk_cal_look lk;
			cal_look_now(server, &lk);
			desk_calendar_unturn(server->desk_w, server->desk_h, &lk, &cx, &cy);
			int yy, mm, dd;
			int z = cal_settled(server) && !on_binder ? desk_calendar_hit(server->desk_w,
				server->desk_h, &lk, cx, cy, &yy, &mm, &dd) : 0;
			int tab = z >= DESK_CAL_HIT_TAB ? z - DESK_CAL_HIT_TAB : -1;
			z = z == DESK_CAL_HIT_PREV ? 1 : (z == DESK_CAL_HIT_NEXT ? 2 : 0);
			if (z != server->cal_zone) {
				server->cal_zone = z;
				server_schedule_frames(server);
			}
			if (tab != server->cal_hover_tab) {
				server->cal_hover_tab = tab;
				paint_calendar(server);
				server_schedule_frames(server);
			}
			return;
		}
		if (server->shelf_binder == DESK_SHELF_BOOK) {
			book_hover_at(server, server->cursor->x, server->cursor->y);
			return;
		}
		if (server->shelf_binder >= 0) {
			if (server->binder_moving || server->binder_to < 0.5) {
				return;   /* on its way, or left open as you turn away */
			}
			int sheet = desk_binder_hit(server->desk_w, server->desk_h,
				binder_count(server), server->binder_pos,
				server->cursor->x, server->cursor->y);
			if (sheet != server->binder_sel) {
				server->binder_sel = sheet;
				paint_shelf(server);
				server_schedule_frames(server);
			}
			bnc_track(server);
			return;
		}
		hover_set(server, desk_shelf_hit(server->desk_w, server->desk_h,
			server->cursor->x, server->cursor->y));
		return;
	}
	bool on_clock = clock_track(server, !window_under(server));
	bool on_slip = slips_track(server, !window_under(server));
	drawer_hover_to(server, window_under(server) || on_clock || on_slip ? -1
		: drawer_entry_at(server, server->cursor->x, server->cursor->y));
	/* with the drawer out, the paper on the desk is not in reach */
	if (server->drawer_t > 0.004) {
		wlr_seat_pointer_clear_focus(server->seat);
		cursor_for_edges(server, 0);
		return;
	}
	pointer_over_paper(server, time);
}

/* The pointer over the paper -- a window, its frame, or the bare desk
 * between them: the corner it is near, the arrows of an edge that can be
 * pulled, or the program's own pointer over the program. */
static void pointer_over_paper(struct desk_server *server, uint32_t time)
{
	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct desk_toplevel *under =
		view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	curl_track(server, under, server->cursor->x, server->cursor->y);
	if (!surface) {
		/* over the desk or a frame: ours -- an arrow, or the edge's own
		 * arrows where the paper can be pulled. Over a client -- an X
		 * menu too, which is no window of ours -- the client's. */
		unsigned e = 0;
		if (under && view_framed(under) && under->scene_tree) {
			e = (unsigned)desk_frame_resize_edges(
				server->cursor->x - under->scene_tree->node.x,
				server->cursor->y - under->scene_tree->node.y,
				under->outer_w, under->outer_h);
		}
		cursor_for_edges(server, e);
	}
	if (surface) {
		wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
	} else {
		wlr_seat_pointer_clear_focus(server->seat);
	}
}

/* The pointer moved by (dx, dy): a mouse's own movement, or worked out from
 * where a tablet's or a virtual machine's pointer now is. A game is told
 * the movement as it is; a game that holds the pointer still keeps it
 * where it is, and one that keeps it inside its window keeps it there. */
static void pointer_moved(struct desk_server *server, struct wlr_input_device *dev,
	double dx, double dy, double udx, double udy, uint32_t time_msec)
{
	/* Relative events are required by Wayland and XWayland game clients. */
	if (server->relative_pointer_manager && (dx != 0.0 || dy != 0.0)) {
		wlr_relative_pointer_manager_v1_send_relative_motion(server->relative_pointer_manager,
			server->seat, (uint64_t)time_msec * 1000, dx, dy, udx, udy);
	}
	input_seen(server, "the pointer");
	/* the bookcase or the drawer may have come up, or gone, since */
	desk_constraint_refresh(server);
	struct wlr_pointer_constraint_v1 *c = server->active_constraint;
	if (c && c->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
		return; /* game gets the delta; desktop cursor stays fixed */
	}
	if (c && c->type == WLR_POINTER_CONSTRAINT_V1_CONFINED) {
		double sx, sy;
		struct wlr_surface *surface = NULL;
		view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
		double cx, cy;
		if (surface != c->surface || !wlr_region_confine(&c->region,
				sx, sy, sx + dx, sy + dy, &cx, &cy)) {
			return;
		}
		dx = cx - sx;
		dy = cy - sy;
	}
	wlr_cursor_move(server->cursor, dev, dx, dy);
	process_cursor_motion(server, time_msec);
}

static void server_cursor_motion(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	pointer_moved(server, &event->pointer->base, event->delta_x, event->delta_y,
		event->unaccel_dx, event->unaccel_dy, event->time_msec);
}

/* A pointer that says where it is: the movement is from where the cursor is
 * -- or, while a game holds the cursor still, from where this pointer last
 * said it was, the cursor not having gone there. */
static void server_cursor_motion_absolute(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(server->cursor, &event->pointer->base, event->x,
		event->y, &lx, &ly);
	desk_constraint_refresh(server);
	struct wlr_pointer_constraint_v1 *c = server->active_constraint;
	double fx = server->cursor->x, fy = server->cursor->y;
	if (c && c->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
		fx = server->abs_seen ? server->abs_x : lx;
		fy = server->abs_seen ? server->abs_y : ly;
	}
	server->abs_x = lx;
	server->abs_y = ly;
	server->abs_seen = true;
	pointer_moved(server, &event->pointer->base, lx - fx, ly - fy, lx - fx, ly - fy,
		event->time_msec);
}

static void server_cursor_button(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	/* a click on the dark screen -- or on it lit again, before it can have
	 * been seen -- wakes it and lands on nothing, nor does its letting go */
	if (!input_seen(server, "a click")) {
		server->wake_button = event->state == WL_POINTER_BUTTON_STATE_PRESSED
			? event->button : 0;
		return;
	}
	if (server->wake_button == event->button
		&& event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		server->wake_button = 0;
		return;
	}
	desk_sound_awake();
	if (locked(server)) {
		/* the key: turning it is Return */
		if (event->state == WL_POINTER_BUTTON_STATE_PRESSED && event->button == BTN_LEFT
			&& server->lock_phase == LOCK_SHUT && desk_lock_key_at(server->desk_w,
				server->desk_h, server->cursor->x, server->cursor->y)) {
			lock_submit(server);
		}
		return;
	}
	if (console_dying(server)) {
		return;
	}
	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		if (event->button == BTN_LEFT && paper_release(server)) {
			return;
		}
		if (server->agd_drag) {
			/* let go of the ruler */
			server->agd_drag = false;
			agd_hover_at(server, server->cursor->x, server->cursor->y);
			return;
		}
		if (server->drw_button && event->button == server->drw_button
			&& drawer_release(server)) {
			return;
		}
		if (clock_release(server)) {
			return;
		}
		if (server->cc_grab >= 0) {
			/* let go of the knob */
			server->cc_grab = -1;
			paint_console(server);
			return;
		}
		if (server->cursor_mode == DESK_CUR_RESIZE && server->grabbed
			&& server->grabbed->sizing) {
			/* let go of the edge: the turn of the paper settles */
			server->grabbed->sz_released = real_sec();
			server_schedule_frames(server);
		}
		reset_cursor_mode(server);
		wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
		return;
	}

	/* On a screen beside the desk's, only paper: a hand put down there
	 * with the drawer out pushes it back in, as anywhere on the desk above
	 * it does; with the camera on its way, nothing yet. */
	if (pointer_on_side(server)) {
		if (server->drawer_t > 0.004) {
			if (!post_up(server) && !console_up(server) && !shelf_up(server)) {
				drawer_toggle(server);
			}
			return;
		}
		if (!paper_moved(server)) {
			/* a packet folded on this screen: taken hold of, as on the desk */
			if ((event->button == BTN_LEFT || event->button == BTN_RIGHT)
				&& drawer_press(server, event->button)) {
				return;
			}
			paper_press(server, event);
		}
		return;
	}

	/* Something in the drawer, or a packet folded on the desk: taken hold
	 * of, to be clicked or carried. */
	if ((event->button == BTN_LEFT || event->button == BTN_RIGHT)
		&& drawer_press(server, event->button)) {
		return;
	}

	/* With the drawer out you are looking into the drawer: a hand put down
	 * on the desk above it (on the paper there too) pushes it back in. */
	if (server->drawer_t > 0.004 && !post_up(server) && !console_up(server)
		&& !shelf_up(server) && server->cursor->y < drawer_line(server)) {
		drawer_toggle(server);
		return;
	}

	/* At the post box, the desk is not in reach either. */
	if (post_up(server)) {
		if (event->button != BTN_LEFT || server->pb_t < 0.9995) {
			return;
		}
		if (pbc_press(server)) {
			return;
		}
		const struct desk_note *notes = NULL;
		int n = post_count(&notes);
		int idx = desk_post_hit(server->desk_w, server->desk_h, n, server->pb_pos,
			server->cursor->x, server->cursor->y);
		if (idx >= 0 && idx < (int)lround(server->pb_pos)) {
			/* one that has gone over: turn back to it */
			pb_riffle(server, idx - (int)lround(server->pb_pos));
		} else if (idx >= 0) {
			post_take(server, idx);
		} else if (!desk_post_view_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y)) {
			post_toggle(server);
		}
		return;
	}

	/* While you are round at the set, the desk is not in reach: your hands
	 * are on the machine. */
	if (console_up(server)) {
		if (server->cc_t < 0.9995) {
			return;   /* mid move: let it land first */
		}
		int which = desk_console_hit(server->desk_w, server->desk_h,
			server->cursor->x, server->cursor->y);
		if (event->button == BTN_RIGHT) {
			/* the other button: what is behind the control, in full */
			console_open_settings(server, which);
			return;
		}
		if (event->button != BTN_LEFT) {
			return;
		}
		if (which == DESK_CC_NET) {
			/* the network's meter: the networks themselves */
			console_open_settings(server, which);
		} else if (which >= 0) {
			console_press(server, which);
		} else if (server->cc_key > 0.999 && !server->cc_key_moving
			&& desk_console_guard_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y, server->cc_guard)) {
			/* the glass itself, anywhere on it: up, or back down */
			console_guard_to(server, server->cc_guard_to > 0.5 ? 0.0 : 1.0);
		} else if (!desk_console_view_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y)) {
			/* off the machine altogether: you have looked away */
			console_toggle(server);
		}
		return;
	}

	/* While you are looking up at the bookcase, the desk is not in reach. */
	if (shelf_up(server)) {
		if (memo_up(server)) {
			memo_press(server, event->button);
			return;
		}
		if (cal_up(server)) {
			/* the calendar in your hand: the head of the page back a page,
			 * its foot on one, a day opens the binder of notes at it, a tab
			 * goes to its section; anywhere else -- or the other button --
			 * puts the binder back, or the calendar on the shelf */
			if (!cal_settled(server)) {
				return;
			}
			if (agd_up(server) && agd_press(server, server->cursor->x, server->cursor->y,
					event->button)) {
				return;
			}
			double cx = server->cursor->x, cy = server->cursor->y;
			cal_unaside(server, &cx, &cy);
			struct desk_cal_look lk;
			cal_look_now(server, &lk);
			desk_calendar_unturn(server->desk_w, server->desk_h, &lk, &cx, &cy);
			if ((event->button == BTN_LEFT || event->button == BTN_RIGHT) && !server->cal_turn
				&& !cal_switching(server)) {
				/* a note stuck on it: taken off to be written on again, or
				 * with the other button peeled off */
				int nid = desk_calendar_note_at(server->desk_w, server->desk_h, &lk, cx, cy);
				struct desk_stick ns;
				if (nid && desk_agenda_get(nid, &ns)) {
					if (event->button == BTN_RIGHT) {
						agd_peel_off(server, nid);
					} else {
						agd_open(server, ns.y, ns.m, ns.d, AGD_START_NONE, -1);
						agd_begin(server, nid);
					}
					return;
				}
			}
			int yy = 0, mm = 0, dd = 0;
			int z = event->button == BTN_LEFT ? desk_calendar_hit(server->desk_w,
				server->desk_h, &lk, cx, cy, &yy, &mm, &dd) : 0;
			if (z >= DESK_CAL_HIT_TAB) {
				cal_switch(server, z - DESK_CAL_HIT_TAB);
			} else if (z == DESK_CAL_HIT_PREV) {
				cal_turn_page(server, -1);
			} else if (z == DESK_CAL_HIT_NEXT) {
				cal_turn_page(server, 1);
			} else if (z == DESK_CAL_HIT_DATE || z == DESK_CAL_HIT_TODO) {
				/* on a day's page, at an hour: a plan at that hour */
				int hr = z == DESK_CAL_HIT_DATE ? desk_calendar_hour_at(server->desk_w,
					server->desk_h, &lk, cx, cy) : -1;
				agd_open(server, yy, mm, dd, z == DESK_CAL_HIT_TODO ? AGD_START_TODO
					: (hr >= 0 ? AGD_START_PLAN : AGD_START_NONE), hr >= 0 ? hr * 60 : -1);
			} else if (z == DESK_CAL_HIT_MONTH) {
				/* a month's name on the year's page: that month */
				int d2 = server->cal_day;
				server->cal_year = yy;
				server->cal_month = mm;
				/* (stepping no months is the day kept in the month) */
				desk_calendar_step(DESK_CAL_MONTH, 0, &server->cal_year, &server->cal_month,
					&d2);
				server->cal_day = d2;
				cal_switch(server, DESK_CAL_MONTH);
			} else if (z == DESK_CAL_HIT_PAGE) {
				agd_finish(server, true);
			} else if (agd_up(server)) {
				agd_close(server);
			} else {
				cal_close(server);
			}
			return;
		}
		if (event->button != BTN_LEFT) {
			return;
		}
		if (server->shelf_t < 0.9995) {
			return;   /* mid move: let it land first */
		}
		if (server->shelf_binder == DESK_SHELF_BOOK) {
			book_press(server, server->cursor->x, server->cursor->y);
			return;
		}
		if (server->shelf_binder >= 0) {
			if (server->binder_moving || server->binder_to < 0.5) {
				return;
			}
			if (bnc_press(server)) {
				return;
			}
			int sheet = desk_binder_hit(server->desk_w, server->desk_h,
				binder_count(server), server->binder_pos,
				server->cursor->x, server->cursor->y);
			if (sheet >= 0) {
				binder_take(server, sheet);
			} else {
				binder_close(server);
			}
			return;
		}
		int bn = desk_shelf_hit(server->desk_w, server->desk_h,
			server->cursor->x, server->cursor->y);
		if (bn >= 0) {
			binder_open(server, bn);
			return;
		}
		/* the calendar on the lower board: taken down to be looked at */
		if (desk_shelf_calendar_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y)) {
			cal_open(server);
			return;
		}
		/* and the memo pad beside it, to be written on */
		if (desk_shelf_memo_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y)) {
			memo_open(server);
			return;
		}
		/* The desk is in the foreground up here, and what is built into
		 * its back edge is close enough to reach: come back down to it
		 * and turn to whichever of them was pointed at. */
		if (desk_console_shelf_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y)) {
			server->pending_look = 1;
			shelf_toggle(server);
			return;
		}
		if (desk_post_shelf_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y)) {
			server->pending_look = 2;
			shelf_toggle(server);
			return;
		}
		shelf_toggle(server);
		return;
	}
	/* the card pulled out from under the Control Panel is over it all */
	if (card_press(server, event->button)) {
		return;
	}
	/* A letter lying on the desk, uppermost under the pointer: taken hold
	 * of -- carried if the hand moves, put away if it is only a click.
	 * (Asked here: it lies in the windows' layer, and below this a click
	 * there was taken for a window's, so a letter could not be put away.) */
	if (event->button == BTN_LEFT) {
		int pi = post_paper_under(server);
		if (pi >= 0) {
			struct desk_paper *pp = &server->pb_paper[pi];
			server->pp_held = pi + 1;
			server->pp_moved = false;
			server->pp_px = server->cursor->x;
			server->pp_py = server->cursor->y;
			server->pp_hx = server->cursor->x - pp->x;
			server->pp_hy = server->cursor->y - pp->y;
			return;
		}
	}
	/* What lies on the desk, if no window lies over it. */
	bool on_window = window_under(server);
	if (!on_window) {
		bool taken;
		clock_press(server, event->button, &taken);
		if (taken) {
			return;
		}
		slips_press(server, event->button, &taken);
		if (taken) {
			return;
		}
		if (event->button == BTN_LEFT
			&& desk_allapps_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y)) {
			shelf_toggle(server);
			return;
		}
		/* the set at the back left: you turn to it */
		if (event->button == BTN_LEFT
			&& desk_console_desk_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y, server->drawer_t)) {
			console_toggle(server);
			return;
		}
		/* and the post box itself */
		if (event->button == BTN_LEFT
			&& desk_post_desk_hit(server->desk_w, server->desk_h,
				server->cursor->x, server->cursor->y, server->drawer_t)) {
			post_toggle(server);
			return;
		}

		if (event->button == BTN_LEFT &&
			desk_drawer_handle_hit(server->desk_w, server->desk_h, server->cursor->x, server->cursor->y,
				server->drawer_t)) {
			drawer_toggle(server);
			return;
		}
	}
	paper_press(server, event);
}

/* A button pressed over the paper: a window's corner, the edge of its
 * frame or its heading -- to be turned up, pulled or moved -- or the
 * program in it, which is focused and handed the click. */
static void paper_press(struct desk_server *server, struct wlr_pointer_button_event *event)
{
	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct desk_toplevel *toplevel = view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	if (toplevel && view_framed(toplevel)) {
		focus_toplevel(toplevel);
		double lx = server->cursor->x - toplevel->scene_tree->node.x;
		double ly = server->cursor->y - toplevel->scene_tree->node.y;
		/* the turned-down corner, and what is printed on its back */
		double cx = lx - (toplevel->outer_w - toplevel->curl_box);
		double rest, open;
		curl_sizes(toplevel, &rest, &open);
		double zone = rest * 2.6, dx = toplevel->outer_w - lx;
		bool on_fold = !toplevel->curl_near && dx >= -6.0 && dx <= zone && ly >= -6.0 && ly <= zone
			&& (!toplevel->own_frame || mount_on_fold(toplevel, dx, ly));
		if (toplevel->curl && toplevel->curl->node.enabled
			&& (on_fold || desk_curl_contains(toplevel->curl_box, toplevel->curl_s, cx, ly))) {
			if (!toplevel->curl_near) {
				/* down: this click only turns it up */
				if (event->button == BTN_LEFT) {
					curl_turn_up(toplevel);
				}
				return;
			}
			int item = desk_curl_hit(toplevel->curl_box, toplevel->curl_s, open, cx, ly);
			if (item >= 0 && event->button == BTN_LEFT && toplevel->curl_act < 0) {
				curl_press(toplevel, item);
			}
			return;
		}
		/* on the program itself, laid on our paper: the program's (its own
		 * heading, its own buttons) */
		if (!(toplevel->own_frame && surface)) {
			int edges = desk_frame_resize_edges(lx, ly, toplevel->outer_w,
				toplevel->outer_h);
			if (edges) {
				begin_interactive(toplevel, DESK_CUR_RESIZE, (uint32_t)edges);
				return;
			}
			if (desk_frame_title_hit(lx, ly, toplevel->outer_w, toplevel->outer_h)) {
				begin_interactive(toplevel, DESK_CUR_MOVE, 0);
				return;
			}
		}
	} else if (toplevel) {
		/* no frame to catch: the click is the client's, and so is moving it */
		focus_toplevel(toplevel);
	}
	wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
}

static void server_cursor_axis(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	if (!input_seen(server, "the wheel")) {
		return;
	}
	desk_sound_awake();
	if (locked(server)) {
		return;
	}
	/* beside the desk, only paper: the program under the pointer's */
	if (pointer_on_side(server)) {
		if (!paper_moved(server)) {
			wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation,
				event->delta, event->delta_discrete, event->source, event->relative_direction);
		}
		return;
	}
	/* Scrolling over an open binder turns the block over. A wheel notch
	 * (DESK_NOTCH) buys one sheet; a device that sends a stream of small
	 * ones banks them until it has a notch's worth. */
	if (console_up(server)) {
		/* A wheel over a knob turns it. It is the one gesture on the set
		 * that is not a hand on the control itself, and it is allowed
		 * because turning a knob is what a wheel is. */
		if (server->cc_t < 0.9995
			|| event->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL) {
			return;
		}
		int which = desk_console_hit(server->desk_w, server->desk_h,
			server->cursor->x, server->cursor->y);
		if (which != DESK_CC_VOL && which != DESK_CC_BRIGHT
			&& !(which == DESK_CC_SCREEN && server->cc_screen_ok)) {
			return;
		}
		double d = event->delta;
		if (d == 0.0 && event->delta_discrete != 0) {
			d = (double)event->delta_discrete / 120.0 * 15.0;
		}
		if (d == 0.0) {
			return;
		}
		double *v = which == DESK_CC_VOL ? &server->cc_vol
			: which == DESK_CC_SCREEN ? &server->cc_screen : &server->cc_bright;
		double was = *v;
		*v = clampf_d(*v - d / DESK_NOTCH * 0.04, 0.0, 1.0);
		if (fabs(*v - was) < 1e-6) {
			return;
		}
		if (which == DESK_CC_VOL) {
			desk_meters_set_volume(*v);
		} else if (which == DESK_CC_SCREEN) {
			screen_apply(server);
		} else {
			console_dim_apply(server);
		}
		paint_console(server);
		server_schedule_frames(server);
		return;
	}
	if (shelf_up(server) && memo_up(server)) {
		/* a sheet a notch */
		if (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
			double d = event->delta;
			if (d == 0.0 && event->delta_discrete != 0) {
				d = (double)event->delta_discrete / 120.0 * 15.0;
			}
			memo_scroll(server, d);
		}
		return;
	}
	if (shelf_up(server) && cal_up(server)) {
		/* a page a notch: down on, up back -- or on the ruler, the time */
		if (event->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL || !cal_settled(server)) {
			return;
		}
		double d = event->delta;
		if (d == 0.0 && event->delta_discrete != 0) {
			d = (double)event->delta_discrete / 120.0 * 15.0;
		}
		int rarg = 0;
		if (agd_up(server) && agd_hit_at(server, server->cursor->x, server->cursor->y, &rarg)
				== DESK_AGENDA_HIT_RULER) {
			server->agd_scroll += d;
			while (server->agd_scroll >= DESK_NOTCH) {
				server->agd_scroll -= DESK_NOTCH;
				agd_ruler_roll(server, 1);
			}
			while (server->agd_scroll <= -DESK_NOTCH) {
				server->agd_scroll += DESK_NOTCH;
				agd_ruler_roll(server, -1);
			}
			return;
		}
		server->cal_scroll += d;
		while (server->cal_scroll >= DESK_NOTCH) {
			server->cal_scroll -= DESK_NOTCH;
			cal_turn_page(server, 1);
		}
		while (server->cal_scroll <= -DESK_NOTCH) {
			server->cal_scroll += DESK_NOTCH;
			cal_turn_page(server, -1);
		}
		return;
	}
	if (shelf_up(server) && server->shelf_binder >= 0) {
		if (event->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL) {
			return;
		}
		double d = event->delta;
		if (d == 0.0 && event->delta_discrete != 0) {
			d = (double)event->delta_discrete / 120.0 * 15.0;
		}
		server->binder_scroll += d;
		int step = 0;
		while (server->binder_scroll >= DESK_NOTCH) {
			step++;
			server->binder_scroll -= DESK_NOTCH;
		}
		while (server->binder_scroll <= -DESK_NOTCH) {
			step--;
			server->binder_scroll += DESK_NOTCH;
		}
		if (!step) {
			return;
		}
		/* One flick of a trackpad arrives as two dozen notches, and every
		 * one of them buying a sheet tore through the whole block and left
		 * it sitting on the last one -- which is what "it does not turn"
		 * looked like. Rate limiting cannot tell the difference, because
		 * the compositor paces those notches out one per frame anyway.
		 *
		 * So it is counted per gesture instead: notches that keep arriving
		 * are the same push of the hand, and a push turns a few sheets, not
		 * the whole block. A quarter of a second of quiet ends the gesture,
		 * so deliberate notch-by-notch scrolling is never capped. */
		double now = real_sec();
		if (now - server->binder_axis_at > 0.25) {
			server->binder_burst = 0;
		}
		server->binder_axis_at = now;
		if (server->binder_burst >= 5) {
			server->binder_scroll = 0.0;
			return;
		}
		server->binder_burst++;
		binder_riffle(server, step > 0 ? 1 : -1);
		return;
	}
	if (shelf_up(server)) {
		return;
	}
	if (post_up(server)) {
		/* Over the open box, the wheel turns the letters in it over, one
		 * a notch, the way it turns a binder's sheets. */
		if (event->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL || server->pb_t < 0.9995) {
			return;
		}
		double d = event->delta;
		if (d == 0.0 && event->delta_discrete != 0) {
			d = (double)event->delta_discrete / 120.0 * 15.0;
		}
		server->pb_scroll += d;
		int step = 0;
		while (server->pb_scroll >= DESK_NOTCH) {
			step++;
			server->pb_scroll -= DESK_NOTCH;
		}
		while (server->pb_scroll <= -DESK_NOTCH) {
			step--;
			server->pb_scroll += DESK_NOTCH;
		}
		if (step) {
			pb_riffle(server, step > 0 ? 1 : -1);
		}
		return;
	}
	/* the clock on the desk, when no window is over it */
	if (!post_up(server) && card_axis(server, event)) {
		return;
	}
	if (!post_up(server) && !window_under(server)
		&& (clock_axis(server, event) || slips_axis(server, event))) {
		return;
	}
	wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation, event->delta,
		event->delta_discrete, event->source, event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, cursor_frame);
	(void)data;
	wlr_seat_pointer_notify_frame(server->seat);
}

/* The desk end of a move. Coming down from the bookcase, the console or
 * the post box, the camera goes a little past the desk and comes back to
 * it, as it does at the other end; the desk -- the room, what stands on
 * it, the windows and the folded sheets -- goes with it, until the move
 * is over. Setting off, it goes with it too, until the film is over it. */
/* How far the drawer, where it is, has pushed the desk up: 0 shut. */
static int drawer_lift(struct desk_server *server)
{
	int w = server->desk_w, h = server->desk_h;
	int ox0, oy0, ox1, oy1;
	double c0, c1;
	if (server->drawer_t <= 0.0005 || w < 8 || h < 8
		|| !desk_desk_place(w, h, 0.0, &ox0, &oy0, &c0)
		|| !desk_desk_place(w, h, server->drawer_t, &ox1, &oy1, &c1)) {
		return 0;
	}
	return oy1 - oy0;
}

/* Where the drawer begins on screen: what is above this is the desk. */
static double drawer_line(struct desk_server *server)
{
	int ox, oy;
	double clip;
	if (!desk_desk_place(server->desk_w, server->desk_h, server->drawer_t, &ox, &oy, &clip)) {
		return server->desk_h;
	}
	return clip;
}

static void desk_follow(struct desk_server *server)
{
	int which = -1;
	double t = 1.0;
	if (server->shelf_moving) {
		which = DESK_VIEW_SHELF;
		t = server->shelf_t;
	} else if (server->cc_moving) {
		which = DESK_VIEW_CONSOLE;
		t = server->cc_t;
	} else if (server->pb_moving) {
		which = DESK_VIEW_POST;
		t = server->pb_t;
	}
	int dx = 0, dy = 0;
	/* past a tenth of the way the film is over all of it */
	if (which >= 0 && t < 0.12) {
		double fx, fy;
		desk_tilt_desk_offset(server->desk_w, server->desk_h, which, t, &fx, &fy);
		dx = (int)lround(fx);
		dy = (int)lround(fy);
	}
	/* The drawer coming out pushes the desk up; the paper lying on it goes
	 * up with it. (The room and what is drawn over it are placed for the
	 * drawer where they are drawn: room_layout, overlay_place.) */
	int lift = drawer_lift(server);
	if (dx == server->desk_dx && dy == server->desk_dy && lift == server->win_dy) {
		return;
	}
	/* the windows on a screen beside the desk's stay where they lie: the
	 * paper is moved back by as much as the desk is moving it */
	int ddx = dx - server->desk_dx;
	int ddy = dy + lift - server->desk_dy - server->win_dy;
	struct desk_toplevel *view;
	wl_list_for_each(view, &server->toplevels, link) {
		if (view->scene_tree && view->scene_tree->node.parent == server->layer_win
			&& screen_is_side(server, view_screen(view))) {
			struct wlr_scene_node *node = &view->scene_tree->node;
			wlr_scene_node_set_position(node, node->x - ddx, node->y - ddy);
			/* folded, it opens out to where the window is now */
			if (view->sheet.phase != SHEET_IDLE) {
				view->sheet.x0 -= ddx;
				view->sheet.y0 -= ddy;
			}
			view_sync_shadow(view);
		}
	}
	/* and so do the sheets folded on one, lying there or on their way to
	 * it (not one in the hand, which is where the hand is) */
	wl_list_for_each(view, &server->toplevels, link) {
		struct desk_sheet *s = &view->sheet;
		if (s->phase == SHEET_IDLE || s->in_drawer || s->to_drawer
			|| (s->tree && s->tree->node.parent == server->layer_drag)
			|| !screen_is_side(server, sheet_screen(server, s))) {
			continue;
		}
		s->x1 -= ddx;
		s->y1 -= ddy;
		s->hit_x -= ddx;
		s->hit_y -= ddy;
		if (s->tree) {
			wlr_scene_node_set_position(&s->tree->node, s->tree->node.x - ddx,
				s->tree->node.y - ddy);
		}
	}
	bool moved = dx != server->desk_dx || dy != server->desk_dy;
	server->desk_dx = dx;
	server->desk_dy = dy;
	server->win_dy = lift;
	if (moved && screens_beside(server)) {
		/* what of the room is on the desk's screen is another part now */
		room_layout(server);
	}
	struct wlr_scene_tree *desk[] = { server->layer_bg, server->layer_desk, server->layer_drawer };
	for (size_t i = 0; i < sizeof desk / sizeof desk[0]; i++) {
		if (desk[i]) {
			wlr_scene_node_set_position(&desk[i]->node, dx, dy);
		}
	}
	struct wlr_scene_tree *paper[] = {
		server->layer_folded, server->layer_win, server->layer_xover, server->layer_parked,
	};
	for (size_t i = 0; i < sizeof paper / sizeof paper[0]; i++) {
		if (paper[i]) {
			wlr_scene_node_set_position(&paper[i]->node, dx, dy + lift);
		}
	}
}

/* What moves, moved on a frame: the paper and everything else on the
 * desk, the views and their moves, the login, the door, the shows. */
static void output_frame_work(struct desk_server *server)
{
	folds_tick(server);
	paper_tick(server);
	curls_tick(server);
	sizing_tick(server);
	unfold_tick(server);
	throws_tick(server);
	drawer_tick(server);
	drawer_slide_tick(server);
	shelf_tick(server);
	console_tick(server);
	post_tick(server);
	binder_tick(server);
	cal_tick(server);
	memo_tick(server);
	boot_tick(server);
	lock_tick(server);
	show_tick(server);
	agd_tick(server);
	agd_fly_tick(server, monotonic_sec());
	binder_warm_tick(server);
	take_tick(server);
	binder_pos_tick(server);
	pb_pos_tick(server);
	pbc_tick(server);
	bnc_tick(server);
	hover_tick(server);
	desk_follow(server);
	clock_tick_frame(server);
	slips_tick_frame(server);
}

static void output_frame(struct wl_listener *listener, void *data)
{
	struct desk_output *output = wl_container_of(listener, output, frame);
	(void)data;
	struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(output->server->scene,
		output->wlr_output);
	/* Off, or dark: nothing is drawn, nothing moves, and no program is
	 * asked for its next frame. Nested, the black over it goes out once. */
	if (!output->wlr_output->enabled || !scene_output || output->dark_shown) {
		return;
	}
	if (output->dark) {
		output->dark_shown = true;
		wlr_scene_output_commit(scene_output, NULL);
		return;
	}
	double f0 = g_prof.on ? real_sec() : 0.0;
	double c0 = g_trace.f ? thread_cpu_sec() : 0.0;
	/* What moves is moved once a frame, on the desk's screen's frames --
	 * or another's, if the desk's has not had one for most of a frame --
	 * not once for every screen there is. */
	double tick_now = monotonic_sec();
	bool tick = output->wlr_output == first_output(output->server)
		|| tick_now - output->server->ticked_at > 0.012;
	if (tick) {
		output->server->ticked_at = tick_now;
		output_frame_work(output->server);
	}
	double f1 = g_prof.on ? real_sec() : 0.0;
	double c1 = g_trace.f ? thread_cpu_sec() : 0.0;
	wlr_scene_output_commit(scene_output, NULL);
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
	if (!output->server->first_frame_logged) {
		output->server->first_frame_logged = true;
		wlr_log(WLR_INFO, "the first frame (%.2f s)", real_sec() - g_start_real);
	}
	if (g_prof.on) {
		double f2 = real_sec();
		trace_frame(f0, f1, f2, c0, c1, g_trace.f ? thread_cpu_sec() : 0.0,
			g_prof.asked > 0.0 ? f0 - g_prof.asked : -1.0);
		/* How long a frame took to arrive once it was asked for. Up to a
		 * refresh is the display's own pace; late is more than one and a
		 * half of them. A pause between two things happening is not late:
		 * nobody asked for a frame in it. */
		int mhz = output->wlr_output->refresh;
		double budget = mhz > 0 ? 1000.0 / mhz : 1.0 / 60.0;
		if (g_prof.asked > 0.0) {
			double wait = f0 - g_prof.asked;
			g_prof.gap_max = wait > g_prof.gap_max ? wait : g_prof.gap_max;
			g_prof.late += wait > budget * 1.5;
			g_prof.asked = 0.0;
		}
		g_prof.last_frame = f0;
		g_prof.frames++;
		double fm = (f2 - f0) * 1000.0, cm = (f2 - f1) * 1000.0;
		g_prof.frame_ms += fm;
		g_prof.commit_ms += cm;
		g_prof.frame_max = fm > g_prof.frame_max ? fm : g_prof.frame_max;
		g_prof.commit_max = cm > g_prof.commit_max ? cm : g_prof.commit_max;
		prof_report(f2);
	}
}

static void output_request_state(struct wl_listener *listener, void *data)
{
	struct desk_output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);
	if (first_output(output->server) == output->wlr_output) {
		desk_resize_to(output->server, output->wlr_output->width,
			output->wlr_output->height);
	}
	om_publish(output->server);
}

static void output_destroy(struct wl_listener *listener, void *data)
{
	struct desk_output *output = wl_container_of(listener, output, destroy);
	struct desk_server *server = output->server;
	(void)data;
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	/* on the way out the scene, and the black in it, has gone already */
	if (output->dark_cover && !server->quitting) {
		wlr_scene_node_destroy(&output->dark_cover->node);
	}
	if (output->side && !server->quitting) {
		wlr_scene_node_destroy(&output->side->node);
	}
	screen_dark_sync(server);
	if (!server->quitting && !wl_list_empty(&server->outputs)) {
		/* out of the layout now, not once it is gone: until then it is
		 * still the screen at the corner */
		wlr_output_layout_remove(server->output_layout, output->wlr_output);
		/* the desk goes over to a screen that is left, which is moved to
		 * the corner it is drawn from, the rest with it */
		struct wlr_output *next = first_output(server);
		struct wlr_box at;
		wlr_output_layout_get_box(server->output_layout, next, &at);
		if (!wlr_box_empty(&at) && (at.x != 0 || at.y != 0)) {
			struct desk_output *o;
			wl_list_for_each(o, &server->outputs, link) {
				struct wlr_box box;
				wlr_output_layout_get_box(server->output_layout, o->wlr_output, &box);
				if (!wlr_box_empty(&box)) {
					wlr_output_layout_add(server->output_layout, o->wlr_output,
						box.x - at.x, box.y - at.y);
				}
			}
		}
		int w, h;
		wlr_output_effective_resolution(next, &w, &h);
		desk_resize_to(server, w, h);
		om_publish(server);
		/* the ones left may have been set out some way of their own */
		displays_changed(server);
	}
	/* the window still being named may be the one that just closed */
	if (server->name_output == output->wlr_output) {
		server->name_output = NULL;
	}
	free(output);

	/* Nested, the output IS the window, so closing the window has to end
	 * the session. Without this the compositor went on running with
	 * nothing to draw on -- invisible, holding its wayland socket, and
	 * indistinguishable from a machine that had simply stopped. */
	if (!server->headless && wl_list_empty(&server->outputs)) {
		wlr_log(WLR_INFO, "last output went away; shutting down");
		wl_display_terminate(server->wl_display);
	}
}

/* ---- the window we are in, when we are in one ------------------------
 *
 * Nested, DEsk is a window on somebody else's desktop, and wlroots calls
 * it "wlroots - X11-1" and gives it no class and no pid -- so the desktop's
 * dock cannot tell it from any other program: it shows a nameless window
 * beside the launcher that started it. Named, classed as the launcher's
 * desktop file is, and carrying our pid, it is the launcher's own window.
 * On a Wayland desktop that is the app_id; on X it has to be done from a
 * connection of our own, because wlroots keeps its window to itself -- so
 * the window is found by a title nobody else has, and then given the real
 * one. */
#define DESK_APP_ID "desk-window"

#if WLR_HAS_X11_BACKEND && defined(DESK_HAVE_XWAYLAND)
static xcb_atom_t x_atom(xcb_connection_t *c, const char *name)
{
	xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c,
		xcb_intern_atom(c, 0, (uint16_t)strlen(name), name), NULL);
	xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
	free(r);
	return a;
}

static xcb_window_t x_find_titled(xcb_connection_t *c, xcb_window_t w, xcb_atom_t name,
	xcb_atom_t utf8, const char *tag, int depth)
{
	xcb_get_property_reply_t *pr = xcb_get_property_reply(c,
		xcb_get_property(c, 0, w, name, utf8, 0, 64), NULL);
	bool hit = pr && xcb_get_property_value_length(pr) == (int)strlen(tag)
		&& memcmp(xcb_get_property_value(pr), tag, strlen(tag)) == 0;
	free(pr);
	if (hit) {
		return w;
	}
	if (depth <= 0) {
		return XCB_WINDOW_NONE;
	}
	/* a window manager puts a frame round it, so it is a level or two down */
	xcb_query_tree_reply_t *tr = xcb_query_tree_reply(c, xcb_query_tree(c, w), NULL);
	xcb_window_t found = XCB_WINDOW_NONE;
	if (tr) {
		xcb_window_t *kids = xcb_query_tree_children(tr);
		int n = xcb_query_tree_children_length(tr);
		for (int i = n - 1; i >= 0 && found == XCB_WINDOW_NONE; i--) {
			found = x_find_titled(c, kids[i], name, utf8, tag, depth - 1);
		}
		free(tr);
	}
	return found;
}

static int name_timer_cb(void *data)
{
	struct desk_server *server = data;
	struct wlr_output *out = server->name_output;
	bool done = false;
	xcb_connection_t *c = xcb_connect(server->parent_display, NULL);
	if (c && !xcb_connection_has_error(c)) {
		xcb_atom_t name = x_atom(c, "_NET_WM_NAME"), utf8 = x_atom(c, "UTF8_STRING");
		xcb_atom_t pid_atom = x_atom(c, "_NET_WM_PID");
		xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(c)).data->root;
		xcb_window_t w = x_find_titled(c, root, name, utf8, server->name_tag, 3);
		if (w != XCB_WINDOW_NONE) {
			static const char cls[] = DESK_APP_ID "\0" DESK_APP_ID;
			xcb_void_cookie_t k1 = xcb_change_property_checked(c, XCB_PROP_MODE_REPLACE, w,
				XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, sizeof cls, cls);
			uint32_t pid = (uint32_t)getpid();
			xcb_void_cookie_t k2 = xcb_change_property_checked(c, XCB_PROP_MODE_REPLACE, w,
				pid_atom, XCB_ATOM_CARDINAL, 32, 1, &pid);
			/* waited for: closing straight after sending them lost them */
			free(xcb_request_check(c, k1));
			free(xcb_request_check(c, k2));
			server->host_window = w;
			done = true;
		}
	}
	if (c) {
		xcb_disconnect(c);
	}
	if (done || ++server->name_tries >= 20) {
		if (out) {
			wlr_x11_output_set_title(out, "DEsk");
		}
		wl_event_source_remove(server->name_timer);
		server->name_timer = NULL;
		server->name_output = NULL;
		return 0;
	}
	wl_event_source_timer_update(server->name_timer, 150);
	return 0;
}

/* Where DEsk's window is on the screen it runs in, when it runs in a
 * window of an X server: for the input method's windows, which go on that
 * screen. */
static bool host_origin(struct desk_server *server, int *ox, int *oy)
{
	if (!server->parent_display || !server->host_window) {
		return false;
	}
	xcb_connection_t *c = server->host_xcb;
	if (!c || xcb_connection_has_error(c)) {
		if (c) {
			xcb_disconnect(c);
		}
		c = xcb_connect(server->parent_display, NULL);
		server->host_xcb = c;
		if (!c || xcb_connection_has_error(c)) {
			return false;
		}
	}
	xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(c)).data->root;
	xcb_translate_coordinates_reply_t *r = xcb_translate_coordinates_reply(c,
		xcb_translate_coordinates(c, server->host_window, root, 0, 0), NULL);
	if (!r) {
		return false;
	}
	*ox = r->dst_x;
	*oy = r->dst_y;
	free(r);
	return true;
}
#else
static bool host_origin(struct desk_server *server, int *ox, int *oy)
{
	(void)server;
	*ox = *oy = 0;
	return false;
}
#endif

static void nested_name(struct desk_server *server, struct wlr_output *out)
{
	if (wlr_output_is_wl(out)) {
		wlr_wl_output_set_title(out, "DEsk");
		wlr_wl_output_set_app_id(out, DESK_APP_ID);
		return;
	}
#if WLR_HAS_X11_BACKEND
	if (!wlr_output_is_x11(out)) {
		return;
	}
#ifdef DESK_HAVE_XWAYLAND
	if (server->parent_display && !server->name_timer) {
		snprintf(server->name_tag, sizeof server->name_tag, "DEsk %d finding itself",
			(int)getpid());
		wlr_x11_output_set_title(out, server->name_tag);
		server->name_output = out;
		server->name_tries = 0;
		server->name_timer = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), name_timer_cb, server);
		if (server->name_timer) {
			wl_event_source_timer_update(server->name_timer, 150);
			return;
		}
	}
#else
	(void)server;   /* without xcb there is no finding the window to class it */
#endif
	wlr_x11_output_set_title(out, "DEsk");
#else
	(void)server;
#endif
}

/* Pixels to a point, the way a Mac has it: 1 on an ordinary screen, 2 on a
 * Retina one. DESK_SCALE says so outright; otherwise it is the screen's
 * density against 96 dots to the inch, in quarters, and 1 below one and a
 * half -- a laptop panel at 120 dpi is still an ordinary screen to anybody
 * sitting at it. A screen that does not say how big it is (a window on
 * another desktop, a projector, an EDID that gives the shape in place of
 * the size) is taken to be ordinary. A display tool's scale for the screen
 * is written into Shell Settings' (om_try). */
static double ui_scale_for(struct desk_server *server, struct wlr_output *out)
{
	double scale = 1.0;
	const char *env = getenv("DESK_SCALE");
	if (env && *env && atof(env) > 0.0) {
		scale = atof(env);
	} else if (server->conf.scale > 0.0) {
		/* Shell Settings, or a display tool through them */
		scale = server->conf.scale;
	} else if (out->phys_width >= 100 && out->phys_height >= 60 && out->width > 0) {
		double dpi = out->width * 25.4 / out->phys_width;
		if (dpi >= 144.0) {
			scale = round(dpi / 96.0 * 4.0) / 4.0;
		}
	}
	return clampf_d(scale, 0.5, 4.0);
}

static void ui_scale_pick(struct desk_server *server, struct wlr_output *out)
{
	double scale = ui_scale_for(server, out);
	desk_set_ui_scale(scale);
	wlr_log(WLR_INFO, "%s: %dx%d, %dx%d mm: a point is %.2f pixels", out->name,
		out->width, out->height, out->phys_width, out->phys_height, scale);
}
static void server_new_output(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;
	wlr_output_init_render(wlr_output, server->allocator, server->renderer);
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	/* --size picks the window's size under a nested backend too, not just
	 * the headless one: an X11 window would otherwise come up at whatever
	 * wlroots defaults to. */
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (server->size_set) {
		wlr_output_state_set_custom_mode(&state, server->headless_w,
			server->headless_h, 0);
	} else if (mode != NULL) {
		wlr_output_state_set_mode(&state, mode);
	}
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);
	nested_name(server, wlr_output);

	struct desk_output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	output->server = server;
	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);
	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);
	wl_list_insert(&server->outputs, &output->link);

	struct wlr_output_layout_output *l_output = wlr_output_layout_add_auto(server->output_layout, wlr_output);
	struct wlr_scene_output *scene_output = wlr_scene_output_create(server->scene, wlr_output);
	wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);

	if (first_output(server) == wlr_output) {
		/* The size we ASKED for, not the one the backend happens to be
		 * showing this instant. Under X11 the window comes up at the
		 * backend's own default and only reaches the custom mode a beat
		 * later -- and by then the first paint has already put a whole
		 * second room on the ray tracer at 1024x768, which costs a minute
		 * of four cores and is thrown away unlooked at. */
		server->desk_w = server->size_set ? server->headless_w : wlr_output->width;
		server->desk_h = server->size_set ? server->headless_h : wlr_output->height;
		ui_scale_pick(server, wlr_output);
	} else {
		/* a second screen plugged in is more room for windows; the desk
		 * stays where it is */
		wlr_log(WLR_INFO, "%s: %dx%d, a screen beside the desk's", wlr_output->name,
			wlr_output->width, wlr_output->height);
	}
	/* pointers that belong to this screen, here before it */
	struct desk_pointer *pt;
	wl_list_for_each(pt, &server->pointers, link) {
		input_map_to_output(server, pt->device);
	}
	/* what is on the shelf, read on a worker while the room bakes */
	desk_apps_init(true);
	desktop_paint(server);
	if (server->dump_path) {
		desk_write_png(server->dump_path, server->desk_w, server->desk_h, true);
		wlr_log(WLR_INFO, "dumped chrome to %s", server->dump_path);
	}
	/* plugged in while nobody is here: it waits dark with the rest */
	if (server->dark) {
		output_set_dark(output, true);
	}
	om_publish(server);
	/* and the screens set out as they were last time these were here */
	displays_changed(server);
}

/* ---- an input method's own windows -----------------------------------
 *
 * IBus's panel shows the candidates -- Mozc's among them, which on Wayland
 * gives up its own window for IBus's -- in a window of its own, and on
 * Wayland that is a toplevel like any program's: it came onto the desk as
 * a sheet of paper, offered a sheet's size, took the keys from the text
 * being written, and was crumpled up and thrown away every time it went,
 * which is every key or two. It is not a window on the desk but a moment
 * of one, as an override-redirect X window is (xview_passing): its own
 * size, nothing of ours round it, never the keys, over every window, and
 * gone without a word. And as a Wayland window cannot say where it goes,
 * it goes where IBus puts it on X: under the text cursor. */
static bool ime_window_is(const struct desk_toplevel *view)
{
	return view->xdg_toplevel && ime_window_name(view->xdg_toplevel->app_id);
}

/* Under the cursor of the text being written, or over it where there is
 * no room under it, and all of it on the screen the cursor is on; with
 * nothing being written, a third of the way down the screen. */
static void ime_window_place(struct desk_toplevel *view)
{
	struct desk_server *server = view->server;
	struct wlr_xdg_surface *xs = view->xdg_toplevel->base;
	struct wlr_box g;
	wlr_xdg_surface_get_geometry(xs, &g);
	int w = g.width > 0 ? g.width : xs->surface->current.width;
	int h = g.height > 0 ? g.height : xs->surface->current.height;
	struct wlr_box at = server->ime_at;
	struct wlr_output *out = at.height > 0 ? wlr_output_layout_output_at(server->output_layout,
		at.x + at.width * 0.5, at.y + at.height * 0.5) : NULL;
	if (!out) {
		out = first_output(server);
	}
	struct wlr_box sc = { 0 };
	if (out) {
		wlr_output_layout_get_box(server->output_layout, out, &sc);
	}
	int x, y;
	if (at.height > 0) {
		x = at.x;
		y = at.y + at.height;
		if (sc.height > 0 && y + h > sc.y + sc.height && at.y - h >= sc.y) {
			y = at.y - h;
		}
	} else {
		x = sc.x + (sc.width - w) / 2;
		y = sc.y + sc.height / 3;
	}
	if (sc.width > 0) {
		x = x > sc.x + sc.width - w ? sc.x + sc.width - w : x;
		x = x < sc.x ? sc.x : x;
		y = y > sc.y + sc.height - h ? sc.y + sc.height - h : y;
		y = y < sc.y ? sc.y : y;
	}
	wlr_scene_node_set_position(&view->scene_tree->node, x - g.x, y - g.y);
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, map);
	(void)data;
	if (toplevel->ime_popup) {
		wlr_scene_node_reparent(&toplevel->scene_tree->node, toplevel->server->layer_xover);
		ime_window_place(toplevel);
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
		return;
	}
	wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
	/* onto the sheet taken out of a binder for it, or the next place down */
	struct desk_base *b = toplevel->xdg_toplevel->parent ? NULL
		: base_claim(toplevel->server, toplevel);
	int x, y;
	if (b) {
		x = b->x;
		y = b->y;
	} else {
		cascade_next(toplevel->server, &x, &y);
	}
	wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
	/* A client that never asked for our frame draws its own -- a GTK one
	 * with a header bar does: it is laid on our paper, or left as it is
	 * (shell.conf) */
	view_frame_init(toplevel);
	view_repaint_chrome(toplevel);
	view_sync_shadow(toplevel);
	view_check_above(toplevel);
	focus_toplevel(toplevel);
	base_after_map(toplevel->server, toplevel);
	wlr_log(WLR_INFO, "a window comes onto the desk: %s (%s) at %d,%d, %dx%d, %s",
		view_title(toplevel), toplevel->xdg_toplevel->app_id ? toplevel->xdg_toplevel->app_id : "-",
		toplevel->scene_tree->node.x, toplevel->scene_tree->node.y,
		toplevel->outer_w, toplevel->outer_h,
		toplevel->bare ? "its own frame" : toplevel->own_frame ? "its own frame, on paper"
			: "on our paper");
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
	(void)data;
	struct desk_server *server = toplevel->server;
	if (toplevel->ime_popup) {
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
		if (server->seat->keyboard_state.focused_surface == view_surface(toplevel)) {
			focus_next(server);
		}
		return;
	}
	/* while what it last drew is still there to screw up */
	view_throw_away(toplevel);
	base_forget_view(server, toplevel);
	view_unfold_cancel(toplevel);
	view_sizing_stop(toplevel);
	if (toplevel == server->grabbed) {
		reset_cursor_mode(server);
	}
	sheet_cleanup(toplevel->server, &toplevel->sheet);
	wl_list_remove(&toplevel->link);
	if (toplevel->fullscreen) {
		toplevel->fullscreen = false;
		wlr_scene_node_reparent(&toplevel->scene_tree->node, server->layer_win);
		fullscreen_layer_order(server);
	}
	/* the frame and the shadow are ours, not the client's: they go off
	 * the screen with it */
	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	/* the keys go to whatever is on top now, not nowhere */
	if (server->seat->keyboard_state.focused_surface == view_surface(toplevel)) {
		focus_next(server);
	}
	end_check(server);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, commit);
	(void)data;
	if (toplevel->xdg_toplevel->base->initial_commit && ime_window_is(toplevel)) {
		/* its own size, and nothing of ours round it; and not a window
		 * for the pointer to find (view_at), so a click on a candidate
		 * leaves the keys where they are */
		toplevel->ime_popup = true;
		toplevel->scene_tree->node.data = NULL;
		struct desk_server *server = toplevel->server;
		for (size_t i = 0; i < sizeof server->ime_win / sizeof *server->ime_win; i++) {
			if (!server->ime_win[i]) {
				server->ime_win[i] = toplevel;
				break;
			}
		}
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
		if (toplevel->deco) {
			wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->deco,
				WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
		}
		return;
	}
	if (toplevel->ime_popup) {
		/* the same place as it grows and shrinks, and the cursor moves */
		if (toplevel->scene_tree->node.enabled) {
			ime_window_place(toplevel);
		}
		return;
	}
	if (toplevel->xdg_toplevel->base->initial_commit) {
		/* the size a new window is offered, within what the desk has room
		 * for -- the size of a sheet put down for it, too */
		int ow, oh;
		window_offer(toplevel->server, &ow, &oh);
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, ow, oh);
		/* a program that draws its own frame and is to be laid on our
		 * paper is told so in its first configure, so its first frame
		 * comes without a shadow of its own */
		if (view_draws_own_frame(toplevel) && view_wants_paper(toplevel->server, toplevel)) {
			wlr_xdg_toplevel_set_tiled(toplevel->xdg_toplevel,
				WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
		}
		/* asked for our frame before it could be told: tell it now */
		if (toplevel->deco) {
			wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->deco,
				WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
		}
	}
	view_repaint_chrome(toplevel);
}

static void deco_forget(struct desk_toplevel *view);

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);
	(void)data;
	struct desk_server *server = toplevel->server;
	for (size_t i = 0; i < sizeof server->ime_win / sizeof *server->ime_win; i++) {
		if (server->ime_win[i] == toplevel) {
			server->ime_win[i] = NULL;
		}
	}
	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_remove(&toplevel->destroy.link);
	wl_list_remove(&toplevel->request_move.link);
	wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->request_fullscreen.link);
	wl_list_remove(&toplevel->request_minimize.link);
	wl_list_remove(&toplevel->set_title.link);
	deco_forget(toplevel);
	base_forget_view(toplevel->server, toplevel);
	sheet_cleanup(toplevel->server, &toplevel->sheet);
	/* and are thrown away with it: the frame points back at this */
	wlr_scene_node_destroy(&toplevel->scene_tree->node);
	free(toplevel);
}

static void xdg_toplevel_request_move(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
	(void)data;
	if (!toplevel->ime_popup) {
		begin_interactive(toplevel, DESK_CUR_MOVE, 0);
	}
}

static void xdg_toplevel_request_resize(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
	if (!toplevel->ime_popup) {
		begin_interactive(toplevel, DESK_CUR_RESIZE, event->edges);
	}
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, request_maximize);
	(void)data;
	if (toplevel->xdg_toplevel->base->initialized && !toplevel->ime_popup) {
		view_toggle_max(toplevel);
	}
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, request_fullscreen);
	(void)data;
	if (toplevel->xdg_toplevel->base->initialized && !toplevel->ime_popup) {
		view_set_fullscreen(toplevel, toplevel->xdg_toplevel->requested.fullscreen);
	}
}

static void xdg_toplevel_request_minimize(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, request_minimize);
	(void)data;
	if (toplevel->sheet.phase == SHEET_IDLE && !toplevel->ime_popup) {
		view_fold_toggle(toplevel);
	}
}

static void xdg_toplevel_set_title(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *toplevel = wl_container_of(listener, toplevel, set_title);
	(void)data;
	if (!toplevel->ime_popup) {
		view_repaint_chrome(toplevel);
	}
	view_check_above(toplevel);
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;
	struct desk_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	toplevel->server = server;
	view_curl_reset(toplevel);
	toplevel->xdg_toplevel = xdg_toplevel;
	toplevel->scene_tree = wlr_scene_tree_create(server->layer_win);
	toplevel->scene_tree->node.data = toplevel;
	/* shown at map, when it has somewhere to be */
	wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
	toplevel->surface_tree = wlr_scene_xdg_surface_create(toplevel->scene_tree, xdg_toplevel->base);
	xdg_toplevel->base->data = toplevel->scene_tree;

	toplevel->map.notify = xdg_toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
	toplevel->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
	toplevel->commit.notify = xdg_toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);
	toplevel->destroy.notify = xdg_toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);
	toplevel->request_move.notify = xdg_toplevel_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
	toplevel->request_resize.notify = xdg_toplevel_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);
	toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);
	toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);
	toplevel->request_minimize.notify = xdg_toplevel_request_minimize;
	wl_signal_add(&xdg_toplevel->events.request_minimize, &toplevel->request_minimize);
	toplevel->set_title.notify = xdg_toplevel_set_title;
	wl_signal_add(&xdg_toplevel->events.set_title, &toplevel->set_title);
}

static void xdg_popup_commit(struct wl_listener *listener, void *data)
{
	struct desk_popup *popup = wl_container_of(listener, popup, commit);
	(void)data;
	if (popup->xdg_popup->base->initial_commit) {
		wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
	}
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data)
{
	struct desk_popup *popup = wl_container_of(listener, popup, destroy);
	(void)data;
	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);
	free(popup);
}

static void server_new_xdg_popup(struct wl_listener *listener, void *data)
{
	(void)listener;
	struct wlr_xdg_popup *xdg_popup = data;
	/* A popup may come with no parent (another protocol, such as
	 * layer-shell, is meant to give it one later; there is none here, and
	 * wlroots refuses it at its first commit), or on such a popup, which
	 * was given no place. Neither is placed, and neither may bring the
	 * desk down. */
	struct wlr_xdg_surface *parent = xdg_popup->parent
		? wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent) : NULL;
	if (!parent || !parent->data) {
		return;
	}
	struct desk_popup *popup = calloc(1, sizeof(*popup));
	if (!popup) {
		return;
	}
	popup->xdg_popup = xdg_popup;
	struct wlr_scene_tree *parent_tree = parent->data;
	/* A menu is placed against its window's geometry, and the window's
	 * surface node is where that is: inside our frame, not at the
	 * frame's corner (where a menu came up the frame's border to the left
	 * and its heading above where it was asked for). */
	if (parent->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL && parent_tree
		&& parent_tree->node.data) {
		struct desk_toplevel *pv = parent_tree->node.data;
		if (pv->surface_tree) {
			parent_tree = pv->surface_tree;
		}
	}
	xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);
	popup->commit.notify = xdg_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);
	popup->destroy.notify = xdg_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

/* A client asking for our frame. It gets it whatever mode it asks for --
 * the frame is the paper -- but it can only be told once its surface is
 * set up, which for most clients is not yet: then at its first commit. */
static void deco_request_mode(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, deco_request_mode);
	(void)data;
	if (view->deco && view->xdg_toplevel->base->initialized) {
		wlr_xdg_toplevel_decoration_v1_set_mode(view->deco, view->ime_popup
			? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
			: WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
}

static void deco_forget(struct desk_toplevel *view)
{
	if (!view->deco) {
		return;
	}
	wl_list_remove(&view->deco_destroy.link);
	wl_list_remove(&view->deco_request_mode.link);
	view->deco = NULL;
}

static void deco_destroy(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, deco_destroy);
	(void)data;
	deco_forget(view);
	/* it draws its own from now on */
	if (view->scene_tree && view->scene_tree->node.enabled) {
		view_frame_update(view);
	}
}

static void server_new_decoration(struct wl_listener *listener, void *data)
{
	(void)listener;
	struct wlr_xdg_toplevel_decoration_v1 *deco = data;
	struct wlr_scene_tree *tree = deco->toplevel->base->data;
	struct desk_toplevel *view = tree ? tree->node.data : NULL;
	if (!view || view->deco) {
		return;
	}
	view->deco = deco;
	view->deco_destroy.notify = deco_destroy;
	wl_signal_add(&deco->events.destroy, &view->deco_destroy);
	view->deco_request_mode.notify = deco_request_mode;
	wl_signal_add(&deco->events.request_mode, &view->deco_request_mode);
	if (deco->toplevel->base->initialized) {
		wlr_xdg_toplevel_decoration_v1_set_mode(deco,
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
	if (view->scene_tree && view->scene_tree->node.enabled) {
		/* asked for late, after it was already on the desk without one */
		view_frame_update(view);
	}
}

/* What a surface says by KDE's protocol has changed, or it has stopped
 * saying anything: if its window is on the desk, its frame is decided
 * again. (One not out yet is decided as it comes out.) */
static void kde_frame_again(struct desk_server *server, struct wlr_surface *surface)
{
	if (server->quitting) {
		return;
	}
	struct desk_toplevel *view = view_of_surface(server, surface);
	if (view && view->xdg_toplevel && view->scene_tree && view->scene_tree->node.enabled) {
		view_frame_update(view);
	}
}

static void kde_deco_mode(struct wl_listener *listener, void *data)
{
	struct desk_kde_deco *k = wl_container_of(listener, k, mode);
	(void)data;
	kde_frame_again(k->server, k->deco->surface);
}

static void kde_deco_destroy(struct wl_listener *listener, void *data)
{
	struct desk_kde_deco *k = wl_container_of(listener, k, destroy);
	(void)data;
	struct desk_server *server = k->server;
	struct wlr_surface *surface = k->deco->surface;
	wl_list_remove(&k->link);
	wl_list_remove(&k->mode.link);
	wl_list_remove(&k->destroy.link);
	free(k);
	/* it draws its own from now on, if it is still there */
	kde_frame_again(server, surface);
}

static void server_new_kde_deco(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, new_kde_deco);
	struct wlr_server_decoration *deco = data;
	struct desk_kde_deco *k = calloc(1, sizeof *k);
	if (!k) {
		return;
	}
	k->server = server;
	k->deco = deco;
	k->mode.notify = kde_deco_mode;
	wl_signal_add(&deco->events.mode, &k->mode);
	k->destroy.notify = kde_deco_destroy;
	wl_signal_add(&deco->events.destroy, &k->destroy);
	wl_list_insert(&server->kde_decos, &k->link);
	/* said late, when its window was already on the desk */
	kde_frame_again(server, deco->surface);
}

#ifdef DESK_HAVE_XWAYLAND
/* ---- X11 programs, through Xwayland -----------------------------------
 *
 * An X program talks to an X server of its own, which wlroots starts the
 * first time one connects and whose windows come to us as surfaces. Most
 * of them are windows like any other and go on the desk as sheets. The
 * rest are moments of a window -- a menu, a tooltip, a list dropping down
 * -- which X lets a program put anywhere without asking. Those go exactly
 * where they say, over everything, and are not ours to move. */

enum {
	XT_NORMAL, XT_DIALOG, XT_UTILITY, XT_TOOLBAR, XT_SPLASH,
	XT_MENU, XT_DROPDOWN_MENU, XT_POPUP_MENU, XT_TOOLTIP, XT_NOTIFICATION,
	XT_COMBO, XT_DND, XT_COUNT
};
static const char *const xtype_names[XT_COUNT] = {
	"_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WM_WINDOW_TYPE_DIALOG",
	"_NET_WM_WINDOW_TYPE_UTILITY", "_NET_WM_WINDOW_TYPE_TOOLBAR",
	"_NET_WM_WINDOW_TYPE_SPLASH", "_NET_WM_WINDOW_TYPE_MENU",
	"_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", "_NET_WM_WINDOW_TYPE_POPUP_MENU",
	"_NET_WM_WINDOW_TYPE_TOOLTIP", "_NET_WM_WINDOW_TYPE_NOTIFICATION",
	"_NET_WM_WINDOW_TYPE_COMBO", "_NET_WM_WINDOW_TYPE_DND",
};
_Static_assert(XT_COUNT <= sizeof ((struct desk_server *)0)->xatom
	/ sizeof ((struct desk_server *)0)->xatom[0], "xatom[] is too small");

static bool xview_is(const struct desk_server *server,
	const struct wlr_xwayland_surface *xs, int type)
{
	uint32_t atom = server->xatom[type];
	if (atom == XCB_ATOM_NONE) {
		return false;
	}
	for (size_t i = 0; i < xs->window_type_len; i++) {
		if (xs->window_type[i] == atom) {
			return true;
		}
	}
	return false;
}

static bool xview_is_splash(const struct desk_server *server,
	const struct wlr_xwayland_surface *xs)
{
	return xview_is(server, xs, XT_SPLASH);
}

/* A moment of a window rather than a window. Override-redirect says so
 * outright; the rest say it by their type. */
static bool xview_passing(const struct desk_server *server,
	const struct wlr_xwayland_surface *xs)
{
	if (ime_window_name(xs->instance) || ime_window_name(xs->class)) {
		/* the input method's, whatever it says it is (ime_window_is) */
		return true;
	}
	if (xs->override_redirect) {
		return true;
	}
	static const int passing[] = { XT_MENU, XT_DROPDOWN_MENU, XT_POPUP_MENU,
		XT_TOOLTIP, XT_NOTIFICATION, XT_COMBO, XT_DND };
	for (size_t i = 0; i < sizeof passing / sizeof passing[0]; i++) {
		if (xview_is(server, xs, passing[i])) {
			return true;
		}
	}
	return false;
}

/* Where a new X window goes on the desk, and how big it may be there --
 * which is no bigger than the screen. A program restoring the size it had
 * on a bigger one would otherwise hang off the edges, and come back that
 * way from every maximise. The client is told both. */
static void xview_place(struct desk_toplevel *view)
{
	struct desk_server *server = view->server;
	struct wlr_xwayland_surface *xs = view->xsurface;
	int l, t, r, b;
	view_insets(view, &l, &t, &r, &b);
	const xcb_size_hints_t *hints = xs->size_hints;
	/* the screen it goes on: its window's, for a dialog; the one it asked
	 * to be on; or the pointer's */
	struct desk_toplevel *parent = xs->parent ? xs->parent->data : NULL;
	bool asked = hints && (hints->flags & (XCB_ICCCM_SIZE_HINT_US_POSITION
		| XCB_ICCCM_SIZE_HINT_P_POSITION)) && (xs->x != 0 || xs->y != 0);
	struct wlr_output *out = parent && parent->scene_tree && !parent->unmanaged
		? view_screen(parent) : asked ? screen_at(server, xs->x, xs->y)
		: screen_at(server, server->cursor->x, server->cursor->y);
	struct wlr_box wa = screen_area(server, out);
	int iw = view->outer_w - l - r, ih = view->outer_h - t - b;
	bool resized = false;
	if (wa.width > 0 && wa.height > 0) {
		int max_w = wa.width - l - r;
		int max_h = wa.height - t - b;
		int min_w = 1, min_h = 1;
		if (hints && (hints->flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE)) {
			min_w = hints->min_width > 1 ? hints->min_width : 1;
			min_h = hints->min_height > 1 ? hints->min_height : 1;
		}
		int fw = iw > max_w ? max_w : iw;
		int fh = ih > max_h ? max_h : ih;
		fw = fw < min_w ? min_w : fw;
		fh = fh < min_h ? min_h : fh;
		if (fw != iw || fh != ih) {
			iw = fw;
			ih = fh;
			view->outer_w = iw + l + r;
			view->outer_h = ih + t + b;
			resized = true;
		}
	}
	int w = view->outer_w, h = view->outer_h;
	int x, y;
	if (parent && parent->scene_tree && !parent->unmanaged) {
		/* a dialog: over the window it belongs to */
		x = parent->scene_tree->node.x + (parent->outer_w - w) / 2;
		y = parent->scene_tree->node.y + (parent->outer_h - h) / 3;
	} else if (asked) {
		/* it asked to be somewhere: -geometry +x+y, or where it was left */
		x = xs->x - l;
		y = xs->y - t;
	} else if (xview_is(server, xs, XT_SPLASH) || xs->modal) {
		x = wa.x + (wa.width - w) / 2;
		y = wa.y + (wa.height - h) / 2;
	} else {
		struct desk_base *base = base_claim(server, view);
		if (base) {
			x = base->x;
			y = base->y;
		} else {
			cascade_next(server, &x, &y);
		}
	}
	/* all of it on its screen, where it fits, and none of it on the tray */
	if (x + w > wa.x + wa.width) {
		x = wa.x + wa.width - w;
	}
	if (y + h > wa.y + wa.height) {
		y = wa.y + wa.height - h;
	}
	if (x < wa.x) {
		x = wa.x;
	}
	if (y < wa.y) {
		y = wa.y;
	}
	wlr_scene_node_set_position(&view->scene_tree->node, x, y);
	if (resized) {
		view_set_size(view, iw, ih);
	} else {
		view_send_position(view);
	}
}

static void xview_map(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, map);
	(void)data;
	struct desk_server *server = view->server;
	struct wlr_xwayland_surface *xs = view->xsurface;
	if (view->scene_tree || !xs->surface) {
		return;
	}
	view->unmanaged = xview_passing(server, xs);
	if (view->unmanaged) {
		view->scene_tree = wlr_scene_tree_create(server->layer_xover);
		view->surface_tree = wlr_scene_subsurface_tree_create(view->scene_tree, xs->surface);
		wlr_scene_node_set_position(&view->scene_tree->node, xs->x, xs->y);
		/* never while the door is locked: the keys are the lock's; and
		 * never the input method's candidates, which say they take the
		 * keys (IBus's are a "normal" window that accepts input) and took
		 * them from the text they were for -- which put them away */
		if (wlr_xwayland_or_surface_wants_focus(xs) && !locked(server)
			&& !ime_window_name(xs->instance) && !ime_window_name(xs->class)) {
			struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
			wlr_seat_keyboard_notify_enter(server->seat, xs->surface,
				kb ? kb->keycodes : NULL, kb ? kb->num_keycodes : 0,
				kb ? &kb->modifiers : NULL);
		}
		return;
	}
	view_frame_init(view);
	view->scene_tree = wlr_scene_tree_create(server->layer_win);
	view->scene_tree->node.data = view;
	view->surface_tree = wlr_scene_subsurface_tree_create(view->scene_tree, xs->surface);
	view->chrome_key = 0;
	int iw = xs->surface->current.width > 0 ? xs->surface->current.width : xs->width;
	int ih = xs->surface->current.height > 0 ? xs->surface->current.height : xs->height;
	view_paint_chrome(view, iw, ih);
	xview_place(view);
	wl_list_insert(&server->toplevels, &view->link);
	view_sync_shadow(view);
	if (xs->fullscreen) {
		view_set_fullscreen(view, true);
	} else if (xs->maximized_horz || xs->maximized_vert) {
		view_toggle_max_now(view);
	}
	view_check_above(view);
	focus_toplevel(view);
	base_after_map(server, view);
}

/* Off the screen: the scene goes with it, and comes back new if it is
 * mapped again -- as a window, or, if it has changed its mind in between,
 * as a menu. */
static void xview_unmap_now(struct desk_toplevel *view)
{
	struct desk_server *server = view->server;
	if (!view->scene_tree) {
		return;
	}
	struct wlr_surface *s = view->xsurface->surface;
	bool had_keys = s && server->seat->keyboard_state.focused_surface == s;
	if (!view->unmanaged) {
		view_throw_away(view);
		base_forget_view(server, view);
		view_unfold_cancel(view);
		view_sizing_stop(view);
		if (view == server->grabbed) {
			reset_cursor_mode(server);
		}
		sheet_cleanup(view->server, &view->sheet);
		wl_list_remove(&view->link);
	}
	wlr_scene_node_destroy(&view->scene_tree->node);
	view->scene_tree = NULL;
	view->surface_tree = NULL;
	view->chrome = NULL;
	view_curl_reset(view);
	memset(&view->shadow, 0, sizeof view->shadow);
	view->chrome_key = 0;
	view->fullscreen = false;
	fullscreen_layer_order(server);
	view->maximized = 0;
	view->unmanaged = false;
	if (had_keys) {
		focus_next(server);
	}
	end_check(server);
}

static void xview_unmap(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, unmap);
	(void)data;
	xview_unmap_now(view);
}

static void xview_commit(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, commit);
	(void)data;
	if (view->scene_tree && !view->unmanaged) {
		view_repaint_chrome(view);
	}
}

static void xview_associate(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, associate);
	(void)data;
	struct wlr_surface *s = view->xsurface->surface;
	if (view->associated || !s) {
		return;
	}
	view->map.notify = xview_map;
	wl_signal_add(&s->events.map, &view->map);
	view->unmap.notify = xview_unmap;
	wl_signal_add(&s->events.unmap, &view->unmap);
	view->commit.notify = xview_commit;
	wl_signal_add(&s->events.commit, &view->commit);
	view->associated = true;
}

static void xview_dissociate(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, dissociate);
	(void)data;
	xview_unmap_now(view);
	if (view->associated) {
		wl_list_remove(&view->map.link);
		wl_list_remove(&view->unmap.link);
		wl_list_remove(&view->commit.link);
		view->associated = false;
	}
}

/* A client asking to be a size, or somewhere. Before it is on the screen,
 * and for a menu, it gets what it asks for. A window on the desk gets the
 * size; where it lies is ours. */
static void xview_request_configure(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, request_configure);
	struct wlr_xwayland_surface_configure_event *ev = data;
	struct wlr_xwayland_surface *xs = view->xsurface;
	if (!view->scene_tree || view->unmanaged) {
		wlr_xwayland_surface_configure(xs, ev->x, ev->y, ev->width, ev->height);
		if (view->scene_tree) {
			wlr_scene_node_set_position(&view->scene_tree->node, ev->x, ev->y);
		}
		return;
	}
	if (view->fullscreen) {
		/* the whole of the screen it is on, whatever it asks */
		struct wlr_box box;
		wlr_output_layout_get_box(view->server->output_layout, view_screen(view), &box);
		if (!wlr_box_empty(&box)) {
			wlr_xwayland_surface_configure(xs, (int16_t)box.x, (int16_t)box.y,
				(uint16_t)box.width, (uint16_t)box.height);
			return;
		}
	}
	view_set_size(view, ev->width, ev->height);
}

static void xview_set_geometry(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, set_geometry);
	(void)data;
	if (view->scene_tree && view->unmanaged) {
		wlr_scene_node_set_position(&view->scene_tree->node,
			view->xsurface->x, view->xsurface->y);
	}
}

static bool xview_on_desk(const struct desk_toplevel *view)
{
	return view->scene_tree && !view->unmanaged;
}

static void xview_request_activate(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, request_activate);
	(void)data;
	if (xview_on_desk(view) && view->sheet.phase == SHEET_IDLE) {
		focus_toplevel(view);
	}
}

static void xview_request_move(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, request_move);
	(void)data;
	if (xview_on_desk(view)) {
		begin_interactive(view, DESK_CUR_MOVE, 0);
	}
}

static void xview_request_resize(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, request_resize);
	struct wlr_xwayland_resize_event *ev = data;
	if (xview_on_desk(view)) {
		begin_interactive(view, DESK_CUR_RESIZE, ev->edges);
	}
}

static void xview_request_maximize(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, request_maximize);
	(void)data;
	if (!xview_on_desk(view)) {
		return;
	}
	struct wlr_xwayland_surface *xs = view->xsurface;
	bool want = xs->maximized_horz || xs->maximized_vert;
	if (want != (view->maximized != 0)) {
		view_toggle_max(view);
	} else {
		view_set_maximized(view, view->maximized != 0);
	}
}

static void xview_request_fullscreen(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, request_fullscreen);
	(void)data;
	if (xview_on_desk(view)) {
		view_set_fullscreen(view, view->xsurface->fullscreen);
	}
}

static void xview_request_minimize(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, request_minimize);
	struct wlr_xwayland_minimize_event *ev = data;
	if (xview_on_desk(view) && ev->minimize == (view->sheet.phase == SHEET_IDLE)) {
		view_fold_toggle(view);
	}
}

static void xview_set_title(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, set_title);
	(void)data;
	if (xview_on_desk(view)) {
		view_repaint_chrome(view);
		view_check_above(view);
	}
}

static void xview_set_decorations(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, set_decorations);
	(void)data;
	if (!xview_on_desk(view)) {
		return;
	}
	view_frame_update(view);
}

static void xview_destroy(struct wl_listener *listener, void *data)
{
	struct desk_toplevel *view = wl_container_of(listener, view, destroy);
	(void)data;
	xview_unmap_now(view);
	if (view->associated) {
		wl_list_remove(&view->map.link);
		wl_list_remove(&view->unmap.link);
		wl_list_remove(&view->commit.link);
	}
	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->associate.link);
	wl_list_remove(&view->dissociate.link);
	wl_list_remove(&view->request_configure.link);
	wl_list_remove(&view->request_activate.link);
	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_fullscreen.link);
	wl_list_remove(&view->request_minimize.link);
	wl_list_remove(&view->set_title.link);
	wl_list_remove(&view->set_decorations.link);
	wl_list_remove(&view->set_geometry.link);
	sheet_cleanup(view->server, &view->sheet);
	view->xsurface->data = NULL;
	free(view);
}

static void server_new_xwayland_surface(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, new_xwayland_surface);
	struct wlr_xwayland_surface *xs = data;
	struct desk_toplevel *view = calloc(1, sizeof *view);
	if (!view) {
		return;
	}
	view->server = server;
	view->xsurface = xs;
	view_curl_reset(view);
	xs->data = view;
	struct { struct wl_signal *sig; struct wl_listener *l; wl_notify_func_t fn; } hooks[] = {
		{ &xs->events.destroy, &view->destroy, xview_destroy },
		{ &xs->events.associate, &view->associate, xview_associate },
		{ &xs->events.dissociate, &view->dissociate, xview_dissociate },
		{ &xs->events.request_configure, &view->request_configure, xview_request_configure },
		{ &xs->events.request_activate, &view->request_activate, xview_request_activate },
		{ &xs->events.request_move, &view->request_move, xview_request_move },
		{ &xs->events.request_resize, &view->request_resize, xview_request_resize },
		{ &xs->events.request_maximize, &view->request_maximize, xview_request_maximize },
		{ &xs->events.request_fullscreen, &view->request_fullscreen, xview_request_fullscreen },
		{ &xs->events.request_minimize, &view->request_minimize, xview_request_minimize },
		{ &xs->events.set_title, &view->set_title, xview_set_title },
		{ &xs->events.set_decorations, &view->set_decorations, xview_set_decorations },
		{ &xs->events.set_geometry, &view->set_geometry, xview_set_geometry },
	};
	for (size_t i = 0; i < sizeof hooks / sizeof hooks[0]; i++) {
		hooks[i].l->notify = hooks[i].fn;
		wl_signal_add(hooks[i].sig, hooks[i].l);
	}
}

/* Two holes in wlroots 0.18's handling of an Xwayland that dies, both of
 * which take the whole compositor down with it -- and an X server does die:
 * a program crashes it, a driver does, somebody kills it. Both are patched
 * from out here, in the server's public state.
 *
 * The window manager's socket belongs to its xcb connection, which closes
 * it -- and wlroots closes it again when the server goes, by which time the
 * number may be somebody else's: the restart has just opened new ones. The
 * wrong file goes, the event loop is left dispatching into freed memory.
 * So wlroots is made to forget the socket once xcb has it.
 *
 * And if Xwayland dies before it has said it is ready, wlroots leaves the
 * dead client in place for the readiness handler to deal with -- which
 * destroys it a second time. So it is taken out of the server once gone. */
#if WLR_VERSION_MAJOR == 0 && WLR_VERSION_MINOR == 18
#define DESK_XWAYLAND_PATCHES 1
#endif

static void xwayland_client_gone(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, xwayland_client_gone);
	wl_list_init(&listener->link);
	struct wlr_xwayland_server *xs = server->xwayland ? server->xwayland->server : NULL;
	if (xs && xs->client == data) {
		xs->client = NULL;
	}
	/* and see, in a moment, whether wlroots has given up on it */
	if (server->xwayland_retry) {
		wl_event_source_timer_update(server->xwayland_retry, 1500);
	}
}

static void xwayland_started(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, xwayland_start);
	(void)data;
	struct wlr_xwayland_server *xs = server->xwayland->server;
	wl_list_remove(&server->xwayland_client_gone.link);
	wl_list_init(&server->xwayland_client_gone.link);
	if (xs && xs->client) {
		wl_client_add_destroy_listener(xs->client, &server->xwayland_client_gone);
	}
}

/* X is up -- the first X program has connected. Give it the seat, an
 * arrow for over its own windows, and learn the names of the window types. */
static void xwayland_ready(struct wl_listener *listener, void *data)
{
	struct desk_server *server = wl_container_of(listener, server, xwayland_ready);
	(void)data;
#ifdef DESK_XWAYLAND_PATCHES
	/* the window manager's xcb connection has the socket now */
	if (server->xwayland->server && server->xwayland->xwm) {
		server->xwayland->server->wm_fd[0] = -1;
	}
#endif
	wlr_xwayland_set_seat(server->xwayland, server->seat);
	if (wlr_xcursor_manager_load(server->cursor_mgr, 1)) {
		struct wlr_xcursor *xc = wlr_xcursor_manager_get_xcursor(server->cursor_mgr,
			"default", 1);
		if (xc && xc->image_count > 0) {
			struct wlr_xcursor_image *im = xc->images[0];
			wlr_xwayland_set_cursor(server->xwayland, im->buffer, im->width * 4,
				im->width, im->height, (int32_t)im->hotspot_x, (int32_t)im->hotspot_y);
		}
	}
	/* where windows go, for the programs that place their own */
	struct wlr_box wa = work_area(server);
	wlr_xwayland_set_workareas(server->xwayland, &wa, 1);
	xcb_connection_t *xc = wlr_xwayland_get_xwm_connection(server->xwayland);
	if (xc) {
		xcb_intern_atom_cookie_t ck[XT_COUNT];
		for (int i = 0; i < XT_COUNT; i++) {
			ck[i] = xcb_intern_atom(xc, 0, (uint16_t)strlen(xtype_names[i]), xtype_names[i]);
		}
		for (int i = 0; i < XT_COUNT; i++) {
			xcb_intern_atom_reply_t *rep = xcb_intern_atom_reply(xc, ck[i], NULL);
			server->xatom[i] = rep ? rep->atom : XCB_ATOM_NONE;
			free(rep);
		}
	}
	wlr_log(WLR_INFO, "X11 programs: Xwayland is up on DISPLAY=%s",
		server->xwayland->display_name);
}

/* Is there an Xwayland to start? WLR_XWAYLAND if it is set, else the first
 * on PATH -- which is then handed to wlroots, which otherwise only knows the
 * one path it was built with. */
static bool xwayland_find(void)
{
	const char *set = getenv("WLR_XWAYLAND");
	if (set && *set) {
		return access(set, X_OK) == 0;
	}
	const char *path = getenv("PATH");
	char buf[1024];
	for (const char *p = path ? path : ""; *p;) {
		const char *e = strchr(p, ':');
		size_t n = e ? (size_t)(e - p) : strlen(p);
		if (n > 0 && n + sizeof "/Xwayland" < sizeof buf) {
			memcpy(buf, p, n);
			memcpy(buf + n, "/Xwayland", sizeof "/Xwayland");
			if (access(buf, X_OK) == 0) {
				setenv("WLR_XWAYLAND", buf, 1);
				return true;
			}
		}
		if (!e) {
			break;
		}
		p = e + 1;
	}
	return access("/usr/bin/Xwayland", X_OK) == 0;
}

static int xwayland_retry_cb(void *data);

static void xwayland_start(struct desk_server *server, struct wlr_compositor *compositor)
{
	const char *off = getenv("DESK_NO_XWAYLAND");
	if (off && *off && strcmp(off, "0") != 0) {
		wlr_log(WLR_INFO, "X11 programs: off (DESK_NO_XWAYLAND)");
		return;
	}
	if (!xwayland_find()) {
		wlr_log(WLR_INFO, "X11 programs: no Xwayland on this machine, "
			"so Wayland programs only");
		return;
	}
	/* Lazily: no X server runs, and nothing is spent on one, until the
	 * first X program connects. */
	server->xwayland = wlr_xwayland_create(server->wl_display, compositor, true);
	if (!server->xwayland) {
		wlr_log(WLR_ERROR, "X11 programs: could not set up Xwayland, "
			"so Wayland programs only");
		return;
	}
	server->xwayland_ready.notify = xwayland_ready;
	wl_signal_add(&server->xwayland->events.ready, &server->xwayland_ready);
	server->new_xwayland_surface.notify = server_new_xwayland_surface;
	wl_signal_add(&server->xwayland->events.new_surface, &server->new_xwayland_surface);
	server->xwayland_client_gone.notify = xwayland_client_gone;
	wl_list_init(&server->xwayland_client_gone.link);
	server->xwayland_start.notify = xwayland_started;
	wl_list_init(&server->xwayland_start.link);
	if (!server->xwayland_retry) {
		server->xwayland_retry = wl_event_loop_add_timer(
			wl_display_get_event_loop(server->wl_display), xwayland_retry_cb, server);
	}
#ifdef DESK_XWAYLAND_PATCHES
	if (server->xwayland->server) {
		wl_signal_add(&server->xwayland->server->events.start, &server->xwayland_start);
	}
#endif
	/* what everything started from here on connects to -- in place of the
	 * X server of the session we may be running in a window of */
	setenv("DISPLAY", server->xwayland->display_name, 1);
	wlr_log(WLR_INFO, "X11 programs: DISPLAY=%s, Xwayland starts with the first one",
		server->xwayland->display_name);
}

static void xwayland_stop(struct desk_server *server);

/* An X server that dies while it is still starting is not started again:
 * wlroots takes its display down and leaves it down, and no X program opens
 * again until DEsk is restarted. So if that is what has happened, set X
 * up again from the beginning -- a few times, and then leave it, rather than
 * start one for ever that dies every time. */
static int xwayland_retry_cb(void *data)
{
	struct desk_server *server = data;
	struct wlr_xwayland_server *xs = server->xwayland ? server->xwayland->server : NULL;
	if (!server->xwayland || (xs && xs->display != -1)) {
		return 0;   /* still there, or restarting by itself */
	}
	double now = real_sec();
	if (now - server->xwayland_rebuilt_at > 60.0) {
		server->xwayland_rebuilds = 0;
	}
	if (server->xwayland_rebuilds >= 3) {
		wlr_log(WLR_ERROR, "X11 programs: Xwayland keeps dying as it starts; "
			"leaving it off until DEsk is restarted");
		return 0;
	}
	server->xwayland_rebuilds++;
	server->xwayland_rebuilt_at = now;
	wlr_log(WLR_INFO, "X11 programs: Xwayland died while starting; setting it up again");
	struct wlr_compositor *compositor = server->xwayland->compositor;
	xwayland_stop(server);
	xwayland_start(server, compositor);
	return 0;
}

static void xwayland_stop(struct desk_server *server)
{
	if (!server->xwayland) {
		return;
	}
	wl_list_remove(&server->xwayland_ready.link);
	wl_list_remove(&server->new_xwayland_surface.link);
	wl_list_remove(&server->xwayland_start.link);
	wl_list_remove(&server->xwayland_client_gone.link);
	wl_list_init(&server->xwayland_client_gone.link);
	wlr_xwayland_destroy(server->xwayland);
	server->xwayland = NULL;
}
#endif

/* Capture what the compositor is actually showing -- clients, shadows and
 * all -- rather than re-rendering the wallpaper. That needs the screencopy
 * protocol, which is also what makes grim work for the user. */
static int screenshot_done_cb(void *data)
{
	struct desk_server *server = data;
	wl_display_terminate(server->wl_display);
	return 0;
}

static int screenshot_cb(void *data)
{
	struct desk_server *server = data;
	bool captured = false;
	if (server->screenshot_path && server->desk_w > 0) {
		if (access("/usr/bin/grim", X_OK) == 0 || access("/bin/grim", X_OK) == 0) {
			char *const argv[] = { (char *)"grim", server->screenshot_path, NULL };
			spawn_argv(argv);
			wlr_log(WLR_INFO, "capturing screenshot to %s", server->screenshot_path);
			captured = true;
		} else {
			/* no grim: fall back to redrawing the desktop, which at least
			 * produces a file, though without any client windows in it */
			desk_write_png(server->screenshot_path, server->desk_w, server->desk_h, true);
			wlr_log(WLR_INFO, "grim not found; wrote desktop-only %s",
				server->screenshot_path);
		}
	}
	if (server->exit_after_shot) {
		if (!captured) {
			wl_display_terminate(server->wl_display);
			return 0;
		}
		/* give grim a moment to connect, capture and write */
		struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
		struct wl_event_source *src = wl_event_loop_add_timer(loop, screenshot_done_cb, server);
		wl_event_source_timer_update(src, 1500);
	}
	return 0;
}

static void usage(const char *argv0)
{
	printf("Usage: %s [options]\n"
		"  -s CMD          startup command\n"
		"  --version       which DEsk this is\n"
		"  --headless      force headless backend\n"
		"  --size WxH      output size (default 1600x1000 headless)\n"
		"  --dump FILE     write cairo desktop PNG when the output appears\n"
		"  --screenshot F  capture with grim after a delay\n"
		"  --delay MS      screenshot delay (default 2500)\n"
		"  --exit-after-screenshot\n"
		"Keybindings:\n"
		"  Super+Enter     terminal\n"
		"  Super+Q         close window\n"
		"  Super/Alt+Esc   quit\n"
		"  Alt/Super+Tab   cycle windows\n"
		"  Super+F         whole screen / back\n"
		"  Super+D         open / close drawer\n"
		"  Super+M         fold / unfold paper\n", argv0);
}

/* Only one of us.
 *
 * The room is ray traced, and a cold start is a minute of every core the
 * machine has. Two of those at once -- which is all a double click on the
 * desktop shortcut takes -- is two minutes of nothing but fans, twice the
 * work for one picture, and two processes racing to write the same cache
 * files at the end of it.
 *
 * flock rather than a pid file: the kernel drops the lock when the process
 * dies however it dies, so there is no stale lock to clean up after a
 * crash. Set DESK_ALLOW_MULTIPLE=1 to run a second one anyway. */
static int desk_claim_session(void)
{
	if (getenv("DESK_ALLOW_MULTIPLE")) {
		return -1;
	}
	const char *run = getenv("XDG_RUNTIME_DIR");
	char path[512];
	if (!run || !*run) {
		run = "/tmp";
	}
	if ((size_t)snprintf(path, sizeof path, "%s/desk.lock", run) >= sizeof path) {
		return -1;
	}
	int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (fd < 0) {
		return -1;   /* cannot lock: better to run than to refuse */
	}
	if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
		close(fd);
		return -2;   /* somebody else has it */
	}
	/* and who has it, for desk-ctl */
	if (ftruncate(fd, 0) == 0) {
		char pid[24];
		int n = snprintf(pid, sizeof pid, "%d\n", (int)getpid());
		ssize_t w = write(fd, pid, (size_t)n);
		(void)w;
	}
	return fd;
}

/* Buffers a client draws on the GPU and hands over (dmabufs). wlroots
 * checks each one on the way in by importing it on the GPU and closing the
 * handle again -- and some drivers do not let that handle be closed the
 * generic way: vmwgfx, the virtual GPU of a VMware machine, is one. There
 * the check fails for every buffer there is, and every client that draws
 * on the GPU -- Xwayland first of all, then any GL program -- is told its
 * buffers are no good and thrown off. The buffers are fine; only the check
 * is not. So it is tried once, on a buffer of our own, and only where it
 * fails like that is it left out: a buffer that really cannot be used is
 * then found out by the renderer, which says so without disconnecting
 * anybody. */
static bool dmabuf_check_nothing(struct wlr_dmabuf_attributes *attribs, void *data)
{
	(void)attribs;
	(void)data;
	return true;
}

static bool dmabuf_check_is_broken(struct desk_server *server)
{
	int rfd = wlr_renderer_get_drm_fd(server->renderer);
	char *node = rfd >= 0 ? drmGetRenderDeviceNameFromFd(rfd) : NULL;
	if (!node) {
		return false;
	}
	int fd = open(node, O_RDWR | O_CLOEXEC);
	free(node);
	if (fd < 0) {
		return false;
	}
	uint64_t mod = DRM_FORMAT_MOD_INVALID;
	struct wlr_drm_format fmt = {
		.format = DRM_FORMAT_XRGB8888, .len = 1, .capacity = 1, .modifiers = &mod,
	};
	bool broken = false;
	struct wlr_buffer *buf = wlr_allocator_create_buffer(server->allocator, 64, 64, &fmt);
	struct wlr_dmabuf_attributes a;
	if (buf && wlr_buffer_get_dmabuf(buf, &a) && a.n_planes > 0) {
		uint32_t handle = 0;
		if (drmPrimeFDToHandle(fd, a.fd[0], &handle) == 0
			&& drmCloseBufferHandle(fd, handle) != 0) {
			broken = true;
		}
	}
	if (buf) {
		wlr_buffer_drop(buf);
	}
	close(fd);   /* and anything left open on it with it */
	return broken;
}

/* What wlr_renderer_init_wl_display() does -- shared memory, and GPU
 * buffers where the renderer takes them -- but keeping hold of the second,
 * so its check can be seen to. */
static void init_client_buffers(struct desk_server *server)
{
	wlr_renderer_init_wl_shm(server->renderer, server->wl_display);
	if (!wlr_renderer_get_texture_formats(server->renderer, WLR_BUFFER_CAP_DMABUF)
		|| wlr_renderer_get_drm_fd(server->renderer) < 0) {
		return;
	}
	struct wlr_linux_dmabuf_v1 *dmabuf =
		wlr_linux_dmabuf_v1_create_with_renderer(server->wl_display, 4, server->renderer);
	if (dmabuf && dmabuf_check_is_broken(server)) {
		wlr_linux_dmabuf_v1_set_check_dmabuf_callback(dmabuf, dmabuf_check_nothing, NULL);
		wlr_log(WLR_INFO, "this GPU cannot close a shared buffer's handle "
			"the usual way; taking clients' GPU buffers on trust");
	}
}

int main(int argc, char **argv)
{
	g_start_real = real_sec();
	desk_compat();
	wlr_log_init(WLR_INFO, NULL);
	/* asked which it is, before anything else -- before the lock a second
	 * one stops at; and else the first thing in the log, which wrote it */
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) {
			printf("%s\n", DESK_RELEASE);
			return 0;
		}
	}
	wlr_log(WLR_INFO, "%s", DESK_RELEASE);
	/* SIGCHLD stays as it is: every child is either waited for or, like
	 * the programs spawn() starts, not ours at all. */
	const char *slow = getenv("DESK_SLOW");
	if (slow && atof(slow) >= 1.0) {
		g_time_slow = atof(slow);
	}
	const char *prof = getenv("DESK_PROFILE");
	g_prof.on = prof && *prof && strcmp(prof, "0") != 0;
	const char *trace = getenv("DESK_PROFILE_FRAMES");
	if (trace && *trace && (g_trace.f = fopen(trace, "we")) != NULL) {
		fputs("t,gap,wait,tick,tick_cpu,render,render_cpu,between_cpu,up_kb,bufs,painters\n",
			g_trace.f);
		g_prof.on = true;
	}
	int lock_fd = desk_claim_session();
	if (lock_fd == -2) {
		wlr_log(WLR_ERROR, "DEsk is already running. "
			"Set DESK_ALLOW_MULTIPLE=1 to start a second one anyway.");
		return 0;
	}
	struct desk_server server = {0};
	/* the desktop we may be nested in, before Xwayland puts its own
	 * display in the environment for the programs we start */
	if (getenv("DISPLAY") && *getenv("DISPLAY")) {
		server.parent_display = strdup(getenv("DISPLAY"));
	}
	server.headless_w = 1600;
	server.headless_h = 1000;
	server.screenshot_delay_ms = 2500;
	drawer_load(&server);
	shell_conf_poll(&server);
	/* the room starts dark, and the clock counts it being made */
	server.boot_on = true;
	server.boot_lamp = server.conf.lights_at_login && server.conf.gfx != DESK_GFX_2D;
	server.boot_shown = -1;
	server.boot_light = server.boot_lamp ? 0.0 : 1.0;
	server.side_light = server.boot_light;
	/* the sounds: made, and the sound server found, on a thread of their own */
	desk_sound_init("DEsk");
	server.bnc_idx = -1;
	server.take.base = -1;
	server.cal_view = server.cal_to_view = DESK_CAL_MONTH;
	server.cal_hover_tab = -1;
	server.agd_when = -1;
	server.agd_mark = -1;
	desk_calendar_set_view(server.cal_view);
	server.take.idx = -1;
	server.bnc_hot = -1;
	server.bnc_act = -1;
	server.clk.hot = -1;
	server.clk.pressed = -1;
	server.clk.media_hot = -1;
	server.clk.media_down = -1;
	server.slp.hot_entry = -1;
	/* The desk starts as a desk, with the drawer pushed in.
	 *
	 * It used to start open, and an open drawer lifts the desk by its own
	 * height to make room for itself -- which carries the whole back edge
	 * off the top of the screen. The console, the post box and the tray
	 * were not merely hidden up there, they were unclickable, and the
	 * first thing anybody did was click the post box and get nothing.
	 * Super+D or the brass pull opens it. */
	server.drawer_t = 0.0;
	server.drawer_to = 0.0;

	static struct option long_opts[] = {
		{"headless", no_argument, NULL, 'H'},
		{"size", required_argument, NULL, 'z'},
		{"dump", required_argument, NULL, 'D'},
		{"screenshot", required_argument, NULL, 'S'},
		{"delay", required_argument, NULL, 'd'},
		{"exit-after-screenshot", no_argument, NULL, 'E'},
		{"help", no_argument, NULL, 'h'},
		{0, 0, 0, 0},
	};
	int c;
	while ((c = getopt_long(argc, argv, "s:h", long_opts, NULL)) != -1) {
		switch (c) {
		case 's':
			server.startup_cmd = optarg;
			break;
		case 'H':
			server.headless = true;
			break;
		case 'z':
			server.size_set = true;
			if (sscanf(optarg, "%dx%d", &server.headless_w, &server.headless_h) != 2
				|| server.headless_w <= 0 || server.headless_h <= 0) {
				usage(argv[0]);
				return 1;
			}
			break;
		case 'D':
			server.dump_path = optarg;
			break;
		case 'S':
			server.screenshot_path = optarg;
			break;
		case 'd':
			server.screenshot_delay_ms = atoi(optarg);
			break;
		case 'E':
			server.exit_after_shot = 1;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	server.wl_display = wl_display_create();
	struct wl_event_loop *loop = wl_display_get_event_loop(server.wl_display);
	/* before the backend starts: nested on Wayland, the window's first
	 * size comes while it does, and a size with no timer to wait on was
	 * dropped -- the desk stayed the size asked for, in a bigger window */
	server.resize_timer = wl_event_loop_add_timer(loop, resize_cb, &server);
	cad_signals(&server);
	if (server.headless) {
		setenv("WLR_RENDERER", "pixman", 0);
		setenv("WLR_LIBINPUT_NO_DEVICES", "1", 1);
		server.backend = wlr_headless_backend_create(loop);
	} else {
		server.backend = wlr_backend_autocreate(loop, &server.session);
	}
	if (server.backend == NULL) {
		wlr_log(WLR_ERROR, "failed to create backend");
		return 1;
	}
	server.renderer = wlr_renderer_autocreate(server.backend);
	if (server.renderer == NULL) {
		wlr_log(WLR_ERROR, "failed to create renderer");
		return 1;
	}
	server.soft_render = wlr_renderer_is_pixman(server.renderer);
	if (server.soft_render) {
		wlr_log(WLR_INFO, "the screen is put together on the processor (pixman)");
	}
	/* On the CPU: 2D Fallback, before anything is traced or drawn at the
	 * level the files had (unless the user chose one), and said so */
	render_check(&server);
	if (server.on_cpu) {
		desk_set_gfx(gfx_wanted(&server));
		motion_set(motion_of(&server.conf));
		server.boot_lamp = server.conf.lights_at_login && desk_gfx_level() != DESK_GFX_2D;
		server.boot_light = server.boot_lamp ? 0.0 : 1.0;
		server.side_light = server.boot_light;
	}
	/* The room is traced on the GPU the screen is drawn with. Left to find
	 * one for itself it takes the first render node, which on a laptop
	 * with two GPUs can be the discrete one: asleep until the trace wakes
	 * it, or under a driver that cannot clock it up. */
	int rfd = wlr_renderer_get_drm_fd(server.renderer);
	char *rnode = rfd >= 0 ? drmGetRenderDeviceNameFromFd(rfd) : NULL;
	if (rnode) {
		wlr_log(WLR_INFO, "the screen is drawn on %s; the room is traced there first", rnode);
		desk_room_gpu_prefer(rnode);
		free(rnode);
	}
	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	if (server.allocator == NULL) {
		wlr_log(WLR_ERROR, "failed to create allocator");
		return 1;
	}
	init_client_buffers(&server);

	struct wlr_compositor *compositor =
		wlr_compositor_create(server.wl_display, 5, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display);
	/* middle-click paste, and the clipboard for the programs that manage
	 * it (clipboard histories, wl-copy and wl-paste) */
	wlr_primary_selection_v1_device_manager_create(server.wl_display);
	wlr_data_control_manager_v1_create(server.wl_display);
	/* so grim, and anything else that screenshots, can see the output --
	 * xdg-output as well, or the capture comes out zero sized */
	wlr_screencopy_manager_v1_create(server.wl_display);
	/* what the programs a desktop comes with take for granted: a program
	 * asking for its window to come to the front (a link opened from mail,
	 * a second start of a program that is already running), video scaled
	 * by the compositor rather than by the program, frame timing, and
	 * one-colour buffers */
	struct wlr_xdg_activation_v1 *activation = wlr_xdg_activation_v1_create(server.wl_display);
	if (activation) {
		server.request_activate.notify = xdg_request_activate;
		wl_signal_add(&activation->events.request_activate, &server.request_activate);
	}
	wlr_viewporter_create(server.wl_display);
	wlr_presentation_create(server.wl_display, server.backend);
	wlr_single_pixel_buffer_manager_v1_create(server.wl_display);

	server.output_layout = wlr_output_layout_create(server.wl_display);
	server.layout_change.notify = layout_change;
	wl_signal_add(&server.output_layout->events.change, &server.layout_change);
	wlr_xdg_output_manager_v1_create(server.wl_display, server.output_layout);
	/* the display tools (wlr-randr, kanshi, wdisplays), and wlopm */
	server.om = wlr_output_manager_v1_create(server.wl_display);
	if (server.om) {
		server.om_apply.notify = om_apply_cb;
		wl_signal_add(&server.om->events.apply, &server.om_apply);
		server.om_test.notify = om_test_cb;
		wl_signal_add(&server.om->events.test, &server.om_test);
	}
	struct wlr_output_power_manager_v1 *power =
		wlr_output_power_manager_v1_create(server.wl_display);
	if (power) {
		server.power_set_mode.notify = power_set_mode;
		wl_signal_add(&power->events.set_mode, &server.power_set_mode);
	}
	/* the screen going dark, held up by a film playing, and told to
	 * swayidle */
	wl_list_init(&server.idle_holds);
	struct wlr_idle_inhibit_manager_v1 *inhibit = wlr_idle_inhibit_v1_create(server.wl_display);
	if (inhibit) {
		server.new_idle_hold.notify = server_new_idle_hold;
		wl_signal_add(&inhibit->events.new_inhibitor, &server.new_idle_hold);
	}
	server.idle_notifier = wlr_idle_notifier_v1_create(server.wl_display);

	wl_list_init(&server.outputs);
	server.new_output.notify = server_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	server.scene = wlr_scene_create();
	server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);
	server.layer_side = wlr_scene_tree_create(&server.scene->tree);
	server.layer_bg = wlr_scene_tree_create(&server.scene->tree);
	server.layer_desk = wlr_scene_tree_create(&server.scene->tree);
	server.layer_folded = wlr_scene_tree_create(&server.scene->tree);
	server.layer_win = wlr_scene_tree_create(&server.scene->tree);
	server.layer_full = wlr_scene_tree_create(&server.scene->tree);
	server.layer_xover = wlr_scene_tree_create(&server.scene->tree);
	server.layer_drawer = wlr_scene_tree_create(&server.scene->tree);
	server.layer_parked = wlr_scene_tree_create(&server.scene->tree);
	/* over everything: while you are looking at the shelf you are not
	 * looking at the desk, and nothing on it should show through */
	server.layer_shelf = wlr_scene_tree_create(&server.scene->tree);
	server.shelf_hover = -1;
	server.hover_from = -1;
	server.shelf_binder = -1;
	server.binder_sel = -1;
	/* and the same again for the set at the back left */
	server.layer_console = wlr_scene_tree_create(&server.scene->tree);
	server.layer_post = wlr_scene_tree_create(&server.scene->tree);
	server.pb_hover = -1;
	server.pbc_idx = -1;
	server.pbc_hot = -1;
	server.pbc_act = -1;
	server.cc_hover = -1;
	server.cc_grab = -1;
	/* where the machine has actually left its radios, so the levers do not
	 * start out telling a story of their own */
	struct desk_meters m0;
	desk_meters_read(&m0);
	server.cc_wifi = m0.have_radio ? !m0.wifi_blocked : 1;
	server.cc_ant = server.cc_ant_to = server.cc_wifi ? 1.0 : 0.0;
	server.cc_bt = m0.have_radio ? !m0.bt_blocked : 0;
	server.cc_vol = 0.62;
	server.cc_bright = 1.0;
	server.cc_screen = 1.0;
	screen_read(&server);
	server.cc_pressed = -1;
	server.layer_top = wlr_scene_tree_create(&server.scene->tree);
	server.layer_drag = wlr_scene_tree_create(&server.scene->tree);
	/* and over all of it, the front door when the screen is locked */
	server.layer_lock = wlr_scene_tree_create(&server.scene->tree);
	wlr_scene_node_set_enabled(&server.layer_lock->node, false);
	/* and over even that, the light of fireworks: nothing to see but light */
	server.layer_glow = wlr_scene_tree_create(&server.scene->tree);
	/* and over everything, nested, the black of the screen gone dark */
	server.layer_dark = wlr_scene_tree_create(&server.scene->tree);

	wl_list_init(&server.toplevels);
	wl_list_init(&server.throws);
	server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
	server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
	wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
	server.new_xdg_popup.notify = server_new_xdg_popup;
	wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

	struct wlr_xdg_decoration_manager_v1 *deco = wlr_xdg_decoration_manager_v1_create(server.wl_display);
	server.new_decoration.notify = server_new_decoration;
	wl_signal_add(&deco->events.new_toplevel_decoration, &server.new_decoration);
	/* and GTK 3's way of asking, KDE's: our frame, unless a window says
	 * it has its own (see kde_server_frame) */
	wl_list_init(&server.kde_decos);
	server.kde_deco = wlr_server_decoration_manager_create(server.wl_display);
	if (server.kde_deco) {
		wlr_server_decoration_manager_set_default_mode(server.kde_deco,
			WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
		server.new_kde_deco.notify = server_new_kde_deco;
		wl_signal_add(&server.kde_deco->events.new_decoration, &server.new_kde_deco);
	}

	server.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
	/* the pointer as programs have it (desk-session takes it from the look
	 * of other programs) */
	const char *cur_theme = getenv("XCURSOR_THEME");
	const char *cur_size = getenv("XCURSOR_SIZE");
	int cur_px = cur_size && atoi(cur_size) > 0 && atoi(cur_size) <= 256 ? atoi(cur_size) : 24;
	server.cursor_mgr = wlr_xcursor_manager_create(cur_theme && *cur_theme ? cur_theme : NULL,
		(uint32_t)cur_px);
	server.cursor_mode = DESK_CUR_PASSTHROUGH;
	server.cursor_motion.notify = server_cursor_motion;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
	server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
	wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);
	server.cursor_button.notify = server_cursor_button;
	wl_signal_add(&server.cursor->events.button, &server.cursor_button);
	server.cursor_axis.notify = server_cursor_axis;
	wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
	server.cursor_frame.notify = server_cursor_frame;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);
	server.touch_down.notify = server_touch_down;
	wl_signal_add(&server.cursor->events.touch_down, &server.touch_down);
	server.touch_motion.notify = server_touch_motion;
	wl_signal_add(&server.cursor->events.touch_motion, &server.touch_motion);

	wl_list_init(&server.keyboards);
	wl_list_init(&server.pointers);
	server.new_input.notify = server_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);
	server.seat = wlr_seat_create(server.wl_display, "seat0");
	server.relative_pointer_manager = wlr_relative_pointer_manager_v1_create(server.wl_display);
	server.pointer_constraints = wlr_pointer_constraints_v1_create(server.wl_display);
	if (server.pointer_constraints) {
		server.new_pointer_constraint.notify = desk_new_pointer_constraint;
		wl_signal_add(&server.pointer_constraints->events.new_constraint,
			&server.new_pointer_constraint);
	}
	/* text-input: programs typing through the input method, with DEsk
	 * as the go-between */
	wl_list_init(&server.text_inputs);
	server.ti_mgr = wlr_text_input_manager_v3_create(server.wl_display);
	if (server.ti_mgr) {
		server.new_text_input.notify = server_new_text_input;
		wl_signal_add(&server.ti_mgr->events.text_input, &server.new_text_input);
	}
	server.kb_focus_change.notify = seat_kb_focus_change;
	wl_signal_add(&server.seat->keyboard_state.events.focus_change, &server.kb_focus_change);
	server.request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);
	server.pointer_focus_change.notify = seat_pointer_focus_change;
	wl_signal_add(&server.seat->pointer_state.events.focus_change, &server.pointer_focus_change);
	server.request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server.seat->events.request_set_selection, &server.request_set_selection);
	server.request_set_primary_selection.notify = seat_request_set_primary_selection;
	wl_signal_add(&server.seat->events.request_set_primary_selection,
		&server.request_set_primary_selection);
	server.request_start_drag.notify = seat_request_start_drag;
	wl_signal_add(&server.seat->events.request_start_drag, &server.request_start_drag);
	server.start_drag.notify = seat_start_drag;
	wl_signal_add(&server.seat->events.start_drag, &server.start_drag);
	server.drag_icon_gone.notify = drag_icon_gone;
	wl_list_init(&server.drag_icon_gone.link);

	/* Nested in somebody else's session, the usual name is a trap: a
	 * desktop on X has no WAYLAND_DISPLAY of its own, so every program
	 * started out there tries the default, wayland-0 -- and would open in
	 * here instead of on that desktop. A name of our own, then, which only
	 * what is started from in here is told. */
	const char *socket = NULL;
	static char named[32];
	if (getenv("WAYLAND_DISPLAY") || getenv("DISPLAY")) {
		for (int i = 0; i < 32 && !socket; i++) {
			snprintf(named, sizeof named, "desk-%d", i);
			if (wl_display_add_socket(server.wl_display, named) == 0) {
				socket = named;
			}
		}
	}
	if (!socket) {
		socket = wl_display_add_socket_auto(server.wl_display);
	}
	if (!socket) {
		wlr_backend_destroy(server.backend);
		return 1;
	}

	if (server.headless) {
		wlr_headless_add_output(server.backend, (unsigned)server.headless_w, (unsigned)server.headless_h);
	}

	if (!wlr_backend_start(server.backend)) {
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, true);
	render_publish(&server, socket);
	setenv("XDG_CURRENT_DESKTOP", "DEsk", true);
	setenv("XDG_SESSION_DESKTOP", "DEsk", true);
	setenv("XDG_SESSION_TYPE", "wayland", true);
	setenv("DESKTOP_SESSION", "DEsk", true);
#ifdef DESK_HAVE_XWAYLAND
	xwayland_start(&server, compositor);
#else
	(void)compositor;
#endif

	/* the desktop's own business at login, when DEsk is the desktop */
	desk_session_start(loop);
	/* and logind's say over the screen lock */
	desk_lock_watch(loop, lock_cb, unlock_cb, &server);
	if (server.startup_cmd) {
		spawn(server.startup_cmd);
	}

	server.tick = wl_event_loop_add_timer(loop, tick_cb, &server);
	server.clk.timer = wl_event_loop_add_timer(loop, clock_timer_cb, &server);
	/* the lamps on the clock that say a camera or a microphone is on */
	desk_camera_watch(loop, clock_changed_cb, &server);
	/* what day it is, where the house is, the weather out there */
	desk_occasion_init(loop, occasion_changed_cb, &server);
	server.cc_timer = wl_event_loop_add_timer(loop, console_live_cb, &server);
	/* the wait for the screen to go dark, counted from now */
	server.idle_timer = wl_event_loop_add_timer(loop, idle_cb, &server);
	server.idle_at = real_sec();
	idle_rearm(&server);
	if (idle_ms(&server) > 0) {
		wlr_log(WLR_INFO, "screen: dark after %d s with nobody at it%s",
			idle_ms(&server) / 1000,
			server.conf.lock_screen_off ? ", the door locked first" : "");
	} else {
		wlr_log(WLR_INFO, "screen: never dark by itself");
	}
#ifdef DESK_HAVE_POST
	/* From here on, what the rest of the machine has to say arrives in the
	 * post box rather than in somebody else's toast. */
	if (desk_post_init(loop)) {
		/* the box is empty when the desk starts: every letter after is new */
		server.post_seen_set = true;
		desk_post_on_change(post_changed, &server);
		wlr_log(WLR_INFO, "the post box has the bus: DEsk is the notification daemon");
	} else {
		wlr_log(WLR_INFO, "no session bus; the post box stays empty");
	}
	/* and the tray, and which input method is on, for the clock */
	if (desk_tray_init(loop, slips_bus_changed, &server)) {
		wlr_log(WLR_INFO, "the clock keeps the tray");
	}
	desk_ime_init(loop, slips_bus_changed, &server);
	/* and what is playing, for its page */
	desk_media_init(loop, media_changed, &server);
#endif
	wl_event_source_timer_update(server.tick, 1000);

	if (server.screenshot_path) {
		server.shot_timer = wl_event_loop_add_timer(loop, screenshot_cb, &server);
		wl_event_source_timer_update(server.shot_timer, server.screenshot_delay_ms);
	}

	wlr_log(WLR_INFO, "DEsk running on WAYLAND_DISPLAY=%s", socket);
	wl_display_run(server.wl_display);

	/* what is in hand is put down first, as the shelf does: the memo's
	 * last second and a note being written are kept */
	cal_drop(&server);
	memo_drop(&server);
	/* nothing is thrown away on the way out: it is all going */
	server.quitting = true;
	if (server.disp_idle) {
		wl_event_source_remove(server.disp_idle);
		server.disp_idle = NULL;
	}
	if (server.screens_idle) {
		wl_event_source_remove(server.screens_idle);
		server.screens_idle = NULL;
	}
	if (server.end_timer) {
		wl_event_source_remove(server.end_timer);
		server.end_timer = NULL;
	}
	/* and the room is not left being traced into exit() */
	desk_room_stop();
	render_unpublish();
#ifdef DESK_HAVE_XWAYLAND
	xwayland_stop(&server);
#endif
	wl_display_destroy_clients(server.wl_display);
	wlr_scene_node_destroy(&server.scene->tree.node);
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_cursor_destroy(server.cursor);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wl_list_remove(&server.layout_change.link);
	desk_sound_shutdown();
#ifdef DESK_HAVE_POST
	desk_post_finish();
	desk_tray_finish();
	desk_ime_finish();
	desk_media_finish();
#endif
	wl_display_destroy(server.wl_display);
	if (lock_fd >= 0) {
		close(lock_fd);
	}
	return 0;
}
