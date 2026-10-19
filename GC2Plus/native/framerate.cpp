// High framerate by render extrapolation.
//
// The engine runs game logic at a fixed 60 ticks/s (FpsCalc frame table; the
// stage clock is a frame counter resynced to the audio), while GLSurfaceView
// renders every vsync (120/180Hz on high refresh screens), so each logic state
// was simply drawn 2-3 times. tuneViewController::drawFrame(false) is a logic
// tick, drawFrame(true) a render.
//
// Each tick SetCommonParam leaves the render inputs in GameScene: stage time
// (ms), the camera's projection/view matrices and the avatar position, which
// DrawGameStage copies into its draw fields. For renders between ticks we
// extrapolate those from the last two ticks by the time elapsed since the latest
// one, draw, then put the tick values back. Logic, judgement and audio sync never
// see the extrapolated values, and nothing is delayed.
#include <math.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "hook.h"
#include "log.h"

void search_post_render();  // search.cpp

namespace {

constexpr int kMainAppScene = 0x29f0;    // g_MainApp -> GameScene*
constexpr int kSceneState = 0x622c;      // 2 = stage playing
constexpr int kSceneGameCount = 0x6228;  // stage clock (frames, float)
constexpr int kSceneTimeMs = 0x644c;     // stage time used for drawing (float ms)
constexpr int kSceneCamera = 0x649c;     // projection + view matrix (32 floats)
constexpr int kSceneAvatar = 0x63e0;     // avatar position (vector3)
constexpr int kCameraFloats = 32;
constexpr int kStatePlaying = 2;
constexpr double kTickMs = 1000.0 / 60.0;
// A bigger per-tick change in any rotation element is a camera cut, not motion.
constexpr float kMaxRotationStep = 0.15f;

using DrawFrameFn = void (*)(void*, bool);
DrawFrameFn orig_drawFrame;
void** g_MainApp;

struct Snapshot {
  float count;
  float time_ms;
  float camera[kCameraFloats];
  float avatar[3];
};
Snapshot g_prev, g_cur;
int g_snapshots;  // consecutive valid ticks captured
double g_last_tick_ms = -1;

double now_ms() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
}

template <typename T>
T* at(void* base, int off) {
  return reinterpret_cast<T*>(static_cast<char*>(base) + off);
}

void* stage_scene() {
  void* app = g_MainApp ? *g_MainApp : nullptr;
  void* scene = app ? *at<void*>(app, kMainAppScene) : nullptr;
  if (!scene || *at<int>(scene, kSceneState) != kStatePlaying) return nullptr;
  return scene;
}

void capture(void* scene, Snapshot& s) {
  s.count = *at<float>(scene, kSceneGameCount);
  s.time_ms = *at<float>(scene, kSceneTimeMs);
  memcpy(s.camera, at<float>(scene, kSceneCamera), sizeof s.camera);
  memcpy(s.avatar, at<float>(scene, kSceneAvatar), sizeof s.avatar);
}

void apply(void* scene, const Snapshot& s) {
  *at<float>(scene, kSceneTimeMs) = s.time_ms;
  memcpy(at<float>(scene, kSceneCamera), s.camera, sizeof s.camera);
  memcpy(at<float>(scene, kSceneAvatar), s.avatar, sizeof s.avatar);
}

bool camera_cut(const Snapshot& a, const Snapshot& b) {
  for (int m = 0; m < 2; m++)
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 3; c++) {
        int i = m * 16 + r * 4 + c;
        if (fabsf(b.camera[i] - a.camera[i]) > kMaxRotationStep) return true;
      }
  return false;
}

void render_frame(void* vc);

void hk_drawFrame(void* vc, bool render) {
  if (!render) {
    orig_drawFrame(vc, render);
    g_last_tick_ms = now_ms();
    if (void* scene = stage_scene()) {
      g_prev = g_cur;
      capture(scene, g_cur);
      g_snapshots++;
    } else {
      g_snapshots = 0;
    }
    return;
  }

  render_frame(vc);
  search_post_render();
}

void render_frame(void* vc) {
  constexpr bool render = true;
  void* scene = g_snapshots >= 2 && gcp::config().interpolate ? stage_scene() : nullptr;
  // Only while the clock advances one frame per tick (not paused / resyncing),
  // and the scene still holds the latest tick's values.
  if (!scene || g_cur.count - g_prev.count != 1.f || *at<float>(scene, kSceneGameCount) != g_cur.count) {
    orig_drawFrame(vc, render);
    return;
  }
  float f = (float)((now_ms() - g_last_tick_ms) / kTickMs);
  if (f <= 0.f) {
    orig_drawFrame(vc, render);
    return;
  }
  if (f > 1.f) f = 1.f;

  Snapshot e = g_cur;
  e.time_ms = g_cur.time_ms + f * (float)kTickMs;
  if (!camera_cut(g_prev, g_cur)) {
    for (int i = 0; i < kCameraFloats; i++) e.camera[i] += f * (g_cur.camera[i] - g_prev.camera[i]);
    for (int i = 0; i < 3; i++) e.avatar[i] += f * (g_cur.avatar[i] - g_prev.avatar[i]);
  }
  apply(scene, e);
  orig_drawFrame(vc, render);
  apply(scene, g_cur);
}

}  // namespace

void install_framerate() {
  g_MainApp = reinterpret_cast<void**>(gcp::find_symbol("g_MainApp"));
  if (!g_MainApp) return;
  orig_drawFrame = reinterpret_cast<DrawFrameFn>(
      gcp::hook_got("_ZN18tuneViewController9drawFrameEb", reinterpret_cast<void*>(hk_drawFrame)));
}
