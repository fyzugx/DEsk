#!/usr/bin/env python3
"""bake.py -- the desk's sounds that are made in Faust rather than in sfx.c, baked to art/sound/<name>-<n>.wav.

These are the ones chosen by ear, 2026-11-14..15, out of takes played side by side with the desk's own
(the drawer opened and shut, a sheet put down on a leather mat or slid, the Control Panel): each is made
again exactly as it was heard -- the same score, the same draws, the noise from the same place -- and
brought to the loudness it was heard at, against what the desk plays it at. A few takes of each, the
noise started somewhere else, so that nothing sounds the same twice; where the desk plays a sound in two
parts (the drawer's run and its stop; the key in and turned; the lid lifted and at its stop), the take is
cut there, the second part what the whole has over the first.

The files are 16-bit mono at 48 kHz and half as loud as they are to be played: sound.c reads them back
at twice (a stop on the drawer peaks within a few dB of full scale). Where there is a file, sound.c plays
it instead of what sfx.c makes, and a sound with none falls back to sfx.c (or, one only baked, to
silence).

Needs Faust (2.88; FAUST= where it is unpacked: bin/faust, share/faust, include) and a C compiler; run
from anywhere:  FAUST=/path/to/faust python3 art/sound/bake.py (without FAUST, the faust on PATH)"""
import array, math, os, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FAUST = os.path.expanduser(os.environ.get('FAUST') or
                           os.path.dirname(os.path.dirname(shutil.which('faust') or '/usr/bin/faust')))
SR = 48000
TMP = tempfile.mkdtemp(prefix='desk-sound-')


def build(dsp):
    exe = os.path.join(TMP, dsp)
    if not os.path.exists(exe):
        c = exe + '.c'
        subprocess.run([FAUST + '/bin/faust', '-lang', 'c', '-cn', 'sfx', '-I', FAUST + '/share/faust',
                        '-I', HERE + '/dsp', HERE + '/dsp/' + dsp + '.dsp', '-o', c], check=True)
        subprocess.run(['cc', '-O2', '-w', '-I' + FAUST + '/include', '-I' + TMP, '-DDSP_C="%s.c"' % dsp,
                        HERE + '/render.c', '-o', exe, '-lm'], check=True)
    return exe


def render(dsp, score, secs, pre):
    """-> the left and the right, as floats"""
    sc, out = os.path.join(TMP, 'score'), os.path.join(TMP, 'out.wav')
    open(sc, 'w').write(score)
    subprocess.run([build(dsp), sc, out, '%.3f' % secs, str(pre)], check=True, capture_output=True)
    a = array.array('f')
    a.frombytes(open(out, 'rb').read()[44:])
    return list(a[0::2]), list(a[1::2])


def mono(x, g):
    """the left and the right as one, placed in the middle by sound.c as they were (constant power)"""
    k = g / math.sqrt(2.0)
    return [(a + b) * k for a, b in zip(*x)]


def minus(x, y):
    return [a - b for a, b in zip(x[0], y[0])], [a - b for a, b in zip(x[1], y[1])]


def trim(x, floor_db=-70.0):
    """to where it has died away under floor_db of its loudest, and faded over the last 10 ms"""
    pk = max(abs(v) for v in x)
    th = pk * 10 ** (floor_db / 20)
    n = min(len(x), max(i for i in range(len(x)) if abs(x[i]) >= th) + SR // 100)
    x = x[:n]
    fo = SR // 100
    for i in range(min(fo, n)):
        x[n - 1 - i] *= i / fo
    return x


def takes(pre, n):
    """the chosen take's start in the noise, and n - 1 others"""
    return [(pre + 16001 * k) % SR for k in range(n)]


def loop(x, a, length, cross):
    """length samples from a, the end crossfaded (equal power: noise) into the start, to go round and round"""
    out = x[a:a + length]
    for i in range(cross):
        u = i / cross
        out[i] = x[a + i] * math.sqrt(u) + x[a + length + i] * math.sqrt(1.0 - u)
    return out


baked = {}


def put(name, xs):
    baked[name] = xs


# ---- the drawer (聞き比べ 4, 開ける・閉める「重く はっきり」: set 1's new 1, struck heavier and softer, the
# weight where it is heard). As wm.c moves it, clicked: the run at 1, its stop at 1.

def drawer_path():
    v = x = t = 0.0
    pts = []
    while x < 265.0:
        for _ in range(48):
            v += (2600.0 - 2.2 * v) / 4800.0
            x += v / 4800.0
            t += 1.0 / 4800.0
        pts.append((t, v / 1200.0))
    return pts, t, min(0.35 + v / 1400.0, 1.0)


def drawer(name, out, hitk, jf, pre, weight, g):
    pts, t_stop, hard = drawer_path()
    ramp, prev = '', (0.0, 0.0)
    for t, s in pts:
        ramp += '%.4f %.4f speed %.4f %.4f\n' % (prev[0], t, prev[1], s)
        prev = (t, s)
    sc = ('0 dir %d\n0 hit %.3f\n0 jf %.4f\n0 heavy 1.00\n0 cb 1.63\n0 ba 1.11\n0 weight %.4f\n0 don %.4f\n'
          % (out, hard * hitk, jf, weight, 1.7 * weight) + ramp + '%.4f speed 0\n' % t_stop)
    runs, stops = [], []
    for p in takes(pre, 3):
        run = render('drawer', sc, t_stop + 0.9, p)
        whole = render('drawer', sc + '%.4f stop 1\n' % t_stop, t_stop + 0.9, p)
        stop = minus(whole, run)
        at = int(t_stop * SR) - SR // 1000
        runs.append(trim(mono(run, g)))
        stops.append(trim(mono((stop[0][at:], stop[1][at:]), g)))
    put('drawer-' + name, runs)
    put('drawer-stop-' + name, stops)


drawer('out', 1, 1.0026, 0.9883, 4664, 1.7384, 1.55993)
drawer('in', 0, 0.9211, 1.0017, 24806, 1.5370, 1.58616)

# ---- a sheet put down on a leather mat (聞き比べ 1, 新しい 革 1), against the desk's sheet-down at 0.7: for any
# soft one (leather, linoleum, felt)

put('sheet-down-soft', [trim(mono(render('sheet_down', '0 mat 1\n0 size 1.037\n0 hard 1.178\n0 jf 1.0377\n0 pan 0\n'
                                              '0.0 air 1\n0.0627 land 1\n', 0.9, p), 0.31715 / 0.7))
                           for p in takes(8377, 3)])

# ---- a sheet slid (聞き比べ 1, all six): on the wood, a soft mat (leather) and the clear one, going slowly
# (450 points a second) and quickly (1700), 1.6 s of each to go round for as long as it moves; wm.c fades
# between the two by the speed.

for mat, mname, pres in ((0, 'wood', (38597, 44333)), (1, 'soft', (36912, 32938)), (2, 'clear', (41082, 13782))):
    for how, speed, p in (('slow', 450.0, pres[0]), ('quick', 1700.0, pres[1])):
        x = mono(render('sheet_slide', '0 mat %d\n0 pan 0\n0 speed %.1f\n' % (mat, speed), 2.3, p), 0.4418)
        put('slide-%s-%s' % (mname, how), [loop(x, int(0.4 * SR), int(1.6 * SR), int(0.15 * SR))])

# ---- the Control Panel (聞き比べ 2). The lit caps, against the push at 0.8: on (点ける 1), off (消す 2), one
# that only does something (表示 2), SLEEP's and SUSPEND's (SLEEP 1)

CAP = 0.71234 / 0.8
for name, score, pre in (('cc-cap-on', '0 kind 1\n0 jf 1.0067\n0 hard 1.021\n', 2563),
                         ('cc-cap-off', '0 kind 2\n0 jf 0.9741\n0 hard 1.057\n', 19109),
                         ('cc-cap', '0 kind 0\n0 jf 0.9711\n0 hard 0.860\n', 37773),
                         ('cc-cap-sleep', '0 kind 0\n0 size 0.75\n0 jf 1.0198\n0 hard 0.899\n', 18810)):
    put(name, [trim(mono(render('cap', score + '0 pan 0\n0.0 down 1\n0.09 up 1\n', 0.6, p), CAP)) for p in takes(pre, 3)])

# AIRPLANE (機内モード, 点ける・戻す): its cap and both levers, against the push at 0.8; the aerial after them,
# at 0.7 as the desk plays it, and for the WIRELESS lever too


def mix(parts):
    n = max(int(at * SR) + len(x[0]) for x, at, g in parts)
    L, R = [0.0] * n, [0.0] * n
    for x, at, g in parts:
        a = int(at * SR)
        for i in range(len(x[0])):
            L[a + i] += x[0][i] * g
            R[a + i] += x[1][i] * g
    return L, R


AIR = 0.75067
for name, cap, l1, l2 in (('cc-air-on', ('1\n0 jf 1.0270', 32221), ('0\n0 jf 1.0216', 8100), ('0\n0 jf 1.0139', 26226)),
                          ('cc-air-off', ('2\n0 jf 0.9777', 46477), ('1\n0 jf 0.9902', 29377), ('1\n0 jf 0.9848', 31808))):
    xs = []
    for k in range(2):
        c = render('cap', '0 kind %s\n0 pan 0\n0.0 down 1\n0.09 up 1\n' % cap[0], 0.6, takes(cap[1], 2)[k])
        a = render('lever', '0 dir %s\n0 pan 0\n0.0 throw 1\n' % l1[0], 0.4, takes(l1[1], 2)[k])
        b = render('lever', '0 dir %s\n0 pan 0\n0.0 throw 1\n' % l2[0], 0.4, takes(l2[1], 2)[k])
        xs.append(trim(mono(mix([(c, 0.0, 1.0), (a, 0.035, 0.8), (b, 0.075, 0.8)]), AIR / 0.8)))
    put(name, xs)
put('aerial-up', [trim(mono(render('aerial', '0 dir 1\n0 jf 1.0001\n0 pan 0\n0.0 go 1\n1.55 stop 1\n', 1.9, 11648), AIR))])
put('aerial-down', [trim(mono(render('aerial', '0 dir 0\n0 jf 0.9979\n0 pan 0\n0.0 go 1\n1.55 stop 1\n', 1.9, 6236), AIR))])

# a lever (レバー, 上げる 2・下げる 2), against the throw at 1

for name, score, pre in (('cc-lever-up', '0 dir 1\n0 jf 1.0229\n0 hard 1.124\n', 29404),
                         ('cc-lever-down', '0 dir 0\n0 jf 1.0179\n0 hard 1.086\n', 183)):
    put(name, [trim(mono(render('lever', score + '0 pan 0\n0.0 throw 1\n', 0.4, p), 0.78886)) for p in takes(pre, 3)])

# a knob's detent (つまみ, ゆっくり・速く), against one at 1: the click, and the shaft's grease as it goes
# into it at the speed the slow turn had at its middle

put('cc-knob', [trim(mono(render('knob', '0 jf 0.9991\n0 pan 0\n0.0 detent 1\n0 0.05 speed 0 0.7\n0.05 0.12 speed 0.7 0\n',
                                 0.4, p), 0.39454)) for p in takes(151, 3)])


def two(dsp, score, first, second, at, secs, pre, g):
    """a take in two: what the first press makes, and what the whole has over it from the second on"""
    a, b = [], []
    for p in takes(pre, 2):
        one = render(dsp, score + '0.0 %s 1\n' % first, secs, p)
        whole = render(dsp, score + '0.0 %s 1\n%.3f %s 1\n' % (first, at, second), secs, p)
        rest = minus(whole, one)
        k = int(at * SR) - SR // 1000
        a.append(trim(mono(one, g)))
        b.append(trim(mono((rest[0][k:], rest[1][k:]), g)))
    return a, b


# the key (鍵, 両方): in, and turned; turned back, and out -- each at 1

KEY = 0.64620
xs = two('lock', '0 jf 1.0233\n0 pan 0\n', 'in', 'turn', 1.45, 2.0, 3545, KEY)
put('cc-key-in', xs[0]); put('cc-key-turn', xs[1])
xs = two('lock', '0 jf 1.0109\n0 pan 0\n', 'back', 'out', 1.45, 2.0, 26153, KEY)
put('cc-key-back', xs[0]); put('cc-key-out', xs[1])

# the walnut lid (くるみの蓋, 両方): lifted, and at its stop, up and down -- each at 1

LID = 1.75273
xs = two('lid', '0 dir 1\n0 jf 1.0251\n0 pan 0\n', 'lift', 'stop', 0.42, 1.0, 35951, LID)
put('cc-lid-up', xs[0]); put('cc-lid-up-stop', xs[1])
xs = two('lid', '0 dir 0\n0 jf 0.9780\n0 pan 0\n', 'lift', 'stop', 0.42, 1.0, 11054, LID)
put('cc-lid-down', xs[0]); put('cc-lid-down-stop', xs[1])

# SHUT DOWN (電源を切る, 新しい 1): the button, the relay and the set going out, at 1

put('cc-power', [trim(mono(render('power', '0 jf 0.9929\n0 pan 0\n0.0 press 1\n', 3.6, 45854), 1.49493))])

# ---- written: the old ones of these names gone first, so that a take fewer is not left behind

for f in os.listdir(HERE):
    if f.endswith('.wav'):
        os.remove(os.path.join(HERE, f))
total = 0
for name, xs in baked.items():
    for k, x in enumerate(xs):
        pk = max(abs(v) for v in x)
        assert pk * 0.5 < 1.0, (name, pk)
        q = array.array('h', (max(-32768, min(32767, int(round(v * 0.5 * 32767.0)))) for v in x))
        path = os.path.join(HERE, '%s-%d.wav' % (name, k + 1))
        with open(path, 'wb') as f:
            n = len(q) * 2
            f.write(struct.pack('<4sI4s4sIHHIIHH4sI', b'RIFF', 36 + n, b'WAVE', b'fmt ', 16, 1, 1, SR, SR * 2, 2, 16,
                                b'data', n))
            f.write(q.tobytes())
        total += 44 + len(q) * 2
        print('%-24s %5.2f s  peak %6.1f dBFS (as played)' % (os.path.basename(path), len(x) / SR, 20 * math.log10(pk)))
print('%d files, %.2f MB' % (sum(len(x) for x in baked.values()), total / 1e6))
shutil.rmtree(TMP)
