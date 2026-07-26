#include <pebble.h>
#include <math.h>

// Trig is done in single precision on purpose: double-precision libm
// (sin/cos/atan2) is very stack-hungry on Pebble's FPU-less core and will
// fault the app. Float is far more accurate than a dot on the screen needs.
#define DEG2RAD_F 0.0174532925f
#define RAD2DEG_F 57.2957795f

// Shake-to-play: the system reacts to wrist movement for this long.
#define PLAY_MS   30000
#define FRAME_MS  33      // ~30fps, only while interacting

// Physics
#define SPRING   0.030f   // pull back toward the true orbital position
#define DAMPING  0.90f    // velocity bleed per frame

// -----------------------------------------------------------------------------
// Real planetary positions (Paul Schlyter's low-precision method).
//
// Every orbital element is a linear function of `d` = days since 1999-12-31.0 UT.
// From those we solve Kepler's equation and rotate into ecliptic coordinates to
// get each planet's true heliocentric longitude — its real angle around the Sun
// today. No network or ephemeris tables required; the RTC date is enough.
// -----------------------------------------------------------------------------

typedef struct {
  const char *name;
  // N: longitude of ascending node   i: inclination   w: argument of perihelion
  // a: semi-major axis (AU)          e: eccentricity   M: mean anomaly
  // each given as base value + per-day rate
  float N0, Nr, i0, ir, w0, wr, a0, ar, e0, er, M0, Mr;
  bool   earth;   // Earth is derived from the Sun's elements (+180deg)
  GColor color;
  uint8_t dot;    // draw radius
  float  react;   // how strongly it answers to a shake (lighter = larger)
} PlanetDef;

// Elements straight from Schlyter. Earth uses the Sun's elements with earth=true.
static const PlanetDef PLANETS[] = {
  { "Mercury",
    48.3313, 3.24587E-5, 7.0047, 5.00E-8, 29.1241, 1.01444E-5,
    0.387098, 0.0, 0.205635, 5.59E-10, 168.6562, 4.0923344368,
    false, {GColorLightGrayARGB8}, 3, 0.00090f },
  { "Venus",
    76.6799, 2.46590E-5, 3.3946, 2.75E-8, 54.8910, 1.38374E-5,
    0.723330, 0.0, 0.006773, -1.302E-9, 48.0052, 1.6021302244,
    false, {GColorPastelYellowARGB8}, 3, 0.00075f },
  { "Earth",   // Sun's elements; heliocentric Earth = Sun longitude + 180
    0.0, 0.0, 0.0, 0.0, 282.9404, 4.70935E-5,
    1.000000, 0.0, 0.016709, -1.151E-9, 356.0470, 0.9856002585,
    true, {GColorVividCeruleanARGB8}, 3, 0.00070f },
  { "Mars",
    49.5574, 2.11081E-5, 1.8497, -1.78E-8, 286.5016, 2.92961E-5,
    1.523688, 0.0, 0.093405, 2.516E-9, 18.6021, 0.5240207766,
    false, {GColorRedARGB8}, 3, 0.00065f },
  { "Jupiter",
    100.4542, 2.76854E-5, 1.3030, -1.557E-7, 273.8777, 1.64505E-5,
    5.20256, 0.0, 0.048498, 4.469E-9, 19.8950, 0.0830853001,
    false, {GColorRajahARGB8}, 6, 0.00035f },
  { "Saturn",
    113.6634, 2.38980E-5, 2.4886, -1.081E-7, 339.3939, 2.97661E-5,
    9.55475, 0.0, 0.055546, -9.499E-9, 316.9670, 0.0334442282,
    false, {GColorIcterineARGB8}, 5, 0.00030f },
  { "Uranus",
    74.0005, 1.3978E-5, 0.7733, 1.9E-8, 96.6612, 3.0565E-5,
    19.18171, -1.55E-8, 0.047318, 7.45E-9, 142.5905, 0.011725806,
    false, {GColorCelesteARGB8}, 4, 0.00028f },
  { "Neptune",
    131.7806, 3.0173E-5, 1.7700, -2.55E-7, 272.8461, -6.027E-6,
    30.05826, 3.313E-8, 0.008606, 2.15E-9, 260.2471, 0.005995147,
    false, {GColorBlueMoonARGB8}, 4, 0.00026f }
};
#define PLANET_COUNT (int)(sizeof(PLANETS) / sizeof(PLANETS[0]))

typedef struct {
  float ox, oy;   // shake offset from the true orbital position
  float vx, vy;   // velocity
} Orbit;

static Window *s_window;
static Layer *s_face_layer;
static TextLayer *s_time_layer;

static AppTimer *s_frame_timer = NULL;
static Orbit s_orbit[PLANET_COUNT];

// Cached base screen positions — computed off the render path so the draw
// callback does no floating-point at all.
static GPoint s_base[PLANET_COUNT];

// Interactive (shake) state
static bool s_playing = false;
static int  s_play_ms = 0;

// Geometry derived from the real screen size at load (resolution independent)
static GPoint s_center;
static int    s_sun_r;
static int    s_r_min, s_r_max;
static float  s_max_off;

// Fixed background stars (fractions of the screen, so they scale)
static const GPoint STAR_FRAC[] = {
  {11, 15}, {81, 13}, {25, 81}, {73, 77}, {46, 8},
  {91, 46}, {9, 51}, {65, 27}, {35, 23}, {77, 62}
};
#define STAR_COUNT (int)(sizeof(STAR_FRAC) / sizeof(STAR_FRAC[0]))

// --- Astronomy ---

__attribute__((unused))
static float revf(float x) {
  x = fmodf(x, 360.0f);
  if (x < 0.0f) x += 360.0f;
  return x;
}

// True heliocentric ecliptic longitude of a planet, in degrees, for day d.
__attribute__((unused))
static float planet_longitude(const PlanetDef *p, float d) {
  float N = p->N0 + p->Nr * d;
  float i = p->i0 + p->ir * d;
  float w = p->w0 + p->wr * d;
  float a = p->a0 + p->ar * d;
  float e = p->e0 + p->er * d;
  float M = revf(p->M0 + p->Mr * d);

  // Solve Kepler's equation for the eccentric anomaly E (degrees).
  float E = M + RAD2DEG_F * e * sinf(M * DEG2RAD_F) * (1.0f + e * cosf(M * DEG2RAD_F));
  for (int it = 0; it < 2; it++) {
    E = E - (E - RAD2DEG_F * e * sinf(E * DEG2RAD_F) - M)
            / (1.0f - e * cosf(E * DEG2RAD_F));
  }

  // Position in the orbital plane → true anomaly v and distance r.
  float xv = a * (cosf(E * DEG2RAD_F) - e);
  float yv = a * sqrtf(1.0f - e * e) * sinf(E * DEG2RAD_F);
  float v = atan2f(yv, xv) * RAD2DEG_F;
  float r = sqrtf(xv * xv + yv * yv);

  // Rotate into ecliptic coordinates.
  float vw = (v + w) * DEG2RAD_F;
  float Nr = N * DEG2RAD_F;
  float ir = i * DEG2RAD_F;
  float xh = r * (cosf(Nr) * cosf(vw) - sinf(Nr) * sinf(vw) * cosf(ir));
  float yh = r * (sinf(Nr) * cosf(vw) + cosf(Nr) * sinf(vw) * cosf(ir));

  float lon = atan2f(yh, xh) * RAD2DEG_F;
  if (p->earth) lon += 180.0f;   // Sun→Earth is the opposite direction
  return revf(lon);
}

// Days since 1999-12-31.0 UT, straight from the epoch — no calendar math needed.
__attribute__((unused))
static float current_day_number(void) {
  return (float)time(NULL) / 86400.0f - 10956.0f;
}

static int ring_radius(int index);   // defined below; used to place the dots

// DIAGNOSTIC BUILD: zero floating point. Planets are placed at fixed, evenly
// spaced angles using Pebble's integer sin_lookup/cos_lookup. This is NOT the
// real orrery yet — it exists to test whether the float trig was the crash.
// If this launches, floating point was the culprit and we rebuild real
// positions in fixed-point next.
static void recompute_positions(void) {
  for (int i = 0; i < PLANET_COUNT; i++) {
    int32_t angle = TRIG_MAX_ANGLE * i / PLANET_COUNT;
    int rr = ring_radius(i);
    s_base[i].x = (int16_t)(s_center.x + rr * sin_lookup(angle) / TRIG_MAX_RATIO);
    s_base[i].y = (int16_t)(s_center.y - rr * cos_lookup(angle) / TRIG_MAX_RATIO);
  }
}

// --- Geometry ---

static int ring_radius(int index) {
  if (PLANET_COUNT <= 1) return s_r_min;
  return s_r_min + (s_r_max - s_r_min) * index / (PLANET_COUNT - 1);
}

// Sun colour shifts across the day.
static GColor sun_tint(int hour) {
  if (hour < 5)  return GColorLavenderIndigo;
  if (hour < 7)  return GColorSunsetOrange;
  if (hour < 10) return GColorChromeYellow;
  if (hour < 16) return GColorYellow;
  if (hour < 19) return GColorOrange;
  if (hour < 21) return GColorSunsetOrange;
  return GColorLavenderIndigo;
}

// --- Drawing ---

static void draw_planet(GContext *ctx, GPoint p, int r, GColor color, bool ringed) {
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_circle(ctx, GPoint(p.x + 1, p.y + 1), r);
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, p, r);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(p.x - r / 3, p.y - r / 3), r / 3 + 1);

  if (ringed) {   // Saturn's ring, sketched as a thin oval
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, p, r + 3);
  }
}

static void face_layer_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);

  // Space background
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  // Stars, positioned as fractions of the screen
  graphics_context_set_fill_color(ctx, GColorLightGray);
  for (int i = 0; i < STAR_COUNT; i++) {
    int sx = STAR_FRAC[i].x * b.size.w / 100;
    int sy = STAR_FRAC[i].y * b.size.h / 100;
    graphics_fill_circle(ctx, GPoint(sx, sy), (i % 3 == 0) ? 2 : 1);
  }

  // Orbit paths
  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  graphics_context_set_stroke_width(ctx, 1);
  for (int i = 0; i < PLANET_COUNT; i++) {
    graphics_draw_circle(ctx, s_center, ring_radius(i));
  }

  // Sun, tinted by time of day
  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  GColor tint = sun_tint(lt->tm_hour);
  graphics_context_set_fill_color(ctx, tint);
  graphics_fill_circle(ctx, s_center, s_sun_r + 3);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, s_center, s_sun_r - 3);
  graphics_context_set_fill_color(ctx, tint);
  graphics_fill_circle(ctx, s_center, s_sun_r - 7);

  // Planets at their pre-computed positions — no trig in the render path
  for (int i = 0; i < PLANET_COUNT; i++) {
    GPoint pt = GPoint(s_base[i].x + (int)s_orbit[i].ox,
                       s_base[i].y + (int)s_orbit[i].oy);
    bool ringed = (PLANETS[i].name[0] == 'S');   // Saturn
    draw_planet(ctx, pt, PLANETS[i].dot, PLANETS[i].color, ringed);
  }
}

// --- Physics (shake mode) ---

static void update_orbit(Orbit *o, int ax, int ay, float react, bool apply) {
  float fx = 0.0f, fy = 0.0f;
  if (apply) {
    fx += ax * react;
    fy += -ay * react;   // accel +y points up; screen +y points down
  }
  fx += -SPRING * o->ox;
  fy += -SPRING * o->oy;

  o->vx = (o->vx + fx) * DAMPING;
  o->vy = (o->vy + fy) * DAMPING;
  o->ox += o->vx;
  o->oy += o->vy;

  float mag = sqrtf(o->ox * o->ox + o->oy * o->oy);
  if (mag > s_max_off) {
    o->ox = o->ox / mag * s_max_off;
    o->oy = o->oy / mag * s_max_off;
  }
}

static bool orbit_moving(const Orbit *o) {
  return fabsf(o->ox) > 0.6f || fabsf(o->oy) > 0.6f
      || fabsf(o->vx) > 0.08f || fabsf(o->vy) > 0.08f;
}

static void frame_timer_callback(void *data) {
  bool apply = s_play_ms > 0;
  if (apply) s_play_ms -= FRAME_MS;

  AccelData accel = {0};
  if (apply) accel_service_peek(&accel);

  bool moving = false;
  for (int i = 0; i < PLANET_COUNT; i++) {
    update_orbit(&s_orbit[i], accel.x, accel.y, PLANETS[i].react, apply);
    if (orbit_moving(&s_orbit[i])) moving = true;
  }

  layer_mark_dirty(s_face_layer);

  if (apply || moving) {
    s_frame_timer = app_timer_register(FRAME_MS, frame_timer_callback, NULL);
  } else {
    s_frame_timer = NULL;
    s_playing = false;
    for (int i = 0; i < PLANET_COUNT; i++) {
      s_orbit[i] = (Orbit){0};
    }
    layer_mark_dirty(s_face_layer);
  }
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
  s_play_ms = PLAY_MS;
  if (!s_playing) {
    s_playing = true;
    if (!s_frame_timer) {
      s_frame_timer = app_timer_register(FRAME_MS, frame_timer_callback, NULL);
    }
  }
}

// --- Time ---

static void update_time(struct tm *t) {
  static char buf[8];
  if (clock_is_24h_style()) {
    strftime(buf, sizeof(buf), "%H:%M", t);
  } else {
    strftime(buf, sizeof(buf), "%I:%M", t);
  }
  text_layer_set_text(s_time_layer, buf);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time(tick_time);
  recompute_positions();              // cheap once a minute, safe (shallow stack)
  if (!s_playing) {
    layer_mark_dirty(s_face_layer);   // planets drift + sun re-tints
  }
}

// --- Window ---

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);

  // Derive all geometry from the real screen — works on chalk, emery, 260, etc.
  s_center = GPoint(b.size.w / 2, b.size.h / 2);
  int radius = (b.size.w < b.size.h ? b.size.w : b.size.h) / 2;
  s_sun_r  = radius / 8;
  s_r_min  = s_sun_r + 16;   // keep the inner planets clear of the sun's glow
  s_r_max  = radius - 6;
  s_max_off = radius * 0.6f;

  recompute_positions();     // seed the cache before the first draw

  s_face_layer = layer_create(b);
  layer_set_update_proc(s_face_layer, face_layer_update);
  layer_add_child(root, s_face_layer);

  // Large digital time below the sun
  s_time_layer = text_layer_create(
    GRect(s_center.x - 70, s_center.y + s_sun_r + 4, 140, 44));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorWhite);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_time_layer));

  time_t now = time(NULL);
  update_time(localtime(&now));

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  accel_tap_service_subscribe(tap_handler);
}

static void window_unload(Window *window) {
  if (s_frame_timer) {
    app_timer_cancel(s_frame_timer);
    s_frame_timer = NULL;
  }
  accel_tap_service_unsubscribe();
  tick_timer_service_unsubscribe();
  layer_destroy(s_face_layer);
  text_layer_destroy(s_time_layer);
}

int main(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, false);   // non-animated: shallower launch stack
  app_event_loop();
  window_destroy(s_window);
  return 0;
}
