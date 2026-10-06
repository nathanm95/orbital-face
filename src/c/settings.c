#include "orbital.h"

Settings g_settings;

// -----------------------------------------------------------------------------
// Settings live as one packed struct under one persist key. The version byte is
// checked on load so that a struct written by an older build is thrown away
// rather than reinterpreted field-by-field into nonsense.
// -----------------------------------------------------------------------------

static void settings_defaults(void) {
  g_settings = (Settings){
    .version          = SETTINGS_VERSION,

    .time_format      = TIMEFMT_AUTO,
    .leading_zero     = true,
    .time_pos         = TIMEPOS_BOTTOM,
    .date_format      = DATE_DOW_DAY,
    .battery_style    = BATT_LOW_ONLY,
    .battery_low_pct  = 20,
    .show_bt          = true,
    .vibe_bt          = false,
    .show_steps       = false,

    .col_bg           = { GColorBlackARGB8 },
    .col_time         = { GColorWhiteARGB8 },
    .col_sun          = { GColorYellowARGB8 },
    .col_orbit        = { PBL_IF_COLOR_ELSE(GColorDarkGrayARGB8, GColorWhiteARGB8) },
    .col_info         = { PBL_IF_COLOR_ELSE(GColorLightGrayARGB8, GColorWhiteARGB8) },
    .sun_auto_tint    = true,

    .show_orbits      = true,
    .star_density     = 2,
    .show_trails      = true,
    .trail_length     = 12,
    .show_moon        = true,
    .highlight_earth  = true,
    .outer_planets    = true,
    .planet_size      = 1,

    .physics_enabled  = true,
    .fling_strength   = 60,
    .gravity_strength = 50,
    .recapture_secs   = 20,
    .nudge_minute     = false,

    .intro_anim       = true,
    .minute_pulse     = true,
  };
}

#ifdef ORBITAL_SHOW_TIME
// Store screenshots. Each preset shows off a different corner of the settings.
// Black-and-white watches only get the layout changes, because every colour
// collapses to black or white there anyway.
static void store_preset(void) {
#ifdef ORBITAL_DEMO_FLING
  g_settings.recapture_secs = 9;   // so the preview GIF holds a whole fling and return
#endif
#if ORBITAL_PRESET == 1
  // "Nebula": navy and ice, clock on top, dense stars, large planets.
  g_settings.time_pos      = TIMEPOS_TOP;
  g_settings.date_format   = DATE_DAY_MON;
  g_settings.battery_style = BATT_PERCENT;
  g_settings.star_density  = 3;
  g_settings.planet_size   = 2;
#ifdef PBL_COLOR
  g_settings.col_bg        = GColorOxfordBlue;
  g_settings.col_time      = GColorCeleste;
  g_settings.col_orbit     = GColorCobaltBlue;
  g_settings.col_info      = GColorPictonBlue;
  g_settings.sun_auto_tint = false;
  g_settings.col_sun       = GColorChromeYellow;
#endif
#elif ORBITAL_PRESET == 2
  // "Ember": no orbit paths, inner six planets, battery as an edge ring.
  g_settings.show_orbits   = false;
  g_settings.outer_planets = false;
  g_settings.star_density  = 1;
  g_settings.battery_style = BATT_ARC;
  g_settings.date_format   = DATE_OFF;
#ifdef PBL_COLOR
  g_settings.col_time      = GColorChromeYellow;
  g_settings.col_info      = GColorRajah;
  g_settings.sun_auto_tint = false;
  g_settings.col_sun       = GColorOrange;
#endif
#endif
}
#endif

void settings_init(void) {
  settings_defaults();

#ifdef ORBITAL_SHOW_TIME
  // Store builds start from known settings, whatever an earlier test left in
  // the emulator's flash.
  store_preset();
#else
  if (persist_exists(SETTINGS_KEY)) {
    Settings stored;
    int read = persist_read_data(SETTINGS_KEY, &stored, sizeof(stored));
    if (read == (int)sizeof(stored) && stored.version == SETTINGS_VERSION) {
      g_settings = stored;
    }
    // Any mismatch (older version, different size) falls through to defaults,
    // which are then re-saved below so the next launch is a clean hit.
  }
#endif
}

void settings_save(void) {
  g_settings.version = SETTINGS_VERSION;
  persist_write_data(SETTINGS_KEY, &g_settings, sizeof(g_settings));
}

// --- Inbox --------------------------------------------------------------------

static bool read_bool(DictionaryIterator *it, uint32_t key, bool *dst) {
  Tuple *t = dict_find(it, key);
  if (!t) return false;
  *dst = (t->value->int32 != 0);
  return true;
}

static bool read_u8(DictionaryIterator *it, uint32_t key, uint8_t *dst, int lo, int hi) {
  Tuple *t = dict_find(it, key);
  if (!t) return false;
  int32_t v = t->value->int32;
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  *dst = (uint8_t)v;
  return true;
}

static bool read_colour(DictionaryIterator *it, uint32_t key, GColor *dst) {
  Tuple *t = dict_find(it, key);
  if (!t) return false;
  *dst = GColorFromHEX(t->value->int32);
  return true;
}

void settings_handle_inbox(DictionaryIterator *iter) {
  read_u8   (iter, MESSAGE_KEY_TimeFormat,      &g_settings.time_format, 0, 2);
  read_bool (iter, MESSAGE_KEY_LeadingZero,     &g_settings.leading_zero);
  read_u8   (iter, MESSAGE_KEY_TimePos,         &g_settings.time_pos, 0, 2);
  read_u8   (iter, MESSAGE_KEY_DateFormat,      &g_settings.date_format, 0, 4);
  read_u8   (iter, MESSAGE_KEY_BatteryStyle,    &g_settings.battery_style, 0, 3);
  read_u8   (iter, MESSAGE_KEY_BatteryLowPct,   &g_settings.battery_low_pct, 5, 50);
  read_bool (iter, MESSAGE_KEY_ShowBT,          &g_settings.show_bt);
  read_bool (iter, MESSAGE_KEY_VibeBT,          &g_settings.vibe_bt);
  read_bool (iter, MESSAGE_KEY_ShowSteps,       &g_settings.show_steps);

  read_colour(iter, MESSAGE_KEY_ColBG,          &g_settings.col_bg);
  read_colour(iter, MESSAGE_KEY_ColTime,        &g_settings.col_time);
  read_colour(iter, MESSAGE_KEY_ColSun,         &g_settings.col_sun);
  read_colour(iter, MESSAGE_KEY_ColOrbit,       &g_settings.col_orbit);
  read_colour(iter, MESSAGE_KEY_ColInfo,        &g_settings.col_info);
  read_bool (iter, MESSAGE_KEY_SunAutoTint,     &g_settings.sun_auto_tint);

  read_bool (iter, MESSAGE_KEY_ShowOrbits,      &g_settings.show_orbits);
  read_u8   (iter, MESSAGE_KEY_StarDensity,     &g_settings.star_density, 0, 3);
  read_bool (iter, MESSAGE_KEY_ShowTrails,      &g_settings.show_trails);
  read_u8   (iter, MESSAGE_KEY_TrailLength,     &g_settings.trail_length, 0, MAX_TRAIL);
  read_bool (iter, MESSAGE_KEY_ShowMoon,        &g_settings.show_moon);
  read_bool (iter, MESSAGE_KEY_HighlightEarth,  &g_settings.highlight_earth);
  read_bool (iter, MESSAGE_KEY_OuterPlanets,    &g_settings.outer_planets);
  read_u8   (iter, MESSAGE_KEY_PlanetSize,      &g_settings.planet_size, 0, 2);

  read_bool (iter, MESSAGE_KEY_PhysicsEnabled,  &g_settings.physics_enabled);
  read_u8   (iter, MESSAGE_KEY_FlingStrength,   &g_settings.fling_strength, 0, 100);
  read_u8   (iter, MESSAGE_KEY_GravityStrength, &g_settings.gravity_strength, 0, 100);
  read_u8   (iter, MESSAGE_KEY_RecaptureSecs,   &g_settings.recapture_secs, 3, 60);
  read_bool (iter, MESSAGE_KEY_NudgeMinute,     &g_settings.nudge_minute);

  read_bool (iter, MESSAGE_KEY_IntroAnim,       &g_settings.intro_anim);
  read_bool (iter, MESSAGE_KEY_MinutePulse,     &g_settings.minute_pulse);

  settings_save();
}
