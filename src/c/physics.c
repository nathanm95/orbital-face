#include "orbital.h"

// -----------------------------------------------------------------------------
// The toy.
//
// Planets are not springs on a leash. They are bodies under real inverse-square
// gravity from the Sun. A planet is handed exactly its circular orbital velocity
// the moment it comes loose, so an untouched fling leaves it tracing a clean
// circle; the shake adds to that velocity, which turns the circle into an
// ellipse, and a hard flick drops the perihelion inside the Sun and slingshots
// it out the far side. All of the interesting behaviour is Kepler's.
//
// Two decisions are worth knowing about, because the obvious versions of both
// are wrong and the failures are slow and confusing:
//
//   Q16, not Q8. At Q8 the gravity components round to zero near the axes on
//   the outer rings. The orbit degenerates into a polygon, gains energy every
//   lap and eventually leaves the screen.
//
//   The recapture is a positional blend, not a force. Fading a spring in while
//   gravity is still on bleeds orbital velocity, and the planet spirals into
//   the Sun instead of gliding home. Here the simulation stays purely
//   Keplerian — nothing ever removes energy from it — and the planet is drawn
//   on a smoothstep between its free trajectory and the truth. Arrival is
//   guaranteed by construction rather than hoped for.
// -----------------------------------------------------------------------------

#define FRAME_MS     33
#define BLEND_START  192      // of 256: free flight until three-quarters through
#define SUBSTEP_MAX  8

static int32_t s_gm;             // GM in px^3/frame^2
static int32_t s_recapture_ms;
static int32_t s_recapture_tot;
static bool    s_running;

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// GM is chosen from the screen so the outermost orbit takes a few seconds to go
// round on every display size. Without this a 260px gabbro would feel treacly
// next to a 144px aplite.
static void recompute_gm(void) {
  int32_t r = g_scene.r_max;
  int32_t base = (int32_t)(((int64_t)r * r * r * 440) / 1000000);
  if (base < 8) base = 8;
  int32_t scale = 30 + (int32_t)g_settings.gravity_strength * 170 / 100;  // 30..200%
  s_gm = base * scale / 100;
  if (s_gm < 4) s_gm = 4;
}

// Circular orbital speed at radius r, Q16 px/frame.
// sqrt(GM/r) * 65536 is computed as sqrt(GM*65536/r) * 256 to keep the argument
// inside 32 bits; the half-ULP that costs is four decimal places down.
static int32_t circular_speed(int32_t r) {
  if (r < 1) r = 1;
  return isqrt32((s_gm * 65536) / r) * 256;
}

void physics_reset(void) {
  for (int i = 0; i < PLANET_COUNT; i++) {
    Body *b = &g_scene.body[i];
    b->loose = false;
    b->vx = b->vy = 0;
    b->px = b->base.x << 16;
    b->py = b->base.y << 16;
    b->shown = b->base;
    b->trail_n = b->trail_head = 0;
  }
  s_recapture_ms = 0;
  s_running = false;
}

// A shake. `ax`/`ay` are raw accelerometer milli-g on the watch's own axes.
void physics_fling(int16_t ax, int16_t ay) {
  if (!g_settings.physics_enabled) return;

  recompute_gm();
  s_recapture_tot = (int32_t)g_settings.recapture_secs * 1000;
  s_recapture_ms  = s_recapture_tot;

  // Milli-g into a signed fraction of a full kick. A brisk shake reads a couple
  // of g, so 1500 is about as hard as anyone actually flicks a wrist.
  int32_t kx = clampi(ax, -1500, 1500);
  int32_t ky = clampi(ay, -1500, 1500);

  for (int i = 0; i < PLANET_COUNT; i++) {
    Body *b = &g_scene.body[i];

    int32_t dx = (b->px >> 16) - g_scene.centre.x;
    int32_t dy = (b->py >> 16) - g_scene.centre.y;
    int32_t r  = isqrt32(dx * dx + dy * dy);
    if (r < 1) r = 1;

    int32_t v = circular_speed(r);

    if (!b->loose) {
      // Hand it the orbital velocity it would really have: tangential and
      // anticlockwise, matching the direction ecliptic longitude increases.
      b->vx = (int32_t)(((int64_t)v * dy) / r);
      b->vy = (int32_t)(-((int64_t)v * dx) / r);
      b->loose = true;
      b->trail_n = b->trail_head = 0;
    }

    // Scale the kick by how much room this planet has before the screen edge.
    // Without it, one shake throws Neptune's apoapsis well past the display and
    // it spends the whole animation out of sight.
    int32_t room = 64 + (int32_t)(g_scene.max_r - b->ring_r) * 192 / g_scene.max_r;
    room = clampi(room, 32, 256);

    int32_t f = (int32_t)g_settings.fling_strength;
    int32_t ix = (kx * room / 256) * f / 100;
    int32_t iy = (ky * room / 256) * f / 100;

    // The kick is a fraction of this planet's own orbital speed, so one shake
    // perturbs Mercury and Neptune by comparable amounts.
    b->vx += (int32_t)(((int64_t)v * ix) / 1500);
    b->vy -= (int32_t)(((int64_t)v * iy) / 1500);   // accel +y up, screen +y down
  }

  s_running = true;
}

bool physics_active(void) { return s_running; }

static void push_trail(Body *b) {
  if (!g_settings.show_trails || g_settings.trail_length == 0) { b->trail_n = 0; return; }
  b->trail[b->trail_head] = b->shown;
  b->trail_head = (b->trail_head + 1) % MAX_TRAIL;
  if (b->trail_n < g_settings.trail_length) b->trail_n++;
}

// smoothstep, 0..256 in and out. Zero slope at both ends, so the planet peels
// off its orbit and arrives home without a visible kink at either join.
static int32_t smoothstep(int32_t s) {
  s = clampi(s, 0, 256);
  return (s * s * (3 * 256 - 2 * s)) / (256 * 256);
}

// One body, one frame of free Kepler flight.
static void integrate(Body *b) {
  int32_t cx16 = g_scene.centre.x << 16;
  int32_t cy16 = g_scene.centre.y << 16;

  // Substep by the angle swept per frame, not by raw speed: what breaks the
  // integrator is turning too far in one step, and that is v/r, not v.
  int32_t manhattan = (abs(b->px - cx16) + abs(b->py - cy16)) >> 16;
  if (manhattan < 1) manhattan = 1;
  int32_t speed = abs(b->vx) > abs(b->vy) ? abs(b->vx) : abs(b->vy);
  int32_t nsub = 1 + clampi((speed / 65536 * 20) / manhattan, 0, SUBSTEP_MAX - 1);

  const int32_t soft8 = g_scene.sun_r * 256;   // softening radius, Q8 pixels

  for (int32_t k = 0; k < nsub; k++) {
    // Distance kept to Q8 sub-pixel precision. Rounding the separation to whole
    // pixels first puts a hard floor on accuracy — about 15% radius wobble on
    // the innermost ring, no matter how finely you substep.
    int32_t dx8 = (cx16 - b->px) >> 8;
    int32_t dy8 = (cy16 - b->py) >> 8;

    int32_t r8 = isqrt64((int64_t)dx8 * dx8 + (int64_t)dy8 * dy8);
    if (r8 < 1) r8 = 1;

    // Inside the Sun, treat it as a uniform sphere so gravity falls linearly to
    // zero at the centre. The singularity is what makes a naive version fling
    // grazing planets into the next county, or glue them to the surface.
    int64_t den = (r8 < soft8) ? (int64_t)soft8 * soft8 * soft8
                               : (int64_t)r8 * r8 * r8;
    if (r8 < soft8 && g_scene.flare < 200) g_scene.flare = 255;

    int32_t ax = (int32_t)(((int64_t)s_gm * dx8 * 4294967296LL) / den);
    int32_t ay = (int32_t)(((int64_t)s_gm * dy8 * 4294967296LL) / den);

    b->vx += ax / nsub;
    b->vy += ay / nsub;
    b->px += b->vx / nsub;
    b->py += b->vy / nsub;
  }

  // The screen edge is an elastic wall. An absorbing one looks harmless and is
  // not: it eats the outward velocity every lap, so any orbit reaching the edge
  // decays and ends up in the Sun.
  int32_t ox = (b->px >> 16) - g_scene.centre.x;
  int32_t oy = (b->py >> 16) - g_scene.centre.y;
  int32_t orr = isqrt32(ox * ox + oy * oy);
  if (orr > g_scene.max_r) {
    b->px = (g_scene.centre.x + ox * g_scene.max_r / orr) << 16;
    b->py = (g_scene.centre.y + oy * g_scene.max_r / orr) << 16;
    int32_t vr = (int32_t)(((int64_t)b->vx * ox + (int64_t)b->vy * oy) / orr);
    if (vr > 0) {
      b->vx -= (int32_t)(2 * (int64_t)vr * ox / orr);
      b->vy -= (int32_t)(2 * (int64_t)vr * oy / orr);
    }
  }
}

bool physics_step(void) {
  bool animating = false;

  // Transient visual state, advanced on the same clock.
  if (g_scene.intro < 255) {
    g_scene.intro = clampi(g_scene.intro + 7, 0, 255);
    animating = true;
  }
  if (g_scene.sun_pulse > 0) {
    g_scene.sun_pulse = clampi(g_scene.sun_pulse - 12, 0, 255);
    animating = true;
  }
  if (g_scene.flare > 0) {
    g_scene.flare = clampi(g_scene.flare - 18, 0, 255);
    animating = true;
  }

  if (!s_running) return animating;

  if (s_recapture_ms > 0) s_recapture_ms -= FRAME_MS;
  int32_t elapsed = s_recapture_tot - s_recapture_ms;
  int32_t t = (s_recapture_tot > 0)
            ? clampi(elapsed * 256 / s_recapture_tot, 0, 256) : 256;

  int32_t blend = (t > BLEND_START)
    ? smoothstep((t - BLEND_START) * 256 / (256 - BLEND_START)) : 0;

  bool still_loose = false;

  for (int i = 0; i < PLANET_COUNT; i++) {
    Body *b = &g_scene.body[i];
    if (!b->loose) continue;

    integrate(b);

    if (blend >= 256) {
      b->loose = false;
      b->vx = b->vy = 0;
      b->px = b->base.x << 16;
      b->py = b->base.y << 16;
      b->shown = b->base;
      b->trail_n = 0;
      continue;
    }

    int32_t bx = b->base.x << 16;
    int32_t by = b->base.y << 16;
    b->shown.x = (int16_t)((bx + (int32_t)(((int64_t)(b->px - bx) * (256 - blend)) / 256)) >> 16);
    b->shown.y = (int16_t)((by + (int32_t)(((int64_t)(b->py - by) * (256 - blend)) / 256)) >> 16);

    push_trail(b);
    still_loose = true;
  }

  g_scene.settle = blend;
  s_running = still_loose;
  return animating || still_loose;
}
