// sheet_down.dsp -- (2026-11-14): a sheet put down flat on the desk. "air" as it starts down (the
// air under it, squeezed out quicker as it falls), "land" as it meets the desk: the puff from under it,
// the fibres at its edges, the desk struck softly through it. mat: 0 the wood, 1 a leather mat, 2 the
// clear mat (a thin hard sheet of its own, and the paper's edge slapping it).
import("desksfx.lib");
mat = int(hslider("mat", 0, 0, 2, 1));
size = hslider("size", 1, 0.5, 1.5, 0.01);
hard = hslider("hard", 1, 0.3, 2, 0.01);
jf = hslider("jf", 1, 0.8, 1.2, 0.001);
p = hslider("pan", 0, -1, 1, 0.01);
gw = hslider("g_whoosh", 1, 0, 4, 0.01);
gf = hslider("g_fwap", 1, 0, 4, 0.01);
gt = hslider("g_thump", 1, 0, 4, 0.01);
gc = hslider("g_crackle", 1, 0, 4, 0.01);
gb = hslider("g_body", 1, 0, 4, 0.01);
gm = hslider("g_mat", 1, 0, 4, 0.01);
leather = mat == 1;
clear = mat == 2;

a = button("air") : ba.impulsify;
l = button("land") : ba.impulsify;

aup = a : step : glide(0.03);
gone = l : step : glide(0.003);
whoosh = air(0, 420.0 * pow(1600.0 / 420.0, aup) / size, 1.3) * aup * aup * (1.0 - gone) * 0.234;

fwlp = select3(mat, 1150.0, 620.0, 1700.0) / sqrt(size);
fwap = n(1) : fi.lowpass(2, fwlp) : fi.highpass(1, 140.0) : *(l : ar(0.0035, 0.026 * size)) : *(hard);
thump = n(2) : fi.lowpass(2, 240.0) : *(l : ar(0.003, 0.016)) : *(hard * 3.3);
ck = crackle(3, (l : ar(0.002, 0.03)) * 2600.0 * hard * select3(mat, 1.0, 0.3, 1.25), 2300.0, 7800.0);
bodyin = (fwap * 0.2 + thump * 0.3) * select3(mat, 1.0, 0.4, 0.75) : fi.lowpass(1, select3(mat, 900.0, 350.0, 1400.0));
body = bodyin : desk(jf, 1.0);

tickF = (1880, 2710, 3930, 5260);
tickT = (0.05, 0.035, 0.025, 0.018);
tickG = (1.0, 0.7, 0.5, 0.3);
tick = l <: par(i, 4, pm.modeFilter(ba.take(i + 1, tickF) * jf, ba.take(i + 1, tickT), ba.take(i + 1, tickG))) :> *(hard);
slap = n(4) : fi.highpass(2, 1900.0) : *(l : @(ba.sec2samp(0.004)) : ar(0.0008, 0.006)) : *(hard);
pat = n(5) : fi.resonbp(430.0 * jf, 0.9, 1.0) : *(l : ar(0.002, 0.02)) : *(hard);
matl = select3(mat, 0.0, pat * 0.9, tick * 0.03 + slap * 0.3);

dry = whoosh * gw + fwap * gf + thump * 0.5 * gt + ck * 0.217 * gc + body * gb + matl * gm;
process = dry : pan(p) : wet(0.12);
