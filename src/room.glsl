/* The room, traced on the GPU.
 *
 * A line for line port of the tracer in draw.c -- scene_map, the materials
 * and shade_surface -- and draw.c stays the reference: this file has to
 * give the same picture, and `desk-gputest --room` is what says whether
 * it does. Change one, change the other.
 *
 * Every number with a name arrives from draw.c ahead of this text (see
 * room_glsl_preamble), so the two cannot disagree about where the desk is.
 * What is written out here is the arithmetic, and the numbers that only
 * ever lived inside it.
 *
 * GLSL ES 3.00 and nothing past it, because this has to run on whatever the
 * machine has: highp for everything, no arrays of arrays, no recursion, and
 * sines of big arguments folded back into one turn before the driver sees
 * them -- the spec only promises a sine between -pi and pi.
 *
 * Four programs are cut from this one text, by ROOM_PASS; the shading
 * section below says where and why. In every one, a fragment is one sample:
 * its column and row, offset by the window, are the tracer's x and y, and
 * rows go up the target the way they go down the sheet, so a readback
 * lands in draw.c's order without a flip. What the last pass hands back is
 * linear RGB and, in alpha, coverage plus twice the owner. */

uniform ivec2 u_origin;   /* this tile's first sample; set by gpu.c */
uniform ivec2 u_frame;    /* the whole sheet, in samples */
uniform vec3 u_eye, u_ww, u_uu, u_vv;
uniform vec2 u_fov;
uniform int u_layer;
uniform float u_open, u_cc_out, u_pb_open, u_cc_lid;
uniform float u_zf, u_aspect, u_sh_hx, u_hx;

const float M_PI = 3.14159265358979323846;

/* ---- what draw.c keeps as globals ---- */

#define RM_ZF u_zf
#define RM_HX u_hx
#define RM_HX_GROW (u_hx - RM_HX_MIN)
#define RM_CC_X0 (-15.20 - RM_HX_GROW)
#define RM_CC_X1 (-5.10 - RM_HX_GROW)
#define RM_CC_PLATE_X0 (-15.05 - RM_HX_GROW)
#define RM_CC_LEDGE_X1 (-11.50 - RM_HX_GROW)
#define RM_PB_CX (9.60 + RM_HX_GROW)
#define ROOM_LAMP_K (u_hx / RM_HX_MIN)
#define ROOM_LAMP_POS (vec3(0.0, 0.0, RM_ZN) + (ROOM_LAMP_POS0 - vec3(0.0, 0.0, RM_ZN)) * ROOM_LAMP_K)
#define ROOM_LAMP_REF (ROOM_LAMP_REF0 * ROOM_LAMP_K)
#define ROOM_LAMP_RADIUS (ROOM_LAMP_RADIUS0 * ROOM_LAMP_K)
#define RM_WALL_Z (u_zf - 1.60)
#define RM_SH_ZF (room_hidden_z(RM_FLOOR) + RM_SH_PLINTH_Z - RM_SH_CLEAR)
#define RM_SH_ZB (RM_SH_ZF - RM_SH_D)
#define RM_BN_ZF (RM_SH_ZF - 0.55)
#define RM_BN_ZC (RM_BN_ZF - RM_BN_DEPTH * 0.5)
#define RM_PB_CZ (RM_ZF + RM_PB_TOP_DZ + RM_PB_TOP_GAP \
	+ (RM_FOV_X_V * 0.5 / u_aspect) * RM_PB_TOP_Y)

float room_hidden_z(float y)
{
	return u_zf + (RM_FOV_X_V * 0.5 / u_aspect) * y;
}

/* ---- arithmetic ---- */

float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

float smoothstepf(float a, float b, float x)
{
	float t = clampf((x - a) / (b - a + 1e-9), 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}

/* sin and cos of anything, folded into [-pi, pi] first */
float ssin(float x)
{
	return sin(x - 6.28318530717958648 * floor(x * 0.159154943091895336 + 0.5));
}

float scos(float x)
{
	return cos(x - 6.28318530717958648 * floor(x * 0.159154943091895336 + 0.5));
}

/* Every base draw.c hands pow is at or above zero, except by a rounding
 * error -- which C shrugs off and a GPU's pow turns into a NaN. */
float spow(float x, float y)
{
	return pow(max(x, 0.0), y);
}

/* atan2 as C has it at the origin, which GLSL leaves undefined */
float atan2f(float y, float x)
{
	return (x == 0.0 && y == 0.0) ? 0.0 : atan(y, x);
}

vec3 vnorm(vec3 a)
{
	float l = length(a);
	return l > 1e-12 ? a * (1.0 / l) : a;
}

vec3 rot_x(vec3 p, float a)
{
	float c = cos(a), s = sin(a);
	return vec3(p.x, p.y * c - p.z * s, p.y * s + p.z * c);
}

vec3 rot_y(vec3 p, float a)
{
	float c = cos(a), s = sin(a);
	return vec3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
}

vec3 rot_z(vec3 p, float a)
{
	float c = cos(a), s = sin(a);
	return vec3(p.x * c - p.y * s, p.x * s + p.y * c, p.z);
}

vec3 reflect3(vec3 i, vec3 n)
{
	return i - n * (2.0 * dot(i, n));
}

/* ---- noise, hashed on integers, to the bit what draw.c computes ---- */

uint room_mix(uint v)
{
	uint s = v * 747796405u + 2891336453u;
	uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
	return (w >> 22u) ^ w;
}

float room_hash(int x, int y)
{
	uint h = room_mix(uint(x) + room_mix(uint(y)));
	return float(h >> 8u) * (1.0 / 16777216.0);
}

float room_seed(int i, int salt)
{
	return room_hash(i, ROOM_SEED_ROW + salt);
}

float room_noise(float x, float y)
{
	float xi = floor(x), yi = floor(y);
	float xf = x - xi, yf = y - yi;
	float u = xf * xf * (3.0 - 2.0 * xf);
	float v = yf * yf * (3.0 - 2.0 * yf);
	int ix = int(xi), iy = int(yi);
	float a = room_hash(ix, iy);
	float b = room_hash(ix + 1, iy);
	float c = room_hash(ix, iy + 1);
	float d = room_hash(ix + 1, iy + 1);
	float top = a + (b - a) * u;
	float bot = c + (d - c) * u;
	return top + (bot - top) * v;
}

float room_fbm(float x, float y)
{
	float sum = 0.0, amp = 0.5, f = 1.0;
	for (int i = 0; i < 5; i++) {
		sum += amp * room_noise(x * f, y * f);
		amp *= 0.5;
		f *= 2.0;
	}
	return sum;
}

float room_fbm2(float x, float y)
{
	return room_noise(x, y) * 0.65 + room_noise(x * 2.17, y * 2.17) * 0.35;
}

/* ---- wear ---- */

float scratch_at(float u, float v, float spacing, float aniso, float halfw, float seed)
{
	float y = v / (spacing * aniso) + seed * 53.1;
	float warp = (room_fbm2(u / (spacing * 6.0) + seed * 3.1, y * 1.7) - 0.5)
		* spacing * 1.6;
	float x = (u + warp) / spacing + seed * 17.3;
	float d = abs(fract(x) - 0.5) * spacing;
	float line = smoothstepf(halfw, halfw * 0.22, d);
	float alive = room_noise(floor(x) * 1.73 + seed * 7.7, y * 6.0);
	return line * smoothstepf(0.50, 0.79, alive);
}

float nick_at(float u, float v, float freq, float seed)
{
	float n = room_noise(u * freq + seed * 91.7, v * freq + seed * 13.3);
	return smoothstepf(0.930, 0.997, n);
}

float room_touch(vec3 w)
{
	float front = smoothstepf(-6.0, 7.4, w.z);
	float dx = (w.x - 1.8) / 9.5, dz = (w.z - 1.4) / 6.2;
	float seat = exp(-(dx * dx + dz * dz));
	float patchy = room_fbm2(w.x * 0.19 + 3.3, w.z * 0.19 + 7.1);
	return clampf((front * 0.58 + seat * 0.62) * (0.40 + patchy * 1.25), 0.0, 1.0);
}

float room_sun(vec3 w)
{
	float side = smoothstepf(9.0, -13.0, w.x);
	float reach = smoothstepf(-11.0, 4.0, w.z);
	float uneven = room_fbm2(w.x * 0.11 + 21.0, w.z * 0.11 + 4.0);
	return clampf(side * reach * (0.30 + uneven * 1.05), 0.0, 1.0);
}

/* ---- light ---- */

vec3 env_ambient(vec3 d)
{
	vec3 c = mix(vec3(0.026, 0.044, 0.024), vec3(0.150, 0.098, 0.052),
		smoothstepf(-0.80, -0.04, d.y));
	c = mix(c, vec3(0.128, 0.116, 0.106), smoothstepf(0.02, 0.88, d.y));
	float k = max(dot(d, vnorm(LIT_KEY_DIR)), 0.0);
	c += LIT_KEY_COL * (0.30 * k * k * k);
	float f = max(dot(d, vnorm(LIT_FILL_DIR)), 0.0);
	c += LIT_FILL_COL * (0.24 * spow(f, 2.5));
	float r = max(dot(d, vnorm(LIT_RIM_DIR)), 0.0);
	c += LIT_RIM_COL * (0.17 * spow(r, 7.0));
	return c;
}

vec3 sh_irradiance(vec3 n)
{
	const float c1 = 0.429043, c2 = 0.511664, c3 = 0.743125;
	const float c4 = 0.886227, c5 = 0.247708;
	vec3 e = ROOM_SH[0] * c4;
	e -= ROOM_SH[6] * c5;
	e += ROOM_SH[6] * (c3 * n.z * n.z);
	e += ROOM_SH[1] * (2.0 * c2 * n.y);
	e += ROOM_SH[2] * (2.0 * c2 * n.z);
	e += ROOM_SH[3] * (2.0 * c2 * n.x);
	e += ROOM_SH[4] * (2.0 * c1 * n.x * n.y);
	e += ROOM_SH[5] * (2.0 * c1 * n.y * n.z);
	e += ROOM_SH[7] * (2.0 * c1 * n.x * n.z);
	e += ROOM_SH[8] * (c1 * (n.x * n.x - n.y * n.y));
	return max(e, vec3(0.0));
}

vec3 env_prefiltered(vec3 d, float rough)
{
	float t = clampf(rough * rough * 1.6, 0.0, 1.0);
	if (t >= 0.999) {
		return sh_irradiance(d) * (1.0 / M_PI);
	}
	vec3 sharp = env_ambient(d);
	if (t <= 0.001) {
		return sharp;
	}
	return mix(sharp, sh_irradiance(d) * (1.0 / M_PI), t);
}

float room_pull()
{
	float e = clampf(u_open, 0.0, 1.0);
	return e * e * (3.0 - 2.0 * e) * RM_DR_PULL;
}

struct key_light {
	vec3 dir;
	float irr;
	float ang;
};

key_light key_at(vec3 p)
{
	key_light k;
	vec3 d = ROOM_LAMP_POS - p;
	float dist = max(length(d), 0.75);
	k.dir = d * (1.0 / dist);
	k.irr = LIT_KEY_IRRADIANCE * (ROOM_LAMP_REF * ROOM_LAMP_REF) / (dist * dist);
	k.ang = atan(ROOM_LAMP_RADIUS / dist);
	float ax = dot(-k.dir, vnorm(ROOM_LAMP_AIM));
	k.irr *= smoothstepf(ROOM_LAMP_CONE_IN, ROOM_LAMP_CONE_OUT, ax);
	return k;
}

vec3 shelf_lamp_at(vec3 p, vec3 n)
{
	vec3 sum = vec3(0.0);
	for (int i = 0; i < 2; i++) {
		vec3 lp = vec3(i != 0 ? 8.2 : -8.2, SHELF_LAMP_Y, RM_SH_ZF - 0.15);
		vec3 d = lp - p;
		float dist = max(length(d), 0.70);
		vec3 l = d * (1.0 / dist);
		float nol = max(dot(n, l), 0.0);
		if (nol <= 0.0) {
			continue;
		}
		float ax = dot(-l, vnorm(SHELF_LAMP_AIM));
		float cone = smoothstepf(-0.05, 0.62, ax);
		sum += SHELF_LAMP_COL * (SHELF_LAMP_IRR * cone * nol / (dist * dist));
	}
	return sum;
}

float lamp_radiance(key_light k)
{
	float s = sin(k.ang);
	return k.irr / (M_PI * s * s);
}

vec3 env_lamp(vec3 d, float rough, key_light k)
{
	float th = sqrt(k.ang * k.ang + rough * rough * 2.1);
	float ca = acos(clampf(dot(d, k.dir), -1.0, 1.0));
	float t = smoothstepf(th * 2.40, th * 0.18, ca);
	t = t * t;
	if (t <= 0.0) {
		return vec3(0.0);
	}
	float kk = min((k.ang * k.ang) / (th * th), 1.0);
	return LIT_KEY_COL * (lamp_radiance(k) * kk * t);
}

void env_brdf(float nov, float rough, out float a, out float b)
{
	float rx = rough * -1.00 + 1.00;
	float ry = rough * -0.0275 + 0.0425;
	float rz = rough * -0.572 + 1.04;
	float rw = rough * 0.022 - 0.04;
	float a004 = min(rx * rx, exp2(-9.28 * nov)) * rx + ry;
	a = a004 * -1.04 + rz;
	b = a004 * 1.04 + rw;
}

/* ---- distance fields ---- */

float sd_sphere(vec3 p, float r)
{
	return length(p) - r;
}

float sd_box(vec3 p, vec3 b)
{
	vec3 q = abs(p) - b;
	return min(max(q.x, max(q.y, q.z)), 0.0) + length(max(q, vec3(0.0)));
}

float sd_round_box(vec3 p, vec3 b, float r)
{
	return sd_box(p, max(b - vec3(r), vec3(0.0))) - r;
}

float sd_cyl_y(vec3 p, float r, float h)
{
	float d = length(p.xz) - r;
	float dy = abs(p.y) - h;
	return min(max(d, dy), 0.0) + length(max(vec2(d, dy), vec2(0.0)));
}

float sd_torus(vec3 p, float r, float t)
{
	float q = length(p.xz) - r;
	return length(vec2(q, p.y)) - t;
}

float sd_capped_cone(vec3 p, float r1, float r2, float h)
{
	vec2 q = vec2(length(p.xz), p.y);
	vec2 k1 = vec2(r2, h);
	vec2 k2 = vec2(r2 - r1, 2.0 * h);
	vec2 ca = vec2(q.x - min(q.x, (q.y < 0.0) ? r1 : r2), abs(q.y) - h);
	float k2l = length(k2) + 1e-8;
	float t = clampf(dot(k1 - q, k2) / (k2l * k2l), 0.0, 1.0);
	vec2 cb = q - (k1 + k2 * -t);
	float s = (cb.x < 0.0 && ca.y < 0.0) ? -1.0 : 1.0;
	return s * sqrt(min(dot(ca, ca), dot(cb, cb)));
}

float sd_cyl_z(vec3 p, float r, float h)
{
	float d = length(p.xy) - r;
	float dz = abs(p.z) - h;
	return min(max(d, dz), 0.0) + length(max(vec2(d, dz), vec2(0.0)));
}

float sd_gear_z(vec3 p, float r, float h, int n)
{
	float ang = atan2f(p.y, p.x);
	float tooth = spow(max(0.0, scos(ang * float(n))), 3.0);
	float rad = r * (0.74 + 0.32 * tooth);
	float d = sd_cyl_z(p, rad, h);
	float chamfer = (length(p.xy) - rad * 0.86) * 0.42 + abs(p.z) - h * 1.06;
	d = max(d, chamfer);
	d = max(d, -sd_cyl_z(p, r * 0.17, h + 0.2));
	d = min(d, sd_cyl_z(p, r * 0.27, h * 0.45));
	float ha = atan2f(p.y, p.x) + M_PI;
	float sector = 2.0 * M_PI / 5.0;
	float hb = ha - sector * floor(ha / sector) - sector * 0.5;
	float hr = length(p.xy);
	vec3 hp = vec3(hb * r * 0.52, hr - r * 0.46, p.z);
	if (r > 0.3) {
		d = max(d, -sd_cyl_z(hp, r * 0.115, h + 0.2));
	}
	return d;
}

struct hit3 {
	float d;
	int id;
};

hit3 hmin3(hit3 a, hit3 b)
{
	return a.d < b.d ? a : b;
}

/* ---- the five things in the drawer, each in its own space ---- */

hit3 map_term(vec3 p)
{
	hit3 h = hit3(1e9, 0);
	vec3 q = rot_y(rot_x(p, -0.32), 0.38);
	h = hmin3(h, hit3(sd_round_box(q - vec3(0.00, 0.12, -0.06), vec3(0.50, 0.38, 0.30), 0.06), MID_CRT_BODY));
	vec3 qs = q - vec3(0.00, 0.18, 0.22);
	qs.z *= 1.25;
	h = hmin3(h, hit3(sd_round_box(qs, vec3(0.36, 0.24, 0.055), 0.04), MID_CRT_SCREEN));
	h = hmin3(h, hit3(sd_round_box(q - vec3(0.00, 0.18, 0.18), vec3(0.40, 0.28, 0.035), 0.02), MID_CRT_BEZEL));
	h = hmin3(h, hit3(sd_round_box(q - vec3(0.00, -0.38, 0.20), vec3(0.52, 0.05, 0.24), 0.03), MID_CRT_KEYBED));
	vec3 k = q - vec3(0.00, -0.32, 0.18);
	float cell = 0.092;
	float kx = k.x - cell * floor(k.x / cell + 0.5);
	float kz = k.z - 0.082 * floor(k.z / 0.082 + 0.5);
	float keys = sd_round_box(vec3(kx, k.y, kz), vec3(0.032, 0.022, 0.030), 0.008);
	keys = max(keys, -sd_sphere(vec3(kx, k.y - 0.086, kz), 0.068));
	keys = max(keys, sd_box(k, vec3(0.42, 0.08, 0.19)));
	h = hmin3(h, hit3(keys, MID_CRT_KEY));
	h = hmin3(h, hit3(sd_sphere(q - vec3(0.32, -0.18, 0.22), 0.026), MID_CRT_LED));
	return h;
}

hit3 map_globe(vec3 p)
{
	hit3 h = hit3(1e9, 0);
	vec3 q = rot_y(rot_x(p, -0.18), 0.22);
	vec3 c = vec3(0.00, 0.12, 0.00);
	h = hmin3(h, hit3(sd_sphere(q - c, 0.46), MID_GLOBE));
	h = hmin3(h, hit3(sd_torus(q - c, 0.472, 0.016), MID_GLOBE_RING));
	vec3 ax = rot_z(q - c, 0.40);
	h = hmin3(h, hit3(sd_cyl_y(ax, 0.012, 0.48), MID_GLOBE_RING));
	h = hmin3(h, hit3(sd_cyl_y(q - vec3(0.0, -0.28, 0.0), 0.042, 0.14), MID_GLOBE_STAND));
	h = hmin3(h, hit3(sd_capped_cone(q - vec3(0.0, -0.48, 0.0), 0.30, 0.16, 0.045), MID_GLOBE_STAND));
	h = hmin3(h, hit3(sd_cyl_y(q - vec3(0.0, -0.52, 0.0), 0.32, 0.018), MID_GLOBE_STAND));
	return h;
}

vec3 warp_sheet(vec3 q, float amp, float freq)
{
	float w = sin(q.x * freq) * 0.6 + sin(q.y * freq * 0.7 + 1.3) * 0.4;
	return vec3(q.x, q.y, q.z - w * amp);
}

hit3 map_folder(vec3 p)
{
	hit3 h = hit3(1e9, 0);
	vec3 q = rot_y(rot_x(p, -0.42), 0.52);
	h = hmin3(h, hit3(sd_round_box(q - vec3(0.00, -0.02, -0.05), vec3(0.48, 0.34, 0.032), 0.016), MID_FOLDER));
	vec3 front = rot_x(q - vec3(0.00, -0.06, 0.08), 0.18);
	h = hmin3(h, hit3(sd_round_box(front, vec3(0.48, 0.30, 0.028), 0.016), MID_FOLDER));
	h = hmin3(h, hit3(sd_round_box(q - vec3(-0.22, 0.34, -0.03), vec3(0.18, 0.08, 0.02), 0.012), MID_FOLDER));
	h = hmin3(h, hit3(sd_box(warp_sheet(q - vec3(0.04, 0.08, -0.01), 0.012, 6.0),
		vec3(0.40, 0.30, 0.02)), MID_FOLDER_PAPER));
	h = hmin3(h, hit3(sd_box(warp_sheet(q - vec3(0.02, 0.04, 0.02), 0.010, 7.5),
		vec3(0.36, 0.26, 0.012)), MID_FOLDER_PAPER));
	return h;
}

hit3 map_notes(vec3 p)
{
	hit3 h = hit3(1e9, 0);
	vec3 q = rot_y(rot_x(p, -0.40), 0.28);
	h = hmin3(h, hit3(sd_round_box(q - vec3(0.00, -0.02, 0.00), vec3(0.38, 0.48, 0.07), 0.018), MID_NOTES));
	h = hmin3(h, hit3(sd_round_box(q - vec3(0.00, 0.46, 0.00), vec3(0.38, 0.05, 0.08), 0.016), MID_NOTES_BIND));
	h = hmin3(h, hit3(sd_round_box(warp_sheet(q - vec3(0.04, -0.05, -0.05), 0.008, 5.0),
		vec3(0.36, 0.45, 0.022), 0.012), MID_FOLDER_PAPER));
	return h;
}

hit3 map_gear(vec3 p)
{
	hit3 h = hit3(1e9, 0);
	vec3 q = rot_y(p, 0.18);
	q = rot_x(q, -0.22);
	h = hmin3(h, hit3(sd_gear_z(q - vec3(-0.06, 0.08, 0.00), 0.40, 0.07, 12), MID_GEAR));
	h = hmin3(h, hit3(sd_gear_z(q - vec3(0.36, -0.28, 0.05), 0.23, 0.055, 8), MID_GEAR));
	return h;
}

hit3 object_map(vec3 p, int kind)
{
	if (kind == DESK_ICON_TERM) {
		return map_term(p);
	}
	if (kind == DESK_ICON_GLOBE) {
		return map_globe(p);
	}
	if (kind == DESK_ICON_FOLDER) {
		return map_folder(p);
	}
	if (kind == DESK_ICON_NOTES) {
		return map_notes(p);
	}
	if (kind == DESK_ICON_GEAR) {
		return map_gear(p);
	}
	return hit3(1e9, 0);
}

/* ---- the drawer's row ---- */

vec3 item_to_local(vec3 p, int i)
{
	vec3 q = p - vec3(ITEM_X[i], ITEM_Y[i], ITEM_Z[i]);
	q = rot_y(q, ITEM_YAW[i]);
	q = rot_x(q, ITEM_PITCH[i]);
	return q * (1.0 / ITEM_SCALE[i]);
}

int drawer_item_at(vec3 p)
{
	int best = -1;
	float bd = 1e9;
	for (int i = 0; i < DESK_DOCK_COUNT; i++) {
		float d = sd_box(p - ITEM_C[i], ITEM_E[i]);
		if (d < 0.0 && d < bd) {
			bd = d;
			best = i;
		}
	}
	return best;
}

vec3 scene_local(vec3 p, int id)
{
	if (id <= 0 || id > MID_GEAR) {
		return p;
	}
	vec3 q = p - vec3(0.0, RM_ITEM_Y, RM_ITEM_Z + room_pull());
	int i = drawer_item_at(q);
	return i >= 0 ? item_to_local(q, i) : p;
}

hit3 drawer_items_map(vec3 q)
{
	hit3 h = hit3(1e9, 0);
	for (int i = 0; i < DESK_DOCK_COUNT; i++) {
		float db = sd_box(q - ITEM_C[i], ITEM_E[i]);
		if (db > DRAWER_NEAR) {
			h = hmin3(h, hit3(db, 0));
			continue;
		}
		hit3 o = object_map(item_to_local(q, i), ITEM_KIND[i]);
		o.d *= ITEM_SCALE[i];
		h = hmin3(h, o);
	}
	return h;
}

float felt_dip(vec3 q)
{
	float dip = 0.0;
	for (int i = 0; i < DESK_DOCK_COUNT; i++) {
		float dx = abs(q.x - ITEM_C[i].x) - ITEM_E[i].x * 0.42;
		float dz = abs(q.z - ITEM_C[i].z) - ITEM_E[i].z * 0.42;
		float d = max(max(dx, 0.0), max(dz, 0.0));
		dip = max(dip, (1.0 - smoothstepf(0.0, 0.70, d)) * 0.030);
	}
	return dip;
}

/* ---- the things standing on the desk and the shelf ---- */

bool gadget_upright(int i)
{
	return GD_TILT[i] != 0.0 || GD_FOOT[i] != 0.0;
}

float gadget_z(int i)
{
	return gadget_upright(i) ? RM_SH_ZF + GD_Z[i]
		: RM_ZF + (RM_FOV_X_V * 0.5 / u_aspect) * GD_H[i] + GD_Z[i];
}

vec3 gadget_to_local(int i, vec3 p)
{
	vec3 q = p - vec3(GD_X[i], GD_FOOT[i] + GD_PLINTH[i], gadget_z(i));
	return GD_TILT[i] != 0.0 ? rot_x(q, GD_TILT[i]) : q;
}

vec3 gadget_to_world(int i, vec3 l)
{
	if (GD_TILT[i] != 0.0) {
		l = rot_x(l, -GD_TILT[i]);
	}
	return l + vec3(GD_X[i], GD_FOOT[i] + GD_PLINTH[i], gadget_z(i));
}

/* draw.c's cal_tent: the two boards of the desk calendar or the memo pad,
 * its pages, its wire */
hit3 cal_tent(int i, vec3 p, hit3 h)
{
	float H = GD_H[i], hx = GD_HX[i];
	vec3 l = gadget_to_local(i, p);
	h = hmin3(h, hit3(sd_round_box(l - vec3(0.0, H * 0.5, -CAL_T),
		vec3(hx, H * 0.5, CAL_T), 0.03), GD_BODY[i]));
	float py1 = H - CAL_PTOP;
	h = hmin3(h, hit3(sd_round_box(l - vec3(0.0, (CAL_PY0 + py1) * 0.5, CAL_PZ * 0.5),
		vec3(hx - CAL_PIN, (py1 - CAL_PY0) * 0.5, CAL_PZ * 0.5), 0.015), GD_FACE[i]));
	vec3 ridge = gadget_to_world(i, vec3(0.0, H, -2.0 * CAL_T));
	float lb = (ridge.y - (GD_FOOT[i] + GD_PLINTH[i])) / cos(CAL_TB);
	vec3 b = rot_x(p - ridge, -CAL_TB);
	h = hmin3(h, hit3(sd_round_box(b - vec3(0.0, -lb * 0.5, -CAL_T),
		vec3(hx, lb * 0.5, CAL_T), 0.03), GD_BODY[i]));
	float rn = floor((hx - CAL_PIN - CAL_RT - CAL_RM) / CAL_RP);
	float k = clamp(floor(l.x / CAL_RP + 0.5), -rn, rn);
	float xr = abs(l.x - k * CAL_RP) - CAL_RT;
	float ry = l.y - (H - CAL_RY), rz = l.z - (CAL_PZ - 2.0 * CAL_T) * 0.5;
	float rr = sqrt(ry * ry + rz * rz) - CAL_RR;
	float ring = sqrt(rr * rr + xr * xr) - CAL_RW;
	return hmin3(h, hit3(ring, MID_CC_STEEL));
}

hit3 room_gadgets(vec3 p)
{
	hit3 h = hit3(1e9, 0);
	for (int i = 0; i < DESK_GADGET_COUNT; i++) {
		bool up = gadget_upright(i);
		if (up && u_layer != 3) {
			continue;
		}
		float gx = GD_X[i], ghx = GD_HX[i], ghz = GD_HZ[i], gh = GD_H[i];
		float ins = GD_INSET[i], pl = GD_PLINTH[i], foot = GD_FOOT[i];
		float base = foot + pl;
		float gz = gadget_z(i);
		vec3 c = up ? vec3(gx, base + gh * 0.5, gz) : vec3(gx, gh * 0.5, gz);
		vec3 e = up ? vec3(ghx, gh * 0.5 + pl, ghz + gh * 0.5 * sin(GD_TILT[i]))
			: vec3(ghx, gh * 0.5, ghz);
		bool tent = GD_KIND[i] == GADGET_TENT;
		if (tent) {
			float t = GD_TILT[i];
			float dep = gh * (sin(t) + cos(t) * tan(CAL_TB)), ht = gh * cos(t);
			c = vec3(gx, foot + (pl + ht) * 0.5, gz - dep * 0.5);
			e = vec3(ghx + 0.34, (pl + ht) * 0.5 + 0.30, dep * 0.5 + 0.50);
		}
		float db = sd_box(p - c, e + vec3(0.40, 0.40, 0.40));
		if (db > DRAWER_NEAR) {
			h = hmin3(h, hit3(db, 0));
			continue;
		}
		if (tent) {
			float t = GD_TILT[i];
			float dep = gh * (sin(t) + cos(t) * tan(CAL_TB)), pz = gz - dep * 0.5;
			h = hmin3(h, hit3(sd_round_box(p - vec3(gx, foot + pl * 0.30, pz),
				vec3(ghx + 0.34, pl * 0.30, dep * 0.5 + 0.46), 0.06), MID_SHELF_CASE));
			h = hmin3(h, hit3(sd_round_box(p - vec3(gx, foot + pl * 0.72, pz),
				vec3(ghx + 0.14, pl * 0.30, dep * 0.5 + 0.26), 0.05), MID_SHELF_CASE));
			h = cal_tent(i, p, h);
			continue;
		}

		if (up) {
			h = hmin3(h, hit3(sd_round_box(p - vec3(gx, foot + pl * 0.30, gz),
				vec3(ghx + 0.34, pl * 0.30, ghz + 0.46), 0.06), MID_SHELF_CASE));
			h = hmin3(h, hit3(sd_round_box(p - vec3(gx, foot + pl * 0.72, gz),
				vec3(ghx + 0.14, pl * 0.30, ghz + 0.26), 0.05), MID_SHELF_CASE));

			vec3 l = gadget_to_local(i, p);
			vec3 lc = l - vec3(0.0, gh * 0.5, 0.0);
			float body = sd_round_box(lc, vec3(ghx, gh * 0.5, ghz),
				GD_ROUND[i] != 0 ? min(ghx, gh * 0.5) * 0.30 : 0.14);
			vec3 fe = vec3(ghx - ins, gh * 0.5 - ins, 0.10);
			float well = sd_round_box(lc - vec3(0.0, 0.0, ghz), fe, 0.05);
			body = max(body, -well);
			h = hmin3(h, hit3(body, GD_BODY[i]));
			h = hmin3(h, hit3(sd_round_box(lc - vec3(0.0, 0.0, ghz - 0.12),
				vec3(fe.x, fe.y, 0.05), 0.04), GD_FACE[i]));
			continue;
		}

		float body = sd_round_box(p - c, e, GD_ROUND[i] != 0 ? min(ghx, ghz) * 0.45 : 0.14);
		vec3 fc = vec3(gx, gh, gz);
		vec3 fe = vec3(ghx - ins, 0.10, ghz - ins);
		float well = sd_round_box(p - fc, fe, 0.05);
		body = max(body, -well);
		h = hmin3(h, hit3(body, GD_BODY[i]));
		h = hmin3(h, hit3(sd_round_box(p - vec3(gx, gh - 0.12, gz),
			vec3(fe.x, 0.05, fe.z), 0.04), GD_FACE[i]));
	}
	return h;
}

/* ---- the control console ---- */

float cc_stow_at(float aspect)
{
	const float k = 0.15;
	float a = max(aspect, 0.2);
	float y = RM_CC_PY0 + (RM_CC_PY1 - RM_CC_PY0) * k;
	float z = RM_CC_PZ0 + (RM_CC_PZ1 - RM_CC_PZ0) * k;
	float zf = RM_ZN - 2.0 * RM_HX / a;
	return z - (zf + (RM_FOV_X_V * 0.5 / a) * y);
}

float cc_forward_at(float aspect)
{
	float a = max(aspect, 0.2);
	float zf = RM_ZN - 2.0 * RM_HX / a;
	return max(0.0, zf + 0.35 - RM_CC_ZB);
}

float cc_dz(float o, float aspect)
{
	float c = clampf(o, 0.0, 1.0);
	return c * cc_forward_at(aspect) - (1.0 - c) * cc_stow_at(aspect);
}

/* draw.c's cc_face: centre, across, up (towards the back), out, and half
 * the length up it */
struct cc_face {
	vec3 o;
	vec3 fu;
	vec3 fs;
	vec3 fn;
	float hl;
};

cc_face cc_face_build(float y0, float z0, float y1, float z1)
{
	cc_face f;
	float dy = y1 - y0, dz = z1 - z0;
	float len = sqrt(dy * dy + dz * dz);
	f.o = vec3((RM_CC_X0 + RM_CC_X1) * 0.5, (y0 + y1) * 0.5, (z0 + z1) * 0.5);
	f.fu = vec3(1.0, 0.0, 0.0);
	f.fs = vec3(0.0, dy / len, dz / len);
	f.fn = vec3(0.0, -f.fs.z, f.fs.y);
	f.hl = len * 0.5;
	return f;
}

vec3 cc_to_face(cc_face f, vec3 p)
{
	vec3 q = p - f.o;
	return vec3(dot(q, f.fu), dot(q, f.fn), dot(q, f.fs));
}

/* draw.c's lid on the top of the tower */
#define CC_LID_HY (RM_CC_TY1 - CC_LID_T * 0.5)
#define CC_LID_HZ (RM_CC_ZB + 0.14)
#define CC_EXT_WV (CC_LID_L * 0.5 + 0.02)

vec3 lid_local(vec3 p, float a)
{
	vec3 q = p - vec3((RM_CC_X0 + RM_CC_X1) * 0.5, CC_LID_HY, CC_LID_HZ);
	float ca = cos(a), sa = sin(a);
	return vec3(q.x, q.y * sa + q.z * ca, -q.y * ca + q.z * sa - CC_LID_T * 0.5);
}

hit3 map_lid(vec3 p, float a)
{
	vec3 l = lid_local(p, a);
	float gross = sd_box(l - vec3(0.0, CC_LID_L * 0.5, -CC_LID_T * 0.5),
		vec3(CC_LID_HU + 0.10, CC_LID_L * 0.5 + 0.20, CC_LID_T * 0.5 + 0.20));
	if (gross > DRAWER_NEAR) {
		return hit3(gross, 0);
	}
	hit3 h = hit3(sd_round_box(l - vec3(0.0, CC_LID_L * 0.5, -CC_LID_T * 0.5),
		vec3(CC_LID_HU, CC_LID_L * 0.5, CC_LID_T * 0.5), 0.05), MID_CC_CHEEK);
	h = hmin3(h, hit3(sd_box(l - vec3(0.0, CC_LID_L * 0.5 + 0.02, 0.0),
		vec3(CC_LID_HU - 0.24, CC_LID_L * 0.5 - 0.12, 0.012)), MID_CC_PANEL));
	{
		vec3 q = l - vec3(0.0, CC_EXT_WV, 0.03);
		float ring = sd_round_box(q, vec3(CC_EXT_WU + 0.12, CC_EXT_WS + 0.12, 0.02), 0.03);
		ring = max(ring, -sd_box(q, vec3(CC_EXT_WU, CC_EXT_WS, 0.20)));
		h = hmin3(h, hit3(ring, MID_CC_BRASS));
	}
	for (int sgn = 0; sgn < 2; sgn++) {
		float uc = (sgn != 0 ? 1.0 : -1.0) * (CC_LID_HU - 0.60);
		float r = sqrt(l.y * l.y + (l.z + CC_LID_T * 0.5) * (l.z + CC_LID_T * 0.5));
		h = hmin3(h, hit3(max(r - CC_LID_T * 0.42, abs(l.x - uc) - 0.35), MID_CC_BRASS));
	}
	return h;
}

hit3 map_console(vec3 p)
{
	hit3 h = hit3(1e9, 0);
	p = p - vec3(0.0, 0.0, cc_dz(u_cc_out, u_aspect));
	float cx = (RM_CC_X0 + RM_CC_X1) * 0.5;
	const float chx = (-5.10 + 15.20) * 0.5;
	const float inner = chx - RM_CC_CHEEK;

	float gross = sd_box(p - vec3(cx, RM_CC_TY1 * 0.5, (RM_CC_PZ0 + RM_CC_ZB) * 0.5),
		vec3(chx + 0.35, RM_CC_TY1 * 0.5 + 0.55, (RM_CC_PZ0 - RM_CC_ZB) * 0.5 + 0.35));
	if (gross > DRAWER_NEAR) {
		return hit3(gross, 0);
	}

	cc_face panel = cc_face_build(RM_CC_PY0, RM_CC_PZ0, RM_CC_PY1, RM_CC_PZ1);
	cc_face tower = cc_face_build(RM_CC_TY0, RM_CC_TZ0, RM_CC_TY1, RM_CC_TZ1);
	vec3 pf = cc_to_face(panel, p);
	vec3 tf = cc_to_face(tower, p);

	float plz0 = RM_CC_PLINTH_Z0, plz1 = RM_CC_ZB + 0.30;
	h = hmin3(h, hit3(sd_round_box(p - vec3(cx, RM_CC_PLINTH * 0.5, (plz0 + plz1) * 0.5),
		vec3(chx - 0.10, RM_CC_PLINTH * 0.5, (plz0 - plz1) * 0.5), 0.06), MID_CC_STEEL));
	{
		float lx0 = RM_CC_PLATE_X0 + 0.10, lx1 = RM_CC_LEDGE_X1;
		float lz0 = RM_CC_PZ0 + RM_CC_LEDGE, lz1 = RM_CC_PLINTH_Z0 - 0.20;
		h = hmin3(h, hit3(sd_round_box(p - vec3((lx0 + lx1) * 0.5, RM_CC_PLINTH * 0.5,
			(lz0 + lz1) * 0.5),
			vec3((lx1 - lx0) * 0.5, RM_CC_PLINTH * 0.5, (lz0 - lz1) * 0.5), 0.06),
			MID_CC_STEEL));
	}

	h = hmin3(h, hit3(sd_round_box(p - vec3(cx, (RM_CC_PLINTH + RM_CC_TY1 * 0.72) * 0.5,
		(RM_CC_PZ1 + RM_CC_ZB) * 0.5),
		vec3(inner, (RM_CC_TY1 * 0.72 - RM_CC_PLINTH) * 0.5, (RM_CC_PZ1 - RM_CC_ZB) * 0.5),
		0.10), MID_CC_STEEL));

	h = hmin3(h, hit3(sd_round_box(pf, vec3(inner, 0.17, panel.hl), 0.06), MID_CC_PANEL));
	h = hmin3(h, hit3(sd_round_box(tf, vec3(inner, 0.17, tower.hl), 0.06), MID_CC_PANEL));

	for (int sgn = 0; sgn < 2; sgn++) {
		float sx = sgn != 0 ? RM_CC_X1 - RM_CC_CHEEK * 0.5 : RM_CC_X0 + RM_CC_CHEEK * 0.5;
		vec3 q = p - vec3(sx, 0.0, 0.0);
		float prof = max(sd_round_box(q - vec3(0.0,
			(RM_CC_PLINTH + RM_CC_TY1) * 0.5, (RM_CC_PZ1 + RM_CC_ZB) * 0.5),
			vec3(RM_CC_CHEEK * 0.5, (RM_CC_TY1 - RM_CC_PLINTH) * 0.5,
				(RM_CC_PZ1 - RM_CC_ZB) * 0.5), 0.10),
			-1e9);
		float front = sd_round_box(q - vec3(0.0,
			(RM_CC_PLINTH + RM_CC_PY1) * 0.5, (RM_CC_PZ0 + RM_CC_PZ1) * 0.5),
			vec3(RM_CC_CHEEK * 0.5, (RM_CC_PY1 - RM_CC_PLINTH) * 0.5,
				(RM_CC_PZ1 - RM_CC_PZ0) * 0.5 * -1.0 + 0.0), 0.10);
		h = hmin3(h, hit3(min(prof, front), MID_CC_CHEEK));
	}

	/* the tube */
	{
		vec3 q = tf - vec3(CC_U[CC_CRT], 0.0, CC_S[CC_CRT]);
		h = hmin3(h, hit3(sd_torus(vec3(q.x, q.y - (RM_CC_SKIN + 0.02), q.z),
			1.18, 0.17), MID_CC_BRASS));
		h = hmin3(h, hit3(sd_cyl_y(vec3(q.x, q.y - (RM_CC_SKIN + 0.02), q.z),
			1.14, 0.09), MID_CC_GLASS));
	}
	/* two meters */
	for (int m = 0; m < 2; m++) {
		int idx = m != 0 ? CC_NET : CC_BATT;
		vec3 q = tf - vec3(CC_U[idx], 0.0, CC_S[idx]);
		h = hmin3(h, hit3(sd_round_box(vec3(q.x, q.y - (RM_CC_SKIN + 0.06), q.z),
			vec3(CC_HU[idx] * 0.92, 0.10, CC_HS[idx] * 0.88), 0.05), MID_CC_BRASS));
		h = hmin3(h, hit3(sd_round_box(vec3(q.x, q.y - (RM_CC_SKIN + 0.05), q.z),
			vec3(CC_HU[idx] * 0.78, 0.07, CC_HS[idx] * 0.74), 0.03), MID_CC_GLASS));
	}

	/* two switches */
	for (int sw = 0; sw < 2; sw++) {
		vec3 q = pf - vec3(CC_U[sw], 0.0, CC_S[sw]);
		h = hmin3(h, hit3(sd_round_box(vec3(q.x, q.y - (RM_CC_SKIN + 0.03), q.z),
			vec3(CC_HU[sw] * 0.82, 0.03, CC_HS[sw] * 0.82), 0.03), MID_CC_PANEL));
		h = hmin3(h, hit3(sd_cyl_y(vec3(q.x, q.y - (RM_CC_SKIN + 0.11), q.z),
			0.14, 0.06), MID_CC_STEEL));
		vec3 lamp = vec3(q.x, q.y - (RM_CC_SKIN + 0.05), q.z - CC_HS[sw] * 0.86);
		h = hmin3(h, hit3(sd_cyl_y(lamp, 0.21, 0.05), MID_CC_BRASS));
		h = hmin3(h, hit3(sd_sphere(vec3(lamp.x, lamp.y - 0.09, lamp.z), 0.155),
			sw != 0 ? MID_CC_LENS_BLUE : MID_CC_LENS_AMBER));
	}

	/* the knob */
	{
		vec3 q = pf - vec3(CC_U[CC_VOL], 0.0, CC_S[CC_VOL]);
		h = hmin3(h, hit3(sd_cyl_y(vec3(q.x, q.y - (RM_CC_SKIN + 0.035), q.z),
			0.88, 0.035), MID_CC_BRASS));
		float ang = atan2f(q.z, q.x);
		float flute = 0.500 + 0.010 * spow(max(0.0, scos(ang * 18.0)), 2.0);
		h = hmin3(h, hit3(0.85 * sd_cyl_y(vec3(q.x, q.y - (RM_CC_SKIN + 0.29), q.z),
			flute, 0.25), MID_CC_BAKELITE));
	}

	/* the two faders, the lamp's and the screen's */
	for (int fd = 0; fd < 2; fd++) {
		int idx = fd != 0 ? CC_SCREEN : CC_BRIGHT;
		vec3 q = pf - vec3(CC_U[idx], 0.0, CC_S[idx]);
		float plate = sd_round_box(vec3(q.x, q.y - (RM_CC_SKIN + 0.04), q.z),
			vec3(CC_HU[idx] * 0.95, 0.04, 0.30), 0.04);
		plate = max(plate, -sd_box(vec3(q.x, q.y - (RM_CC_SKIN + 0.14), q.z),
			vec3(CC_HU[idx] * 0.82, 0.14, 0.11)));
		h = hmin3(h, hit3(plate, MID_CC_BRASS));
	}

	/* the guarded set */
	{
		float ps = (CC_PW_G_S0 + CC_PW_G_S1) * 0.5;
		h = hmin3(h, hit3(sd_round_box(
			vec3(pf.x - 0.50, pf.y - (RM_CC_SKIN + 0.035), pf.z - ps),
			vec3(2.90, 0.035, (CC_PW_G_S0 - CC_PW_G_S1) * 0.5 + 0.09), 0.05),
			MID_CC_PANEL));

		float bs = CC_S[CC_PW_OFF];
		for (int i = 0; i < 3; i++) {
			vec3 q = pf - vec3(CC_U[CC_PW_OFF + i], 0.0, bs);
			h = hmin3(h, hit3(sd_cyl_y(vec3(q.x, q.y - (RM_CC_SKIN + 0.045), q.z),
				CC_PW_RING, 0.045), MID_CC_BRASS));
			h = hmin3(h, hit3(sd_cyl_y(vec3(q.x, q.y - (RM_CC_SKIN + 0.075), q.z),
				CC_PW_R + 0.045, 0.075), MID_CC_BRASS));
			float cap = max(
				sd_sphere(vec3(q.x, q.y - (RM_CC_SKIN - 0.30), q.z), 0.50),
				sd_cyl_y(vec3(q.x, q.y - (RM_CC_SKIN + 0.05), q.z), CC_PW_R, 0.16));
			h = hmin3(h, hit3(cap, i == 0 ? MID_CC_LENS_RED
				: i == 1 ? MID_CC_LENS_AMBER : MID_CC_LENS_GREEN));
		}

		{
			vec3 q = pf - vec3(CC_U[CC_LOCK], 0.0, CC_S[CC_LOCK]);
			h = hmin3(h, hit3(sd_cyl_y(vec3(q.x, q.y - (RM_CC_SKIN + 0.045), q.z),
				CC_PW_RING, 0.045), MID_CC_BRASS));
			h = hmin3(h, hit3(sd_cyl_y(vec3(q.x, q.y - (RM_CC_SKIN + 0.075), q.z),
				CC_PW_R + 0.045, 0.075), MID_CC_BRASS));
			float cap = max(
				sd_sphere(vec3(q.x, q.y - (RM_CC_SKIN - 0.30), q.z), 0.50),
				sd_cyl_y(vec3(q.x, q.y - (RM_CC_SKIN + 0.05), q.z), CC_PW_R, 0.16));
			h = hmin3(h, hit3(cap, MID_CC_LENS_BLUE));
		}

		{
			vec3 q = pf - vec3(CC_PW_LOCK_U, 0.0, bs);
			float barrel = sd_cyl_y(vec3(q.x, q.y - (RM_CC_SKIN + 0.055), q.z),
				0.235, 0.055);
			barrel = max(barrel, -sd_box(vec3(q.x, q.y - (RM_CC_SKIN + 0.105), q.z),
				vec3(0.032, 0.055, 0.145)));
			h = hmin3(h, hit3(barrel, MID_CC_BRASS));
		}

		for (int sgn = 0; sgn < 2; sgn++) {
			float u = sgn != 0 ? CC_PW_G_U : -CC_PW_G_U;
			h = hmin3(h, hit3(sd_round_box(
				vec3(pf.x - u, pf.y - (RM_CC_SKIN + CC_PW_G_N * 0.5), pf.z - CC_PW_G_S0),
				vec3(0.070, CC_PW_G_N * 0.5, 0.070), 0.040), MID_CC_BRASS));
		}
	}

	/* the pilot lenses along the lip */
	for (int i = 0; i < 6; i++) {
		float u = -inner + 0.70 + float(i) * (2.0 * (inner - 0.70) / 5.0);
		vec3 q = pf - vec3(u, 0.0, -panel.hl + 0.136);
		h = hmin3(h, hit3(sd_cyl_y(vec3(q.x, q.y - (RM_CC_SKIN + 0.035), q.z),
			0.13, 0.035), MID_CC_BRASS));
		h = hmin3(h, hit3(sd_sphere(vec3(q.x, q.y - (RM_CC_SKIN + 0.095), q.z), 0.095),
			(i & 1) != 0 ? MID_CC_LENS_GREEN : MID_CC_LENS_AMBER));
	}

	/* the walnut top of the tower, the recess the lid lies in, and the lid */
	{
		float z0 = RM_CC_TZ1 - 0.10, z1 = RM_CC_ZB + 0.08;
		h = hmin3(h, hit3(sd_round_box(p - vec3(cx, RM_CC_TY1 - 0.04, (z0 + z1) * 0.5),
			vec3(inner + 0.02, 0.06, (z0 - z1) * 0.5), 0.03), MID_CC_CHEEK));
		float y0 = RM_CC_TY1 - CC_LID_T - 0.03, y1 = RM_CC_TY1 + 1.5;
		float rz0 = CC_LID_HZ - 0.20, rz1 = CC_LID_HZ + CC_LID_L + 0.05;
		float recess = sd_box(p - vec3(cx, (y0 + y1) * 0.5, (rz0 + rz1) * 0.5),
			vec3(CC_LID_HU + 0.03, (y1 - y0) * 0.5, (rz1 - rz0) * 0.5));
		h.d = max(h.d, -recess);
		h = hmin3(h, hit3(sd_box(p - vec3(cx, y0 - 0.03, (rz0 + rz1) * 0.5),
			vec3(CC_LID_HU + 0.03, 0.03, (rz1 - rz0) * 0.5)), MID_CC_PANEL));
		if (u_layer != 6 && u_layer != 7) {
			h = hmin3(h, map_lid(p, 0.0));
		}
	}

	float edge = (RM_ZF + 0.02) - (p.z + cc_dz(u_cc_out, u_aspect));
	h.d = max(h.d, edge);
	return h;
}

/* draw.c's map_cc_ext: the lid on its own, open as far as u_cc_lid */
hit3 map_cc_ext(vec3 p)
{
	return map_lid(p - vec3(0.0, 0.0, cc_dz(u_cc_out, u_aspect)), u_cc_lid);
}

bool cc_ext_at(vec3 p)
{
	return map_cc_ext(p).d < 0.01;
}

int console_owner(vec3 p)
{
	p = p - vec3(0.0, 0.0, cc_dz(u_cc_out, u_aspect));
	cc_face panel = cc_face_build(RM_CC_PY0, RM_CC_PZ0, RM_CC_PY1, RM_CC_PZ1);
	cc_face tower = cc_face_build(RM_CC_TY0, RM_CC_TZ0, RM_CC_TY1, RM_CC_TZ1);
	for (int i = 0; i < CC_COUNT; i++) {
		vec3 q;
		if (CC_TOWER[i] != 0) {
			q = cc_to_face(tower, p);
		} else {
			q = cc_to_face(panel, p);
		}
		vec3 d = vec3(q.x - CC_U[i], q.y - 0.45, q.z - CC_S[i]);
		if (sd_box(d, vec3(CC_HU[i], 0.60, CC_HS[i])) < 0.0) {
			return i + 1;
		}
	}
	return 0;
}

/* ---- the post box ---- */

vec3 pb_to_cap(vec3 p)
{
	vec3 q = p - vec3(RM_PB_CX, RM_PB_SHOULDER + RM_PB_CAP_T * 0.5, RM_PB_CZ);
	return rot_x(q, -RM_PB_TILT);
}

hit3 map_post(vec3 p)
{
	hit3 h = hit3(1e9, 0);
	float gross = sd_box(p - vec3(RM_PB_CX, (RM_PB_SHOULDER + RM_PB_CAP_T) * 0.5, RM_PB_CZ),
		vec3(RM_PB_HX + 0.30, (RM_PB_SHOULDER + RM_PB_CAP_T) * 0.5 + 0.35, RM_PB_HZ + 0.55));
	if (gross > DRAWER_NEAR) {
		return hit3(gross, 0);
	}

	h = hmin3(h, hit3(sd_round_box(p - vec3(RM_PB_CX, RM_PB_PLINTH * 0.5, RM_PB_CZ),
		vec3(RM_PB_HX + 0.16, RM_PB_PLINTH * 0.5, RM_PB_HZ + 0.16), 0.07), MID_POST_DARK));

	float body_y0 = RM_PB_PLINTH, body_y1 = RM_PB_SHOULDER;
	float mouth_y0 = body_y0 + 0.14, mouth_y1 = body_y1 - 0.26;
	{
		float shell = sd_round_box(p - vec3(RM_PB_CX, (body_y0 + body_y1) * 0.5, RM_PB_CZ),
			vec3(RM_PB_HX, (body_y1 - body_y0) * 0.5, RM_PB_HZ), 0.16);
		float cav = sd_round_box(p - vec3(RM_PB_CX, (mouth_y0 + mouth_y1) * 0.5, RM_PB_CZ + 0.08),
			vec3(RM_PB_HX - 0.26, (mouth_y1 - mouth_y0) * 0.5, RM_PB_HZ - 0.18), 0.10);
		float mouth = sd_box(p - vec3(RM_PB_CX, (mouth_y0 + mouth_y1) * 0.5, RM_PB_CZ + RM_PB_HZ),
			vec3(RM_PB_HX - 0.26, (mouth_y1 - mouth_y0) * 0.5, 0.50));
		h = hmin3(h, hit3(max(shell, -min(cav, mouth)), MID_POST_RED));
		h = hmin3(h, hit3(sd_round_box(p - vec3(RM_PB_CX, (mouth_y0 + mouth_y1) * 0.5,
			RM_PB_CZ - RM_PB_HZ + 0.30),
			vec3(RM_PB_HX - 0.30, (mouth_y1 - mouth_y0) * 0.5 - 0.04, 0.05), 0.04),
			MID_POST_DARK));
	}

	/* the door */
	{
		float hy = body_y0 + 0.16, hz = RM_PB_CZ + RM_PB_HZ;
		vec3 q = rot_x(p - vec3(RM_PB_CX, hy, hz), -u_pb_open * 1.78);
		float dh = mouth_y1 + 0.16 - hy;
		vec3 c = q - vec3(0.0, dh * 0.5, 0.0);
		float door = sd_round_box(c, vec3(RM_PB_HX - 0.06, dh * 0.5, 0.10), 0.07);
		float slot = sd_box(q - vec3(0.0, RM_PB_FS_Y, 0.0),
			vec3(RM_PB_FS_HU, RM_PB_FS_HH, 0.30));
		h = hmin3(h, hit3(max(door, -slot), MID_POST_RED));
		{
			vec3 f = q - vec3(0.0, RM_PB_FS_Y, 0.12);
			float outer = sd_round_box(f, vec3(RM_PB_FS_HU + RM_PB_FS_FR,
				RM_PB_FS_HH + RM_PB_FS_FR, 0.035), 0.03);
			float inner = sd_box(f, vec3(RM_PB_FS_HU, RM_PB_FS_HH, 0.30));
			h = hmin3(h, hit3(max(outer, -inner), MID_CC_BRASS));
		}
		{
			vec3 hq = rot_x(q - vec3(0.0, RM_PB_FS_Y + RM_PB_FS_HH + RM_PB_FS_FR - 0.01,
				0.14), -RM_PB_HOOD_TILT);
			h = hmin3(h, hit3(sd_round_box(hq - vec3(0.0, 0.0, RM_PB_HOOD_OUT * 0.5),
				vec3(RM_PB_FS_HU + RM_PB_FS_FR + 0.04, 0.022, RM_PB_HOOD_OUT * 0.5), 0.015),
				MID_CC_BRASS));
		}
		h = hmin3(h, hit3(sd_round_box(q - vec3(0.0, dh * 0.42, 0.07),
			vec3(0.24, 0.32, 0.05), 0.09), MID_CC_BRASS));
	}

	vec3 cp = pb_to_cap(p);
	float cap = sd_round_box(cp, vec3(RM_PB_HX + 0.14, RM_PB_CAP_T * 0.5, RM_PB_HZ + 0.14), 0.13);
	h = hmin3(h, hit3(cap, MID_POST_RED));

	h.d = max(h.d, (RM_ZF + 0.02) - p.z);
	return h;
}

/* ---- the bookcase ---- */

void binder_bound(int i, out vec3 c, out vec3 e)
{
	float sway = abs(sin(BN_LEAN[i])) * BN_H[i];
	c = vec3(BN_X[i], RM_BN_FOOT + BN_H[i] * 0.5, RM_BN_ZC);
	e = vec3(BN_HW[i] + sway * 0.5 + 0.20, BN_H[i] * 0.5 + 0.20, RM_BN_DEPTH * 0.5 + 0.20);
}

hit3 shelf_binder_map(vec3 p, int i)
{
	float bh = BN_H[i], bhw = BN_HW[i];
	hit3 h = hit3(1e9, 0);
	vec3 q = p - vec3(BN_X[i], RM_BN_FOOT, RM_BN_ZC);
	if (BN_LEAN[i] != 0.0) {
		q = rot_z(q, -BN_LEAN[i]);
	}
	q = q - vec3(0.0, bh * 0.5, 0.0);

	float bow = 0.055 * (1.0 - abs(q.y) / max(bh * 0.5, 0.1));
	float body = sd_round_box(q, vec3(bhw + bow, bh * 0.5, RM_BN_DEPTH * 0.5), 0.16);

	vec3 lp = q - vec3(0.0, BN_LABEL_Y[i] - bh * 0.5, RM_BN_DEPTH * 0.5);
	float pocket = sd_round_box(lp, vec3(bhw * 0.72, 1.55, 0.13), 0.08);
	body = max(body, -pocket);
	h = hmin3(h, hit3(body, BN_MID[i]));
	h = hmin3(h, hit3(sd_round_box(lp - vec3(0.0, 0.0, -0.10),
		vec3(bhw * 0.72, 1.55, 0.05), 0.06), MID_BINDER_LABEL));

	h = hmin3(h, hit3(sd_box(q - vec3(0.0, bh * 0.5 - 0.30, -0.35),
		vec3(bhw - 0.22, 0.26, RM_BN_DEPTH * 0.5 - 0.55)), MID_FOLDER_PAPER));
	return h;
}

hit3 shelf_one_book(vec3 p, float x, float foot, float z, float ht,
	float hw, float lean, int mid)
{
	vec3 q = p - vec3(x, foot, z);
	if (lean != 0.0) {
		q = rot_z(q, -lean);
	}
	q = q - vec3(0.0, ht * 0.5, 0.0);
	return hit3(sd_round_box(q, vec3(hw, ht * 0.5, RM_BN_DEPTH * 0.44), 0.10), mid);
}

#define RM_BK_X (RM_BK_X0 + RM_BK_HW)
#define RM_BK_ZC (RM_BN_ZF - RM_BK_D * 0.5)

float book_band_y(int k)
{
	return RM_BK_H * (0.20 + 0.16 * float(k));
}

void book_bound(out vec3 c, out vec3 e)
{
	c = vec3(RM_BK_X, RM_BN_FOOT + RM_BK_H * 0.5, RM_BK_ZC);
	e = vec3(RM_BK_HW + 0.25, RM_BK_H * 0.5 + 0.25, RM_BK_D * 0.5 + 0.40);
}

hit3 shelf_book_map(vec3 p)
{
	hit3 h = hit3(1e9, 0);
	vec3 q = p - vec3(RM_BK_X, RM_BN_FOOT + RM_BK_H * 0.5, RM_BK_ZC);
	float hz = RM_BK_D * 0.5;
	float ax = clampf(q.x / RM_BK_HW, -1.0, 1.0);
	float bulge = 0.12 * (1.0 - ax * ax);
	float body = sd_round_box(q - vec3(0.0, 0.0, bulge * 0.5),
		vec3(RM_BK_HW, RM_BK_H * 0.5, hz + bulge * 0.5), 0.14);
	float head = sd_box(q - vec3(0.0, RM_BK_H * 0.5, -0.30),
		vec3(RM_BK_HW - 0.10, 0.13, hz - 0.15));
	body = max(body, -head);
	h = hmin3(h, hit3(body, MID_BOOK_LEATHER));
	h = hmin3(h, hit3(sd_box(q - vec3(0.0, RM_BK_H * 0.5 - 0.20, -0.30),
		vec3(RM_BK_HW - 0.12, 0.07, hz - 0.20)), MID_BOOK_GILT));
	for (int k = 0; k < 5; k++) {
		float y = book_band_y(k) - RM_BK_H * 0.5;
		h = hmin3(h, hit3(sd_round_box(q - vec3(0.0, y, hz + bulge - 0.02),
			vec3(RM_BK_HW - 0.03, 0.065, 0.09), 0.06), MID_BOOK_LEATHER));
	}
	return h;
}

hit3 shelf_books(vec3 p)
{
	hit3 h = hit3(1e9, 0);
	{
		vec3 c, e;
		book_bound(c, e);
		float db = sd_box(p - c, e);
		h = hmin3(h, db > DRAWER_NEAR ? hit3(db, 0) : shelf_book_map(p));
	}
	float x0 = RM_BK_X0 + 2.0 * RM_BK_HW + 0.15;
	float run = u_sh_hx - RM_SH_SIDE - x0;
	if (run > 2.0) {
		for (int i = 0; i < 3; i++) {
			float t = (float(i) + 0.7) / 3.4;
			h = hmin3(h, shelf_one_book(p, x0 + run * t, RM_BN_FOOT,
				RM_BN_ZC + 0.40, 6.6 + room_seed(i, 1) * 2.4, 0.62,
				0.16 + 0.10 * room_seed(i, 2),
				(i & 1) != 0 ? MID_BINDER_B : MID_BINDER_A));
		}
	}
	return h;
}

hit3 map_shelf(vec3 p)
{
	hit3 h = hit3(1e9, 0);
	float zc = (RM_SH_ZF + RM_SH_ZB) * 0.5;
	float zh = (RM_SH_ZF - RM_SH_ZB) * 0.5;
	float hx = u_sh_hx;

	float gross = sd_box(p - vec3(0.0, (RM_SH_CORN_Y + RM_FLOOR) * 0.5, zc),
		vec3(hx + RM_SH_CORN_P, (RM_SH_CORN_Y - RM_FLOOR) * 0.5, zh + RM_SH_CORN_P));
	if (gross > DRAWER_NEAR) {
		return hit3(gross, 0);
	}

	h = hmin3(h, hit3(sd_box(p - vec3(0.0, (RM_SH_PLINTH + RM_FLOOR) * 0.5, zc - RM_SH_PLINTH_Z),
		vec3(hx - 0.30, (RM_SH_PLINTH - RM_FLOOR) * 0.5, zh - RM_SH_PLINTH_Z)),
		MID_SHELF_CASE));
	for (int sgn = 0; sgn < 2; sgn++) {
		float sx = (sgn != 0 ? 1.0 : -1.0) * (hx - RM_SH_SIDE * 0.5);
		h = hmin3(h, hit3(sd_box(p - vec3(sx, (RM_SH_TOP + RM_SH_PLINTH) * 0.5, zc),
			vec3(RM_SH_SIDE * 0.5, (RM_SH_TOP - RM_SH_PLINTH) * 0.5, zh)), MID_SHELF_CASE));
	}
	h = hmin3(h, hit3(sd_box(p - vec3(0.0, (RM_SH_TOP + RM_SH_PLINTH) * 0.5, zc),
		vec3(RM_SH_SIDE * 0.5, (RM_SH_TOP - RM_SH_PLINTH) * 0.5, zh - 0.30)), MID_SHELF_CASE));
	h = hmin3(h, hit3(sd_box(p - vec3(0.0, (RM_SH_TOP + RM_SH_PLINTH) * 0.5,
		RM_SH_ZB + RM_SH_BACKT * 0.5),
		vec3(hx - RM_SH_SIDE, (RM_SH_TOP - RM_SH_PLINTH) * 0.5, RM_SH_BACKT * 0.5)),
		MID_SHELF_BACK));
	float board_top[4] = float[4](
		RM_SH_PLINTH, RM_SH_BAY_B, RM_SH_BAY_T + RM_SH_BOARD, RM_SH_TOP);
	for (int i = 0; i < 4; i++) {
		h = hmin3(h, hit3(sd_box(p - vec3(0.0, board_top[i] - RM_SH_BOARD * 0.5, zc + 0.12),
			vec3(hx - RM_SH_SIDE, RM_SH_BOARD * 0.5, zh - 0.12)), MID_SHELF_BOARD));
	}
	h = hmin3(h, hit3(sd_round_box(p - vec3(0.0, (RM_SH_CORN_Y + RM_SH_TOP) * 0.5,
		zc + RM_SH_CORN_P * 0.5),
		vec3(hx + RM_SH_CORN_P, (RM_SH_CORN_Y - RM_SH_TOP) * 0.5,
			zh + RM_SH_CORN_P * 0.5), 0.16), MID_SHELF_CASE));

	for (int i = 0; i < 2; i++) {
		float lx = i != 0 ? 8.2 : -8.2;
		vec3 q = p - vec3(lx, SHELF_LAMP_Y + 0.30, RM_SH_ZF - 0.15);
		float shade = sd_round_box(q, vec3(2.30, 0.26, 0.40), 0.16);
		shade = max(shade, -sd_round_box(q - vec3(0.0, -0.24, 0.0),
			vec3(2.10, 0.22, 0.26), 0.10));
		h = hmin3(h, hit3(shade, MID_CC_BRASS));
		h = hmin3(h, hit3(sd_round_box(q - vec3(0.0, -0.22, 0.0),
			vec3(1.95, 0.10, 0.17), 0.09), MID_CC_LENS_AMBER));
		h = hmin3(h, hit3(sd_round_box(q - vec3(0.0, 0.30, -0.55),
			vec3(0.26, 0.20, 0.55), 0.06), MID_CC_BRASS));
	}

	for (int i = 0; i < DESK_SHELF_COUNT; i++) {
		vec3 c, e;
		binder_bound(i, c, e);
		float db = sd_box(p - c, e);
		if (db > DRAWER_NEAR) {
			h = hmin3(h, hit3(db, 0));
			continue;
		}
		h = hmin3(h, shelf_binder_map(p, i));
	}
	h = hmin3(h, shelf_books(p));
	return h;
}

/* ---- the room ---- */

bool room_is_drawer(int id)
{
	return !(id == MID_ROOM_FLOOR || id == MID_ROOM_WALL || id == MID_ROOM_SKIRT
		|| id == MID_DESK_TOP || id == MID_DESK_BODY || id == MID_GADGET_BODY
		|| id == MID_GADGET_GLASS || id == MID_GADGET_LEATHER
		|| id == MID_GADGET_DIAL || id == MID_GADGET_PAPER);
}

hit3 scene_map(vec3 p)
{
	hit3 h = hit3(1e9, 0);

	if (u_layer != 2) {
		float wall_z = (u_layer >= 3) ? (RM_SH_ZB - 0.50) : RM_WALL_Z;
		h = hmin3(h, hit3(p.y - RM_FLOOR, MID_ROOM_FLOOR));
		h = hmin3(h, hit3(p.z - wall_z, MID_ROOM_WALL));
		h = hmin3(h, hit3(sd_box(p - vec3(0.0, RM_FLOOR + 1.10, wall_z + 0.32),
			vec3(90.0, 1.10, 0.32)), MID_ROOM_SKIRT));
		if (u_layer == 3) {
			h = hmin3(h, map_shelf(p));
		}
		h = hmin3(h, map_console(p));
		h = hmin3(h, map_post(p));
		if (u_layer == 6) {
			h = hmin3(h, map_cc_ext(p));
		}
	}

	h = hmin3(h, hit3(sd_round_box(p - vec3(0.0, -RM_TOP_T * 0.5, (RM_ZN + RM_ZF) * 0.5),
		vec3(RM_HX, RM_TOP_T * 0.5, (RM_ZN - RM_ZF) * 0.5), 0.16), MID_DESK_TOP));

	if (u_layer != 2) {
		for (int s = 0; s < 2; s++) {
			float sx = s != 0 ? (RM_HX - 0.85) : -(RM_HX - 0.85);
			h = hmin3(h, hit3(sd_box(p - vec3(sx, -9.5, (RM_ZN + RM_ZF) * 0.5 + 0.5),
				vec3(0.52, 9.0, (RM_ZN - RM_ZF) * 0.5 - 0.6)), MID_DESK_BODY));
		}
		h = hmin3(h, hit3(sd_box(p - vec3(0.0, -2.6, RM_ZF + 0.6),
			vec3(RM_HX - 0.6, 2.1, 0.38)), MID_DESK_BODY));
		h = hmin3(h, hit3(sd_box(p - vec3(0.0, -0.70, RM_ZN - 0.34),
			vec3(RM_HX - 0.6, 0.28, 0.34)), MID_DESK_BODY));
		h = hmin3(h, hit3(sd_box(p - vec3(0.0, -4.10, RM_ZN - 0.34),
			vec3(RM_HX - 0.6, 0.28, 0.34)), MID_DESK_BODY));
		h = hmin3(h, room_gadgets(p));
	}

	if (u_layer == 1 || u_layer >= 4) {
		return h;
	}

	/* the drawer */
	vec3 dp = p - vec3(0.0, 0.0, room_pull());
	float cy = (RM_DR_TOP + RM_DR_BOT) * 0.5;
	float chy = (RM_DR_TOP - RM_DR_BOT) * 0.5;
	h = hmin3(h, hit3(sd_round_box(dp - vec3(0.0, cy, RM_DR_FACE - 0.26),
		vec3(RM_DR_HX, chy, 0.26), 0.10), MID_DRAWER_FACE));

	float in_hx = RM_DR_HX - 0.42;
	float in_zc = (RM_DR_FACE - 0.52 + RM_DR_BACK) * 0.5;
	float in_zh = (RM_DR_FACE - 0.52 - RM_DR_BACK) * 0.5;
	float felt_y = RM_DR_BOT + 0.22
		- (u_layer == 2 ? 0.0 : felt_dip(dp - vec3(0.0, 0.0, RM_ITEM_Z)));
	h = hmin3(h, hit3(sd_box(dp - vec3(0.0, felt_y, in_zc), vec3(in_hx, 0.22, in_zh)), MID_FELT));
	for (int s = 0; s < 2; s++) {
		float sx = s != 0 ? in_hx : -in_hx;
		h = hmin3(h, hit3(sd_box(dp - vec3(sx, cy + 0.16, in_zc),
			vec3(0.22, chy - 0.16, in_zh)), MID_DRAWER_WOOD));
	}
	h = hmin3(h, hit3(sd_box(dp - vec3(0.0, cy + 0.16, RM_DR_BACK - 0.22),
		vec3(in_hx, chy - 0.16, 0.22)), MID_DRAWER_WOOD));

	vec3 hp = dp - vec3(0.0, cy - 0.08, RM_DR_FACE + 0.02);
	float plate = sd_round_box(hp, vec3(1.95, 0.42, 0.10), 0.09);
	float cup = sd_round_box(hp - vec3(0.0, -0.30, 0.16), vec3(1.62, 0.34, 0.24), 0.16);
	cup = max(cup, -sd_round_box(hp - vec3(0.0, -0.46, 0.40), vec3(1.44, 0.34, 0.26), 0.14));
	h = hmin3(h, hit3(min(plate, cup), MID_BRASS));

	if (u_layer != 2) {
		h = hmin3(h, drawer_items_map(dp - vec3(0.0, RM_ITEM_Y, RM_ITEM_Z)));
	}
	return h;
}

/* The four taps of draw.c's scene_normal and scene_curvature, as a table
 * so that each is one loop and one call of scene_map rather than four
 * copies of it for the compiler to inline. */
const vec3 TETRA[4] = vec3[4](vec3(1.0, -1.0, -1.0), vec3(-1.0, -1.0, 1.0),
	vec3(-1.0, 1.0, -1.0), vec3(1.0, 1.0, 1.0));

vec3 scene_normal(vec3 p)
{
	const float e = 0.0035;
	vec3 n = vec3(0.0);
	for (int i = 0; i < 4; i++) {
		n += TETRA[i] * scene_map(p + TETRA[i] * e).d;
	}
	return vnorm(n);
}

float scene_curvature(vec3 p)
{
	const float e = 0.030;
	float s = 0.0;
	for (int i = 0; i < 4; i++) {
		s += scene_map(p + TETRA[i] * e).d;
	}
	return clampf(-s / (e * 4.0) * 0.5, -1.0, 1.0);
}

float scene_shadow(vec3 ro, vec3 rd, float ang)
{
	float tmax = 40.0;
	float k = 1.0 / max(ang, 0.01);
	float t = 0.05, res = 1.0, ph = 1e9;
	for (int i = 0; i < 40 && t < tmax; i++) {
		float d = scene_map(ro + rd * t).d;
		if (d < 0.004) {
			return 0.0;
		}
		float y = d * d / (2.0 * ph);
		float dd = sqrt(max(d * d - y * y, 0.0));
		res = min(res, k * dd / max(0.0001, t - y));
		ph = d;
		t += clampf(d, 0.05, 1.40);
	}
	res = clampf(res, 0.0, 1.0);
	return res * res * (3.0 - 2.0 * res);
}

float scene_ao(vec3 p, vec3 n)
{
	const float scale = 2.2;
	float occ = 0.0, sca = 1.0;
	for (int i = 1; i <= 5; i++) {
		float dist = 0.022 * float(i * i) * scale;
		float d = scene_map(p + n * dist).d;
		occ += (dist - d) * sca / scale;
		sca *= 0.72;
	}
	return clampf(1.0 - occ * 1.70, 0.05, 1.0);
}

float continent(vec3 n)
{
	float lon = atan2f(n.x, n.z);
	float lat = n.y;
	float d = 0.0;
	d = max(d, 0.80 - sqrt((lon + 0.55) * (lon + 0.55) * 1.7 + (lat - 0.06) * (lat - 0.06) * 3.5));
	d = max(d, 0.52 - sqrt((lon + 0.32) * (lon + 0.32) * 4.2 + (lat - 0.58) * (lat - 0.58) * 6.4));
	d = max(d, 0.74 - sqrt((lon - 1.18) * (lon - 1.18) * 1.25 + (lat - 0.40) * (lat - 0.40) * 3.1));
	d = max(d, 0.70 - sqrt((lon + 1.88) * (lon + 1.88) * 1.55 + (lat - 0.46) * (lat - 0.46) * 2.5));
	d = max(d, 0.60 - sqrt((lon + 1.38) * (lon + 1.38) * 3.6 + (lat + 0.28) * (lat + 0.28) * 2.15));
	d = max(d, 0.48 - sqrt((lon - 2.15) * (lon - 2.15) * 4.6 + (lat + 0.42) * (lat + 0.42) * 8.2));
	d += (room_fbm(lon * 3.1 + 2.0, lat * 3.4) - 0.5) * 0.24;
	return d;
}

/* ---- materials ---- */

struct mtl {
	vec3 albedo;
	float rough;
	float metal;
	vec3 emit;
	float coat;
	float coat_rough;
	float bump_freq;
	float bump_amp;
	float dust;
	float edge;
};

void walnut_at(vec3 q, float u, float v, inout mtl m, float plank, float use)
{
	float board = plank > 0.0 ? floor(u / plank) : 0.0;
	float bu = plank > 0.0 ? u - board * plank : u;
	float jitter = room_seed(int(board), 3);

	float pith = plank * (0.24 + jitter * 0.52);
	float wob = room_fbm(v * 0.09 + board * 3.7, 4.2) - 0.5;
	float r = abs(bu - pith + wob * plank * 0.30);

	float season = room_fbm2(r * 0.52 + board * 5.3, 2.7) - 0.5;
	float rate = (7.2 + jitter * 2.4) * (1.0 + season * 0.85);
	float ring = r * rate
		+ room_fbm(v * 0.16 + board * 2.1, r * 0.8) * 3.4
		+ room_fbm2(v * 0.62 + board * 9.1, r * 1.9) * 1.25;
	float g = 0.5 + 0.5 * ssin(ring * 2.0 * M_PI);
	g = spow(g, 2.7);

	float cath = smoothstepf(0.0, plank * 0.18, r);
	g = g + (g * 0.55 + 0.28 - g) * (1.0 - cath);

	float btone = room_seed(int(board), 4);
	vec3 light = vec3(0.234, 0.114, 0.049) * (0.80 + btone * 0.44);
	vec3 dark = vec3(0.052, 0.021, 0.009) * (0.82 + btone * 0.38);
	m.albedo = mix(light, dark, clampf(g, 0.0, 1.0));

	float fleck = spow(room_noise(u * 46.0, v * 7.0), 7.0);
	m.albedo += vec3(0.14, 0.11, 0.07) * fleck;
	float sap = smoothstepf(0.62, 0.86, room_fbm(u * 0.5 + 31.0, v * 0.11));
	m.albedo = mix(m.albedo, vec3(0.265, 0.190, 0.124), sap * 0.35);

	float pore = spow(room_noise(u * 130.0, v * 6.5), 4.0);
	m.albedo *= 1.0 - pore * 0.30;

	if (plank > 0.0) {
		float seam = abs(bu - plank * 0.5);
		seam = smoothstepf(plank * 0.5, plank * 0.5 - 0.055, seam);
		m.albedo *= 1.0 - seam * 0.55;
	}

	m.rough = 0.26 + g * 0.13 + pore * 0.22;
	m.coat = 0.26 + room_fbm2(u * 0.42 + 8.0, v * 0.42) * 0.14;
	m.coat_rough = 0.19 + room_fbm(u * 0.8, v * 0.8) * 0.085;
	m.bump_freq = 26.0;
	m.bump_amp = 0.030;
	m.dust = 1.0;

	if (use <= 0.0) {
		return;
	}

	float touch = clampf(use * (0.30 + 0.85 * room_touch(q)), 0.0, 1.0);

	float bleach = room_sun(q) * (0.55 + 0.45 * use);
	vec3 faded = mix(m.albedo, vec3(0.208, 0.156, 0.112), 0.62);
	m.albedo = mix(m.albedo, faded, bleach * 0.40);

	float sc = scratch_at(u, v, 1.90, 13.0, 0.026, 1.0) * 0.85
		+ scratch_at(v, u, 3.40, 17.0, 0.034, 6.0) * 0.62
		+ scratch_at(u, v, 0.52, 8.0, 0.010, 13.0) * 0.48;
	sc = clampf(sc, 0.0, 1.0);
	sc = clampf(sc * (0.35 + touch * 1.35), 0.0, 1.0);

	float nick = nick_at(u, v, 1.5, 3.0) * clampf(0.30 + touch, 0.0, 1.0);

	float worn = clampf(touch * 1.05 + sc * 0.90, 0.0, 1.0);
	m.coat = clampf(m.coat - 0.42 * worn, 0.04, 1.0);
	m.coat_rough = clampf(m.coat_rough + worn * 0.30 + sc * 0.14, 0.02, 1.0);
	m.rough = clampf(m.rough + worn * 0.18 + sc * 0.20, 0.05, 1.0);

	m.albedo = mix(m.albedo, (m.albedo + vec3(0.055, 0.044, 0.034)) * 1.55, sc * 0.62);
	m.albedo = mix(m.albedo, m.albedo * 0.40, nick * 0.85);
	m.rough = clampf(m.rough + nick * 0.30, 0.05, 1.0);
	m.coat = clampf(m.coat * (1.0 - nick * 0.80), 0.0, 1.0);

	m.edge = 1.0 + use * 1.5;

	float grime = (1.0 - touch) * use * 0.55
		* clampf(room_fbm2(q.x * 0.34 + 11.0, q.z * 0.34 + 2.0) * 1.5 - 0.30, 0.0, 1.0);
	m.albedo = mix(m.albedo, vec3(0.062, 0.050, 0.042), grime * 0.26);
	m.rough = clampf(m.rough + grime * 0.16, 0.05, 1.0);
}

void desk_stains(vec3 w, inout mtl m)
{
	const vec4 RING[3] = vec4[3](
		vec4(-2.10, 3.40, 1.30, 1.00),
		vec4(5.70, 0.90, 1.14, 0.72),
		vec4(-6.90, -1.10, 1.46, 0.46));
	for (int i = 0; i < 3; i++) {
		float dx = w.x - RING[i].x, dz = w.z - RING[i].y;
		float d = sqrt(dx * dx + dz * dz);
		float edge = smoothstepf(0.17, 0.02, abs(d - RING[i].z));
		float inside = smoothstepf(RING[i].z, RING[i].z - 0.55, d);
		float a = atan2f(dz, dx);
		float keep = smoothstepf(0.28, 0.64,
			room_fbm2(cos(a) * 1.7 + float(i) * 7.3, sin(a) * 1.7 + float(i) * 2.9));
		float ring = edge * keep * RING[i].w;
		m.albedo = mix(m.albedo, m.albedo * 0.58, ring * 0.60);
		m.albedo = mix(m.albedo, m.albedo * 1.12, inside * keep * RING[i].w * 0.14);
		m.rough = clampf(m.rough + ring * 0.26, 0.05, 1.0);
		m.coat = clampf(m.coat * (1.0 - ring * 0.60), 0.0, 1.0);
	}
	float sx = (w.x - 8.60) / 2.30, sz = (w.z + 4.10) / 1.70;
	float sd = sqrt(sx * sx + sz * sz) * (0.75 + room_fbm2(w.x * 0.8 + 3.0, w.z * 0.8) * 0.6);
	float spill = smoothstepf(1.05, 0.45, sd);
	m.albedo = mix(m.albedo, m.albedo * 0.68, spill * 0.40);
	m.coat = clampf(m.coat * (1.0 - spill * 0.50), 0.0, 1.0);
	m.rough = clampf(m.rough + spill * 0.14, 0.05, 1.0);
}

mtl icon_material(int id, vec3 pw)
{
	mtl m;
	m.albedo = vec3(0.5, 0.5, 0.5);
	m.rough = 0.45;
	m.metal = 0.0;
	m.edge = 1.0;
	m.emit = vec3(0.0);
	m.coat = 0.0;
	m.coat_rough = 0.08;
	m.bump_freq = 0.0;
	m.bump_amp = 0.0;
	m.dust = 0.0;

	vec3 w = pw;                    /* world space, for the room */
	vec3 p = scene_local(pw, id);   /* object space, for the objects */
	/* Seven materials are walnut, each cut its own way. draw.c calls
	 * walnut_at from each case; here each case only says how it is cut --
	 * u, v, plank, use -- and the one call is after the switch. A compiler
	 * inlines every call it is given, and seven copies of the biggest
	 * function in the file is more program than some drivers will take. */
	vec4 wood = vec4(0.0, 0.0, 0.0, -1.0);
	switch (id) {
	case MID_CRT_BODY: {
		float grain = room_fbm(p.x * 6.2 + p.z * 1.4, p.y * 5.1);
		vec3 q = rot_y(rot_x(p, -0.32), 0.38);
		float age = smoothstepf(0.34, -0.30, q.y);
		vec3 fresh = vec3(0.575, 0.552, 0.485);
		vec3 yellowed = vec3(0.585, 0.508, 0.352);
		m.albedo = mix(fresh, yellowed, age * 0.85);
		m.albedo *= 0.94 + grain * 0.12;
		float seam = smoothstepf(0.010, 0.0, abs(q.y + 0.145));
		m.albedo *= 1.0 - seam * 0.30;
		m.rough = 0.46 + grain * 0.10 + seam * 0.10;
		m.bump_freq = 190.0;
		m.bump_amp = 0.030;
		m.dust = 1.0;
		break;
	}
	case MID_CRT_BEZEL:
		m.albedo = vec3(0.055, 0.055, 0.062);
		m.rough = 0.34;
		m.bump_freq = 240.0;
		m.bump_amp = 0.022;
		m.dust = 0.8;
		break;
	case MID_CRT_SCREEN: {
		vec3 q = rot_y(rot_x(p, -0.32), 0.38);
		float u = clampf((q.x + 0.32) / 0.64, 0.0, 1.0);
		float v = clampf((0.40 - q.y) / 0.46, 0.0, 1.0);
		float scan = 0.42 + 0.58 * (0.5 + 0.5 * ssin(v * 92.0));
		float vig = smoothstepf(0.0, 0.16, u) * smoothstepf(1.0, 0.84, u)
			* smoothstepf(0.0, 0.14, v) * smoothstepf(1.0, 0.86, v);
		vig = 0.34 + 0.66 * vig;
		m.albedo = vec3(0.020, 0.055, 0.026);
		m.rough = 0.045;
		m.coat = 0.0;
		vec3 phos = vec3(0.10, 0.78, 0.20);
		float lum = 0.30 * scan;
		if (u > 0.13 && u < 0.29 && v > 0.33 && v < 0.63) {
			lum = 2.30;
		}
		if (u > 0.31 && u < 0.80 && v > 0.55 && v < 0.655) {
			lum = 1.55;
		}
		if (u > 0.13 && u < 0.62 && v > 0.20 && v < 0.29) {
			lum = 1.20;
		}
		m.emit = phos * (lum * scan * vig);
		break;
	}
	case MID_CRT_KEYBED:
		m.albedo = vec3(0.115, 0.110, 0.104);
		m.rough = 0.58;
		m.bump_freq = 210.0;
		m.bump_amp = 0.030;
		m.dust = 1.0;
		break;
	case MID_CRT_KEY: {
		vec3 q = rot_y(rot_x(p, -0.32), 0.38);
		vec3 k = q - vec3(0.00, -0.32, 0.18);
		float cx = floor(k.x / 0.092 + 0.5);
		float cz = floor(k.z / 0.082 + 0.5);
		float tint = room_seed(int(cx) * 64 + int(cz), 5);
		m.albedo = vec3(0.735, 0.715, 0.660) * (0.92 + tint * 0.16);
		m.rough = 0.40 + tint * 0.10;
		m.bump_freq = 260.0;
		m.bump_amp = 0.018;
		m.dust = 0.5;
		break;
	}
	case MID_CRT_LED:
		m.albedo = vec3(0.10, 0.45, 0.16);
		m.rough = 0.08;
		m.emit = vec3(0.30, 3.20, 0.60);
		break;
	case MID_GLOBE: {
		vec3 q = rot_y(rot_x(p, -0.18), 0.22);
		vec3 gn = vnorm(q - vec3(0.0, 0.12, 0.0));
		float land = continent(gn);
		if (gn.y > 0.80 || gn.y < -0.74) {
			m.albedo = vec3(0.88, 0.91, 0.95);
			m.rough = 0.42;
		} else if (land > 0.18) {
			float desert = clampf(land * 0.4 + room_fbm(gn.x * 4.0, gn.z * 4.0) * 0.5, 0.0, 1.0);
			m.albedo = mix(vec3(0.135, 0.320, 0.098), vec3(0.520, 0.390, 0.155), desert * 0.60);
			m.rough = 0.60;
			m.bump_freq = 46.0;
			m.bump_amp = 0.055;
		} else {
			float shallow = clampf(0.55 + gn.y * 0.2 + land * 1.4, 0.0, 1.0);
			m.albedo = mix(vec3(0.018, 0.070, 0.220), vec3(0.075, 0.330, 0.520), shallow);
			m.rough = 0.30;
		}
		float lon = atan2f(gn.x, gn.z);
		float gore = abs(mod(lon + 20.0 * M_PI, M_PI / 6.0) - M_PI / 12.0);
		m.albedo *= 1.0 - smoothstepf(0.018, 0.0, gore) * 0.16;
		m.coat = 1.0;
		m.coat_rough = 0.055;
		m.dust = 0.35;
		break;
	}
	case MID_GLOBE_RING: {
		float brush = room_noise(p.x * 120.0 + p.y * 18.0, p.z * 120.0);
		m.albedo = vec3(0.86, 0.66, 0.28) * (0.90 + brush * 0.18);
		m.rough = 0.155 + brush * 0.10;
		m.metal = 1.0;
		m.dust = 0.25;
		break;
	}
	case MID_GLOBE_STAND: {
		float grain = room_fbm(p.x * 6.2 + p.z * 1.4, p.y * 5.1);
		vec3 q = rot_y(rot_x(p, -0.18), 0.22);
		float r = sqrt(q.x * q.x + q.z * q.z);
		float rings = 0.5 + 0.5 * ssin(r * 130.0 + room_fbm(r * 9.0, q.y * 6.0) * 5.0);
		m.albedo = mix(vec3(0.105, 0.048, 0.022), vec3(0.215, 0.110, 0.052), rings * 0.7 + grain * 0.3);
		m.rough = 0.34 + rings * 0.10;
		m.coat = 0.6;
		m.coat_rough = 0.16;
		m.bump_freq = 120.0;
		m.bump_amp = 0.022;
		m.dust = 0.8;
		break;
	}
	case MID_FOLDER: {
		float fib = room_fbm(p.x * 150.0, p.y * 24.0);
		m.albedo = vec3(0.760, 0.580, 0.245) * (0.93 + fib * 0.14);
		m.rough = 0.62 + fib * 0.12;
		m.bump_freq = 330.0;
		m.bump_amp = 0.026;
		m.dust = 0.9;
		break;
	}
	case MID_FOLDER_PAPER: {
		float fib = room_fbm(p.x * 170.0, p.y * 28.0);
		m.albedo = vec3(0.855, 0.838, 0.785) * (0.96 + fib * 0.08);
		m.rough = 0.70;
		m.bump_freq = 360.0;
		m.bump_amp = 0.020;
		m.dust = 0.7;
		break;
	}
	case MID_NOTES: {
		vec3 q = rot_y(rot_x(p, -0.40), 0.28);
		float fib = room_fbm(q.x * 160.0, q.y * 26.0);
		m.albedo = vec3(0.900, 0.815, 0.400) * (0.95 + fib * 0.10);
		if (q.x < -0.20) {
			m.albedo = vec3(0.640, 0.105, 0.075);
		} else if (abs(mod(q.y + 10.0, 0.078) - 0.039) < 0.010 && q.y < 0.38) {
			m.albedo = mix(m.albedo, vec3(0.30, 0.44, 0.66), 0.80);
		}
		m.rough = 0.66;
		m.bump_freq = 340.0;
		m.bump_amp = 0.024;
		m.dust = 0.8;
		break;
	}
	case MID_NOTES_BIND:
		m.albedo = vec3(0.480, 0.070, 0.048);
		m.rough = 0.44;
		m.bump_freq = 200.0;
		m.bump_amp = 0.030;
		m.dust = 0.6;
		break;
	case MID_GEAR: {
		vec3 q = rot_x(rot_y(p, 0.18), -0.22);
		float r = sqrt(q.x * q.x + q.y * q.y);
		float turn = 0.5 + 0.5 * ssin(r * 520.0);
		float scratch = spow(room_noise(q.x * 34.0, q.y * 11.0 + q.z * 5.0), 9.0);
		float tarnish = clampf(room_fbm(q.x * 7.0, q.y * 7.0) * 1.3 - 0.35, 0.0, 1.0);
		m.albedo = mix(vec3(0.880, 0.662, 0.245), vec3(0.395, 0.300, 0.150), tarnish * 0.55);
		m.albedo += vec3(1.0) * (scratch * 0.10);
		m.rough = 0.185 + turn * 0.075 + tarnish * 0.24 - scratch * 0.10;
		m.rough = clampf(m.rough, 0.05, 0.85);
		m.metal = 1.0;
		m.bump_freq = 430.0;
		m.bump_amp = 0.016;
		m.dust = 0.45;
		break;
	}

	case MID_FELT: {
		float fib = room_fbm(w.x * 34.0, w.z * 34.0);
		float nap = room_fbm(w.x * 7.0 + 13.0, w.z * 7.0);
		m.albedo = mix(vec3(0.036, 0.082, 0.048), vec3(0.072, 0.134, 0.078), fib);
		m.albedo *= 0.86 + nap * 0.30;
		float bare = clampf(room_fbm2(w.x * 0.95 + 21.0, w.z * 0.95) * 1.6 - 0.42, 0.0, 1.0);
		m.albedo = mix(m.albedo, vec3(0.062, 0.078, 0.058), bare * 0.55);
		m.rough = 0.94 - bare * 0.14;
		m.bump_freq = 52.0;
		m.bump_amp = 0.075;
		m.dust = 0.4;
		break;
	}
	case MID_DRAWER_WOOD: {
		bool vert = abs(w.x) > RM_DR_HX - 1.0;
		wood = vec4(vert ? w.z : w.x, vert ? w.y * 3.0 : w.z, 0.0, 0.60);
		break;
	}
	case MID_DRAWER_FACE:
		wood = vec4(w.x, w.y * 2.6, 0.0, 1.10);
		break;
	case MID_DESK_TOP:
		wood = vec4(w.x, w.z, 5.4, 1.00);
		break;
	case MID_DESK_BODY:
		wood = vec4(w.z, w.y, 0.0, 0.50);
		break;
	case MID_BRASS: {
		float brush = room_noise(w.x * 90.0, w.y * 14.0 + w.z * 6.0);
		float rub = clampf(0.22 + room_touch(w) * 1.5, 0.0, 1.0);
		float tarn = clampf(room_fbm2(w.x * 2.3 + 4.0, (w.y * 1.7 + w.z) * 2.3) * 1.55 - 0.32,
			0.0, 1.0);
		tarn = clampf(tarn * (1.0 - rub * 0.78) + 0.12 * (1.0 - rub), 0.0, 1.0);
		m.albedo = mix(vec3(0.862, 0.660, 0.296), vec3(0.318, 0.272, 0.158), tarn * 0.80);
		m.albedo *= 0.88 + brush * 0.22;
		float sc = scratch_at(w.x, w.y * 0.7 + w.z * 0.7, 0.30, 7.0, 0.006, 2.0);
		m.albedo = mix(m.albedo, vec3(0.930, 0.818, 0.560), sc * 0.34);
		m.rough = clampf(0.185 + brush * 0.10 + tarn * 0.40 - sc * 0.07, 0.04, 1.0);
		m.metal = 1.0;
		m.dust = 0.3;
		break;
	}
	case MID_ROOM_FLOOR: {
		float board = floor(w.x / 3.6);
		float bu = w.x - board * 3.6;
		float j = room_seed(int(board), 6);
		float g = 0.5 + 0.5 * ssin((bu * 2.4 + room_fbm(w.z * 0.22 + board, bu) * 4.0) * 2.0 * M_PI);
		m.albedo = mix(vec3(0.168, 0.118, 0.072), vec3(0.098, 0.066, 0.040), g * (0.6 + j * 0.4));
		float seam = smoothstepf(1.80, 1.74, abs(bu - 1.8));
		m.albedo *= 1.0 - seam * 0.6;
		m.rough = 0.42 + g * 0.18;
		m.coat = 0.35;
		m.coat_rough = 0.22;
		m.bump_freq = 14.0;
		m.bump_amp = 0.030;
		m.dust = 0.9;
		break;
	}
	case MID_ROOM_WALL: {
		float t = room_fbm(w.x * 1.6, w.y * 1.6);
		m.albedo = vec3(0.300, 0.286, 0.262) * (0.92 + t * 0.16);
		m.rough = 0.88;
		m.bump_freq = 18.0;
		m.bump_amp = 0.045;
		break;
	}
	case MID_ROOM_SKIRT:
		m.albedo = vec3(0.300, 0.284, 0.268);
		m.rough = 0.52;
		m.coat = 0.4;
		m.coat_rough = 0.20;
		break;
	case MID_GADGET_BODY: {
		float t = room_fbm(w.x * 9.0, w.z * 9.0 + w.y * 4.0);
		m.albedo = mix(vec3(0.036, 0.030, 0.028), vec3(0.082, 0.062, 0.050), t * 0.8);
		float sc = scratch_at(w.x, w.z, 0.26, 6.0, 0.006, 31.0);
		m.albedo = mix(m.albedo, m.albedo * 2.10, clampf(sc, 0.0, 1.0) * 0.30);
		m.rough = 0.26 + t * 0.10 + sc * 0.25;
		m.coat = 0.34;
		m.coat_rough = 0.185;
		m.bump_freq = 140.0;
		m.bump_amp = 0.020;
		m.dust = 0.9;
		m.edge = 2.3;
		break;
	}
	case MID_GADGET_LEATHER: {
		float t = room_fbm(w.x * 26.0, w.z * 26.0);
		float cell = spow(room_noise(w.x * 52.0, w.z * 52.0), 2.2);
		m.albedo = mix(vec3(0.128, 0.060, 0.034), vec3(0.226, 0.118, 0.066), t);
		m.albedo *= 0.90 + cell * 0.22;
		float rub = clampf(0.25 + room_touch(w) * 1.4, 0.0, 1.0);
		float bald = clampf(room_fbm2(w.x * 1.4 + 6.0, w.z * 1.4) * 1.5 - 0.30, 0.0, 1.0) * rub;
		m.albedo = mix(m.albedo, m.albedo * 1.62, bald * 0.42);
		m.rough = 0.54 + t * 0.20 - bald * 0.16;
		m.coat = 0.30 + bald * 0.20;
		m.coat_rough = 0.28 - bald * 0.10;
		m.bump_freq = 60.0;
		m.bump_amp = 0.090 * (1.0 - bald * 0.45);
		m.dust = 0.9;
		m.edge = 2.2;
		break;
	}
	case MID_GADGET_GLASS:
		m.albedo = vec3(0.030, 0.030, 0.032);
		m.rough = 0.035;
		m.coat = 1.0;
		m.coat_rough = 0.030;
		m.dust = 0.25;
		break;
	case MID_GADGET_DIAL:
		m.albedo = vec3(0.060, 0.058, 0.054);
		m.rough = 0.10;
		m.coat = 1.0;
		m.coat_rough = 0.045;
		m.dust = 0.3;
		break;
	case MID_SHELF_CASE:
		wood = vec4(w.y, w.x, 0.0, 0.55);
		break;
	case MID_SHELF_BOARD:
		wood = vec4(w.x, w.z, 6.2, 0.70);
		break;
	case MID_SHELF_BACK: {
		float t = room_fbm(w.x * 3.0, w.y * 3.0);
		m.albedo = vec3(0.086, 0.052, 0.032) * (0.86 + t * 0.30);
		m.rough = 0.78;
		m.bump_freq = 34.0;
		m.bump_amp = 0.040;
		m.dust = 0.6;
		break;
	}
	case MID_BINDER_A: {
		float t = room_fbm(w.x * 22.0, w.y * 22.0);
		float cell = spow(room_noise(w.x * 48.0, w.y * 48.0), 2.4);
		m.albedo = mix(vec3(0.176, 0.048, 0.042), vec3(0.286, 0.098, 0.074), t);
		m.albedo *= 0.90 + cell * 0.20;
		float head = smoothstepf(RM_BN_FOOT + 3.6, RM_BN_FOOT + 7.4, w.y);
		float fade = clampf(head * (0.24 + room_fbm2(w.x * 1.9 + 3.0, w.y * 0.8) * 1.05), 0.0, 1.0);
		m.albedo = mix(m.albedo, vec3(0.208, 0.132, 0.112), fade * 0.30);
		float sc = scratch_at(w.y, w.x, 0.50, 5.0, 0.011, 4.0) * 0.85
			+ scratch_at(w.x, w.y, 0.28, 4.0, 0.008, 9.0) * 0.60;
		m.albedo = mix(m.albedo, m.albedo * 1.80, clampf(sc, 0.0, 1.0) * 0.32);
		m.rough = 0.50 + t * 0.22 + fade * 0.16;
		m.coat = 0.26 * (1.0 - fade * 0.65);
		m.coat_rough = 0.30 + fade * 0.22;
		m.bump_freq = 56.0;
		m.bump_amp = 0.085;
		m.dust = 1.0;
		m.edge = 2.6;
		break;
	}
	case MID_BINDER_B: {
		float t = room_fbm(w.x * 26.0, w.y * 26.0);
		float weave = 0.5 + 0.5 * ssin(w.x * 120.0) * ssin(w.y * 120.0);
		m.albedo = mix(vec3(0.044, 0.082, 0.056), vec3(0.086, 0.140, 0.092), t);
		m.albedo *= 0.92 + weave * 0.14;
		float head = smoothstepf(RM_BN_FOOT + 3.6, RM_BN_FOOT + 7.4, w.y);
		float fade = clampf(head * (0.28 + room_fbm2(w.x * 1.7 + 8.0, w.y * 0.8) * 1.0), 0.0, 1.0);
		m.albedo = mix(m.albedo, vec3(0.108, 0.118, 0.098), fade * 0.34);
		float sc = scratch_at(w.y, w.x, 0.46, 5.0, 0.010, 14.0) * 0.8;
		m.albedo = mix(m.albedo, m.albedo * 1.55, clampf(sc, 0.0, 1.0) * 0.30);
		m.rough = 0.82 + fade * 0.10;
		m.bump_freq = 78.0;
		m.bump_amp = 0.070;
		m.dust = 1.0;
		m.edge = 2.4;
		break;
	}
	case MID_BOOK_LEATHER: {
		float t = room_fbm(w.x * 30.0, w.y * 30.0);
		float peb = spow(room_noise(w.x * 70.0, w.y * 70.0), 1.8);
		m.albedo = mix(vec3(0.030, 0.042, 0.086), vec3(0.058, 0.078, 0.150), t);
		m.albedo *= 0.86 + peb * 0.26;
		float head = smoothstepf(RM_BN_FOOT + 5.0, RM_BN_FOOT + 9.0, w.y);
		float fade = clampf(head * (0.20 + room_fbm2(w.x * 2.3 + 5.0, w.y * 0.9) * 0.9), 0.0, 1.0);
		m.albedo = mix(m.albedo, vec3(0.090, 0.098, 0.130), fade * 0.30);
		float sc = scratch_at(w.y, w.x, 0.40, 5.0, 0.009, 21.0) * 0.7;
		m.albedo = mix(m.albedo, m.albedo * 1.70, clampf(sc, 0.0, 1.0) * 0.28);
		m.rough = 0.44 + t * 0.18 + fade * 0.12;
		m.coat = 0.34 * (1.0 - fade * 0.5);
		m.coat_rough = 0.26 + fade * 0.20;
		m.bump_freq = 90.0;
		m.bump_amp = 0.070;
		m.dust = 1.0;
		m.edge = 2.2;
		break;
	}
	case MID_BOOK_GILT: {
		float leaf = 0.5 + 0.5 * ssin(w.x * 260.0);
		float tarn = clampf(room_fbm2(w.x * 6.0 + 2.0, w.z * 6.0) * 1.3 - 0.30, 0.0, 1.0);
		m.albedo = mix(vec3(0.900, 0.700, 0.300), vec3(0.560, 0.420, 0.190), tarn * 0.45);
		m.albedo *= 0.88 + leaf * 0.16;
		m.rough = 0.22 + tarn * 0.20 + leaf * 0.05;
		m.metal = 1.0;
		m.dust = 0.6;
		break;
	}
	case MID_BINDER_LABEL: {
		float fib = room_fbm(w.x * 44.0, w.y * 44.0);
		float age = clampf(room_fbm2(w.x * 2.1 + 17.0, w.y * 2.1) * 1.5 - 0.22, 0.0, 1.0);
		float fox = nick_at(w.x, w.y, 9.0, 11.0);
		m.albedo = vec3(0.762, 0.716, 0.612) * (0.94 + fib * 0.12);
		m.albedo = mix(m.albedo, vec3(0.560, 0.484, 0.348), age * 0.42);
		m.albedo = mix(m.albedo, vec3(0.402, 0.300, 0.196), fox * 0.55);
		m.rough = 0.74 + age * 0.12;
		m.bump_freq = 100.0;
		m.bump_amp = 0.032;
		m.dust = 1.0;
		break;
	}
	case MID_CC_STEEL:
	case MID_CC_PANEL: {
		float t = room_fbm(w.x * 30.0, (w.y + w.z) * 30.0);
		float crin = spow(room_noise(w.x * 90.0, (w.y - w.z) * 90.0), 1.6);
		m.albedo = mix(vec3(0.026, 0.027, 0.030), vec3(0.062, 0.063, 0.069), t);
		m.albedo *= 0.88 + crin * 0.26;
		m.rough = (id == MID_CC_PANEL) ? 0.78 : 0.68;
		m.coat = 0.12;
		m.coat_rough = 0.40;
		m.bump_freq = 170.0;
		m.bump_amp = 0.090;
		m.dust = 0.65;
		break;
	}
	case MID_CC_BRASS: {
		float brush = room_noise(w.x * 130.0, (w.y + w.z) * 22.0);
		float tarn = clampf(room_fbm2(w.x * 4.2 + 9.0, (w.y + w.z) * 4.2) * 1.45 - 0.22, 0.0, 1.0);
		float lac = smoothstepf(1.4, 4.6, w.y);
		tarn = clampf(tarn * (0.55 + lac * 0.70), 0.0, 1.0);
		m.albedo = mix(vec3(0.800, 0.620, 0.266), vec3(0.352, 0.286, 0.150), tarn * 0.62);
		m.albedo *= 0.90 + brush * 0.18;
		float sc = scratch_at(w.x, w.y * 0.8 + w.z * 0.8, 0.22, 6.0, 0.0045, 5.0);
		m.albedo = mix(m.albedo, vec3(0.916, 0.812, 0.570), sc * 0.30);
		m.rough = clampf(0.245 + brush * 0.13 + tarn * 0.30 - sc * 0.06, 0.05, 1.0);
		m.metal = 1.0;
		m.dust = 0.35;
		break;
	}
	case MID_CC_BAKELITE: {
		float t = room_fbm(w.x * 44.0, (w.y + w.z) * 44.0);
		m.albedo = mix(vec3(0.019, 0.015, 0.013), vec3(0.047, 0.034, 0.026), t * 0.9);
		m.rough = 0.74 + t * 0.10;
		m.coat = 0.09;
		m.coat_rough = 0.38;
		m.bump_freq = 190.0;
		m.bump_amp = 0.030;
		m.dust = 0.7;
		break;
	}
	case MID_CC_GLASS:
		m.albedo = vec3(0.018, 0.019, 0.021);
		m.rough = 0.028;
		m.coat = 1.0;
		m.coat_rough = 0.022;
		m.dust = 0.30;
		break;
	case MID_CC_CHEEK:
		wood = vec4(w.z, w.y, 0.0, 0.75);
		break;
	case MID_POST_RED: {
		float t = room_fbm(w.x * 6.0, (w.y + w.z) * 6.0);
		float orange = room_fbm2(w.x * 1.3 + 12.0, w.y * 1.3);
		m.albedo = mix(vec3(0.232, 0.024, 0.016), vec3(0.318, 0.048, 0.026), t * 0.85);
		m.albedo = mix(m.albedo, vec3(0.186, 0.038, 0.024), orange * 0.32);
		float chip = nick_at(w.x, w.y + w.z, 2.6, 41.0);
		m.albedo = mix(m.albedo, vec3(0.088, 0.064, 0.052), chip * 0.85);
		float sc = scratch_at(w.y, w.x, 0.34, 6.0, 0.007, 27.0);
		m.albedo = mix(m.albedo, m.albedo * 1.70, clampf(sc, 0.0, 1.0) * 0.30);
		m.rough = 0.34 + t * 0.14 + chip * 0.42;
		m.coat = 0.46 * (1.0 - chip * 0.9);
		m.coat_rough = 0.12 + t * 0.09 + chip * 0.30;
		m.bump_freq = 34.0;
		m.bump_amp = 0.055;
		m.dust = 1.0;
		m.edge = 2.4;
		break;
	}
	case MID_POST_DARK: {
		float t = room_fbm(w.x * 14.0, (w.y + w.z) * 14.0);
		m.albedo = mix(vec3(0.036, 0.026, 0.022), vec3(0.074, 0.054, 0.044), t);
		m.rough = 0.62 + t * 0.14;
		m.coat = 0.14;
		m.coat_rough = 0.34;
		m.bump_freq = 120.0;
		m.bump_amp = 0.040;
		m.dust = 1.0;
		m.edge = 2.0;
		break;
	}
	case MID_CC_LENS_AMBER:
	case MID_CC_LENS_GREEN:
	case MID_CC_LENS_BLUE:
	case MID_CC_LENS_RED: {
		vec3 tint = id == MID_CC_LENS_AMBER ? vec3(0.620, 0.310, 0.060)
			: id == MID_CC_LENS_GREEN ? vec3(0.110, 0.420, 0.160)
			: id == MID_CC_LENS_RED ? vec3(0.640, 0.098, 0.062)
			: vec3(0.110, 0.230, 0.560);
		m.albedo = tint * 0.34;
		m.rough = 0.075;
		m.coat = 1.0;
		m.coat_rough = 0.035;
		m.emit = tint * 0.10;
		break;
	}
	case MID_GADGET_PAPER: {
		float fib = room_fbm(w.x * 40.0, w.z * 40.0);
		float age = clampf(room_fbm2(w.x * 1.7 + 5.0, w.y * 1.7) * 1.5 - 0.25, 0.0, 1.0);
		float fox = nick_at(w.x, w.y, 7.0, 23.0);
		m.albedo = vec3(0.742, 0.706, 0.618) * (0.94 + fib * 0.12);
		m.albedo = mix(m.albedo, vec3(0.590, 0.520, 0.382), age * 0.38);
		m.albedo = mix(m.albedo, vec3(0.430, 0.330, 0.216), fox * 0.50);
		m.rough = 0.76;
		m.bump_freq = 90.0;
		m.bump_amp = 0.030;
		m.dust = 1.0;
		break;
	}
	default:
		break;
	}
	if (wood.w >= 0.0) {
		walnut_at(w, wood.x, wood.y, m, wood.z, wood.w);
		if (id == MID_DRAWER_WOOD) {
			m.coat = 0.0;
			m.albedo *= 0.78;
			m.rough = clampf(m.rough + 0.22, 0.2, 1.0);
		} else if (id == MID_DESK_TOP) {
			desk_stains(w, m);
		} else if (id == MID_DESK_BODY) {
			m.albedo *= 0.72;
			m.coat *= 0.55;
		} else if (id == MID_SHELF_CASE) {
			m.albedo *= 0.86;
			m.coat *= 0.66;
		} else if (id == MID_SHELF_BOARD) {
			m.albedo *= 0.80;
			m.coat *= 0.48;
		} else if (id == MID_CC_CHEEK) {
			m.albedo *= 0.92;
			m.coat *= 0.84;
		}
	}
	return m;
}

vec3 perturb_normal(vec3 n, vec3 p, float freq, float amp)
{
	if (amp <= 0.0 || freq <= 0.0) {
		return n;
	}
	float e = 0.35 / freq;
	float b0 = room_fbm(p.x * freq, p.y * freq + p.z * freq * 0.63);
	float bx = room_fbm((p.x + e) * freq, p.y * freq + p.z * freq * 0.63);
	float by = room_fbm(p.x * freq, (p.y + e) * freq + p.z * freq * 0.63);
	float bz = room_fbm(p.x * freq, p.y * freq + (p.z + e) * freq * 0.63);
	vec3 g = vec3((bx - b0) / e, (by - b0) / e, (bz - b0) / e);
	g = g - n * dot(g, n);
	float gl = length(g);
	if (gl < 1e-9) {
		return n;
	}
	return vnorm(n - g * (amp / max(gl, 1.0)));
}

/* ---- shading ----
 *
 * draw.c's shade_surface calls itself once, for the thing a smooth surface
 * reflects, and GLSL does not do that. It does not need to: the colour
 * comes out linear in the two reflections --
 *
 *   ((base + refl * w_refl) * cmul + crefl * w_crefl) + emit
 *
 * so a surface is evaluated up to the point where it needs them, the one
 * ray both would trace is traced once (they look along the same mirror
 * direction from the same point, so C traces the same ray twice and finds
 * the same thing), and what it hits is lit the same way one level down,
 * where a reflection is only ever the sky.
 *
 * And not in one program. A driver has to translate all of this, and some
 * will not take it whole: VMware's SVGA3D, handed the room in one piece,
 * runs a shader that draws nothing instead and says so to nobody. So the
 * work is cut where it can be carried across in a texture, and each piece
 * is a program of its own:
 *
 *   1  march, the normal, the curvature, who owns the hit
 *   2  what the surface is made of, bumped and worn
 *   3  what it can see: occlusion, the shadow, and the mirror ray's hit
 *   2  again, for whatever the mirror ray hit
 *   4  the light, and the two levels put together
 *
 * Everything carried is float32, the same numbers the one program would
 * have held in registers, so the cut changes nothing in the picture. */

/* pass 2: what a surface is made of at p -- its material, its normal
 * bumped, and the wear and dust its curvature gives it */
void surface_material(vec3 p, int id, vec3 n_geo, float curv, out mtl m, out vec3 n)
{
	m = icon_material(id, p);
	n = perturb_normal(n_geo, scene_local(p, id), m.bump_freq, m.bump_amp);
	if (m.dust > 0.0 || m.metal > 0.5) {
		float wear = clampf(curv * 1.4 * m.edge, 0.0, 1.0);
		if (m.metal > 0.5) {
			m.rough = max(0.045, m.rough - wear * 0.11);
			m.albedo = mix(m.albedo, vec3(0.95, 0.80, 0.42), wear * 0.45);
		} else {
			m.albedo = mix(m.albedo, m.albedo * 1.30, wear * 0.55);
			m.rough = clampf(m.rough - wear * 0.14, 0.05, 1.0);
		}
		float up = clampf(n.y, 0.0, 1.0);
		float settle = m.dust * up * up * clampf(-curv * 0.8 + 0.42, 0.0, 1.0);
		m.albedo = mix(m.albedo, vec3(0.42, 0.40, 0.37), settle * 0.16);
		m.rough = clampf(m.rough + settle * 0.22, 0.03, 1.0);
	}
}

struct surf {
	vec3 base;
	vec3 w_refl;
	float cmul;
	vec3 w_crefl;
	vec3 emit;
	vec3 r;          /* the mirror direction */
	float rough;
	float coat;
	float coat_rough;
	float sh;
	key_light kl;
};

/* pass 4: how that surface is lit, given what pass 3 found it could see */
surf surface_light(vec3 p, vec3 rd, mtl m, vec3 n, float ao, float sh)
{
	vec3 v = vnorm(-rd);
	key_light kl = key_at(p);
	vec3 l = kl.dir;
	float nol = max(dot(n, l), 0.0);
	float nov = max(dot(n, v), 1e-4);

	vec3 f0 = mix(vec3(0.04), m.albedo, m.metal);
	vec3 diff_albedo = m.albedo * (1.0 - m.metal);
	vec3 key = LIT_KEY_COL * kl.irr;

	vec3 col = diff_albedo * key * (nol * sh / M_PI);
	col += diff_albedo * sh_irradiance(n) * (ao / M_PI);
	col += diff_albedo * shelf_lamp_at(p, n) * (ao / M_PI);

	float ea, eb;
	env_brdf(nov, m.rough, ea, eb);
	vec3 dfg = f0 * ea + vec3(eb);
	float so = clampf(pow(nov + ao, exp2(-16.0 * m.rough - 1.0)) - 1.0 + ao, 0.0, 1.0);

	surf s;
	s.base = col;
	s.w_refl = dfg * so;
	if (m.metal > 0.05) {
		vec3 comp = f0 * ((vec3(1.0) - vec3(ea + eb)) * (1.0 / max(ea + eb, 1e-3)));
		s.w_refl += dfg * comp * (so * m.metal);
	}
	s.cmul = 1.0;
	s.w_crefl = vec3(0.0);
	if (m.coat > 0.0) {
		vec3 cf0 = vec3(0.04);
		float cft = spow(1.0 - nov, 5.0);
		float cfres = 0.04 + 0.96 * cft;
		s.cmul = 1.0 - cfres * m.coat;
		float ca, cb;
		env_brdf(nov, m.coat_rough, ca, cb);
		vec3 cdfg = cf0 * ca + vec3(cb);
		s.w_crefl = cdfg * (m.coat * so);
	}
	s.emit = m.emit;
	s.r = reflect3(-v, n);
	s.rough = m.rough;
	s.coat = m.coat;
	s.coat_rough = m.coat_rough;
	s.sh = sh;
	s.kl = kl;
	return s;
}

/* spec_ambient's sky: the prefiltered room, and the lamp through the lobe */
vec3 surf_sky(surf s, float rough)
{
	return env_prefiltered(s.r, rough) + env_lamp(s.r, rough, s.kl) * s.sh;
}

vec3 surf_combine(surf s, vec3 refl, vec3 crefl)
{
	return (s.base + refl * s.w_refl) * s.cmul + crefl * s.w_crefl + s.emit;
}

int room_owner(vec3 p, int id)
{
	if (u_layer == 4) {
		return console_owner(p);
	}
	if (u_layer == 6 || u_layer == 7) {
		return 0;
	}
	if (u_layer == 5) {
		return (id == MID_POST_RED || id == MID_POST_DARK) ? 1 : 0;
	}
	if (u_layer >= 3) {
		if (id == MID_BOOK_LEATHER || id == MID_BOOK_GILT) {
			return DESK_SHELF_COUNT + 1;
		}
		if (id != MID_BINDER_A && id != MID_BINDER_B && id != MID_BINDER_LABEL
			&& id != MID_FOLDER_PAPER) {
			return 0;
		}
		for (int i = 0; i < DESK_SHELF_COUNT; i++) {
			vec3 c, e;
			binder_bound(i, c, e);
			if (sd_box(p - c, e) < 0.0) {
				return i + 1;
			}
		}
		return 0;
	}
	if (id <= 0 || id > MID_GEAR) {
		return 0;
	}
	vec3 ip = p - vec3(0.0, RM_ITEM_Y, RM_ITEM_Z + room_pull());
	return drawer_item_at(ip) + 1;
}

/* ---- the passes ---- */

/* This fragment's sample, and the ray through it. */
vec3 pixel_ray()
{
	int x = int(gl_FragCoord.x) + u_origin.x;
	int y = int(gl_FragCoord.y) + u_origin.y;
	int hw = u_frame.x, hh = u_frame.y;
	vec2 off = SS_OFF[(y * hw + x) & 3];
	float u = (float(x) + off.x) / float(hw);
	float v = (float(y) + off.y) / float(hh);
	return vnorm(u_ww + (u_uu * ((u - 0.5) * u_fov.x) + u_vv * ((0.5 - v) * u_fov.y)));
}

/* A hit travels as a point and a number: the material id, plus 256 times
 * the owner where there is one. Both are small integers, exact in float. */
int hit_id(vec4 hit)
{
	return int(hit.w + 0.5) % 256;
}

int hit_owner(vec4 hit)
{
	return int(hit.w + 0.5) / 256;
}

#if ROOM_PASS == 1

layout(location = 0) out vec4 o_hit;   /* p, id + 256 owner */
layout(location = 1) out vec4 o_geo;   /* the normal, the curvature */

void main()
{
	vec3 ro = u_eye;
	vec3 rd = pixel_ray();
	float t = 0.02;
	int id = 0;
	vec3 hp = ro;
	for (int i = 0; i < 260 && t < 240.0; i++) {
		vec3 q = ro + rd * t;
		hit3 h = scene_map(q);
		float eps = 0.0018 * max(t * 0.06, 1.0);
		if (h.d < eps && h.id > 0) {
			if (!(u_layer == 2 && !room_is_drawer(h.id)) && !(u_layer == 6 && !cc_ext_at(q))) {
				id = h.id;
				hp = q;
			}
			break;
		}
		t += max(h.d, eps * 0.9);
	}
	if (id == 0) {
		o_hit = vec4(0.0);
		o_geo = vec4(0.0);
		return;
	}
	o_hit = vec4(hp, float(id + 256 * room_owner(hp, id)));
	o_geo = vec4(scene_normal(hp), scene_curvature(hp));
}

#elif ROOM_PASS == 2

uniform highp sampler2D u_in0;   /* the hit */
uniform highp sampler2D u_in1;   /* its normal and curvature */

layout(location = 0) out vec4 o_m0;   /* albedo, rough */
layout(location = 1) out vec4 o_m1;   /* emit, metal */
layout(location = 2) out vec4 o_m2;   /* the bumped normal, coat */
layout(location = 3) out vec4 o_m3;   /* coat_rough */

void main()
{
	ivec2 at = ivec2(gl_FragCoord.xy);
	vec4 hit = texelFetch(u_in0, at, 0);
	int id = hit_id(hit);
	if (id == 0) {
		o_m0 = vec4(0.0);
		o_m1 = vec4(0.0);
		o_m2 = vec4(0.0);
		o_m3 = vec4(0.0);
		return;
	}
	vec4 geo = texelFetch(u_in1, at, 0);
	mtl m;
	vec3 n;
	surface_material(hit.xyz, id, geo.xyz, geo.w, m, n);
	o_m0 = vec4(m.albedo, m.rough);
	o_m1 = vec4(m.emit, m.metal);
	o_m2 = vec4(n, m.coat);
	o_m3 = vec4(m.coat_rough, 0.0, 0.0, 0.0);
}

#elif ROOM_PASS == 3

uniform highp sampler2D u_in0;   /* the hit */
uniform highp sampler2D u_in1;   /* its normal and curvature */
uniform highp sampler2D u_in2;   /* albedo, rough */
uniform highp sampler2D u_in3;   /* the bumped normal, coat */
uniform highp sampler2D u_in4;   /* coat_rough */

layout(location = 0) out vec4 o_occ;    /* ao, shadow */
layout(location = 1) out vec4 o_hit;    /* the mirror ray's hit: p, id */
layout(location = 2) out vec4 o_geo;    /* its normal and curvature */

void main()
{
	ivec2 at = ivec2(gl_FragCoord.xy);
	vec4 hit = texelFetch(u_in0, at, 0);
	int id = hit_id(hit);
	o_occ = vec4(0.0);
	o_hit = vec4(0.0);
	o_geo = vec4(0.0);
	if (id == 0) {
		return;
	}
	vec3 p = hit.xyz;
	vec3 n_geo = texelFetch(u_in1, at, 0).xyz;
	float rough = texelFetch(u_in2, at, 0).w;
	vec4 nc = texelFetch(u_in3, at, 0);
	vec3 n = nc.xyz;
	float coat = nc.w;
	float coat_rough = texelFetch(u_in4, at, 0).x;

	key_light kl = key_at(p);
	float nol = max(dot(n, kl.dir), 0.0);
	float ao = scene_ao(p, n);
	float sh = 1.0;
	if (nol > 0.0) {
		sh = scene_shadow(p + n_geo * 0.040, kl.dir, kl.ang);
	}
	o_occ = vec4(ao, sh, 0.0, 0.0);

	/* the mirror ray, as spec_ambient marches it, when either lobe is
	 * smooth enough to show what it hits */
	if (!(rough <= 0.38 || (coat > 0.0 && coat_rough <= 0.38))) {
		return;
	}
	vec3 v = vnorm(-pixel_ray());
	vec3 r = reflect3(-v, n);
	vec3 ro = p + n * 0.004;
	float t = 0.02;
	for (int i = 0; i < 40 && t < 30.0; i++) {
		hit3 hh = scene_map(ro + r * t);
		if (hh.d < 0.004) {
			if (hh.id > 0) {
				vec3 q = ro + r * t;
				o_hit = vec4(q, float(hh.id));
				o_geo = vec4(scene_normal(q), scene_curvature(q));
			}
			break;
		}
		t += max(hh.d, 0.004);
	}
}

#elif ROOM_PASS == 4

uniform highp sampler2D u_in0;    /* the hit */
uniform highp sampler2D u_in1;    /* its material: albedo, rough */
uniform highp sampler2D u_in2;    /*   emit, metal */
uniform highp sampler2D u_in3;    /*   the bumped normal, coat */
uniform highp sampler2D u_in4;    /*   coat_rough */
uniform highp sampler2D u_in5;    /* ao, shadow */
uniform highp sampler2D u_in6;    /* the mirror ray's hit */
uniform highp sampler2D u_in7;    /* its material, likewise */
uniform highp sampler2D u_in8;
uniform highp sampler2D u_in9;
uniform highp sampler2D u_in10;

layout(location = 0) out vec4 o_col;   /* linear RGB, coverage + 2 owner */

mtl fetch_mtl(vec4 a, vec4 b, vec4 c, vec4 d)
{
	mtl m;
	m.albedo = a.xyz;
	m.rough = a.w;
	m.emit = b.xyz;
	m.metal = b.w;
	m.coat = c.w;
	m.coat_rough = d.x;
	m.bump_freq = 0.0;
	m.bump_amp = 0.0;
	m.dust = 0.0;
	m.edge = 1.0;
	return m;
}

void main()
{
	ivec2 at = ivec2(gl_FragCoord.xy);
	vec4 hit = texelFetch(u_in0, at, 0);
	int id = hit_id(hit);
	if (id == 0) {
		o_col = vec4(0.0);
		return;
	}
	vec4 m2 = texelFetch(u_in3, at, 0);
	mtl m = fetch_mtl(texelFetch(u_in1, at, 0), texelFetch(u_in2, at, 0), m2,
		texelFetch(u_in4, at, 0));
	vec4 occ = texelFetch(u_in5, at, 0);
	surf s0 = surface_light(hit.xyz, pixel_ray(), m, m2.xyz, occ.x, occ.y);

	/* one level down, where a reflection is only ever the sky */
	vec4 hit1 = texelFetch(u_in6, at, 0);
	int id1 = hit_id(hit1);
	vec3 c1 = vec3(0.0);
	if (id1 > 0) {
		vec4 n2 = texelFetch(u_in9, at, 0);
		mtl m1 = fetch_mtl(texelFetch(u_in7, at, 0), texelFetch(u_in8, at, 0), n2,
			texelFetch(u_in10, at, 0));
		surf s1 = surface_light(hit1.xyz, s0.r, m1, n2.xyz, 0.85, 1.0);
		c1 = surf_combine(s1, surf_sky(s1, s1.rough),
			s1.coat > 0.0 ? surf_sky(s1, s1.coat_rough) : vec3(0.0));
	}

	vec3 sky = surf_sky(s0, s0.rough);
	vec3 refl = (id1 > 0 && s0.rough <= 0.38)
		? mix(c1, sky, clampf(s0.rough * 2.4, 0.0, 1.0)) : sky;
	vec3 crefl = vec3(0.0);
	if (s0.coat > 0.0) {
		vec3 csky = surf_sky(s0, s0.coat_rough);
		crefl = (id1 > 0 && s0.coat_rough <= 0.38)
			? mix(c1, csky, clampf(s0.coat_rough * 2.4, 0.0, 1.0)) : csky;
	}
	o_col = vec4(surf_combine(s0, refl, crefl), 1.0 + 2.0 * float(hit_owner(hit)));
}

#endif
