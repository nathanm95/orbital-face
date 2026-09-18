#include "orbital.h"

Scene g_scene;

// -----------------------------------------------------------------------------
// Scene geometry. Everything derives from the real screen bounds, so the same
// code lays out a 144x168 aplite, a 180x180 round chalk and a 200x228 emery.
// -----------------------------------------------------------------------------

int32_t isqrt32(int32_t v) {
  if (v <= 0) return 0;
  int32_t x = v, y = (x + 1) / 2;
  while (y < x) { x = y; y = (x + v / x) / 2; }
  return x;
}

// Newton's method seeded from the bit length, so it lands in four or five
// iterations instead of walking down from v itself.
int32_t isqrt64(int64_t v) {
  if (v <= 0) return 0;
  int bits = 0;
  for (int64_t t = v; t; t >>= 1) bits++;
  int64_t x = (int64_t)1 << ((bits + 1) / 2);
  for (int i = 0; i < 8; i++) {
    int64_t nx = (x + v / x) >> 1;
    if (nx >= x) break;
    x = nx;
  }
  return (int32_t)x;
}

GColor fade_color(GColor c, int num, int den) {
  if (den <= 0) return GColorBlack;
  if (num >= den) return c;
  if (num <= 0) return GColorBlack;
#ifdef PBL_COLOR
  // GColor8 carries two bits per channel, so a fade is just a per-channel
  // rescale — no alpha blending needed, and it stays inside the 64-colour cube.
  GColor out = c;
  out.r = (uint8_t)((c.r * num) / den);
  out.g = (uint8_t)((c.g * num) / den);
  out.b = (uint8_t)((c.b * num) / den);
  return out;
#else
  // On black-and-white hardware a fade can only be "still there" or "gone".
  return (num * 2 >= den) ? c : GColorBlack;
#endif
}

// How many planets are actually on screen, honouring the outer-planets setting.
static int visible_count(void) {
  return g_settings.outer_planets ? PLANET_COUNT : PL_URANUS;
}

void scene_layout(GRect bounds) {
  g_scene.bounds = bounds;
  g_scene.centre = GPoint(bounds.size.w / 2, bounds.size.h / 2);

  int16_t radius = (bounds.size.w < bounds.size.h ? bounds.size.w : bounds.size.h) / 2;
  g_scene.sun_r = radius / 9;
  if (g_scene.sun_r < 5) g_scene.sun_r = 5;

  // Round displays have no corners to spend, so the outermost ring has to sit
  // further in or it runs straight into the bezel.
  g_scene.r_min = g_scene.sun_r + 10;
  g_scene.r_max = radius - PBL_IF_ROUND_ELSE(9, 5);
  g_scene.max_r = radius - PBL_IF_ROUND_ELSE(6, 2);

  int n = visible_count();
  for (int i = 0; i < PLANET_COUNT; i++) {
    int slot = (i < n) ? i : n - 1;
    g_scene.body[i].ring_r = (n <= 1)
      ? g_scene.r_min
      : g_scene.r_min + (g_scene.r_max - g_scene.r_min) * slot / (n - 1);
  }

  g_scene.intro = g_settings.intro_anim ? 0 : 255;
}

void scene_update_positions(time_t now) {
  PlanetPos pos[PLANET_COUNT];
  astro_positions(now, pos);
  astro_moon(now, &g_scene.moon_phase, &g_scene.moon_illum);

  for (int i = 0; i < PLANET_COUNT; i++) {
    Body *b = &g_scene.body[i];

    // The planet sits at r = R(1 - e.cos(L - peri)), i.e. on an ellipse with
    // the Sun at one focus. To first order in e that is a circle of radius R
    // whose centre is pushed R.e toward aphelion — which is how the orbit path
    // gets drawn below, so dot and path always agree.
    int32_t r_disp = ((int32_t)b->ring_r * pos[i].r_q16) / 65536;

    int32_t c = cos_lookup(pos[i].longitude);
    int32_t s = sin_lookup(pos[i].longitude);
    b->base.x = (int16_t)(g_scene.centre.x + (r_disp * c) / TRIG_MAX_RATIO);
    b->base.y = (int16_t)(g_scene.centre.y - (r_disp * s) / TRIG_MAX_RATIO);

    int32_t peri, ecc_q16;
    astro_orbit_shape(i, &peri, &ecc_q16);
    int32_t off = ((int32_t)b->ring_r * ecc_q16) / 65536;
    b->ring_c.x = (int16_t)(g_scene.centre.x - (off * cos_lookup(peri)) / TRIG_MAX_RATIO);
    b->ring_c.y = (int16_t)(g_scene.centre.y + (off * sin_lookup(peri)) / TRIG_MAX_RATIO);

    if (!b->loose) {
      b->px = b->base.x << 16;
      b->py = b->base.y << 16;
      b->shown = b->base;
    }
  }
}
