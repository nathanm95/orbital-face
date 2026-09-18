#!/usr/bin/env python3
"""Host-side model of the integer physics in src/c/physics.c.

Checking a change to the dynamics against the emulator means waiting for a
build, an install and a screendump, and then squinting at a 180px image. This
runs the same integer arithmetic in a second and tells you the numbers.

It must be kept in step with physics.c by hand. If you change the integrator
there, change it here and re-run before trusting the watch.

    python3 tools/orbit_sim.py
"""

def isqrt(v):
    if v <= 0:
        return 0
    x, y = v, (v + 1) // 2
    while y < x:
        x, y = y, (y + v // y) // 2
    return x


def idiv(a, b):
    """C integer division: truncates toward zero, unlike Python's //."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


# emery geometry; change these to model another display
CX, CY = 100, 114
RADIUS = 100
SUN_R = RADIUS // 9
R_MIN, R_MAX = SUN_R + 10, RADIUS - 5
MAX_R = RADIUS - 2

FRAME_MS = 33
Q = 16
BLEND_START = 192          # of 256
SUBSTEP_MAX = 8

RINGS = [R_MIN + (R_MAX - R_MIN) * i // 7 for i in range(8)]
NAMES = "Mercury Venus Earth Mars Jupiter Saturn Uranus Neptune".split()


def gm(gravity_strength=50):
    base = (R_MAX ** 3 * 440) // 1000000
    return max(4, base * (30 + gravity_strength * 170 // 100) // 100)


def smoothstep(s):
    s = max(0, min(256, s))
    return (s * s * (3 * 256 - 2 * s)) // (256 * 256)


def run(kx=1000, ky=0, fling=60, recap_secs=20, gravity=50, frames=1000):
    GM = gm(gravity)
    total = recap_secs * 1000
    bodies = []
    for ring in RINGS:
        px, py = (CX + ring) << Q, CY << Q
        v = isqrt((GM * 65536) // ring) * 256          # circular speed, Q16
        vx, vy = 0, -v
        room = max(32, min(256, 64 + (MAX_R - ring) * 192 // MAX_R))
        ix = idiv(idiv(max(-1500, min(1500, kx)) * room, 256) * fling, 100)
        iy = idiv(idiv(max(-1500, min(1500, ky)) * room, 256) * fling, 100)
        vx += idiv(v * ix, 1500)
        vy -= idiv(v * iy, 1500)
        bodies.append([px, py, vx, vy, ring])

    soft8 = SUN_R * 256
    rem = total
    free_r = [[] for _ in bodies]
    shown_r = [[] for _ in bodies]
    incore = [0] * len(bodies)
    substeps = 0

    for _ in range(frames):
        if rem > 0:
            rem -= FRAME_MS
        t = max(0, min(256, (total - rem) * 256 // total))
        blend = smoothstep((t - BLEND_START) * 256 // (256 - BLEND_START)) if t > BLEND_START else 0

        for i, b in enumerate(bodies):
            px, py, vx, vy, ring = b
            manhattan = max(1, (abs(px - (CX << Q)) + abs(py - (CY << Q))) >> Q)
            speed = max(abs(vx), abs(vy))
            nsub = 1 + max(0, min(SUBSTEP_MAX - 1, (speed // 65536 * 20) // manhattan))
            substeps += nsub

            for _ in range(nsub):
                dx8 = ((CX << Q) - px) >> 8
                dy8 = ((CY << Q) - py) >> 8
                r8 = isqrt(dx8 * dx8 + dy8 * dy8) or 1
                if r8 < soft8:
                    incore[i] += 1
                    den = soft8 ** 3
                else:
                    den = r8 ** 3
                vx += idiv(idiv(GM * dx8 * (1 << 32), den), nsub)
                vy += idiv(idiv(GM * dy8 * (1 << 32), den), nsub)
                px += idiv(vx, nsub)
                py += idiv(vy, nsub)

            ox, oy = (px >> Q) - CX, (py >> Q) - CY
            orr = isqrt(ox * ox + oy * oy) or 1
            if orr > MAX_R:
                px = (CX + idiv(ox * MAX_R, orr)) << Q
                py = (CY + idiv(oy * MAX_R, orr)) << Q
                vr = idiv(vx * ox + vy * oy, orr)
                if vr > 0:
                    vx -= 2 * idiv(vr * ox, orr)
                    vy -= 2 * idiv(vr * oy, orr)
                orr = MAX_R
            b[0], b[1], b[2], b[3] = px, py, vx, vy

            bx, by = (CX + ring) << Q, CY << Q
            sx = bx + idiv((px - bx) * (256 - blend), 256)
            sy = by + idiv((py - by) * (256 - blend), 256)
            free_r[i].append(orr)
            shown_r[i].append(isqrt(((sx >> Q) - CX) ** 2 + ((sy >> Q) - CY) ** 2))

    return free_r, shown_r, incore, substeps / frames


def report(label, **kw):
    free_r, shown_r, incore, subs = run(**kw)
    print(f"--- {label} ---")
    print(f"{'planet':9}{'ring':>5}{'free min':>9}{'free max':>9}"
          f"{'shown@3s':>9}{'@10s':>6}{'@16s':>6}{'final':>6}{'in Sun':>8}")
    for i, name in enumerate(NAMES):
        f, s = free_r[i], shown_r[i]
        at = lambda a, sec: a[min(len(a) - 1, int(sec * 1000 / FRAME_MS))]
        print(f"{name:9}{RINGS[i]:5}{min(f):9}{max(f):9}"
              f"{at(s, 3):9}{at(s, 10):6}{at(s, 16):6}{s[-1]:6}{incore[i]:8}")
    print(f"  substeps/frame across 8 bodies: {subs:.1f}\n")


if __name__ == '__main__':
    print(f"geometry: centre ({CX},{CY}) sun_r {SUN_R} rings {R_MIN}..{R_MAX} "
          f"edge {MAX_R}   GM {gm()}\n")
    print("'final' must equal 'ring' for every planet: the recapture is supposed")
    print("to be exact. 'free max' above the edge means the wall is doing work.\n")
    report("default (fling 60, ~1g shake)")
    report("hard flick (fling 100, 1.5g)", fling=100, kx=1500, ky=-800)
    report("gentle (0.3g)", kx=300)
    report("5 second window", recap_secs=5)
    report("weak gravity", gravity=10)
    report("strong gravity", gravity=100)
