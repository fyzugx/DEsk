/* An offscreen GLES context, for running the room's shader somewhere that
 * is not five CPU threads. See include/gpu.h. */

#include "gpu.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>

#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif

#define GPU_MAX_PROGS 8
#define GPU_MAX_TEX 32

struct desk_gpu {
	int fd;
	struct gbm_device *gbm;
	EGLDisplay dpy;
	bool own_dpy;       /* ours to terminate: only a display on our own gbm device */
	EGLContext ctx;
	/* programs by number, and the one set_* speaks to */
	GLuint progs[GPU_MAX_PROGS];
	int nprogs, cur;
	/* float textures by number, every one of them tw x th: as large as any
	 * window asked for so far */
	GLuint tex[GPU_MAX_TEX];
	int ntex, tw, th;
	GLuint vao, fbo, rfbo;
	int attached;       /* colour attachments on fbo just now */
	int max_size, max_out, max_in;
	GLenum ifmt;        /* GL_RGBA32F, or GL_RGBA16F where that is all there is */
	bool half_read;     /* and the driver will only hand 16F back as 16F */
	uint16_t *half;
	size_t half_n;
	/* whatever the calling thread had current, put back on close */
	EGLDisplay prev_dpy;
	EGLContext prev_ctx;
	EGLSurface prev_draw, prev_read;
	char name[192];
	char node[64];      /* the render node it is on, if it was opened by one */
};

/* One triangle bigger than the target, so there is no seam down the middle
 * of the picture where two triangles of a quad meet. */
static const char *VERT_SRC =
	"#version 300 es\n"
	"void main() {\n"
	"  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
	"  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";

static bool compile(GLuint sh, const char *src, char *err, size_t errsz)
{
	glShaderSource(sh, 1, &src, NULL);
	glCompileShader(sh);
	GLint ok = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (ok) {
		return true;
	}
	if (err && errsz) {
		GLsizei n = 0;
		glGetShaderInfoLog(sh, (GLsizei)errsz - 1, &n, err);
		err[n < (GLsizei)errsz ? n : (GLsizei)errsz - 1] = '\0';
	}
	return false;
}

static bool has_ext(const char *list, const char *name)
{
	if (!list) {
		return false;
	}
	size_t n = strlen(name);
	for (const char *p = list; (p = strstr(p, name)) != NULL; p += n) {
		if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == '\0')) {
			return true;
		}
	}
	return false;
}

/* A float target to render into. Linear light with numbers well over one
 * goes to the tonemapper, and a byte per channel cannot carry it. 32F where
 * the driver will render to it; 16F where that is all it has, which a lot of
 * mobile parts are -- and which is still ten bits of mantissa on a value
 * that ends up in eight. DESK_GPU_HALF=1 takes the 16F road on purpose,
 * so that it gets driven on a machine that would never need it. */
static bool gpu_pick_target(struct desk_gpu *g)
{
	const char *ext = (const char *)glGetString(GL_EXTENSIONS);
	bool f32 = has_ext(ext, "GL_EXT_color_buffer_float");
	bool f16 = has_ext(ext, "GL_EXT_color_buffer_half_float");
	if (f32 && !getenv("DESK_GPU_HALF")) {
		g->ifmt = GL_RGBA32F;
		return true;
	}
	if (f32 || f16) {
		g->ifmt = GL_RGBA16F;
		return true;
	}
	return false;
}

/* Everything from a display to a current context with a float target, or
 * nothing: a display that turns out not to do GLES 3 or not to render to a
 * float is let go of here, and the caller tries the next place to look,
 * rather than deciding the machine has no GPU because the first thing it
 * found did not suit.
 *
 * own says whether the display is ours to terminate. It is only when it
 * sits on a gbm device we opened ourselves. Any other display may be the
 * very one the compositor's renderer is drawing with -- EGL hands out the
 * same display for the same device -- and terminating it would pull it out
 * from under the screen. */
static bool gpu_try_display(struct desk_gpu *g, EGLDisplay d, bool own)
{
	if (d == EGL_NO_DISPLAY || !eglInitialize(d, NULL, NULL)) {
		return false;
	}
	/* No surface type asked for, because there is no surface: a GBM
	 * display has no pbuffer configs at all and asking for one comes back
	 * with none, which is how this failed to open the first time. */
	static const EGLint cfg_attr[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_NONE,
	};
	/* 3.0, not 3.1. Everything here -- a fragment program over one
	 * triangle, gl_VertexID, a float target by extension -- is 3.0. What
	 * 3.1 adds is compute and what compute needs (storage buffers, image
	 * stores), none of which a march per pixel wants. Asking for it is what
	 * turned away a VMware guest, whose SVGA3D stops at 3.0 while being real
	 * graphics hardware. A driver that has more hands out more: llvmpipe
	 * still comes back as 3.2. */
	static const EGLint ctx_attr[] = {
		EGL_CONTEXT_MAJOR_VERSION, 3,
		EGL_CONTEXT_MINOR_VERSION, 0,
		EGL_NONE,
	};
	g->dpy = d;
	g->own_dpy = own;
	EGLConfig cfg = EGL_NO_CONFIG_KHR;
	EGLint n = 0;
	const char *ext;

	if (!eglBindAPI(EGL_OPENGL_ES_API)) {
		goto fail;
	}
	/* No surface anywhere in this: everything is drawn into a texture and
	 * read back, so there is nothing to present and nothing to wait for. */
	ext = eglQueryString(d, EGL_EXTENSIONS);
	if (!has_ext(ext, "EGL_KHR_surfaceless_context")) {
		goto fail;
	}
	if (!eglChooseConfig(d, cfg_attr, &cfg, 1, &n) || n < 1) {
		/* and if it has no configs we can use either, a context without
		 * one will do -- we only ever draw into our own framebuffer */
		if (!has_ext(ext, "EGL_KHR_no_config_context")) {
			goto fail;
		}
		cfg = EGL_NO_CONFIG_KHR;
	}
	g->ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ctx_attr);
	if (g->ctx == EGL_NO_CONTEXT) {
		goto fail;
	}
	if (!eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, g->ctx)) {
		goto fail;
	}
	if (!gpu_pick_target(g)) {
		goto fail;
	}
	return true;

fail:
	if (g->ctx != EGL_NO_CONTEXT) {
		eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		eglDestroyContext(d, g->ctx);
		g->ctx = EGL_NO_CONTEXT;
	}
	if (own) {
		eglTerminate(d);
	}
	g->dpy = EGL_NO_DISPLAY;
	g->own_dpy = false;
	return false;
}

/* A display on a render node through gbm: the path every compositor on
 * this system is already using, and the only way to name one device on a
 * machine that has several. */
static bool gpu_try_node(struct desk_gpu *g, PFNEGLGETPLATFORMDISPLAYEXTPROC get_dpy,
	const char *path)
{
	int fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		return false;
	}
	struct gbm_device *gbm = gbm_create_device(fd);
	if (!gbm) {
		close(fd);
		return false;
	}
	if (gpu_try_display(g, get_dpy(EGL_PLATFORM_GBM_KHR, gbm, NULL), true)) {
		g->fd = fd;
		g->gbm = gbm;
		snprintf(g->node, sizeof g->node, "%s", path);
		return true;
	}
	gbm_device_destroy(gbm);
	close(fd);
	return false;
}

static char g_prefer[64];

void desk_gpu_prefer(const char *node)
{
	snprintf(g_prefer, sizeof g_prefer, "%s", node && strlen(node) < sizeof g_prefer ? node : "");
}

/* Where to look, in order, and the first place that gives a GLES 3 context
 * with a float target wins:
 *
 *   DESK_GPU_NODE, when it names a node
 *   the node the screen is drawn with (desk_gpu_prefer)
 *   each render node, through gbm
 *   each EGL device that is a real one -- what NVIDIA's driver offers when
 *     it has no gbm, and it has no surfaceless platform either
 *   Mesa's surfaceless platform, which is its own idea of the best device,
 *     llvmpipe included
 *   and EGL's default display, whatever that turns out to be. */
static bool gpu_egl_open(struct desk_gpu *g)
{
	g->fd = -1;
	g->dpy = EGL_NO_DISPLAY;
	g->ctx = EGL_NO_CONTEXT;

	const char *client = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
	PFNEGLGETPLATFORMDISPLAYEXTPROC get_dpy = NULL;
	if (has_ext(client, "EGL_EXT_platform_base")) {
		get_dpy = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
			eglGetProcAddress("eglGetPlatformDisplayEXT");
	}

	if (get_dpy && (has_ext(client, "EGL_KHR_platform_gbm")
			|| has_ext(client, "EGL_MESA_platform_gbm"))) {
		const char *node = getenv("DESK_GPU_NODE");
		if (node && *node && gpu_try_node(g, get_dpy, node)) {
			return true;
		}
		if (g_prefer[0] && !(node && !strcmp(node, g_prefer))
			&& gpu_try_node(g, get_dpy, g_prefer)) {
			return true;
		}
		for (int i = 128; i < 136; i++) {
			char path[32];
			snprintf(path, sizeof path, "/dev/dri/renderD%d", i);
			if (!strcmp(path, g_prefer) || (node && !strcmp(path, node))) {
				continue;   /* tried, above */
			}
			if (gpu_try_node(g, get_dpy, path)) {
				return true;
			}
		}
	}

	if (get_dpy && has_ext(client, "EGL_EXT_platform_device")
		&& (has_ext(client, "EGL_EXT_device_enumeration")
			|| has_ext(client, "EGL_EXT_device_base"))) {
		PFNEGLQUERYDEVICESEXTPROC query_devices = (PFNEGLQUERYDEVICESEXTPROC)
			eglGetProcAddress("eglQueryDevicesEXT");
		PFNEGLQUERYDEVICESTRINGEXTPROC query_string = (PFNEGLQUERYDEVICESTRINGEXTPROC)
			eglGetProcAddress("eglQueryDeviceStringEXT");
		EGLDeviceEXT devs[16];
		EGLint n = 0;
		if (query_devices && query_string && query_devices(16, devs, &n)) {
			for (int i = 0; i < n; i++) {
				const char *de = query_string(devs[i], EGL_EXTENSIONS);
				if (!has_ext(de, "EGL_EXT_device_drm")) {
					continue;
				}
				if (gpu_try_display(g, get_dpy(EGL_PLATFORM_DEVICE_EXT, devs[i], NULL),
						false)) {
					return true;
				}
			}
		}
	}

	if (get_dpy && has_ext(client, "EGL_MESA_platform_surfaceless")
		&& gpu_try_display(g, get_dpy(EGL_PLATFORM_SURFACELESS_MESA,
			EGL_DEFAULT_DISPLAY, NULL), false)) {
		return true;
	}
	return gpu_try_display(g, eglGetDisplay(EGL_DEFAULT_DISPLAY), false);
}

struct desk_gpu *desk_gpu_open(void)
{
	if (getenv("DESK_NO_GPU")) {
		return NULL;
	}
	struct desk_gpu *g = calloc(1, sizeof(*g));
	if (!g) {
		return NULL;
	}
	g->prev_dpy = eglGetCurrentDisplay();
	g->prev_ctx = eglGetCurrentContext();
	g->prev_draw = eglGetCurrentSurface(EGL_DRAW);
	g->prev_read = eglGetCurrentSurface(EGL_READ);
	if (!gpu_egl_open(g)) {
		desk_gpu_close(g);
		return NULL;
	}
	const GLubyte *rend = glGetString(GL_RENDERER);
	const GLubyte *ver = glGetString(GL_VERSION);
	snprintf(g->name, sizeof g->name, "%s (%s%s)",
		rend ? (const char *)rend : "?", ver ? (const char *)ver : "?",
		g->ifmt == GL_RGBA16F ? ", 16F" : "");

	/* A window is drawn and read back whole, so it can be no bigger than a
	 * texture or a viewport can. GLES 3.0 promises 2048 of either. */
	GLint tex = 0, vp[2] = { 0, 0 };
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &tex);
	glGetIntegerv(GL_MAX_VIEWPORT_DIMS, vp);
	g->max_size = tex;
	if (vp[0] < g->max_size) {
		g->max_size = vp[0];
	}
	if (vp[1] < g->max_size) {
		g->max_size = vp[1];
	}
	if (g->max_size < 64) {
		desk_gpu_close(g);
		return NULL;
	}
	/* GLES 3.0 promises four colour attachments and sixteen samplers */
	GLint mo = 0, ma = 0, mi = 0;
	glGetIntegerv(GL_MAX_DRAW_BUFFERS, &mo);
	glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &ma);
	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &mi);
	g->max_out = mo < ma ? mo : ma;
	g->max_in = mi;
	glGenVertexArrays(1, &g->vao);
	glGenFramebuffers(1, &g->fbo);
	glGenFramebuffers(1, &g->rfbo);
	return g;
}

void desk_gpu_close(struct desk_gpu *g)
{
	if (!g) {
		return;
	}
	if (g->dpy != EGL_NO_DISPLAY) {
		if (g->ctx != EGL_NO_CONTEXT) {
			if (eglGetCurrentContext() == g->ctx) {
				for (int i = 0; i < g->nprogs; i++) {
					if (g->progs[i]) {
						glDeleteProgram(g->progs[i]);
					}
				}
				if (g->ntex) {
					glDeleteTextures(g->ntex, g->tex);
				}
				glDeleteFramebuffers(1, &g->fbo);
				glDeleteFramebuffers(1, &g->rfbo);
				glDeleteVertexArrays(1, &g->vao);
			}
			eglMakeCurrent(g->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
			eglDestroyContext(g->dpy, g->ctx);
		}
		if (g->own_dpy) {
			eglTerminate(g->dpy);
		}
	}
	if (g->prev_ctx != EGL_NO_CONTEXT && g->prev_dpy != EGL_NO_DISPLAY) {
		eglMakeCurrent(g->prev_dpy, g->prev_draw, g->prev_read, g->prev_ctx);
	}
	if (g->gbm) {
		gbm_device_destroy(g->gbm);
	}
	if (g->fd >= 0) {
		close(g->fd);
	}
	free(g->half);
	free(g);
}

const char *desk_gpu_name(const struct desk_gpu *g)
{
	return g ? g->name : "none";
}

const char *desk_gpu_node(const struct desk_gpu *g)
{
	return g ? g->node : "";
}

bool desk_gl_name_software(const char *name)
{
	/* not "LLVM", which VMware's own 3D driver says it is built with
	 * ("SVGA3D; build: RELEASE; LLVM;") */
	static const char *const SOFT[] = {
		"llvmpipe", "softpipe", "swrast", "SwiftShader", "Software Rasterizer", "lavapipe",
	};
	for (size_t i = 0; name && i < sizeof SOFT / sizeof SOFT[0]; i++) {
		if (strstr(name, SOFT[i])) {
			return true;
		}
	}
	return false;
}

/* Graphics of its own, by the name the driver gives it: NVIDIA's, AMD's
 * Radeon RX and Pro, Intel's Arc A and B cards. Not the graphics built into
 * a processor (Intel's UHD, Iris and the Arc of Meteor Lake and after, AMD's
 * "Radeon Graphics", "Radeon 780M", Vega 8), nor a virtual machine's: on a
 * laptop they share its memory and its warmth with the processor, and what
 * a desktop's card makes light of is heavy on them (2026-11-16, the owner on
 * real machines: "here, where it all runs smoothly, is no measure at all"). */
bool desk_gl_name_own(const char *name)
{
	char low[256];
	size_t n = 0;
	for (; name[n] && n + 1 < sizeof low; n++) {
		low[n] = (char)tolower((unsigned char)name[n]);
	}
	low[n] = '\0';
	if (strstr(low, "nvidia") || strstr(low, "geforce") || strstr(low, "quadro")) {
		return true;
	}
	/* nouveau calls a card by its chip: NV174 */
	if (low[0] == 'n' && low[1] == 'v' && isxdigit((unsigned char)low[2])
		&& isxdigit((unsigned char)low[3])) {
		return true;
	}
	if (strstr(low, "radeon") && (strstr(low, "radeon rx") || strstr(low, " rx ")
		|| strstr(low, "radeon pro") || strstr(low, "radeon vii") || strstr(low, " r9 "))) {
		return true;
	}
	if (strstr(low, "(dg2") || strstr(low, "(bmg")) {
		return true;
	}
	const char *arc = strstr(low, "arc");
	if (arc && strstr(low, "intel")) {
		arc += 3;
		if (!strncmp(arc, "(tm)", 4)) {
			arc += 4;
		}
		if (arc[0] == ' ' && (((arc[1] == 'a' || arc[1] == 'b') && isdigit((unsigned char)arc[2]))
			|| !strncmp(arc + 1, "pro", 3))) {
			return true;
		}
	}
	return false;
}

bool desk_gpu_software(const struct desk_gpu *g)
{
	return g && desk_gl_name_software(g->name);
}

bool desk_gl_renderer_of(void *display, void *context, char *out, size_t n)
{
	if (!out || n == 0) {
		return false;
	}
	out[0] = '\0';
	EGLDisplay dpy = display;
	EGLContext ctx = context;
	if (dpy == EGL_NO_DISPLAY || ctx == EGL_NO_CONTEXT) {
		return false;
	}
	EGLDisplay was_dpy = eglGetCurrentDisplay();
	EGLContext was_ctx = eglGetCurrentContext();
	EGLSurface was_draw = eglGetCurrentSurface(EGL_DRAW);
	EGLSurface was_read = eglGetCurrentSurface(EGL_READ);
	if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		return false;
	}
	const char *name = (const char *)glGetString(GL_RENDERER);
	snprintf(out, n, "%s", name ? name : "");
	if (was_ctx != EGL_NO_CONTEXT) {
		eglMakeCurrent(was_dpy, was_draw, was_read, was_ctx);
	} else {
		eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	}
	return out[0] != '\0';
}

int desk_gpu_max_size(const struct desk_gpu *g)
{
	return g ? g->max_size : 0;
}

static GLuint gpu_link(const char *frag_src, char *err, size_t errsz)
{
	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
	bool ok = compile(vs, VERT_SRC, err, errsz) && compile(fs, frag_src, err, errsz);
	GLuint prog = 0;
	if (ok) {
		prog = glCreateProgram();
		glAttachShader(prog, vs);
		glAttachShader(prog, fs);
		glLinkProgram(prog);
		GLint linked = 0;
		glGetProgramiv(prog, GL_LINK_STATUS, &linked);
		if (!linked) {
			if (err && errsz) {
				GLsizei n = 0;
				glGetProgramInfoLog(prog, (GLsizei)errsz - 1, &n, err);
				err[n < (GLsizei)errsz ? n : (GLsizei)errsz - 1] = '\0';
			}
			glDeleteProgram(prog);
			prog = 0;
		}
	}
	glDeleteShader(vs);
	glDeleteShader(fs);
	if (prog) {
		/* the inputs of a pass are the samplers u_in0, u_in1, ... on
		 * texture units 0, 1, ... in that order */
		glUseProgram(prog);
		for (int i = 0; i < 16; i++) {
			char name[16];
			snprintf(name, sizeof name, "u_in%d", i);
			GLint loc = glGetUniformLocation(prog, name);
			if (loc >= 0) {
				glUniform1i(loc, i);
			}
		}
	}
	return prog;
}

int desk_gpu_add_program(struct desk_gpu *g, const char *frag_src,
	char *err, size_t errsz)
{
	if (!g || g->nprogs >= GPU_MAX_PROGS) {
		return -1;
	}
	GLuint prog = gpu_link(frag_src, err, errsz);
	if (!prog) {
		return -1;
	}
	g->progs[g->nprogs] = prog;
	g->cur = g->nprogs;
	return g->nprogs++;
}

void desk_gpu_use_program(struct desk_gpu *g, int prog)
{
	if (g && prog >= 0 && prog < g->nprogs) {
		g->cur = prog;
	}
}

bool desk_gpu_program(struct desk_gpu *g, const char *frag_src,
	char *err, size_t errsz)
{
	if (!g) {
		return false;
	}
	GLuint prog = gpu_link(frag_src, err, errsz);
	if (!prog) {
		return false;
	}
	if (g->nprogs > 0 && g->progs[0]) {
		glDeleteProgram(g->progs[0]);
	}
	g->progs[0] = prog;
	if (g->nprogs < 1) {
		g->nprogs = 1;
	}
	g->cur = 0;
	return true;
}

static GLint gpu_loc(struct desk_gpu *g, const char *name)
{
	if (!g || g->cur >= g->nprogs || !g->progs[g->cur]) {
		return -1;
	}
	glUseProgram(g->progs[g->cur]);
	return glGetUniformLocation(g->progs[g->cur], name);
}

void desk_gpu_set(struct desk_gpu *g, const char *name, const float *v, int n)
{
	GLint loc = gpu_loc(g, name);
	if (loc < 0) {
		return;
	}
	switch (n) {
	case 1: glUniform1f(loc, v[0]); break;
	case 2: glUniform2f(loc, v[0], v[1]); break;
	case 3: glUniform3f(loc, v[0], v[1], v[2]); break;
	default: glUniform4f(loc, v[0], v[1], v[2], v[3]); break;
	}
}

void desk_gpu_set_int(struct desk_gpu *g, const char *name, int v)
{
	desk_gpu_set_ivec(g, name, &v, 1);
}

void desk_gpu_set_ivec(struct desk_gpu *g, const char *name, const int *v, int n)
{
	GLint loc = gpu_loc(g, name);
	if (loc < 0) {
		return;
	}
	switch (n) {
	case 1: glUniform1i(loc, v[0]); break;
	case 2: glUniform2i(loc, v[0], v[1]); break;
	case 3: glUniform3i(loc, v[0], v[1], v[2]); break;
	default: glUniform4i(loc, v[0], v[1], v[2], v[3]); break;
	}
}

static float half_to_float(uint16_t h)
{
	uint32_t s = (uint32_t)(h & 0x8000u) << 16;
	uint32_t e = (h >> 10) & 0x1fu;
	uint32_t m = h & 0x3ffu;
	uint32_t bits;
	if (e == 0) {
		if (m == 0) {
			bits = s;
		} else {
			/* subnormal: normalise it */
			e = 127 - 15 + 1;
			while (!(m & 0x400u)) {
				m <<= 1;
				e--;
			}
			bits = s | (e << 23) | ((m & 0x3ffu) << 13);
		}
	} else if (e == 31) {
		bits = s | 0x7f800000u | (m << 13);
	} else {
		bits = s | ((e + 127 - 15) << 23) | (m << 13);
	}
	float f;
	memcpy(&f, &bits, sizeof f);
	return f;
}

/* Every texture up to n exists and is at least w x h. Growing one grows
 * them all, and loses what was in them: a pass writes its outputs whole
 * before anything reads them, so there is nothing to keep. */
static bool gpu_textures(struct desk_gpu *g, int n, int w, int h)
{
	if (n > GPU_MAX_TEX || w > g->max_size || h > g->max_size) {
		return false;
	}
	int tw = w > g->tw ? w : g->tw;
	int th = h > g->th ? h : g->th;
	bool grow = tw != g->tw || th != g->th;
	if (n > g->ntex) {
		glGenTextures(n - g->ntex, g->tex + g->ntex);
	}
	GLenum type = g->ifmt == GL_RGBA16F ? GL_HALF_FLOAT : GL_FLOAT;
	for (int i = 0; i < (n > g->ntex ? n : g->ntex); i++) {
		if (!grow && i < g->ntex) {
			continue;
		}
		glBindTexture(GL_TEXTURE_2D, g->tex[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, (GLint)g->ifmt, tw, th, 0, GL_RGBA, type, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	if (n > g->ntex) {
		g->ntex = n;
	}
	g->tw = tw;
	g->th = th;
	return true;
}

static int gpu_top(const int *v, int n)
{
	int top = -1;
	for (int i = 0; i < n; i++) {
		top = v[i] > top ? v[i] : top;
	}
	return top;
}

bool desk_gpu_pass(struct desk_gpu *g, int prog, const int *in, int nin,
	const int *out, int nout, int x0, int y0, int w, int h)
{
	if (!g || prog < 0 || prog >= g->nprogs || !g->progs[prog] || w < 1 || h < 1
		|| nout < 1 || nout > g->max_out || nout > 8 || nin < 0 || nin > g->max_in) {
		return false;
	}
	int top = gpu_top(in, nin);
	int otop = gpu_top(out, nout);
	top = otop > top ? otop : top;
	if (top < 0 || !gpu_textures(g, top + 1, w, h)) {
		return false;
	}
	while (glGetError() != GL_NO_ERROR) {
	}
	glBindFramebuffer(GL_FRAMEBUFFER, g->fbo);
	GLenum bufs[8];
	for (int j = 0; j < nout; j++) {
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + j,
			GL_TEXTURE_2D, g->tex[out[j]], 0);
		bufs[j] = GL_COLOR_ATTACHMENT0 + j;
	}
	for (int j = nout; j < g->attached; j++) {
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + j,
			GL_TEXTURE_2D, 0, 0);
	}
	g->attached = nout;
	glDrawBuffers(nout, bufs);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		return false;
	}
	glUseProgram(g->progs[prog]);
	for (int i = 0; i < nin; i++) {
		glActiveTexture(GL_TEXTURE0 + i);
		glBindTexture(GL_TEXTURE_2D, g->tex[in[i]]);
	}
	GLint loc = glGetUniformLocation(g->progs[prog], "u_origin");
	if (loc >= 0) {
		glUniform2i(loc, x0, y0);
	}
	glViewport(0, 0, w, h);
	glBindVertexArray(g->vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	/* nothing stays bound to be read and written at once by the next pass */
	for (int i = nin - 1; i >= 0; i--) {
		glActiveTexture(GL_TEXTURE0 + i);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	return glGetError() == GL_NO_ERROR;
}

/* Reading a 16F texture back. GLES 3.0 promises RGBA/FLOAT from a float
 * target; not every driver agrees that a half target is one, and when it
 * says no, the halves come back as halves and are widened here. */
bool desk_gpu_read(struct desk_gpu *g, int tex, int w, int h, float *out)
{
	if (!g || tex < 0 || tex >= g->ntex || w > g->tw || h > g->th || !out) {
		return false;
	}
	while (glGetError() != GL_NO_ERROR) {
	}
	glBindFramebuffer(GL_READ_FRAMEBUFFER, g->rfbo);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, g->tex[tex], 0);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		return false;
	}
	if (!g->half_read) {
		glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, out);
		GLenum e = glGetError();
		if (e == GL_NO_ERROR) {
			return true;
		}
		if (g->ifmt != GL_RGBA16F || e != GL_INVALID_OPERATION) {
			return false;
		}
		g->half_read = true;
	}
	size_t n = (size_t)w * h * 4;
	if (g->half_n < n) {
		uint16_t *p = realloc(g->half, n * sizeof *p);
		if (!p) {
			return false;
		}
		g->half = p;
		g->half_n = n;
	}
	glReadPixels(0, 0, w, h, GL_RGBA, GL_HALF_FLOAT, g->half);
	if (glGetError() != GL_NO_ERROR) {
		return false;
	}
	for (size_t i = 0; i < n; i++) {
		out[i] = half_to_float(g->half[i]);
	}
	return true;
}

bool desk_gpu_float32(const struct desk_gpu *g)
{
	return g && g->ifmt == GL_RGBA32F;
}

bool desk_gpu_render_rect(struct desk_gpu *g, int x0, int y0, int w, int h, float *out)
{
	if (!g || !out) {
		return false;
	}
	int target = 0;
	if (!desk_gpu_pass(g, g->cur, NULL, 0, &target, 1, x0, y0, w, h)) {
		return false;
	}
	return desk_gpu_read(g, target, w, h, out);
}

bool desk_gpu_render(struct desk_gpu *g, int w, int h, float *out)
{
	float res[2] = { (float)w, (float)h };
	desk_gpu_set(g, "u_res", res, 2);
	return desk_gpu_render_rect(g, 0, 0, w, h, out);
}
