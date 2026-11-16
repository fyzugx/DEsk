// lever.dsp -- (2026-11-14): a bat lever thrown (WIRELESS, BLUETOOTH). The finger pushes it
// over its spring -- the bat's ball scraping in its bushing -- it snaps over centre, the chromed bat
// rings, the steel face and the case take the knock, and it bounces once on its stop. dir: 1 on (up),
// 0 off (down, a little lower).
import("consolesfx.lib");
dir = hslider("dir", 1, 0, 1, 1);
jf = hslider("jf", 1, 0.8, 1.2, 0.001);
hard = hslider("hard", 1, 0.3, 2, 0.01);
gp = hslider("g_push", 1, 0, 4, 0.01);
gs = hslider("g_snap", 1, 0, 4, 0.01);
gb = hslider("g_body", 1, 0, 4, 0.01);
t = button("throw") : ba.impulsify;
at(s, x) = x : @(ba.sec2samp(s));
fs = jf * select2(dir > 0.5, 0.94, 1.0);

push = n(1) : fi.highpass(2, 2800.0) : fi.lowpass(2, 9000.0) : *(t : ar(0.002, 0.007)) : *(0.25);
over = at(0.008, t);
sn = snap(over) * 1.3 + (over : bat(fs)) * 1.0;
knock = over <: panel(jf) * 0.8, lidwood(jf * 1.1) * 0.25 :> _;
bounce = (at(0.0145, t) : bat(fs)) * 0.18 + snap(at(0.0145, t)) * 0.2;

dry = (push * 0.86 * gp + (sn + bounce) * gs + knock * 22.6 * gb) * hard;
p = hslider("pan", 0, -1, 1, 0.01);
process = dry : pan(p) : wet(0.10);
