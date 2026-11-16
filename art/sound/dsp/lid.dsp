// lid.dsp -- (2026-11-14): the walnut lid over the power section, on its brass hinge. "lift":
// the knuckle turning -- a short dry creak, lower than a door's -- and the air the board moves; "stop"
// (0.42 s later): walnut meeting its stop or the plate, a dull knock low in the wood, the brass once.
// dir 1 up, 0 down (a little softer, the creak a little higher).
import("consolesfx.lib");
dir = hslider("dir", 1, 0, 1, 1);
jf = hslider("jf", 1, 0.8, 1.2, 0.001);
gc = hslider("g_creak", 1, 0, 4, 0.01);
gk = hslider("g_knock", 1, 0, 4, 0.01);
l = button("lift") : ba.impulsify;
s = button("stop") : ba.impulsify;
at(t, x) = x : @(ba.sec2samp(t));
rate = (210.0 + 60.0 * (l : step : glide(0.08))) * select2(dir > 0.5, 1.12, 1.0);
going = (l : ar(0.02, 0.09));
catches = no.sparse_noise(max(rate, 1.0)) : abs : *(going > 0.05) : *(going);
creak = catches : bank((780, 1900, 3100), (0.012, 0.008, 0.005), (1.0, 0.6, 0.35), jf, 1.0) : *(0.012);
woodcreak = catches : lidwood(jf) : *(1.4);
airm = air(5, 380.0 + 500.0 * (l : step : glide(0.12)), 0.8) * (l : ar(0.06, 0.12)) * 0.12;
knock = (s <: lidwood(jf * 0.95) * 2.0, panel(jf) * 0.5 :> _) + (n(4) : fi.lowpass(2, 600.0) : *(s : ar(0.001, 0.012)) : *(0.6))
	+ (at(0.008, s) : brass(jf)) * 0.3;
dry = (creak + woodcreak + airm) * gc + knock * gk * select2(dir > 0.5, 0.85, 1.0);
p = hslider("pan", -0.55, -1, 1, 0.01);
process = dry : pan(p) : wet(0.11);
