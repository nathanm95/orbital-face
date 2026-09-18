#pragma once
#include <pebble.h>

// -----------------------------------------------------------------------------
// Orbital — a solar-system watchface you can knock about.
//
// Three layers, kept deliberately separate:
//   astro    real heliocentric positions, pure integer maths
//   physics  the toy: gravity, flinging, re-capture
//   render   everything that touches a GContext
//
// Nothing outside astro.c does floating point on the astronomy. That is the
// whole point: the previous build crashed on double-precision libm at launch
// and shipped a fake, frozen sky as a workaround. See README.
// -----------------------------------------------------------------------------

#define PLANET_COUNT 8

// Index into the planet tables. Order matters: it is sun-outward.
typedef enum {
  PL_MERCURY = 0, PL_VENUS, PL_EARTH, PL_MARS,
  PL_JUPITER, PL_SATURN, PL_URANUS, PL_NEPTUNE
} PlanetId;

// --- Settings -----------------------------------------------------------------
// One packed struct, persisted under a single key with a version byte so an
// older stored blob is discarded cleanly rather than read as garbage.

#define SETTINGS_VERSION 1
#define SETTINGS_KEY     100

typedef enum { TIMEFMT_AUTO = 0, TIMEFMT_12, TIMEFMT_24 } TimeFormat;
typedef enum { DATE_OFF = 0, DATE_DOW_DAY, DATE_DAY_MON, DATE_NUMERIC, DATE_MON_DAY } DateFormat;
typedef enum { BATT_OFF = 0, BATT_PERCENT, BATT_ARC, BATT_LOW_ONLY } BatteryStyle;
typedef enum { TIMEPOS_BOTTOM = 0, TIMEPOS_CENTRE, TIMEPOS_TOP } TimePos;

typedef struct {
  uint8_t version;

  // Time & info
  uint8_t time_format;     // TimeFormat
  bool    leading_zero;
  uint8_t time_pos;        // TimePos
  uint8_t date_format;     // DateFormat
  uint8_t battery_style;   // BatteryStyle
  uint8_t battery_low_pct;
  bool    show_bt;
  bool    vibe_bt;
  bool    show_steps;

  // Colours
  GColor  col_bg;
  GColor  col_time;
  GColor  col_sun;
  GColor  col_orbit;
  GColor  col_info;
  bool    sun_auto_tint;   // overrides col_sun with a time-of-day tint

  // Scene
  bool    show_orbits;
  uint8_t star_density;    // 0 none .. 3 dense
  bool    show_trails;
  uint8_t trail_length;    // 0..MAX_TRAIL
  bool    show_moon;
  bool    highlight_earth;
  bool    outer_planets;   // false hides Uranus + Neptune
  uint8_t planet_size;     // 0 small, 1 normal, 2 large

  // Physics
  bool    physics_enabled;
  uint8_t fling_strength;  // 0..100
  uint8_t gravity_strength;// 0..100
  uint8_t recapture_secs;  // how long before they settle back
  bool    nudge_minute;    // a gentle automatic fling on each minute tick

  // Animation
  bool    intro_anim;
  bool    minute_pulse;
} Settings;

extern Settings g_settings;

void settings_init(void);
void settings_save(void);
void settings_handle_inbox(DictionaryIterator *iter);

// --- Astronomy ----------------------------------------------------------------

typedef struct {
  int32_t longitude;  // heliocentric ecliptic longitude, Pebble trig units
  int32_t r_q16;      // distance as a fraction of the orbit's mean radius, Q16
} PlanetPos;

// Fill `out` with every planet's position for the given unix time.
void astro_positions(time_t utc, PlanetPos out[PLANET_COUNT]);

// Moon phase 0..359 (0 = new, 180 = full) and illuminated fraction 0..100.
void astro_moon(time_t utc, int32_t *phase_deg, int *illum_pct);

// Perihelion longitude (trig units) and eccentricity (Q16), for drawing paths.
void astro_orbit_shape(int i, int32_t *peri, int32_t *ecc_q16);

// --- Scene geometry -----------------------------------------------------------

#define MAX_TRAIL 24

typedef struct {
  GPoint  base;                  // where the planet truly is right now
  int16_t ring_r;                // mean display radius of its orbit
  GPoint  ring_c;                // orbit-circle centre (offset from the sun,
                                 // which is what makes the ellipse read)
  // Free-flight state, Q16 pixels. Q16 rather than something coarser is not
  // fussiness: at Q8 the gravity components round to zero near the axes on the
  // outer rings, the orbit degenerates into a polygon and gains energy.
  int32_t px, py, vx, vy;
  GPoint  shown;                 // where it is drawn, after the recapture blend
  bool    loose;                 // has been knocked off its true position

  GPoint  trail[MAX_TRAIL];
  uint8_t trail_n, trail_head;
} Body;

typedef struct {
  GRect   bounds;
  GPoint  centre;
  int16_t sun_r;
  int16_t r_min, r_max;
  int16_t max_r;                 // hard edge; nothing is allowed past this
  Body    body[PLANET_COUNT];

  // moon
  int32_t moon_phase;
  int     moon_illum;

  // transient animation state
  int32_t intro;                 // 0..255, 255 = fully arrived
  int32_t sun_pulse;             // 0..255, decays after a minute tick
  int32_t flare;                 // 0..255, a planet grazed the sun
  int32_t settle;                // 0..255 recapture progress, for the UI hint
  uint32_t frame;                // free-running, drives twinkle and breathing
} Scene;

extern Scene g_scene;

void scene_layout(GRect bounds);
void scene_update_positions(time_t now);   // recompute the true positions

// --- Physics ------------------------------------------------------------------

void physics_reset(void);
void physics_fling(int16_t ax, int16_t ay);
bool physics_step(void);                   // returns true while still animating
bool physics_active(void);

// --- Render -------------------------------------------------------------------

void render_scene(Layer *layer, GContext *ctx);
GColor render_sun_tint(int hour);
void render_invalidate_text(void);   // time/date strings need rebuilding

// --- Small helpers ------------------------------------------------------------

// Scale a colour toward black. num/den of 0 gives black, den/den the original.
GColor fade_color(GColor c, int num, int den);
int32_t isqrt32(int32_t v);
int32_t isqrt64(int64_t v);
