#include "ambient_orbs.h"

#define ORB_COUNT 7
#define ORB_ORBIT_PERIOD_MS 2200
#define ORB_SPREAD_PERIOD_MS 4300
#define ORB_TILT_PERIOD_MS 6100
#define ORB_DESYNC_PERIOD_MS 5200
#define ORB_CONTRACT_PERIOD_MS 24000
#define ORB_PULSE_PERIOD_MS 1800
#define ORB_SPREAD_PHASE 2600
#define ORB_DESYNC_PHASE 3800
#define ORB_TRAIL_STEP_MS 40
#define ORB_TRAIL_SEGMENTS 6

static const int orb_sin[32] = {
    0, 25, 49, 71, 90, 106, 117, 125,
    127, 125, 117, 106, 90, 71, 49, 25,
    0, -25, -49, -71, -90, -106, -117, -125,
    -127, -125, -117, -106, -90, -71, -49, -25};

typedef struct {
  uint32_t orbit;
  float breath;
  float desync;
  float tilt;
  float scale;
} OrbMotion;

typedef struct {
  uint32_t base;
  float spread_weight;
  float desync_weight;
} OrbPath;

static uint32_t orb_phase(uint32_t elapsed_ms, uint32_t period_ms) {
  return (uint32_t)(((uint64_t)elapsed_ms << 16) / period_ms);
}

// Cubic interpolation keeps both position and velocity smooth between samples.
static float orb_wave(uint32_t phase) {
  const int index = (phase >> 11) & 31;
  const float p0 = orb_sin[(index + 31) & 31];
  const float p1 = orb_sin[index];
  const float p2 = orb_sin[(index + 1) & 31];
  const float p3 = orb_sin[(index + 2) & 31];
  const float t = (phase & 2047) / 2048.0f;
  return 0.5f * ((2.0f * p1) +
                 t * ((p2 - p0) +
                      t * ((2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) +
                           t * (-p0 + 3.0f * p1 - 3.0f * p2 + p3))));
}

static void draw_glow_disc(GSGLOBAL *gs, float center_x, float center_y,
                           float radius, uint64_t center_color,
                           uint64_t edge_color) {
  const float scale = radius / 127.0f;
  for (int i = 0; i < 16; i++) {
    const int point = i * 2;
    const int next = (point + 2) & 31;
    const float x1 = center_x + orb_sin[(point + 8) & 31] * scale;
    const float y1 = center_y + orb_sin[point] * scale;
    const float x2 = center_x + orb_sin[(next + 8) & 31] * scale;
    const float y2 = center_y + orb_sin[next] * scale;
    gsKit_prim_triangle_gouraud(gs, center_x, center_y, x1, y1, x2, y2,
                                0, center_color, edge_color, edge_color);
  }
}

static OrbMotion orb_motion(uint32_t elapsed_ms) {
  OrbMotion motion;
  motion.orbit = orb_phase(elapsed_ms, ORB_ORBIT_PERIOD_MS);
  motion.breath = orb_wave(orb_phase(elapsed_ms, ORB_SPREAD_PERIOD_MS));
  motion.desync = orb_wave(orb_phase(elapsed_ms, ORB_DESYNC_PERIOD_MS));
  float tilt_wave = orb_wave(orb_phase(elapsed_ms, ORB_TILT_PERIOD_MS));
  if (tilt_wave < 0)
    tilt_wave = -tilt_wave;
  motion.tilt = 26.0f + (tilt_wave * 74.0f) / 127.0f;
  const float slow_scale = 62.0f +
      orb_wave(orb_phase(elapsed_ms, ORB_CONTRACT_PERIOD_MS) + (8 << 11)) *
          38.0f / 127.0f;
  const float pulse_scale = 85.0f +
      orb_wave(orb_phase(elapsed_ms, ORB_PULSE_PERIOD_MS) + (8 << 11)) *
          15.0f / 127.0f;
  motion.scale = slow_scale * pulse_scale / 100.0f;
  return motion;
}

static void orb_position(const OrbPath *path, const OrbMotion *motion,
                         int center_x, int center_y, int radius_x,
                         int radius_y, float *x, float *y, float *depth) {
  const int spread = (int)(path->spread_weight * motion->breath *
                           ORB_SPREAD_PHASE / (127.0f * 127.0f));
  const uint32_t phase_x = motion->orbit + path->base + spread;
  const int offset_y = (int)(path->desync_weight * motion->desync *
                             ORB_DESYNC_PHASE / (127.0f * 127.0f));
  const uint32_t phase_y = phase_x + offset_y;
  if (depth != NULL)
    *depth = (orb_wave(phase_x) + 127.0f) / 2.0f;
  *x = center_x + orb_wave(phase_x + (8 << 11)) * radius_x *
      motion->scale / (127.0f * 100.0f);
  *y = center_y + orb_wave(phase_y) * radius_y * motion->tilt *
      motion->scale / (127.0f * 100.0f * 100.0f);
}

static void draw_trail_segment(GSGLOBAL *gs, float old_x, float old_y,
                               float new_x, float new_y, float old_width,
                               float new_width, uint64_t old_color,
                               uint64_t new_color) {
  const float dx = new_x - old_x;
  const float dy = new_y - old_y;
  const float abs_dx = dx < 0 ? -dx : dx;
  const float abs_dy = dy < 0 ? -dy : dy;
  const float longer = abs_dx > abs_dy ? abs_dx : abs_dy;
  const float shorter = abs_dx > abs_dy ? abs_dy : abs_dx;
  const float length = longer + shorter * 0.375f;
  if (length < 0.01f)
    return;
  const float old_offset_x = -dy * old_width / (2.0f * length);
  const float old_offset_y = dx * old_width / (2.0f * length);
  const float new_offset_x = -dy * new_width / (2.0f * length);
  const float new_offset_y = dx * new_width / (2.0f * length);
  gsKit_prim_quad_gouraud(gs,
                          old_x + old_offset_x, old_y + old_offset_y,
                          new_x + new_offset_x, new_y + new_offset_y,
                          old_x - old_offset_x, old_y - old_offset_y,
                          new_x - new_offset_x, new_y - new_offset_y, 0,
                          old_color, new_color, old_color, new_color);
}

void ambient_orbs_draw_background(GSGLOBAL *gs) {
  const uint64_t top = GS_SETREG_RGBA(0x00, 0x00, 0x04, 0x80);
  const uint64_t bottom = GS_SETREG_RGBA(0x00, 0x01, 0x08, 0x80);
  gsKit_prim_quad_gouraud(gs, 0, 0, gs->Width, 0, 0, gs->Height,
                          gs->Width, gs->Height, 0,
                          top, top, bottom, bottom);
}

void ambient_orbs_draw(GSGLOBAL *gs, uint32_t elapsed_ms,
                       int center_x, int center_y,
                       int radius_x, int radius_y, int glow_scale) {
  OrbMotion motion[ORB_TRAIL_SEGMENTS + 1];
  for (int sample = 0; sample <= ORB_TRAIL_SEGMENTS; sample++) {
    const uint32_t age = sample * ORB_TRAIL_STEP_MS;
    motion[sample] = orb_motion(elapsed_ms > age ? elapsed_ms - age : 0);
  }

  // Additive blend makes overlapping halos merge into a brighter light.
  gs->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 2, 0, 1, 0), 0);
  for (int i = 0; i < ORB_COUNT; i++) {
    OrbPath path;
    path.base = (uint32_t)(((uint64_t)i << 16) / ORB_COUNT);
    path.spread_weight = orb_wave(path.base + (2 << 11));
    path.desync_weight = orb_wave(path.base);
    float x, y, depth;
    orb_position(&path, &motion[0], center_x, center_y, radius_x, radius_y,
                 &x, &y, &depth);
    float new_x = x;
    float new_y = y;
    for (int segment = 1; segment <= ORB_TRAIL_SEGMENTS; segment++) {
      float old_x, old_y;
      orb_position(&path, &motion[segment], center_x, center_y,
                   radius_x, radius_y, &old_x, &old_y, NULL);
      const int old_outer_alpha = 4 +
          (ORB_TRAIL_SEGMENTS - segment) * 28 / ORB_TRAIL_SEGMENTS;
      const int new_outer_alpha = 4 +
          (ORB_TRAIL_SEGMENTS - segment + 1) * 28 / ORB_TRAIL_SEGMENTS;
      const int old_inner_alpha = 5 +
          (ORB_TRAIL_SEGMENTS - segment) * 45 / ORB_TRAIL_SEGMENTS;
      const int new_inner_alpha = 5 +
          (ORB_TRAIL_SEGMENTS - segment + 1) * 45 / ORB_TRAIL_SEGMENTS;
      const int old_outer_width = 3 +
          (ORB_TRAIL_SEGMENTS - segment) * 6 / ORB_TRAIL_SEGMENTS;
      const int new_outer_width = 3 +
          (ORB_TRAIL_SEGMENTS - segment + 1) * 6 / ORB_TRAIL_SEGMENTS;
      const int old_inner_width = 2 +
          (ORB_TRAIL_SEGMENTS - segment) * 3 / ORB_TRAIL_SEGMENTS;
      const int new_inner_width = 2 +
          (ORB_TRAIL_SEGMENTS - segment + 1) * 3 / ORB_TRAIL_SEGMENTS;
      draw_trail_segment(gs, old_x, old_y, new_x, new_y,
                         old_outer_width, new_outer_width,
                         GS_SETREG_RGBA(0x40, 0x78, 0xC8, old_outer_alpha),
                         GS_SETREG_RGBA(0x40, 0x78, 0xC8, new_outer_alpha));
      draw_trail_segment(gs, old_x, old_y, new_x, new_y,
                         old_inner_width, new_inner_width,
                         GS_SETREG_RGBA(0xA0, 0xD8, 0xFF, old_inner_alpha),
                         GS_SETREG_RGBA(0xA0, 0xD8, 0xFF, new_inner_alpha));
      new_x = old_x;
      new_y = old_y;
    }
    const float halo_radius = (18.0f + depth / 14.0f) * glow_scale / 100.0f;
    const float core_radius = (5.6f + depth / 52.0f) * glow_scale / 100.0f;
    const int halo_alpha = 0x13 + (int)(depth / 13.0f);
    const int core_alpha = 0x50 + (int)(depth / 3.0f);

    draw_glow_disc(gs, x, y, halo_radius,
                   GS_SETREG_RGBA(0x70, 0xA8, 0xE8, halo_alpha),
                   GS_SETREG_RGBA(0x70, 0xA8, 0xE8, 0));
    draw_glow_disc(gs, x, y, core_radius,
                   GS_SETREG_RGBA(0xE0, 0xF0, 0xFF, core_alpha),
                   GS_SETREG_RGBA(0xE0, 0xF0, 0xFF, 0));
  }
  gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}
