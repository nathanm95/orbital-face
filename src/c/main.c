#include "orbital.h"

// -----------------------------------------------------------------------------
// App lifecycle. One window, one layer, one timer.
//
// The timer only exists while something is actually moving. With physics idle
// the face redraws once a minute and costs nothing, which is the difference
// between a watchface people keep and one they uninstall on day two.
// -----------------------------------------------------------------------------

#define FRAME_MS 33

static Window   *s_window;
static Layer    *s_layer;
static AppTimer *s_timer;

static void ensure_timer(void);

static void frame_cb(void *ctx) {
  s_timer = NULL;
  g_scene.frame++;

  bool more = physics_step();
  layer_mark_dirty(s_layer);

  if (more) ensure_timer();
}

static void ensure_timer(void) {
  if (!s_timer) s_timer = app_timer_register(FRAME_MS, frame_cb, NULL);
}

// The Sun breathes and the stars twinkle whenever the timer happens to be
// running, but neither is worth waking the watch for on its own.
static void kick_animation(void) {
  ensure_timer();
}

// --- Services -----------------------------------------------------------------

#ifdef ORBITAL_DEBUG
extern int g_dbg_taps, g_dbg_flings;
#endif

static void tap_handler(AccelAxisType axis, int32_t direction) {
#ifdef ORBITAL_DEBUG
  g_dbg_taps++;
#endif
  if (!g_settings.physics_enabled) return;
  AccelData a = {0};
  accel_service_peek(&a);
  physics_fling(a.x, a.y);
#ifdef ORBITAL_DEBUG
  if (physics_active()) g_dbg_flings++;
#endif
  kick_animation();
  layer_mark_dirty(s_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units) {
  scene_update_positions(time(NULL));
  render_invalidate_text();

  if (g_settings.minute_pulse) g_scene.sun_pulse = 255;

  if (g_settings.nudge_minute && g_settings.physics_enabled) {
    // A small deterministic shove so the sky has some life without being
    // touched. Direction rotates each minute so it never looks like a loop.
    int32_t ang = ((int32_t)tick_time->tm_min * 65536 / 60) & 0xFFFF;
    physics_fling((int16_t)(300 * cos_lookup(ang) / TRIG_MAX_RATIO),
                  (int16_t)(300 * sin_lookup(ang) / TRIG_MAX_RATIO));
  }

  layer_mark_dirty(s_layer);
  if (g_scene.sun_pulse > 0 || physics_active()) kick_animation();
}

static void battery_handler(BatteryChargeState state) {
  render_invalidate_text();
  layer_mark_dirty(s_layer);
}

static void connection_handler(bool connected) {
  if (!connected && g_settings.vibe_bt) vibes_double_pulse();
  layer_mark_dirty(s_layer);
}

// --- Settings arriving from the phone -----------------------------------------

static void inbox_received(DictionaryIterator *iter, void *context) {
  settings_handle_inbox(iter);

  // Ring spacing depends on whether the outer planets are shown, and the intro
  // flag is read at layout time, so a full relayout is the honest response.
  bool had_intro = (g_scene.intro >= 255);
  scene_layout(layer_get_bounds(s_layer));
  if (had_intro) g_scene.intro = 255;   // don't replay the intro on every save

  scene_update_positions(time(NULL));
  physics_reset();
  render_invalidate_text();
  layer_mark_dirty(s_layer);
}

// --- Window -------------------------------------------------------------------

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);

  scene_layout(b);
  scene_update_positions(time(NULL));
  physics_reset();

  s_layer = layer_create(b);
  layer_set_update_proc(s_layer, render_scene);
  layer_add_child(root, s_layer);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  accel_tap_service_subscribe(tap_handler);
  battery_state_service_subscribe(battery_handler);
  connection_service_subscribe((ConnectionHandlers){
    .pebble_app_connection_handler = connection_handler,
  });

  if (g_settings.intro_anim) kick_animation();

#ifdef ORBITAL_DEBUG
  // The emulator's tap injection goes through pypkjs, which stops delivering
  // once the host is under memory pressure. Flinging ourselves on launch makes
  // the physics reachable from a plain screendump.
  physics_fling(1000, -400);
  kick_animation();
#endif
}

static void window_unload(Window *window) {
  if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
  connection_service_unsubscribe();
  battery_state_service_unsubscribe();
  accel_tap_service_unsubscribe();
  tick_timer_service_unsubscribe();
  layer_destroy(s_layer);
}

int main(void) {
  settings_init();

  app_message_register_inbox_received(inbox_received);
  app_message_open(512, 64);

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load, .unload = window_unload,
  });
  window_stack_push(s_window, false);

  app_event_loop();
  window_destroy(s_window);
  return 0;
}
