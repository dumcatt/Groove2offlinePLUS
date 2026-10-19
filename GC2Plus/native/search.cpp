// Song search for the song list (MenuRouteView, both the route and the 2D LIST view).
//
// The list shows Main->ownedCount songs whose ids sit in Main->owned[] (4-byte
// entries, u16 stage index first). MenuRouteView::_SortList copies them into its
// own array (+0xb8) and sorts them. Nothing in the route view keeps its own count:
// every route-view function reads Main->ownedCount. So to filter we
//   - move the matching songs to the front of the route view's array after it
//     is sorted, and
//   - while a route-view entry point runs (Run/Paint/OnTouch/OnMove/Setup/
//     ChangeRoute), temporarily set Main->ownedCount to the number of matches.
// Anything that saves or appends owned songs (SaveGameData, AddStage) always
// sees the real count. Queries come from Java (GC2PlusSearch) on the UI thread and
// are applied on the GL thread at the next MenuRouteView::Run.
#include <EGL/egl.h>
#include <GLES/gl.h>
#include <ctype.h>
#include <jni.h>
#include <math.h>
#include <mutex>
#include <stdio.h>
#include <string.h>
#include <string>
#include <time.h>

#include "gametext.h"
#include "hook.h"
#include "log.h"

namespace {

constexpr int kMainStageParams = 0x2b40;   // StageParam* (stride 0xe8)
constexpr int kStageParamSize = 0xe8;
constexpr int kStageTitle = 0x18;          // const char*
constexpr int kStageArtist = 0x28;         // const char*
constexpr int kMainOwned = 0x499c8;        // owned stage entries (4 bytes each, u16 id)
constexpr int kMainOwnedCount = 0x4b9c8;   // int
constexpr int kMainSortKind = 0x47d88;     // current PLAYLIST_SORTKIND
constexpr int kRouteList = 0xb8;           // route view's sorted entries (4 bytes each)
constexpr int kRouteFocus = 0x40d8;        // float, focused list position
constexpr int kRouteChanging = 0x68;       // bool, LIST <-> route view switch animating
// MenuButton: anchor type, size, message id; vtable slot of GetPosition(vector2&).
constexpr int kButtonAnchor = 0x0c;
constexpr int kButtonWidth = 0x28;
constexpr int kButtonHeight = 0x2c;
constexpr int kButtonMessage = 0x90;
constexpr int kButtonGetPosSlot = 0x58 / 8;
constexpr int kMsgSortTitle = 178;       // "TITLE" sort button
constexpr int kMsgSortDifficulty = 177;  // "DIFFICULTY" sort button
// Box top below the sort buttons' top, in draw units (centres it in the
// "TITLE ▲" row between the divider lines).
constexpr float kBoxBelowButtons = 24.33f;

struct Rect {
  float l, t, r, b;
};
Rect g_box;  // search box in draw-screen units (update_box_rect)

// Search box text (RGBA) and size relative to the box height.
constexpr float kTextHeight = 0.5f;
constexpr unsigned kColorText = 0xFFFFFFFF;
constexpr unsigned kColorHint = 0x808080FF;
constexpr unsigned kColorDim = 0xC0C0C0FF;
constexpr unsigned kColorCyan = 0x38D8FFFF;
constexpr unsigned kColorOrange = 0xFF5A3CFF;
constexpr unsigned kColorCaret = 0x38D8FFFF;
// The pixel font has no "_" or "|"; its "1" is a plain vertical bar.
constexpr const char* kCaret = "1";

using GetMainFn = void* (*)();
using RouteVoidFn = void (*)(void*);
using RouteIntFn = void (*)(void*, int);
using RouteRunFn = int (*)(void*, int);
using RoutePaintFn = void (*)(void*, void*);
using RouteTouchFn = void (*)(void*, int, int, int, bool, int);
using RouteMoveFn = void (*)(void*, int, int, int, int, int);
using ChangeRouteFn = void (*)(void*, int, bool);
using AppVoidFn = void (*)(void*);

GetMainFn GetMain;
RouteVoidFn orig_Setup;
RouteIntFn orig_SortList, orig_AddStage;
RouteRunFn orig_Run;
RoutePaintFn orig_Paint, orig_ModePaint;
RouteTouchFn orig_OnTouch;
RouteMoveFn orig_OnMove;
ChangeRouteFn orig_ChangeRoute;
AppVoidFn orig_SaveGameData;

// Query, set from the UI thread.
std::mutex g_mutex;
std::string g_pending;
bool g_dirty;

// GL-thread state.
std::string g_query;    // lower-cased; empty = no filter
int g_matches = -1;     // list rows while filtering: matches + the fixed first row (-1: no filter)
int g_match_songs = -1; // songs matching g_query, for the UI
int g_swap_depth;
int g_real_count;
// Status shared with Java through a direct ByteBuffer, so the UI can poll it
// without calling into native code (frequent JNI calls from the UI thread while
// the GL thread runs translated ARM code crashed MuMu's translator).
//   [0..7]  int64: CLOCK_MONOTONIC ms when the song list was last drawn (0 = never)
//   [8..11] int32: songs matching the query (-1: no query)
//   [16..31] float[4]: box left, top, right, bottom as fractions of the GL surface
//            (laid out here from the sort buttons; Java places its frame there)
// Written by Java (the search box is drawn here, in the game's font):
//   [12..15] int32: kUi* flags
//   [32..35] int32: length of text
//   [36..]   text: the query, upper-cased ASCII
constexpr int kUiAttached = 1;  // Java overlay is up; draw the box contents
constexpr int kUiDrawText = 2;  // query is ASCII: draw it here (else Java shows it)
constexpr int kUiFocused = 4;   // keyboard focus: show a caret
struct Status {
  volatile int64_t last_list_draw_ms;
  volatile int32_t match_songs;
  volatile int32_t ui_flags;
  volatile float box[4];
  volatile int32_t text_len;
  char text[124];
};

}  // namespace

Status g_status = {0, -1, 0, {0, 0, 0, 0}, 0, {0}};

namespace {

double now_seconds() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

template <typename T>
T& field(void* base, int off) {
  return *reinterpret_cast<T*>(static_cast<char*>(base) + off);
}

int& owned_count() { return field<int>(GetMain(), kMainOwnedCount); }

// Case-insensitive (ASCII) substring; other bytes (e.g. Japanese) must match exactly.
bool contains(const char* text, const std::string& needle) {
  if (!text) return false;
  size_t n = needle.size();
  for (const char* p = text; *p; p++) {
    size_t i = 0;
    while (i < n && p[i] && tolower((unsigned char)p[i]) == (unsigned char)needle[i]) i++;
    if (i == n) return true;
  }
  return false;
}

bool stage_matches(int stage) {
  char* params = field<char*>(GetMain(), kMainStageParams);
  if (!params || stage < 0) return false;
  char* sp = params + (size_t)stage * kStageParamSize;
  return contains(field<const char*>(sp, kStageTitle), g_query) ||
         contains(field<const char*>(sp, kStageArtist), g_query);
}

int entry_stage(const char* entries, int i) { return *reinterpret_cast<const unsigned short*>(entries + i * 4); }

// Show only the matches: while swapped, route-view code sees ownedCount = matches.
struct ListSwap {
  bool active;
  ListSwap() : active(g_matches > 0) {
    if (!active) return;
    if (g_swap_depth++ == 0) {
      g_real_count = owned_count();
      owned_count() = g_matches;
    }
  }
  ~ListSwap() {
    if (active && --g_swap_depth == 0) owned_count() = g_real_count;
  }
};

// The opposite, for code that must see every owned song (saving, adding songs).
struct RealCount {
  bool active;
  RealCount() : active(g_swap_depth > 0) {
    if (active) owned_count() = g_real_count;
  }
  ~RealCount() {
    if (active) owned_count() = g_matches;
  }
};

void apply_pending_query(void* route) {
  std::string q;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_dirty) return;
    g_dirty = false;
    q = g_pending;
  }
  for (char& c : q) c = (char)tolower((unsigned char)c);
  if (q == "#avatars") {  // debug: avatar/player table sizes
    char* m = static_cast<char*>(GetMain());
    LOGI("players(0x2b48)=%d items(0x2b58)=%d skins(0x2b68)=%d", field<int>(m, 0x2b48), field<int>(m, 0x2b58),
         field<int>(m, 0x2b68));
    for (int i = 250; i < 368; i++)
      LOGI("  p[%d]=%d,%d,%d", i, field<int>(m, 0x4e300 + i * 12), field<int>(m, 0x4e304 + i * 12),
           field<int>(m, 0x4e308 + i * 12));
    for (int i = 0; i < 11; i++)
      LOGI("  item[%d]=%d,%d,%d", i, field<int>(m, 0x41798 + i * 12), field<int>(m, 0x4179c + i * 12),
           field<int>(m, 0x417a0 + i * 12));
    return;
  }
  if (q == "#check") {  // debug: owned list integrity
    int n = g_swap_depth > 0 ? g_real_count : owned_count();
    const char* owned = &field<char>(GetMain(), kMainOwned);
    static unsigned char seen[0x10000];
    memset(seen, 0, sizeof seen);
    int dups = 0, maxid = 0;
    for (int i = 0; i < n; i++) {
      int s = entry_stage(owned, i);
      if (seen[s]++) {
        if (dups < 10) LOGI("  duplicate stage %d at entry %d", s, i);
        dups++;
      }
      if (s > maxid) maxid = s;
    }
    LOGI("check: owned=%d duplicates=%d maxid=%d", n, dups, maxid);
    return;
  }
  if (q == "#dump") {  // debug: log a few StageParam records
    char* params = field<char*>(GetMain(), kMainStageParams);
    for (int s = 0; s < 6 && params; s++) {
      char* sp = params + s * kStageParamSize;
      char hex[kStageParamSize * 3 + 1];
      for (int i = 0; i < kStageParamSize; i++) snprintf(hex + i * 3, 4, "%02x ", (unsigned char)sp[i]);
      LOGI("stage %d '%s' / '%s': %s", s, field<const char*>(sp, kStageTitle), field<const char*>(sp, kStageArtist), hex);
    }
    return;
  }
  g_query = q;
  g_matches = -1;
  g_match_songs = -1;
  if (!q.empty()) {
    int n = owned_count();
    const char* owned = &field<char>(GetMain(), kMainOwned);
    int m = 0;
    char* params = field<char*>(GetMain(), kMainStageParams);
    for (int i = 1; i < n; i++) {  // entry 0 is the fixed first row, kept below
      int s = entry_stage(owned, i);
      if (!stage_matches(s)) continue;
      if (m < 8) {
        char* sp = params + (size_t)s * kStageParamSize;
        LOGI("  match #%d entry %d stage %d '%s' / '%s'", m, i, s, field<const char*>(sp, kStageTitle),
             field<const char*>(sp, kStageArtist));
      }
      m++;
    }
    g_matches = m > 0 ? m + 1 : 0;  // + the fixed first row
    g_match_songs = m;
  }
  LOGI("search '%s': %d matches", g_query.c_str(), g_matches);
  // Rebuild the list for the new count, keeping the scroll position in range.
  ListSwap swap;
  float& focus = field<float>(route, kRouteFocus);
  float last = (float)(owned_count() - 1);
  if (focus > last) focus = last;
  if (focus < 0.f) focus = 0.f;
  orig_ChangeRoute(route, field<int>(GetMain(), kMainSortKind), true);
}

void hk_SortList(void* route, int kind) {
  {
    RealCount real;  // copy and sort the full list
    orig_SortList(route, kind);
  }
  if (g_matches <= 0) return;
  // Keep the matching entries (in sorted order) and mark every other slot empty
  // (stage id 0xffff), which the list painters skip. Entry 0 is the fixed
  // "The Beginning" row (never sorted, title drawn from a system message), so it
  // stays put and the matches follow it.
  int n = g_swap_depth > 0 ? g_real_count : owned_count();
  char* list = &field<char>(route, kRouteList);
  int w = 1;
  for (int i = 1; i < n; i++) {
    if (!stage_matches(entry_stage(list, i))) continue;
    if (w != i) memmove(list + w * 4, list + i * 4, 4);
    w++;
  }
  for (int i = w; i < n; i++) *reinterpret_cast<unsigned short*>(list + i * 4) = 0xffff;
}

int hk_Run(void* route, int arg) {
  apply_pending_query(route);
  ListSwap swap;
  return orig_Run(route, arg);
}

// Search box contents in the game's pixel font: query (or a "SEARCH" placeholder)
// on the left, match count and the clear "X" on the right. The frame and the
// touch handling are the Java overlay's.
void draw_search_box(void* g) {
  int flags = g_status.ui_flags;
  if (!(flags & kUiAttached) || !game_text_ready()) return;
  float l = g_box.l, t = g_box.t, r = g_box.r, b = g_box.b;
  float bh = b - t, cy = (t + b) * 0.5f;
  if (bh <= 0 || r <= l) return;

  float gw, gh;
  game_text_size(g, "A", &gw, &gh);
  float scale = gh > 0 ? bh * kTextHeight / gh : 1.f;
  float pad = bh * 0.3f;
  float x = l + pad;
  float right = r - bh;  // the "X" takes a square at the right end

  draw_game_text(g, "X", right + bh * 0.5f, cy, scale, kColorDim, kTextAlignCenter);

  char count[32] = "";
  unsigned count_rgba = kColorCyan;
  int n = g_status.match_songs;
  int len = g_status.text_len;
  if (len > 0 && n >= 0) {
    if (n == 0) {
      snprintf(count, sizeof count, "NO MATCH");
      count_rgba = kColorOrange;
    } else {
      snprintf(count, sizeof count, n == 1 ? "%d SONG" : "%d SONGS", n);
    }
  }
  if (count[0]) {
    float cw, ch;
    game_text_size(g, count, &cw, &ch);
    right -= cw * scale;
    draw_game_text(g, count, right, cy, scale, count_rgba, kTextAlignLeft);
    right -= pad;
  }

  bool caret = (flags & kUiFocused) && fmod(now_seconds(), 1.0) < 0.5;
  if (len <= 0) {
    draw_game_text(g, "SEARCH", x, cy, scale, kColorHint, kTextAlignLeft);
    if (caret) draw_game_text(g, kCaret, x, cy, scale, kColorCaret, kTextAlignLeft);
    return;
  }
  if (!(flags & kUiDrawText)) return;
  char text[sizeof g_status.text + 2];
  if (len > (int)sizeof g_status.text - 1) len = sizeof g_status.text - 1;
  memcpy(text, g_status.text, len);
  text[len] = 0;
  // Too long for the box: show the end, like a text field scrolled to the caret.
  const char* shown = text;
  float tw, th;
  game_text_size(g, shown, &tw, &th);
  while (*shown && x + tw * scale > right) {
    shown++;
    game_text_size(g, shown, &tw, &th);
  }
  draw_game_text(g, shown, x, cy, scale, kColorText, kTextAlignLeft);
  if (caret) draw_game_text(g, kCaret, x + tw * scale, cy, scale, kColorCaret, kTextAlignLeft);
}

// The menu header is painted after the list and would cover the box contents,
// so they are drawn at the end of the frame (search_post_render). The list keeps
// being painted under the MODE screen while it fades in and out, so the box only
// counts as on screen in frames where the MODE screen wasn't painted.
void* g_painted_with;
bool g_mode_painted;

void hk_Paint(void* route, void* g) {
  // Paint draws the 2D LIST when Main's list-mode field is set, otherwise the 3D
  // route view (no sort row, so no box); +0x68 is the switch animation between them.
  bool list_view = field<int>(GetMain(), kMainSortKind) != 0 && !field<uint8_t>(route, kRouteChanging);
  if (list_view) g_painted_with = g;
  ListSwap swap;
  orig_Paint(route, g);
}

using ButtonPosFn = void (*)(void*, float*);
using CalcAnchorPosFn = void (*)(int, int, int, int, int, float*);
RoutePaintFn orig_ButtonPaint;
CalcAnchorPosFn CalcAnchorPos;

// The box is laid out from the game's own TITLE and DIFFICULTY sort buttons
// (the layout depends on the screen's aspect ratio): it spans from TITLE's left
// edge to DIFFICULTY's right edge, button height, in the sort-label row below.
Rect g_title_button, g_difficulty_button;

void hk_ButtonPaint(void* btn, void* g) {
  orig_ButtonPaint(btn, g);
  int id = field<int>(btn, kButtonMessage);
  if (id != kMsgSortTitle && id != kMsgSortDifficulty) return;
  float pos[2], tl[2];
  reinterpret_cast<ButtonPosFn>((*static_cast<void***>(btn))[kButtonGetPosSlot])(btn, pos);
  float w = field<float>(btn, kButtonWidth), h = field<float>(btn, kButtonHeight);
  CalcAnchorPos((int)pos[0], (int)pos[1], (int)w, (int)h, field<int>(btn, kButtonAnchor), tl);
  (id == kMsgSortTitle ? g_title_button : g_difficulty_button) = {tl[0], tl[1], tl[0] + w, tl[1] + h};
}

// Lays out the box and publishes it for the Java frame as fractions of the GL
// surface. The draw screen maps onto the GL viewport, which can be inset in the
// surface (e.g. letterboxed on tall phones).
void update_box_rect() {
  const Rect& a = g_title_button;
  const Rect& b = g_difficulty_button;
  if (a.r <= a.l || b.r <= b.l) return;
  float top = a.t + kBoxBelowButtons;
  g_box = {a.l, top, b.r, top + (a.b - a.t)};

  int vp[4];
  glGetIntegerv(GL_VIEWPORT, vp);
  EGLint sw = 0, sh = 0;
  EGLDisplay display = eglGetCurrentDisplay();
  EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
  eglQuerySurface(display, surface, EGL_WIDTH, &sw);
  eglQuerySurface(display, surface, EGL_HEIGHT, &sh);
  float dw = game_screen_width(), dh = game_screen_height();
  if (sw <= 0 || sh <= 0 || vp[2] <= 0 || vp[3] <= 0 || dw <= 0 || dh <= 0) return;
  float vp_top = (float)(sh - vp[1] - vp[3]);  // GL viewport y is from the bottom
  float kx = vp[2] / dw, ky = vp[3] / dh;
  g_status.box[0] = (vp[0] + g_box.l * kx) / sw;
  g_status.box[1] = (vp_top + g_box.t * ky) / sh;
  g_status.box[2] = (vp[0] + g_box.r * kx) / sw;
  g_status.box[3] = (vp_top + g_box.b * ky) / sh;
}

void hk_ModePaint(void* menu, void* g) {
  g_mode_painted = true;
  orig_ModePaint(menu, g);
}

void hk_OnTouch(void* route, int a, int b, int c, bool d, int e) {
  ListSwap swap;
  orig_OnTouch(route, a, b, c, d, e);
}

void hk_OnMove(void* route, int a, int b, int c, int d, int e) {
  ListSwap swap;
  orig_OnMove(route, a, b, c, d, e);
}

// Setup also adds newly available songs to the owned list (and saves), so it must
// always see the real count; its tail call to ChangeRoute applies the filter.
void hk_Setup(void* route) {
  RealCount real;
  orig_Setup(route);
}

void hk_ChangeRoute(void* route, int kind, bool anim) {
  ListSwap swap;
  orig_ChangeRoute(route, kind, anim);
}

void hk_AddStage(void* route, int stage) {
  RealCount real;
  orig_AddStage(route, stage);
}

void hk_SaveGameData(void* app) {
  RealCount real;
  orig_SaveGameData(app);
}

template <typename T>
void hook(T& orig, const char* sym, void* fn) {
  orig = reinterpret_cast<T>(gcp::hook_got(sym, fn));
}

}  // namespace

void search_post_render() {
  if (g_painted_with && !g_mode_painted) {
    update_box_rect();
    g_status.last_list_draw_ms = (int64_t)(now_seconds() * 1000.0);
    g_status.match_songs = g_match_songs;
    draw_search_box(g_painted_with);
  }
  g_painted_with = nullptr;
  g_mode_painted = false;
}

extern "C" JNIEXPORT void JNICALL Java_jp_co_taito_groovecoasterzero_GC2PlusSearch_nativeSetQuery(JNIEnv* env, jclass,
                                                                                                   jstring query) {
  const char* s = env->GetStringUTFChars(query, nullptr);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_pending = s ? s : "";
    g_dirty = true;
  }
  env->ReleaseStringUTFChars(query, s);
}

extern "C" JNIEXPORT jobject JNICALL Java_jp_co_taito_groovecoasterzero_GC2PlusSearch_nativeStatus(JNIEnv* env,
                                                                                                    jclass) {
  return env->NewDirectByteBuffer(&g_status, sizeof g_status);
}

void install_search() {
  GetMain = reinterpret_cast<GetMainFn>(gcp::find_symbol("_Z7GetMainv"));
  if (!GetMain) return;
  hook(orig_SortList, "_ZN13MenuRouteView9_SortListENS_17PLAYLIST_SORTKINDE", (void*)hk_SortList);
  hook(orig_Run, "_ZN13MenuRouteView3RunEi", (void*)hk_Run);
  hook(orig_Paint, "_ZN13MenuRouteView5PaintER13MtxGraphics3D", (void*)hk_Paint);
  orig_ModePaint = reinterpret_cast<RoutePaintFn>(gcp::hook_vtable(
      "_ZTV14ModeSelectMenu", "_ZN14ModeSelectMenu5PaintER13MtxGraphics3D", (void*)hk_ModePaint));
  CalcAnchorPos = reinterpret_cast<CalcAnchorPosFn>(gcp::find_symbol("_Z13CalcAnchorPosiiiiiR7vector2"));
  if (CalcAnchorPos)
    orig_ButtonPaint = reinterpret_cast<RoutePaintFn>(gcp::hook_vtable(
        "_ZTV10MenuButton", "_ZN10MenuButton5PaintER13MtxGraphics3D", (void*)hk_ButtonPaint));
  hook(orig_OnTouch, "_ZN13MenuRouteView7OnTouchEiiibi", (void*)hk_OnTouch);
  hook(orig_OnMove, "_ZN13MenuRouteView6OnMoveEiiiii", (void*)hk_OnMove);
  hook(orig_Setup, "_ZN13MenuRouteView5SetupEv", (void*)hk_Setup);
  hook(orig_ChangeRoute, "_ZN13MenuRouteView11ChangeRouteENS_17PLAYLIST_SORTKINDEb", (void*)hk_ChangeRoute);
  hook(orig_AddStage, "_ZN13MenuRouteView8AddStageEi", (void*)hk_AddStage);
  hook(orig_SaveGameData, "_ZN11TuneAppMain12SaveGameDataEv", (void*)hk_SaveGameData);
}
