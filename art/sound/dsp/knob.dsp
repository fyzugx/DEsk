// knob.dsp -- (2026-11-14): the VOLUME knob (and LAMP, SCREEN) turned: a ball dropping into
// each detent -- a small hard click, the Bakelite knob and the steel face under it -- and between them
// the grease of the shaft, heard only as fast as it is turned. "detent" each step; speed 0..1.
import("consolesfx.lib");
jf = hslider("jf", 1, 0.8, 1.2, 0.001);
v = hslider("speed", 0, 0, 2, 0.001) : glide(0.03);
gc = hslider("g_click", 1, 0, 4, 0.01);
gb = hslider("g_body", 1, 0, 4, 0.01);
gf = hslider("g_friction", 1, 0, 4, 0.01);
d = button("detent") : ba.impulsify;
click = (d : bank((3600, 6100, 8800), (0.004, 0.0025, 0.0018), (1.0, 0.6, 0.35), jf, 1.0)) * 0.004
	+ (n(1) : fi.highpass(2, 4000.0) : *(d : ar(0.0002, 0.0008)) : *(0.35));
bakelite = (d : bank((880, 1520, 2650), (0.010, 0.007, 0.005), (1.0, 0.6, 0.35), jf, 1.0)) * 0.004;
body = (d : panel(jf)) * 0.25;
fric = n(2) : fi.bandpass(1, 900.0, 3500.0) : *(pow(min(v, 1.5), 1.2) * 0.04);
dry = click * gc + (bakelite + body) * 10.1 * gb + fric * gf;
p = hslider("pan", 0, -1, 1, 0.01);
process = dry : pan(p) : wet(0.08);
