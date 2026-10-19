// FAST/LATE judgement display.
//
// GameScene::setMarkRank(MarkInfo*, int hitTime, int rankFlags, int, bool) commits
// a judgement; bit 0x10 of rankFlags means "show the rank popup", the low nibble is
// the rank. Hit and note times are milliseconds.
// After GameScene::DrawGameStageMenu (the HUD pass) we project the avatar position
// to the screen and draw "FAST"/"LATE" above it with the HUD's UV font.
#include <GLES/gl.h>
#include <jni.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

#include "autoplay.h"
#include "config.h"
#include "gametext.h"
#include "hook.h"
#include "log.h"

namespace {

// GameScene field offsets (libtune.so v1.0.18).
constexpr int kProjMatrix = 0x659c;
constexpr int kViewMatrix = 0x65dc;
constexpr int kAvatarPos = 0x661c;
// MarkInfo -> TuneMarkData*, TuneMarkData -> note time (ms).
constexpr int kMarkInfoData = 0x48;
constexpr int kMarkDataTime = 0x08;

constexpr int kRankGreat = 4;
constexpr int kFlagShowPopup = 0x10;

// Popup animation, modelled on the game's AD-LIB / MISS popups: it appears where
// the hit happened, shrinks steadily while drifting, flashes bright on the first
// frames and fades out at the end.
constexpr double kShowSeconds = 0.6;
constexpr double kFadeSeconds = 0.2;
constexpr double kFlashSeconds = 0.05;
constexpr float kOffsetUp = 34.f;      // above the avatar, in HUD units
constexpr float kOffsetRight = 0.06f;  // fraction of the HUD width
constexpr float kScaleStart = 1.15f;  // pops in, then shrinks
constexpr float kScaleEnd = 0.8f;
constexpr float kDriftX = 6.f;         // HUD units over the whole popup
constexpr unsigned kFastColor = 0x38D8FF00;  // RGBA without alpha
constexpr unsigned kLateColor = 0xFF5A3C00;
constexpr float kAutoPlayLabelY = 52.f;  // just under the GROOVE gauge
constexpr unsigned kAutoPlayColor = 0xFFE040FF;

using SetMarkRankFn = void (*)(void*, void*, int, int, int, bool);
using DrawMenuFn = void (*)(void*, void*);
using GetUVListFn = void* (*)(int);
using DrawUVStringFn = void (*)(void*, void*, int, const char*, int, int, int, int);
using GetIntFn = int (*)();
using SetMatrixModeFn = void (*)(void*, int);
using LoadIdentityFn = void (*)(void*);
using OrthoFn = void (*)(void*, float, float, float, float, float, float);
using StringSizeFn = void (*)(void*, void*, int, const char*, int*, int*);

SetMarkRankFn orig_setMarkRank;
DrawMenuFn orig_drawMenu;
GetUVListFn GetUVList;
DrawUVStringFn DrawUVDataString;
GetIntFn GetDrawScreenWidth, GetDrawScreenHeight;
SetMatrixModeFn SetMatrixMode;
LoadIdentityFn LoadIdentity;
OrthoFn Ortho;
StringSizeFn GetUVDataStringSize;
bool g_text_ready;

struct {
  double at = -1;  // monotonic seconds when shown
  int diff = 0;    // hitTime - noteTime (ms); negative = early
  bool anchored = false;
  float x = 0, y = 0;  // HUD position fixed on the popup's first frame
} g_last;

double now_seconds() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// Last time the stage HUD was drawn; the gamepad only drives the engine while
// a stage is on screen.
double g_last_stage_draw = -1;

template <typename T>
T& field(void* base, int off) {
  return *reinterpret_cast<T*>(static_cast<char*>(base) + off);
}

// Row vector times matrix, matching vector4::Mul(matrix44&) in libtune.
void mul(float out[4], const float v[4], const float* m) {
  for (int j = 0; j < 4; j++)
    out[j] = v[0] * m[j] + v[1] * m[4 + j] + v[2] * m[8 + j] + v[3] * m[12 + j];
}

void hk_setMarkRank(void* scene, void* markInfo, int hitTime, int rankFlags, int a4, bool a5) {
  orig_setMarkRank(scene, markInfo, hitTime, rankFlags, a4, a5);
  void* data = markInfo ? field<void*>(markInfo, kMarkInfoData) : nullptr;
  if (!data) return;
  int diff = hitTime - field<int>(data, kMarkDataTime);
  int rank = rankFlags & 0xf;
  if (!(rankFlags & kFlagShowPopup) || rank == kRankGreat || rank < 2 || diff == 0) return;
  g_last.at = now_seconds();
  g_last.diff = diff;
  g_last.anchored = false;
}

void draw_hud_text(void* g, const char* text, float x, float y, float scale, unsigned rgba) {
  draw_game_text(g, text, x, y, scale, rgba, kTextAlignCenter);
}

void draw_fast_late(void* scene, void* g) {
  double age = now_seconds() - g_last.at;
  if (g_last.at < 0 || age > kShowSeconds || !gcp::config().fastlate) return;

  // Anchor where the avatar was on the popup's first frame, like the game's popups.
  if (!g_last.anchored) {
    float w = GetDrawScreenWidth(), h = GetDrawScreenHeight();
    const float* pos = &field<float>(scene, kAvatarPos);
    float v[4] = {pos[0], pos[1], pos[2], 1.f}, eye[4], clip[4];
    mul(eye, v, &field<float>(scene, kViewMatrix));
    mul(clip, eye, &field<float>(scene, kProjMatrix));
    if (clip[3] <= 0.f) return;
    g_last.x = (clip[0] / clip[3] + 1.f) * 0.5f * w + kOffsetRight * w;
    g_last.y = (1.f - clip[1] / clip[3]) * 0.5f * h - kOffsetUp;
    g_last.anchored = true;
  }

  const char* text = g_last.diff < 0 ? "FAST" : "LATE";
  unsigned rgb = g_last.diff < 0 ? kFastColor : kLateColor;
  double fade = (kShowSeconds - age) / kFadeSeconds;
  float alpha = fade > 1 ? 1.f : (float)fade;
  auto scale_at = [](double t) {
    float p = (float)(t / kShowSeconds);
    return kScaleStart + (kScaleEnd - kScaleStart) * (p < 0 ? 0 : p);
  };
  float x = g_last.x + kDriftX * (float)(age / kShowSeconds);
  unsigned main_rgb = age < kFlashSeconds ? 0xFFFFFF00 : rgb;
  draw_hud_text(g, text, x, g_last.y, scale_at(age), main_rgb | (unsigned)(255 * alpha));
}

void hk_drawMenu(void* scene, void* g) {
  g_last_stage_draw = now_seconds();
  orig_drawMenu(scene, g);
  draw_fast_late(scene, g);
  if (autoplay_active())
    draw_hud_text(g, "AUTO PLAY", GetDrawScreenWidth() * 0.5f, kAutoPlayLabelY, 1.f, kAutoPlayColor);
}

template <typename T>
bool resolve(T& out, const char* sym) {
  out = reinterpret_cast<T>(gcp::find_symbol(sym));
  return out != nullptr;
}

}  // namespace

bool game_text_ready() { return g_text_ready; }
float game_screen_width() { return (float)GetDrawScreenWidth(); }
float game_screen_height() { return (float)GetDrawScreenHeight(); }

void game_text_size(void* g, const char* text, float* w, float* h) {
  int iw = 0, ih = 0;
  if (GetUVDataStringSize) GetUVDataStringSize(g, GetUVList(0), 0, text, &iw, &ih);
  *w = (float)iw;
  *h = (float)ih;
}

void draw_game_text(void* g, const char* text, float x, float y, float scale, unsigned rgba, int align) {
  float w = GetDrawScreenWidth(), h = GetDrawScreenHeight();
  // Scale the text by shrinking the ortho volume around the screen.
  glDisable(GL_DEPTH_TEST);
  SetMatrixMode(g, 1);
  LoadIdentity(g);
  Ortho(g, 0.f, w / scale, h / scale, 0.f, 0.f, 1000.f);
  SetMatrixMode(g, 0);
  LoadIdentity(g);
  DrawUVDataString(g, GetUVList(0), 0, text, (int)lroundf(x / scale), (int)lroundf(y / scale), (int)rgba, align);
  SetMatrixMode(g, 1);
  LoadIdentity(g);
  Ortho(g, 0.f, w, h, 0.f, 0.f, 1000.f);
  SetMatrixMode(g, 0);
  LoadIdentity(g);
}

extern "C" JNIEXPORT jboolean JNICALL Java_jp_co_taito_groovecoasterzero_GC2PlusPad_nativeInGame(JNIEnv*, jclass) {
  return g_last_stage_draw >= 0 && now_seconds() - g_last_stage_draw < 0.25;
}

void install_fast_late() {
  bool ok = resolve(GetUVList, "_Z9GetUVListi") &&
            resolve(DrawUVDataString, "_ZN13MtxGraphics3D16DrawUVDataStringER6UVListiPKciiii") &&
            resolve(GetDrawScreenWidth, "_Z18GetDrawScreenWidthv") &&
            resolve(GetDrawScreenHeight, "_Z19GetDrawScreenHeightv") &&
            resolve(SetMatrixMode, "_ZN13MtxGraphics3D13SetMatrixModeEi") &&
            resolve(LoadIdentity, "_ZN13MtxGraphics3D12LoadIdentiryEv") &&
            resolve(Ortho, "_ZN13MtxGraphics3D5OrthoEffffff");
  if (!ok) return;
  // Only used for measuring (search box).
  resolve(GetUVDataStringSize, "_ZN13MtxGraphics3D19GetUVDataStringSizeER6UVListiPKcRiS4_");
  g_text_ready = true;
  orig_setMarkRank = reinterpret_cast<SetMarkRankFn>(
      gcp::hook_got("_ZN9GameScene11setMarkRankEP8MarkInfoiiib", reinterpret_cast<void*>(hk_setMarkRank)));
  orig_drawMenu = reinterpret_cast<DrawMenuFn>(
      gcp::hook_got("_ZN9GameScene17DrawGameStageMenuER13MtxGraphics3D", reinterpret_cast<void*>(hk_drawMenu)));
}
