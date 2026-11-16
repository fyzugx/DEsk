#ifndef DESK_GPU_H
#define DESK_GPU_H

#include <stdbool.h>
#include <stddef.h>

/* Somewhere to run a shader that is not a screen.
 *
 * The room is a signed distance field marched once per pixel, which is the
 * workload graphics hardware exists for -- and DEsk has been doing it on
 * five CPU threads because that is where the scene is written. This is the
 * other end of that: an offscreen GLES context, a fragment program, and a
 * float buffer to read the answer back out of.
 *
 * Deliberately not tied to the compositor's own renderer. The bake happens
 * on a worker thread before there is anything on screen, it happens in the
 * preview tool where there is no compositor at all, and it must keep
 * working when the session is running on a different device from the one
 * doing the arithmetic.
 *
 * Asks for no more than GLES 3.0 and a float colour buffer (32F, or 16F
 * where that is all there is), because this has to run on whatever the
 * machine turns out to have.
 *
 *   DESK_NO_GPU=1      never open one: the CPU tracer does everything
 *   DESK_GPU_NODE=path the render node to try first
 *   DESK_GPU_HALF=1    render to 16F even where 32F is there */

struct desk_gpu;

/* Open a device and make a context current on the calling thread; close
 * puts back whatever the thread had current before. Everything in between
 * happens on that thread.
 *
 * Tried in turn: DESK_GPU_NODE, the node desk_gpu_prefer named, the render
 * nodes through gbm, EGL's own device list, Mesa's surfaceless platform and
 * EGL's default display; the first that gives GLES 3.0 with a float target
 * is the one. NULL means there is no GPU path on this machine, which is
 * not an error -- the CPU tracer is still there and is still the
 * reference. */
struct desk_gpu *desk_gpu_open(void);
void desk_gpu_close(struct desk_gpu *g);

/* The render node to try ahead of the others: the compositor's own, the
 * GPU the screen is drawn with. Without it the first render node is the
 * one, and on a laptop with two GPUs that can be the discrete one --
 * asleep, to be woken for the trace and kept awake through it, or under a
 * driver that cannot clock it up from where it starts. Called once, before
 * any trace begins. */
void desk_gpu_prefer(const char *node);

/* What we ended up talking to, for the log. */
const char *desk_gpu_name(const struct desk_gpu *g);
/* The render node it was opened on, or "" if it came some other way. */
const char *desk_gpu_node(const struct desk_gpu *g);

/* Whether that is a rasterizer running on the CPU (llvmpipe and friends),
 * which is still worth having -- it runs a shader several pixels at a time
 * -- but is not the same promise as a GPU. */
bool desk_gpu_software(const struct desk_gpu *g);
/* The same test of a renderer's name (GL_RENDERER) from anywhere. */
bool desk_gl_name_software(const char *name);
/* graphics of its own -- NVIDIA, Radeon RX and Pro, Intel Arc A and B -- by the
 * renderer's name: not the processor's own, nor a virtual machine's */
bool desk_gl_name_own(const char *name);
/* GL_RENDERER of a context someone else made -- the compositor's own, an
 * EGLDisplay and EGLContext passed as pointers -- and whatever was current
 * on the calling thread put back. False if it could not be made current. */
bool desk_gl_renderer_of(void *display, void *context, char *out, size_t n);

/* The largest window render_rect will take across or down. */
int desk_gpu_max_size(const struct desk_gpu *g);

/* Compile a fragment program. The vertex half is ours: it draws one
 * triangle over the whole target, so the fragment program sees every pixel
 * exactly once and can read its own position out of gl_FragCoord.
 *
 * Returns false and fills err with the driver's complaint, which for a
 * shader this size is the only way to find out what went wrong. This one
 * replaces program 0; add_program adds another and returns its number, or
 * -1. Either becomes the program the set_* calls below speak to, and
 * use_program changes which that is. */
bool desk_gpu_program(struct desk_gpu *g, const char *frag_src,
	char *err, size_t errsz);
int desk_gpu_add_program(struct desk_gpu *g, const char *frag_src,
	char *err, size_t errsz);
void desk_gpu_use_program(struct desk_gpu *g, int prog);

/* Set a named uniform (up to four components). Missing names are not an
 * error: a program that does not use a value has none. */
void desk_gpu_set(struct desk_gpu *g, const char *name, const float *v, int n);
void desk_gpu_set_int(struct desk_gpu *g, const char *name, int v);
void desk_gpu_set_ivec(struct desk_gpu *g, const char *name, const int *v, int n);

/* Run it over w x h and read the result back as linear RGBA floats, which
 * is the same thing the CPU tracer hands its tonemapper. out must have room
 * for w * h * 4 floats. The program is told the size as vec2 u_res. */
bool desk_gpu_render(struct desk_gpu *g, int w, int h, float *out);

/* The same over a w x h window of something bigger, whose first pixel is
 * (x0, y0) of it: the program is told that as ivec2 u_origin, and a program
 * that takes gl_FragCoord plus u_origin as its pixel draws the window of
 * the frame. Rows come back in the order they go up the target, row 0
 * first. One window is one draw, and a draw is what a GPU watchdog times,
 * so a long program wants its frame in windows rather than in one go. */
bool desk_gpu_render_rect(struct desk_gpu *g, int x0, int y0, int w, int h,
	float *out);

/* A job done in passes. Textures are numbered from 0 and made as they are
 * first named, every one of them float RGBA and as large as any window so
 * far. A pass runs program prog over a w x h window whose first pixel is
 * (x0, y0) of the frame, exactly as render_rect does, with textures in[]
 * readable as sampler2D u_in0, u_in1, ... (texelFetch at gl_FragCoord) and
 * its outputs -- layout(location = j) -- going to textures out[]. GLES 3.0
 * promises four outputs and sixteen inputs. A texture is never both.
 *
 * The passes queue on the GPU; read waits for them and brings a texture's
 * first w x h back. float32 says whether the textures are 32F, which a job
 * that keeps positions in them needs: sixteen bits of float put a point on
 * the desk a few centimetres from where it is. */
bool desk_gpu_pass(struct desk_gpu *g, int prog, const int *in, int nin,
	const int *out, int nout, int x0, int y0, int w, int h);
bool desk_gpu_read(struct desk_gpu *g, int tex, int w, int h, float *out);
bool desk_gpu_float32(const struct desk_gpu *g);

#endif
