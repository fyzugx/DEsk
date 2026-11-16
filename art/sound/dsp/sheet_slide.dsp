// sheet_slide.dsp -- (2026-11-14): a sheet drawn across the desk under the hand -- heard for as
// long as it moves, louder and brighter the faster it goes; the grain going by under it in how hard it
// catches. speed: points a second. mat: 0 the wood, 1 leather (soft, dark, hardly a catch), 2 the clear
// mat (smooth: a thin high hiss).
import("desksfx.lib");
mat = int(hslider("mat", 0, 0, 2, 1));
v = hslider("speed", 0, 0, 4000, 1) : glide(0.025);
p = hslider("pan", 0, -1, 1, 0.01) : glide(0.03);
gh = hslider("g_hiss", 1, 0, 4, 0.01);
gr_ = hslider("g_rub", 1, 0, 4, 0.01);
gc = hslider("g_crackle", 1, 0, 4, 0.01);
gb = hslider("g_body", 1, 0, 4, 0.01);

s = v / 1500.0;
amp = pow(max(s, 0.0), 0.75);
bright = 1300.0 + 5200.0 * pow(min(s, 2.0), 0.6);
gcut = 6.0 + 60.0 * s;
grain = n(6) : fi.lowpass(2, gcut) : fi.lowpass(2, gcut) : abs : *(7.0) : min(1.6);
gr = select3(mat, 0.6, 0.25, 0.12);
rough = 1.0 - gr + gr * grain;
hiss = n(0) : fi.highpass(2, select3(mat, 900.0, 650.0, 1900.0)) : fi.lowpass(2, bright * select3(mat, 1.0, 0.5, 1.2));
rub = n(1) : fi.bandpass(1, select3(mat, 220.0, 150.0, 420.0), select3(mat, 720.0, 520.0, 1300.0));
ck = crackle(2, 300.0 * s * select3(mat, 1.0, 0.15, 0.4), 2500.0, 8500.0);
body = rub * amp : desk(1.0, 0.6);
lvl = select3(mat, 1.0, 0.55, 0.4);

dry = ((hiss * 0.3 * gh + rub * select3(mat, 0.3, 0.4, 0.2) * gr_) * rough * amp + body * 0.289 * gb * select3(mat, 1.0, 0.35, 0.5)) * lvl
	+ ck * 0.127 * select3(mat, 1.0, 0.4, 0.6) * gc;
process = dry : pan(p) : wet(0.10);
