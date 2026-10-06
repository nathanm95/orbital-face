#!/usr/bin/env python3
"""Build the appstore image set from tools/record_store.py recordings.

Reads <rec_dir>/<platform>_{classic,nebula,ember}/ and writes into store/:

  <platform>_1_preview.gif    the settled sky, a fling, and the glide home
  <platform>_2_time.png       10:08, default settings
  <platform>_3_fling.png      planets loose with their trails, FLING_SHOT
                              app frames after the fling
  <platform>_4_nebula.png     navy and ice, clock on top, large planets
                              (black-and-white watches: _4_top.png)
  <platform>_5_ember.png      no orbit paths, six planets, battery ring
                              (black-and-white watches: _5_minimal.png)
  icon_80x80.png, icon_144x144.png   pebble publish --icon-small / --icon-large
  banner_720x320.png                 store header image

The GIF is timed by app frames, not by the recording's wall clock. The
emulator on a memory-starved Mac runs the animation at about half speed with
stalls, so the recording catches every frame the app draws (it dumps faster
than the 33ms frame timer), repeats and half-redrawn dumps are dropped, and
each distinct frame gets the 33ms the watch gives it.

Screenshot filenames start with the platform because `pebble publish` infers
the platform from the text before the first underscore.

Usage: store_assets.py <rec_dir>
"""
import glob, json, math, os, random, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shot import full_scale, snap_table  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
OUT = os.path.join(ROOT, 'store')
BW = {'aplite', 'diorite', 'flint'}
NATIVE = {'aplite': (144, 168), 'basalt': (144, 168), 'chalk': (180, 180),
          'diorite': (144, 168), 'emery': (200, 228), 'flint': (144, 168),
          'gabbro': (260, 260)}

APP_FRAME_MS = 33          # main.c FRAME_MS
FLING_SHOT = 55            # app frames after the fling for shot 3
SETTLED_S = 2.0            # this long without a change means the fling is over
OPEN_MS, LINGER_MS = 700, 1500
MAX_GIF = 4_300_000        # the store's limit is 4.4MB

SPACE = (0, 0, 0)
SUN = (255, 255, 0)
CORONA = (255, 170, 0)
ORBIT = (85, 85, 85)
WHITE = (255, 255, 255)


# ----------------------------------------------------------------- frames --

def recording(rec, name):
    d = os.path.join(rec, name)
    return d, json.load(open(os.path.join(d, 'times.json')))


def frame(d, i):
    platform = os.path.basename(d).split('_')[0]
    im = normalise(Image.open(os.path.join(d, 'f%03d.png' % i)).convert('RGB'), platform)
    # The basalt emulator's dump has a 2px black frame round the 144x168
    # screen; the store rejects anything but the native size.
    w, h = NATIVE[platform]
    if im.size != (w, h):
        x, y = (im.width - w) // 2, (im.height - h) // 2
        im = im.crop((x, y, x + w, y + h))
    return im


def normalise(im, platform):
    """Snap anything still on the emulator's reduced scale to the watch's
    0/85/170/255 levels, the same way shot.py does."""
    levels = sorted({v for band in im.split() for _, v in band.getcolors(256)} - {0})
    if not levels or set(levels) <= {85, 170, 255}:
        return im
    table = list(snap_table(full_scale(levels, platform)))
    return im.point(table * 3)


def distinct(d, times):
    """(index, time) of every frame that differs from the one before it."""
    out, prev = [], None
    for i in range(len(times)):
        data = frame(d, i).tobytes()
        if data != prev:
            out.append((i, times[i]))
        prev = data
    return out


def drop_torn(d, idxs):
    """Drop dumps caught halfway through a redraw.

    The black-and-white emulators redraw top-down, and a dump can land in the
    middle: its top rows already belong to the next frame and its bottom rows
    still to the previous one. During a fling every frame moves all eight
    planets, so a real frame always has some row unlike both neighbours. A
    frame built entirely from its neighbours' rows is torn. Repeats until
    stable, because torn frames can come in runs."""
    rows = {}

    def rows_of(i):
        if i not in rows:
            im = frame(d, i)
            n = im.width * 3
            data = im.tobytes()
            rows[i] = [data[y * n:(y + 1) * n] for y in range(im.height)]
        return rows[i]

    while True:
        keep = [idxs[0]]
        for k in range(1, len(idxs) - 1):
            a, b, c = rows_of(idxs[k - 1]), rows_of(idxs[k]), rows_of(idxs[k + 1])
            if not all(b[y] == a[y] or b[y] == c[y] for y in range(len(b))):
                keep.append(idxs[k])
        keep.append(idxs[-1])
        if len(keep) == len(idxs):
            return idxs
        idxs = keep


def fling_span(d, times):
    """Distinct frames from the fling to the moment everything is home.

    The fling is the first change after the screen has held still for
    SETTLED_S (so the tail of the launch intro is never mistaken for it); the
    return is the last change before it holds still that long again."""
    changes = distinct(d, times)
    start = next((k for k in range(1, len(changes))
                  if changes[k][1] - changes[k - 1][1] >= SETTLED_S), None)
    if start is None:
        raise SystemExit('no fling found in ' + d)
    end = start
    for k in range(start + 1, len(changes)):
        if changes[k][1] - changes[k - 1][1] >= SETTLED_S:
            break
        end = k
    else:
        if times[-1] - changes[end][1] < SETTLED_S:
            raise SystemExit('fling never settled in ' + d)
    settled = changes[end][0]
    motion = drop_torn(d, [i for i, _ in changes[start:end + 1]])
    return changes[start - 1][0], motion, settled


def still_frame(d, times):
    """The last frame that matches the one before it, so a static shot never
    lands on a one-off minute pulse."""
    for i in range(len(times) - 1, 0, -1):
        if frame(d, i).tobytes() == frame(d, i - 1).tobytes():
            return i
    return len(times) - 1


def save_gif(d, before, motion, path):
    """Settled sky, the fling at the watch's own frame rate, settled sky again.
    Drops every other frame (and doubles the delay) if it comes out too big."""
    pal_frames = [frame(d, i) for i in [before] + motion]
    colours = sorted({c for f in pal_frames for _, c in f.getcolors(1 << 16)})
    pal = Image.new('P', (1, 1))
    flat = [v for c in colours for v in c]
    pal.putpalette(flat + [0] * (768 - len(flat)))
    step = 1
    while True:
        picked = motion[::step]
        if picked[-1] != motion[-1]:
            picked.append(motion[-1])
        frames = [pal_frames[0]] + [pal_frames[1 + motion.index(i)] for i in picked]
        pframes = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
        # GIF delays are in 10ms units, so 33ms per frame is 30,30,40 repeating.
        durations = [OPEN_MS] + [10 * ((k + 1) * step * APP_FRAME_MS // 10
                                       - k * step * APP_FRAME_MS // 10)
                                 for k in range(len(picked))]
        durations[-1] = LINGER_MS
        pframes[0].save(path, save_all=True, append_images=pframes[1:], duration=durations,
                        loop=0, optimize=False, disposal=1)
        if os.path.getsize(path) <= MAX_GIF or step >= 4:
            return step
        step += 1


# ----------------------------------------------------------------- artwork --

def orrery(size, ss=4):
    """The icon: the Sun, two orbits, and a planet flung off its ring with a
    trail behind it, which is the face's whole idea in one picture."""
    S = size * ss
    im = Image.new('RGB', (S, S), SPACE)
    d = ImageDraw.Draw(im)
    c = S / 2

    def disc(x, y, r, fill):
        d.ellipse((x - r, y - r, x + r, y + r), fill=fill)

    rnd = random.Random(5)
    for _ in range(12):
        disc(rnd.uniform(0, S), rnd.uniform(0, S), S * 0.006, (170, 170, 170))
    for r in (S * 0.29, S * 0.44):
        d.ellipse((c - r, c - r, c + r, c + r), outline=ORBIT, width=max(2, S // 70))
    # Sun: corona, body, hot core, as render.c draws it.
    disc(c, c, S * 0.16, (85, 85, 0))
    disc(c, c, S * 0.13, (170, 170, 0))
    disc(c, c, S * 0.12, SUN)
    disc(c, c, S * 0.04, WHITE)
    # Earth on the inner ring, with its highlight ring.
    a = math.radians(140)
    ex, ey = c + S * 0.29 * math.cos(a), c - S * 0.29 * math.sin(a)
    d.ellipse((ex - S * 0.085, ey - S * 0.085, ex + S * 0.085, ey + S * 0.085),
              outline=(170, 170, 170), width=max(2, S // 90))
    disc(ex, ey, S * 0.06, (0, 85, 170))
    disc(ex - S * 0.012, ey - S * 0.012, S * 0.05, (0, 170, 255))
    # Saturn on the outer ring, ringed.
    a = math.radians(250)
    sx, sy = c + S * 0.44 * math.cos(a), c - S * 0.44 * math.sin(a)
    d.ellipse((sx - S * 0.10, sy - S * 0.10, sx + S * 0.10, sy + S * 0.10),
              outline=(170, 170, 85), width=max(2, S // 80))
    disc(sx, sy, S * 0.065, (255, 255, 85))
    # Jupiter, flung loose: off its ring, trailing dots back along its path.
    a0, a1 = math.radians(330), math.radians(28)
    for k in range(7):
        t = k / 6
        a = a0 + (a1 - a0) * t
        r = S * (0.44 - 0.07 * t)
        x, y = c + r * math.cos(a), c - r * math.sin(a)
        if k < 6:
            lvl = (85, 170, 255)[min(2, k // 2)]
            disc(x, y, S * (0.012 + 0.006 * (k // 2)), (lvl, lvl * 2 // 3, lvl // 3))
    disc(x, y, S * 0.085, (170, 85, 0))
    disc(x - S * 0.02, y - S * 0.02, S * 0.07, (255, 170, 85))
    return im.resize((size, size), Image.LANCZOS)


def round_mask(im):
    m = Image.new('L', im.size, 0)
    ImageDraw.Draw(m).ellipse((0, 0, im.width - 1, im.height - 1), fill=255)
    return m


def make_banner(rec):
    W, H = 720, 320
    out = Image.new('RGBA', (W, H), SPACE + (255,))
    d = ImageDraw.Draw(out)
    rnd = random.Random(3)
    for _ in range(220):
        x, y = rnd.randrange(W), rnd.randrange(H)
        lvl = rnd.choice((85, 170, 170, 255))
        d.point((x, y), fill=(lvl, lvl, lvl))
    # Big faint orbits round an off-screen Sun, sweeping behind the title.
    for r in (260, 330, 400):
        d.ellipse((-120 - r, H / 2 - r, -120 + r, H / 2 + r), outline=(40, 40, 40), width=2)

    def shot(name, which):
        dd, times = recording(rec, name)
        if which == 'fling':
            _, motion, _ = fling_span(dd, times)
            return frame(dd, motion[min(FLING_SHOT, len(motion) - 1)])
        return frame(dd, still_frame(dd, times))

    gabbro = shot('gabbro_nebula', 'still')
    emery = shot('emery_classic', 'fling')
    scale = 0.86
    gabbro = gabbro.resize((round(gabbro.width * scale), round(gabbro.height * scale)), Image.LANCZOS)
    emery = emery.resize((round(emery.width * scale), round(emery.height * scale)), Image.LANCZOS)
    gx, gy = W - gabbro.width - 24, (H - gabbro.height) // 2
    ex, ey = gx - emery.width - 26, (H - emery.height) // 2
    shadow = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow)
    sd.rounded_rectangle((ex - 7, ey - 7, ex + emery.width + 7, ey + emery.height + 7), 16,
                         fill=(255, 255, 255, 40))
    sd.ellipse((gx - 7, gy - 7, gx + gabbro.width + 7, gy + gabbro.height + 7),
               fill=(255, 255, 255, 40))
    out.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(6)))
    bez = ImageDraw.Draw(out)
    bez.rounded_rectangle((ex - 5, ey - 5, ex + emery.width + 4, ey + emery.height + 4), 14,
                          fill=(34, 34, 40))
    bez.ellipse((gx - 5, gy - 5, gx + gabbro.width + 4, gy + gabbro.height + 4), fill=(34, 34, 40))
    out.paste(emery, (ex, ey))
    out.paste(gabbro, (gx, gy), round_mask(gabbro))

    futura = '/System/Library/Fonts/Supplemental/Futura.ttc'
    d = ImageDraw.Draw(out)
    d.text((28, 96), 'Orbital', font=ImageFont.truetype(futura, 60, index=2), fill=WHITE)
    tag = ImageFont.truetype(futura, 18, index=0)
    d.text((32, 176), 'The real solar system,', font=tag, fill=(255, 170, 0))
    d.text((32, 199), 'on your wrist. Shake it.', font=tag, fill=(255, 170, 0))
    return out.convert('RGB')


# -------------------------------------------------------------------- main --

def main():
    rec = sys.argv[1]
    os.makedirs(OUT, exist_ok=True)
    platforms = sorted({os.path.basename(p).split('_')[0]
                        for p in glob.glob(os.path.join(rec, '*_classic'))})
    for p in platforms:
        for old in glob.glob(os.path.join(OUT, p + '_[1-5]_*')):
            os.remove(old)
    written = []
    for p in platforms:
        d, times = recording(rec, p + '_classic')
        before, motion, _ = fling_span(d, times)
        path = os.path.join(OUT, '%s_1_preview.gif' % p)
        step = save_gif(d, before, motion, path)
        print('%s: %d app frames in the fling (%.1fs at 30fps), GIF step %d'
              % (p, len(motion), len(motion) * APP_FRAME_MS / 1000, step))
        written.append(path)
        shots = [('2_time', d, before),
                 ('3_fling', d, motion[min(FLING_SHOT, len(motion) - 1)])]
        for slot, variant in (('4_' + ('top' if p in BW else 'nebula'), 'nebula'),
                              ('5_' + ('minimal' if p in BW else 'ember'), 'ember')):
            vd, vt = recording(rec, '%s_%s' % (p, variant))
            shots.append((slot, vd, still_frame(vd, vt)))
        for slot, sd, i in shots:
            path = os.path.join(OUT, '%s_%s.png' % (p, slot))
            frame(sd, i).save(path)
            written.append(path)
    for size in (80, 144):
        path = os.path.join(OUT, 'icon_%dx%d.png' % (size, size))
        orrery(size).save(path)
        written.append(path)
    if os.path.isdir(os.path.join(rec, 'gabbro_nebula')) and os.path.isdir(
            os.path.join(rec, 'emery_classic')):
        path = os.path.join(OUT, 'banner_720x320.png')
        make_banner(rec).save(path)
        written.append(path)
    for w in written:
        im = Image.open(w)
        print('%-34s %dx%d  %dKB' % (os.path.relpath(w, ROOT), im.size[0], im.size[1],
                                     os.path.getsize(w) // 1024))


if __name__ == '__main__':
    main()
