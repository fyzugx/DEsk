// drawer.dsp -- (2026-11-14): the drawer on its runners and at its stop. speed: how fast it is
// going, 0..1 of the fastest; "stop": it meets its stop (out, dir 1) or comes home (in, dir 0), as hard
// as hit. Wood on wood along the runners, the box of it chattering as the runners catch and let go, the
// small things in it shifting; at the stop a knock through the box and the desk's carcass, the drawer
// coming back off it a little, and the things in it sliding on and settling. Home: deeper, and the air
// pushed out of the carcass.
import("desksfx.lib");
dir = hslider("dir", 1, 0, 1, 1);
v = hslider("speed", 0, 0, 1.5, 0.001) : glide(0.012);
hard = hslider("hit", 1, 0, 2, 0.01);
jf = hslider("jf", 1, 0.8, 1.2, 0.001);
gru = hslider("g_run", 1, 0, 4, 0.01);
gch = hslider("g_chatter", 1, 0, 4, 0.01);
gco = hslider("g_contents", 1, 0, 4, 0.01);
gk = hslider("g_knock", 1, 0, 4, 0.01);
st = button("stop") : ba.impulsify;

boxF = (128, 206, 318, 472, 690, 1015, 1490);
boxT = (0.18, 0.15, 0.12, 0.09, 0.065, 0.045, 0.03);
boxG = (1.0, 0.8, 0.65, 0.5, 0.35, 0.25, 0.15);
box(fs) = _ <: par(i, 7, pm.modeFilter(ba.take(i + 1, boxF) * fs, ba.take(i + 1, boxT), ba.take(i + 1, boxG))) :> _;
carF = (72, 105, 152, 210, 300);
carT = (0.16, 0.13, 0.10, 0.08, 0.06);
carG = (1.0, 0.8, 0.6, 0.45, 0.3);
carcass = _ <: par(i, 5, pm.modeFilter(ba.take(i + 1, carF) * jf, ba.take(i + 1, carT), ba.take(i + 1, carG))) :> _;

amp = pow(max(v, 0.0), 0.8);
gcut = 4.0 + 34.0 * v;
grain = n(6) : fi.lowpass(2, gcut) : fi.lowpass(2, gcut) : abs : *(7.0) : min(1.6);
rough = 0.45 + 0.55 * grain;
// (run_dark, set 4: the upper two bands down and the lowpass from 2400 to 1320 Hz -- to hear whether the run is
// also what is heard as high)
dk = hslider("run_dark", 0, 0, 1, 0.01);
run = n(0) <: fi.resonbp(165.0 * jf, 1.1, 0.8), fi.resonbp(430.0 * jf, 1.6, 0.55), fi.resonbp(1250.0, 0.9, 0.22 * (1 - 0.6 * dk)),
	fi.resonbp(3200.0, 0.8, 0.03 * (1 - 0.8 * dk)) :> fi.lowpass(2, 2400.0 * (1 - 0.45 * dk)) : *(rough * amp);   // (wood on wood
	// is dull: the first take hissed up to 15 kHz)
chat = no.sparse_noise(max(18.0 + 70.0 * v, 0.01)) : *(amp) : box(jf * select2(dir > 0.5, 0.82, 1.0)) : *(0.05);

jerk = (v - v') : max(0) : *(400.0) : min(1) : glide(0.02);
crate = jerk * 60.0 + (st : ar(0.01, 0.05)) * 160.0 * hard;
// (heavy: the things in it a little larger, so a little lower and quieter)
hv = hslider("heavy", 0, 0, 1, 0.01);
item(x) = x <: pm.modeFilter(2300.0 * (1 - 0.25 * hv), 0.02, 1.0), pm.modeFilter(850.0 * (1 - 0.25 * hv), 0.04, 0.6) :> _;
contents = no.sparse_noise(max(crate, 0.01)) : *(crate > 0.5) : item : *(0.04);

out_ = dir > 0.5;
k1 = st * hard;
k2 = (st : @(ba.sec2samp(0.016))) * hard * 0.22 * out_;
kn = k1 + k2;
// ドン 2 (11/15, the owner on set 3: "高い音に感じるのが良くない。開ける、閉じる音代わりました？" -- set 3's ドン was all
// under 250 Hz, where an ear at a quiet level and a laptop's speakers hear next to nothing: as heard it added
// 0.7 dB). Struck by a single sample, the box and the carcass gave their click to every octave, and the
// carcass (72..300 Hz) was heard at 700 Hz. "heavy": the drawer heavier and the blow softer -- what strikes
// rises and falls over a few ms (the strike through a lowpass, mixed in), so they ring at their own notes;
// the tick goes. "weight": the carcass struck harder and a dull blow of 100..500 Hz, where it is heard (and
// set 3's ドン, "don", under them).
kns = kn : fi.lowpass(2, 330.0);
ke = kn * (1 - hv) + kns * hv;
wt = hslider("weight", 0, 0, 4, 0.01);
gba = hslider("g_blow", 1, 0, 4, 0.01);
// (the first take's knock was a thin spike: today's stop has a body of 100..300 Hz for a tenth of a second)
thud = n(7) : fi.lowpass(2, 380.0) : *(st : ar(0.002, 0.035)) : *(hard * 1.6);
gkb = hslider("g_kbox", 1, 0, 4, 0.01);
gkc = hslider("g_kcarcass", 1, 0, 4, 0.01);
gkt = hslider("g_kthud", 1, 0, 4, 0.01);
gkk = hslider("g_ktick", 1, 0, 4, 0.01);
gkd = hslider("g_kdesk", 1, 0, 4, 0.01);
gwh = hslider("g_whump", 1, 0, 4, 0.01);
knock = (ke : box(jf * select2(out_, 0.62, 1.0))) * 0.5 * gkb + (ke : carcass) * select2(out_, 0.8, 0.6) * (1 + CB * wt) * gkc
	+ thud * gkt
	+ (n(3) : fi.highpass(2, 2500.0) : *(st : ar(0.0005, 0.004)) : *(hard * 0.6)) * (1 - hv) * gkk
	+ (ke : desk(jf, 1.0)) * 0.2 * gkd;
CB = hslider("cb", 1, 0, 8, 0.01);
blowa = n(1) : fi.lowpass(4, 380.0) : fi.highpass(2, 80.0) : *(st : ar(0.0025, 0.03)) : *(hard);
BA = hslider("ba", 1, 0, 8, 0.01);
whump = n(4) : fi.lowpass(2, 110.0) : *(st : ar(0.004, 0.05)) : *(hard * 1.4 * (1 - out_)) * gwh;

// ドン (11/15; the owner on set 1: "1が一番良いが、気持ちもう少しドン！という感じかな。", and shutting "開けるときと
// 同様"): the weight of the drawer and what is in it landing on the stop through the whole carcass -- a low,
// short, round thud, 58..178 Hz and a dull blow under 220 Hz, so that small speakers still carry some of it.
// Home it is a little lower. "don" 0 is set 1's drawer, unchanged.
don = hslider("don", 0, 0, 4, 0.01);
donF = (58, 86, 124, 178);
donT = (0.22, 0.16, 0.11, 0.08);
donG = (1.0, 0.8, 0.55, 0.35);
blow = st : ar(0.0015, 0.004) : *(hard);
dbody = blow <: par(i, 4, pm.modeFilter(ba.take(i + 1, donF) * jf * select2(out_, 0.85, 1.0), ba.take(i + 1, donT),
	ba.take(i + 1, donG))) :> _;
dthud = n(5) : fi.lowpass(2, 220.0) : fi.lowpass(2, 220.0) : *(st : ar(0.003, 0.05)) : *(hard) : fi.highpass(2, 30.0);
// (the highpass from set 4 on: low noise under a 50 ms swell left the file off centre by -3e-3 -- set 3 was made
// without it)
gdb = hslider("g_donbody", 1, 0, 4, 0.01);
gdt = hslider("g_donthud", 1, 0, 4, 0.01);
donl = dbody * 0.00023 * gdb + dthud * gdt;   // (the round part 2 dB over the blow, loudest 30 ms; pm.modeFilter rings
	// 74 dB high this low)

// (the ドン turned over, in phase with the knock's own body: the other way round it took 0.5 dB off 120..300 Hz
// at the stop, where small speakers hear a thud; this way it adds 2.7 dB there. Delayed 3..10 ms it did less.)
dry = run * 0.5 * gru + chat * 0.148 * gch + contents * 0.46 * (1 - 0.4 * hv) * gco + (knock + whump) * 0.055 * gk - donl * don
	+ blowa * BA * wt * gba;
process = dry : pan(0.0) : wet(0.13);
