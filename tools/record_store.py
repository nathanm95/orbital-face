#!/usr/bin/env python3
"""Record everything tools/store_assets.py needs, one emulator at a time.

Builds three store variants, all pinned to 10:08 by build flags so nothing
depends on pypkjs once a variant is installed:

  classic   default settings, flings itself FLING_S after launch
            (recapture shortened to 9s)                  -> GIF, time, fling
  nebula    preset 1: navy and ice, clock on top          -> alt colours
  ember     preset 2: no orbit paths, battery ring        -> second scheme

then, per platform, installs each one and records it with tools/record.py into
<rec_dir>/<platform>_<variant>/. Runs `pebble kill` between platforms because
the Mac is short of memory, so make sure no other session is using emulators.

Usage: record_store.py <rec_dir> [platform ...] [--only variant,...]
"""
import os, shutil, subprocess, sys, tempfile, time

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
SDK = '4.33.1'
PLATFORMS = ['aplite', 'basalt', 'chalk', 'diorite', 'emery', 'flint', 'gabbro']
BW = {'aplite', 'diorite', 'flint'}
FLING_S = 10

# name: (build flags, seconds to record, frame interval)
# The classic recording dumps faster than the app's 33ms frame timer so that
# every frame the app draws is caught at least once; store_assets.py then
# drops the repeats and times the GIF by app frames, because an emulator on a
# memory-starved Mac runs the animation at half speed with stalls.
VARIANTS = {
    'classic': ({'ORBITAL_DEMO_FLING': str(FLING_S)}, 32, 0.02),
    'nebula': ({'ORBITAL_PRESET': '1'}, 4, 0.8),
    'ember': ({'ORBITAL_PRESET': '2'}, 4, 0.8),
}

NATIVE = {'aplite': (144, 168), 'basalt': (144, 168), 'chalk': (180, 180),
          'diorite': (144, 168), 'emery': (200, 228), 'flint': (144, 168),
          'gabbro': (260, 260)}

# The Sun's colour at 10:08 for each variant: auto tint (yellow), chrome
# yellow, orange. Used to tell Orbital apart from whatever face the emulator
# may have kept on screen through the install.
SUN = {'classic': (255, 255, 0), 'nebula': (255, 170, 0), 'ember': (255, 85, 0)}


def pebble(*args, timeout=240):
    env = dict(os.environ, PATH=os.path.expanduser('~/.local/bin') + ':' + os.environ['PATH'])
    try:
        r = subprocess.run(['pebble', *args], capture_output=True, text=True, timeout=timeout,
                           env=env, cwd=ROOT)
        return r.returncode == 0, r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        return False, 'timeout'


def build_variants(out, names):
    paths = {}
    for name in names:
        flags = VARIANTS[name][0]
        env = dict(os.environ, ORBITAL_SHOW_TIME='1008', **flags,
                   PATH=os.path.expanduser('~/.local/bin') + ':' + os.environ['PATH'])
        for k in ('ORBITAL_DEBUG', 'ORBITAL_PRESET', 'ORBITAL_DEMO_FLING'):
            if k not in flags:
                env.pop(k, None)
        r = subprocess.run(['pebble', 'build'], capture_output=True, text=True, env=env, cwd=ROOT)
        if r.returncode:
            sys.exit('build failed for %s:\n%s' % (name, r.stdout[-2000:]))
        paths[name] = os.path.join(out, 'orbital_%s.pbw' % name)
        shutil.copy(os.path.join(ROOT, 'build', 'orbital-face.pbw'), paths[name])
        print('built', name)
    return paths


def looks_like_orbital(platform, variant):
    """The Sun sits dead centre: a white core inside a disc of the variant's
    Sun colour (black on black-and-white watches)."""
    from PIL import Image
    shot = os.path.join(tempfile.gettempdir(), 'orbital_check.png')
    if os.path.exists(shot):
        os.remove(shot)
    subprocess.run(['python3', os.path.join(TOOLS, 'shot.py'), '%s@%s' % (platform, SDK), shot],
                   capture_output=True)
    if not os.path.exists(shot):
        return False
    # shot.py has already snapped the colours to the watch's levels; snapping
    # again would push emery's 85s up to 255 and fail every orange Sun.
    im = Image.open(shot).convert('RGB')
    w, h = NATIVE[platform]
    cx, cy = im.width // 2, im.height // 2
    sun_r = max(5, min(w, h) // 2 // 9)
    ring = im.getpixel((cx + sun_r * 3 // 4, cy))
    want = (0, 0, 0) if platform in BW else SUN[variant]
    return im.getpixel((cx, cy)) == (255, 255, 255) and \
        sum(abs(a - b) for a, b in zip(ring, want)) <= 90


def install(platform, pbw, variant):
    for attempt in range(4):
        ok, out = pebble('install', '--emulator', platform, '--sdk', SDK, pbw)
        if ok:
            time.sleep(2)
            if looks_like_orbital(platform, variant):
                return True
            print('  wrong face on screen, dismissing and retrying', flush=True)
            pebble('emu-button', '--emulator', platform, '--sdk', SDK, 'click', 'back')
            time.sleep(2)
        else:
            print('  install failed (%s), retrying' % out.strip().splitlines()[-1][:80], flush=True)
            time.sleep(5)
    return False


def main():
    args = sys.argv[1:]
    names = list(VARIANTS)
    if '--only' in args:
        k = args.index('--only')
        names = args[k + 1].split(',')
        del args[k:k + 2]
    rec = os.path.abspath(args[0])
    platforms = args[1:] or PLATFORMS
    os.makedirs(rec, exist_ok=True)
    pbws = build_variants(rec, names)
    for platform in platforms:
        print(platform, flush=True)
        for variant in names:
            _, seconds, interval = VARIANTS[variant]
            if not install(platform, pbws[variant], variant):
                print('  FAILED to get %s on screen' % variant, flush=True)
                continue
            d = os.path.join(rec, '%s_%s' % (platform, variant))
            subprocess.run(['python3', os.path.join(TOOLS, 'record.py'), '%s@%s' % (platform, SDK),
                            d, str(seconds), '--interval', str(interval)], check=True)
        pebble('kill')
        time.sleep(3)


if __name__ == '__main__':
    main()
