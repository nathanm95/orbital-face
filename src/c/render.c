#include "orbital.h"

// -----------------------------------------------------------------------------
// Everything that touches a GContext.
//
// Draw order is back to front: background, stars, orbit paths, trails, Sun,
// planets, moon, then the text furniture on top. The text is drawn last and
// outlined, because with physics running a planet can end up anywhere,
// including directly behind the hour digits.
// -----------------------------------------------------------------------------

typedef struct {
  GColor  color;
  uint8_t dot;        // base radius at the "normal" size setting
  bool    ringed;
} PlanetStyle;

static const PlanetStyle STYLE[PLANET_COUNT] = {
  { { GColorLightGrayARGB8     }, 2, false },  // Mercury
  { { GColorPastelYellowARGB8  }, 3, false },  // Venus
  { { GColorVividCeruleanARGB8 }, 3, false },  // Earth
  { { GColorRedARGB8           }, 3, false },  // Mars
  { { GColorRajahARGB8         }, 6, false },  // Jupiter
  { { GColorIcterineARGB8      }, 5, true  },  // Saturn
  { { GColorCelesteARGB8       }, 4, false },  // Uranus
  { { GColorBlueMoonARGB8      }, 4, false },  // Neptune
};

static char s_time_buf[8];
static char s_date_buf[16];
static char s_info_buf[16];
static bool s_text_dirty = true;

void render_invalidate_text(void) { s_text_dirty = true; }

GColor render_sun_tint(int hour) {
  if (hour < 5)  return GColorLavenderIndigo;
  if (hour < 7)  return GColorSunsetOrange;
  if (hour < 10) return GColorChromeYellow;
  if (hour < 16) return GColorYellow;
  if (hour < 19) return GColorOrange;
  if (hour < 21) return GColorSunsetOrange;
  return GColorLavenderIndigo;
}

static int planet_radius(int i) {
  int r = STYLE[i].dot;
  if (g_settings.planet_size == 0) r = (r * 2 + 2) / 3;
  else if (g_settings.planet_size == 2) r = r + 2;
  return r < 1 ? 1 : r;
}

static bool planet_visible(int i) {
  if (!g_settings.outer_planets && i >= PL_URANUS) return false;
  return true;
}

// --- Stars --------------------------------------------------------------------

// Deterministic pseudo-random field. Regenerating it every frame costs less
// than storing it, and it lets the density setting change with no reallocation.
static void draw_stars(GContext *ctx, GRect b) {
  static const int COUNT[4] = { 0, 10, 22, 40 };
  int n = COUNT[g_settings.star_density > 3 ? 3 : g_settings.star_density];
  if (n == 0) return;

  uint32_t seed = 0x9E3779B9;
  for (int i = 0; i < n; i++) {
    seed = seed * 1664525u + 1013904223u;
    int x = (int)((seed >> 16) % (uint32_t)b.size.w);
    seed = seed * 1664525u + 1013904223u;
    int y = (int)((seed >> 16) % (uint32_t)b.size.h);

    // Twinkle: each star breathes on its own phase, slowly.
    int32_t phase = ((int32_t)g_scene.frame * 700 + i * 8000) & 0xFFFF;
    int32_t tw = sin_lookup(phase);                      // -65536..65536
    int level = 3 + (int)(tw / 32768);                   // 2..4, out of 4

    graphics_context_set_fill_color(ctx, fade_color(GColorWhite, level, 4));
    graphics_fill_circle(ctx, GPoint(x, y), (i % 4 == 0) ? 1 : 0);
  }
}

// An orbit path. On colour hardware a thin grey circle recedes properly; with
// one bit there is no grey, so a solid ring would shout louder than the planets
// on it. Dotting it gives back the sense of a faint line.
static void draw_orbit_ring(GContext *ctx, GPoint c, int r) {
#ifdef PBL_COLOR
  graphics_draw_circle(ctx, c, r);
#else
  int n = r * 2 / 3;                       // roughly one dot every 9 pixels
  if (n < 10) n = 10;
  for (int i = 0; i < n; i++) {
    int32_t a = TRIG_MAX_ANGLE * i / n;
    graphics_fill_circle(ctx,
      GPoint(c.x + r * cos_lookup(a) / TRIG_MAX_RATIO,
             c.y + r * sin_lookup(a) / TRIG_MAX_RATIO), 0);
  }
#endif
}

// --- Sun ----------------------------------------------------------------------

static void draw_sun(GContext *ctx, GColor tint) {
  int r = g_scene.sun_r;

  // The corona breathes continuously, and a minute tick or a planet grazing the
  // surface push it out further for a moment.
  int32_t breathe = sin_lookup(((int32_t)g_scene.frame * 900) & 0xFFFF) / 16384;  // -4..4
  int bump = (int)(g_scene.sun_pulse / 64) + (int)(g_scene.flare / 48);
  int corona = r + 3 + (int)breathe + bump;

#ifdef PBL_COLOR
  // Four shells, dimmest outward. The white core is kept small on purpose —
  // make it any bigger and the Sun reads as a pearl rather than a star.
  graphics_context_set_fill_color(ctx, fade_color(tint, 1, 3));
  graphics_fill_circle(ctx, g_scene.centre, corona);
  graphics_context_set_fill_color(ctx, fade_color(tint, 2, 3));
  graphics_fill_circle(ctx, g_scene.centre, r + 1);
  graphics_context_set_fill_color(ctx, tint);
  graphics_fill_circle(ctx, g_scene.centre, r);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, g_scene.centre, r / 3);
#else
  // A solid white disc would just be the biggest planet. A white annulus with
  // a hot core reads as a star and cannot be mistaken for one.
  (void)tint;
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, g_scene.centre, corona);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, g_scene.centre, r);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, g_scene.centre, r / 2);
#endif
}

// --- Planets ------------------------------------------------------------------

static void draw_planet(GContext *ctx, GPoint p, int r, GColor color, bool ringed) {
#ifdef PBL_COLOR
  if (ringed) {
    graphics_context_set_stroke_color(ctx, fade_color(color, 2, 3));
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, p, r + 3);
  }
  graphics_context_set_fill_color(ctx, fade_color(color, 1, 3));
  graphics_fill_circle(ctx, p, r);
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, GPoint(p.x - r / 4, p.y - r / 4), r - (r > 2 ? 1 : 0));
  if (r >= 3) {
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, GPoint(p.x - r / 2, p.y - r / 2), r / 3);
  }
#else
  // One bit of colour. Shading a planet is not an option, so a black halo
  // separates it from whatever it happens to be crossing, and size becomes the
  // only thing distinguishing the planets — which is what the style table's
  // radii are for.
  (void)color;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, p, r + 1);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, p, r);
  if (r >= 4) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, p, r - 2);
  }
  if (ringed) {
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, p, r + 3);
  }
#endif
}

static void draw_trail(GContext *ctx, const Body *b, GColor color) {
  if (b->trail_n == 0) return;
  for (int k = 0; k < b->trail_n; k++) {
    // walk oldest -> newest so newer dots overdraw older ones
    int idx = (b->trail_head - b->trail_n + k + 2 * MAX_TRAIL) % MAX_TRAIL;
    int age = b->trail_n - k;                   // 1 = newest

    // Quantise the fade to three bands rather than sweeping it smoothly.
    // Each colour channel carries only two bits, so anything much below a
    // third of full brightness rounds to black — a smooth ramp makes most of
    // the trail invisible, which is exactly what the first cut of this did.
    int band = 3 - (age - 1) * 3 / b->trail_n;
    if (band < 1) band = 1;

    graphics_context_set_fill_color(ctx, fade_color(color, band, 3));
    graphics_fill_circle(ctx, b->trail[idx], band >= 3 ? 1 : 0);
  }
}

// The Moon rides round Earth at its real elongation, so the dot you see beside
// Earth is on the same side of it as the real Moon is tonight.
static void draw_moon(GContext *ctx, GPoint earth, int earth_r) {
  int orbit = earth_r + 5;
  int32_t ang = (g_scene.moon_phase * 65536 / 360) & 0xFFFF;
  GPoint m = GPoint(earth.x + orbit * cos_lookup(ang) / TRIG_MAX_RATIO,
                    earth.y - orbit * sin_lookup(ang) / TRIG_MAX_RATIO);

  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_circle(ctx, m, 2);

  // Lit limb, faked by an offset disc — at two pixels across that reads as a
  // phase far better than trying to draw a real terminator.
  int shift = 2 - (g_scene.moon_illum * 4 / 100);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(m.x - shift, m.y), g_scene.moon_illum > 12 ? 1 : 0);
}

// --- Text furniture -----------------------------------------------------------

static void rebuild_text(struct tm *t) {
  bool h24 = (g_settings.time_format == TIMEFMT_24)
          || (g_settings.time_format == TIMEFMT_AUTO && clock_is_24h_style());

  strftime(s_time_buf, sizeof(s_time_buf), h24 ? "%H:%M" : "%I:%M", t);
  if (!g_settings.leading_zero && s_time_buf[0] == '0') {
    memmove(s_time_buf, s_time_buf + 1, strlen(s_time_buf));
  }

  switch (g_settings.date_format) {
    case DATE_DOW_DAY: strftime(s_date_buf, sizeof(s_date_buf), "%a %d", t); break;
    case DATE_DAY_MON: strftime(s_date_buf, sizeof(s_date_buf), "%d %b", t); break;
    case DATE_NUMERIC: strftime(s_date_buf, sizeof(s_date_buf), "%d/%m", t); break;
    case DATE_MON_DAY: strftime(s_date_buf, sizeof(s_date_buf), "%b %d", t); break;
    default:           s_date_buf[0] = '\0'; break;
  }

  s_info_buf[0] = '\0';
  BatteryChargeState batt = battery_state_service_peek();
  bool want_pct = (g_settings.battery_style == BATT_PERCENT)
               || (g_settings.battery_style == BATT_LOW_ONLY
                   && batt.charge_percent <= g_settings.battery_low_pct);
  if (want_pct) {
    snprintf(s_info_buf, sizeof(s_info_buf), "%d%%", batt.charge_percent);
  }
#if defined(PBL_HEALTH)
  if (g_settings.show_steps) {
    HealthValue steps = health_service_sum_today(HealthMetricStepCount);
    char tmp[12];
    snprintf(tmp, sizeof(tmp), "%ld", (long)steps);
    if (s_info_buf[0]) {
      strncat(s_info_buf, " ", sizeof(s_info_buf) - strlen(s_info_buf) - 1);
    }
    strncat(s_info_buf, tmp, sizeof(s_info_buf) - strlen(s_info_buf) - 1);
  }
#endif
  s_text_dirty = false;
}

// A one-pixel outline in the background colour. Cheap, and it is the only thing
// keeping the digits legible when a flung Jupiter parks itself behind them.
static void draw_text_outlined(GContext *ctx, const char *text, GFont font,
                               GRect box, GColor fg, GColor outline) {
  if (!text || !text[0]) return;
  static const int8_t OFF[8][2] = { {-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,-1},{-1,1},{1,1} };
  graphics_context_set_text_color(ctx, outline);
  for (int i = 0; i < 8; i++) {
    GRect r = box;
    r.origin.x += OFF[i][0];
    r.origin.y += OFF[i][1];
    graphics_draw_text(ctx, text, font, r, GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
  }
  graphics_context_set_text_color(ctx, fg);
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
}

static void draw_battery_arc(GContext *ctx, GRect b) {
  BatteryChargeState batt = battery_state_service_peek();
  GRect ring = grect_inset(b, GEdgeInsets(1));
  int32_t end = TRIG_MAX_ANGLE * batt.charge_percent / 100;

  graphics_context_set_stroke_color(ctx, fade_color(g_settings.col_info, 1, 3));
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_arc(ctx, ring, GOvalScaleModeFitCircle, 0, TRIG_MAX_ANGLE);

  graphics_context_set_stroke_color(ctx,
    batt.charge_percent <= g_settings.battery_low_pct ? GColorRed : g_settings.col_info);
  graphics_draw_arc(ctx, ring, GOvalScaleModeFitCircle, 0, end);
  graphics_context_set_stroke_width(ctx, 1);
}

// --- Main entry ---------------------------------------------------------------

#ifdef ORBITAL_DEBUG
// A screenshot almost never lands on the frame carrying a one-off event, so
// count them rather than trying to catch them.
int g_dbg_taps = 0, g_dbg_flings = 0;
#endif

void render_scene(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);

  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  if (s_text_dirty) rebuild_text(lt);

  GColor tint = g_settings.sun_auto_tint ? render_sun_tint(lt->tm_hour) : g_settings.col_sun;

  graphics_context_set_fill_color(ctx, g_settings.col_bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  draw_stars(ctx, b);

  // Orbit paths. Each is drawn as a circle pushed off-centre toward aphelion,
  // which is a first-order ellipse — so Mercury and Mars visibly lean.
  if (g_settings.show_orbits) {
    graphics_context_set_stroke_color(ctx, g_settings.col_orbit);
    graphics_context_set_fill_color(ctx, g_settings.col_orbit);
    graphics_context_set_stroke_width(ctx, 1);
    for (int i = 0; i < PLANET_COUNT; i++) {
      if (!planet_visible(i)) continue;
      int rr = g_scene.body[i].ring_r * g_scene.intro / 255;
      if (rr <= 0) continue;
      draw_orbit_ring(ctx, g_scene.body[i].ring_c, rr);
    }
  }

  if (g_settings.show_trails) {
    for (int i = 0; i < PLANET_COUNT; i++) {
      if (!planet_visible(i)) continue;
      draw_trail(ctx, &g_scene.body[i], STYLE[i].color);
    }
  }

  draw_sun(ctx, tint);

  for (int i = 0; i < PLANET_COUNT; i++) {
    if (!planet_visible(i)) continue;
    const Body *body = &g_scene.body[i];

    GPoint p = body->shown;
    if (g_scene.intro < 255) {
      // Intro: planets spiral out of the Sun along their own radius.
      p.x = g_scene.centre.x + (p.x - g_scene.centre.x) * g_scene.intro / 255;
      p.y = g_scene.centre.y + (p.y - g_scene.centre.y) * g_scene.intro / 255;
    }

    int r = planet_radius(i);
    if (i == PL_EARTH && g_settings.highlight_earth) {
      graphics_context_set_stroke_color(ctx, fade_color(GColorWhite, 2, 3));
      graphics_context_set_stroke_width(ctx, 1);
      graphics_draw_circle(ctx, p, r + 4);
    }
    draw_planet(ctx, p, r, STYLE[i].color, STYLE[i].ringed);

    if (i == PL_EARTH && g_settings.show_moon && g_scene.intro > 200) {
      draw_moon(ctx, p, r);
    }
  }

  if (g_settings.battery_style == BATT_ARC) draw_battery_arc(ctx, b);

  // --- text ---
  GFont f_time = fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS);
  GFont f_small = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);

  bool has_date = (g_settings.date_format != DATE_OFF);
  int time_h = 40, date_h = 16;
  int block_h = time_h + (has_date ? date_h : 0);

  int block_y;
  switch (g_settings.time_pos) {
    case TIMEPOS_TOP:    block_y = 2; break;
    case TIMEPOS_CENTRE: block_y = g_scene.centre.y - block_h / 2; break;
    default:             block_y = b.size.h - block_h - PBL_IF_ROUND_ELSE(10, 2); break;
  }

  GRect time_box = GRect(0, block_y, b.size.w, time_h);
  draw_text_outlined(ctx, s_time_buf, f_time, time_box, g_settings.col_time, g_settings.col_bg);

  if (has_date) {
    GRect date_box = GRect(0, block_y + time_h - 4, b.size.w, date_h);
    draw_text_outlined(ctx, s_date_buf, f_small, date_box, g_settings.col_info, g_settings.col_bg);
  }

  // Status sits opposite the time block, so the two can never collide.
  if (s_info_buf[0]) {
    int info_y = (g_settings.time_pos == TIMEPOS_TOP) ? b.size.h - 20 : PBL_IF_ROUND_ELSE(6, 2);
    GRect info_box = GRect(0, info_y, b.size.w, 18);
    draw_text_outlined(ctx, s_info_buf, f_small, info_box, g_settings.col_info, g_settings.col_bg);
  }

#ifdef ORBITAL_DEBUG
  {
    char dbg[48];
    snprintf(dbg, sizeof(dbg), "t%d f%d L%d s%d",
             g_dbg_taps, g_dbg_flings,
             g_scene.body[PL_EARTH].loose ? 1 : 0, (int)g_scene.settle);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, dbg, f_small, GRect(0, 20, b.size.w, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
#endif

  if (g_settings.show_bt && !connection_service_peek_pebble_app_connection()) {
    int y = (g_settings.time_pos == TIMEPOS_TOP) ? b.size.h - 20 : PBL_IF_ROUND_ELSE(6, 2);
    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_fill_circle(ctx, GPoint(b.size.w / 2 - 34, y + 8), 3);
  }
}
