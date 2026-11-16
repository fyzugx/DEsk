// cap.dsp -- (2026-11-14): one of the lit glass caps pushed (the four on the lid; LOCK and
// SLEEP smaller). "down": the finger on the glass, the plunger going down its bore, the snap switch going
// over and the cap bottoming in its bezel, the walnut and the steel under it taking the knock. "up" (the
// desk lets go of it 0.09 s later): the spring sending it back, the switch resetting, the cap's shoulder
// against the bezel. kind: 0 a plain push, 1 latching in (the light comes on: it stays a little down,
// caught), 2 let go of the latch (it pops right up).
import("consolesfx.lib");
kind = int(hslider("kind", 0, 0, 2, 1));
sz = hslider("size", 1, 0.6, 1.2, 0.01);
jf = hslider("jf", 1, 0.8, 1.2, 0.001);
hard = hslider("hard", 1, 0.3, 2, 0.01);
gg = hslider("g_glass", 1, 0, 4, 0.01);
gs = hslider("g_switch", 1, 0, 4, 0.01);
gb = hslider("g_body", 1, 0, 4, 0.01);
gu = hslider("g_up", 1, 0, 4, 0.01);
d = button("down") : ba.impulsify;
u = button("up") : ba.impulsify;
at(s, x) = x : @(ba.sec2samp(s));
fs = jf / sz;

touch = (d : glass(fs)) * 15.8;   // (the glass bank comes out 60 dB under the wood: brought to 12 dB under the switch)
travel = n(1) : fi.bandpass(1, 1100.0, 3200.0) : *(d : ar(0.003, 0.012)) : *(0.18);
click = snap(at(0.016, d)) * 1.0;
bottom = at(0.032, d) <: lidwood(jf) * 1.25, panel(jf) * 0.45, brass(fs) * 0.3 :> _;
thock = n(2) : fi.lowpass(2, 900.0) : *(at(0.032, d) : ar(0.0015, 0.012)) : *(0.5);
latchin = (snap(at(0.044, d)) * 0.5 + (at(0.044, d) : brass(fs * 0.9)) * 0.4) * (kind == 1);

upk = select3(kind, 1.0, 0.35, 1.25);
reset = snap(u) * 0.55 * upk;
shoulder = (at(0.018, u) <: brass(fs) * 0.7, glass(fs) * 0.15, panel(jf) * 0.12 :> _) * upk;
release = (snap(at(-0.0, u)) * 0.0 + (n(3) : fi.highpass(2, 2500.0) : *(u : ar(0.0005, 0.003)) : *(0.3))) * (kind == 2);

dry = (touch * gg + (travel + click + latchin) * gs + (bottom + thock) * gb) * hard
	+ (reset + shoulder + release) * gu * hard;
p = hslider("pan", 0, -1, 1, 0.01);
process = dry : pan(p) : wet(0.10);
