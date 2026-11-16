// aerial.dsp -- (2026-11-14): the wireless aerial's little geared motor, up or down: the
// brushes' buzz, the gears' rattle, the nylon cable over its pulley; easing in as it gets going and out
// as it runs down, pitched up as the load comes off (up) or down; then the cable snatching at the stop.
import("consolesfx.lib");
dir = hslider("dir", 1, 0, 1, 1);
jf = hslider("jf", 1, 0.8, 1.2, 0.001);
gm = hslider("g_motor", 1, 0, 4, 0.01);
gs = hslider("g_stop", 1, 0, 4, 0.01);
g = button("go") : ba.impulsify;
s = button("stop") : ba.impulsify;
running = (g : step) * (1.0 - (s : step));
env = running : glide(0.05);
el = (g : step) : (+ ~ _) : /(ma.SR);   // (seconds since it set off)
f0 = select2(dir > 0.5, 214.0, 178.0) * jf;
f = f0 * (1.0 + select2(dir > 0.5, -0.14, 0.2) * min(el / 1.55, 1.0));
buzz = sum(k, 7, os.osc(f * (k + 1)) / ((k + 1) * 0.9)) * 0.18;
rattle = 0.55 + 0.45 * pow(0.5 + 0.5 * os.osc(f * 0.21), 3.0);
gear = no.sparse_noise(max(f * 0.5, 1.0)) : abs : bank((2900, 4400), (0.003, 0.002), (1.0, 0.5), jf, 1.0) : *(0.003);
cable = n(2) : fi.bandpass(1, 1200.0, 3500.0) : *(0.05);
motor = (buzz * rattle + gear + cable) * env : fi.highpass(1, 80.0);
stop = (s <: bank((1800, 3300, 5200), (0.012, 0.007, 0.004), (1.0, 0.55, 0.25), jf, 1.0) * 0.004, panel(jf) * 0.5 :> _)
	+ snap(s) * 0.4;
dry = (motor * gm + stop * 3.4 * gs) * 0.24;   // (alone it came out 12 dB over today's aerial, and buried the cap and the levers under it)
p = hslider("pan", 0.55, -1, 1, 0.01);
process = dry : pan(p) : wet(0.08);
