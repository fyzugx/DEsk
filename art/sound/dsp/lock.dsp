// lock.dsp -- (2026-11-14): the POWER key. "in": steel going past the pins, each one ticking
// over a cut, and the key seating; "turn" (1.45 s later in the desk): the cylinder going round, the cam
// catching, the bolt thrown -- a short heavy clack through the steel face. "back": the bolt drawn back;
// "out": the key drawn out over the pins, quicker.
import("consolesfx.lib");
jf = hslider("jf", 1, 0.8, 1.2, 0.001);
gi = hslider("g_in", 1, 0, 4, 0.01);
gt = hslider("g_turn", 1, 0, 4, 0.01);
i = button("in") : ba.impulsify;
o = button("out") : ba.impulsify;
tu = button("turn") : ba.impulsify;
bk = button("back") : ba.impulsify;
at(s, x) = x : @(ba.sec2samp(s));
pins(x, gap) = sum(k, 5, at(0.03 + k * gap * (1.0 + 0.13 * ((k * 7) % 3 - 1)), x) * (1.0 - 0.1 * k));
ticks(x, gap) = (pins(x, gap) <: brass(jf * 1.25) * 0.8, panel(jf * 1.3) * 0.15 :> _)
	+ (n(1) : fi.highpass(2, 4500.0) : *(pins(x, gap) : ar(0.0003, 0.0015)) : *(0.3));
slide(x, dur) = n(2) : fi.bandpass(1, 3800.0, 8500.0) : *(x : ar(0.03, dur)) : *(0.06);
seat(x) = (at(0.26, x) <: brass(jf) * 0.5, panel(jf) * 0.5 :> _);
keyin = ticks(i, 0.045) + slide(i, 0.12) + seat(i);
keyout = ticks(o, 0.03) + slide(o, 0.08);
cyl(x) = n(3) : fi.bandpass(1, 2500.0, 6000.0) : *(x : ar(0.01, 0.03)) : *(0.08);
cam(x) = snap(at(0.025, x)) * 0.6;
bolt(x, k) = (at(0.032, x) <: bank((600, 1450, 3100), (0.05, 0.03, 0.015), (1.0, 0.6, 0.3), jf * k, 1.0) * 0.004,
	panel(jf) * 1.2, lidwood(jf) * 0.3 :> _) + (n(4) : fi.lowpass(2, 700.0) : *(at(0.032, x) : ar(0.0015, 0.02)) : *(0.5));
turn = cyl(tu) + cam(tu) + bolt(tu, 1.0);
back = cyl(bk) + cam(bk) * 0.7 + bolt(bk, 1.12) * 0.7;
dry = (keyin + keyout) * gi + (turn + back) * 1.53 * gt;
p = hslider("pan", -0.55, -1, 1, 0.01);
process = dry : pan(p) : wet(0.10);
