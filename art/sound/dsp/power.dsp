// power.dsp -- (2026-11-14): SHUT DOWN (or RESTART, LOG OUT) pushed, and the set going out. The
// big button all the way down; the relay behind the panel dropping out a moment later; then 2.8 s of
// the set dying: the hum of the transformer sagging and fading, the scope's deflection whine falling
// through the octaves to nothing, the charge on the glass crackling away, the needles falling back
// against their pegs, and the glass ticking as it cools.
import("consolesfx.lib");
jf = hslider("jf", 1, 0.8, 1.2, 0.001);
gb = hslider("g_button", 1, 0, 4, 0.01);
gd = hslider("g_die", 1, 0, 4, 0.01);
p_ = button("press") : ba.impulsify;
at(t, x) = x : @(ba.sec2samp(t));
t0 = p_ : step;
el = t0 : (+ ~ _) : /(ma.SR);   // (seconds since the press)
button_ = (p_ <: bank((1400, 2600), (0.01, 0.006), (1.0, 0.5), jf, 1.0) * 0.004, lidwood(jf) * 1.0, panel(jf) * 0.6 :> _)
	+ snap(at(0.012, p_)) * 0.8 + (n(1) : fi.lowpass(2, 800.0) : *(p_ : ar(0.001, 0.015)) : *(0.6));
relay = (at(0.075, p_) <: bank((2500, 4300, 7000), (0.012, 0.008, 0.005), (1.0, 0.6, 0.3), jf, 1.0) * 0.004,
	panel(jf) * 0.6 :> _) + snap(at(0.075, p_)) * 0.6;
sag = 100.0 * (1.0 - 0.05 * (1.0 - exp(0.0 - el / 1.2)));
hum = humtone(sag) * exp(0.0 - el / 0.9) * t0 * 0.9;
whine = os.osc(70.0 + 1150.0 * exp(0.0 - el / 0.55)) * exp(0.0 - el / 0.75) * t0 * 0.08;
charge = crackle(5, (p_ : ar(0.01, 0.12)) * 4500.0, 2500.0, 12000.0) * 0.25;
pegs = (at(0.55, p_) + at(0.68, p_) * 0.7) : bank((5200, 7600), (0.006, 0.004), (1.0, 0.5), jf, 1.0) : *(0.004);
cool = no.sparse_noise(3.0) : *(el > 0.9) : abs : bank((4800, 6900), (0.004, 0.003), (1.0, 0.4), jf, 1.0) : *(0.004);
dry = (button_ + relay) * gb + (hum + whine + charge + pegs + cool) * 0.1035 * gd;
p = hslider("pan", -0.55, -1, 1, 0.01);
process = dry : pan(p) : wet(0.11);
