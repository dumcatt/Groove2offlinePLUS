// Autoplay.
//
// Every frame GameScene::ExecGameStage2 calls checkHitMark(MarkInfo*, flags&, time)
// for the current note, which dispatches to the per-type checker (tap, flick,
// hold, beat, ...). Those read touch state and finally call setMarkRank. With
// autoplay on we skip the touch checks and judge the note perfectly ourselves:
//   - shot notes: judged at the note time (CalcMarkShotRank -> GREAT)
//   - long notes (hold / slide / beat): "pressed" at the start, judged once the
//     note's end time is reached (CalcMarkLongShotRank over the full span -> GREAT)
//
// Starting autoplay from the mode screen: each difficulty has a REPLAY button
// (ModeSelectMenu), normally only registered once the chart is cleared and has
// a saved replay. We register it for every difficulty, and pressing it plays the
// chart with autoplay instead of replaying. REPLAY sets MenuOutsideData+8, which
// GameScene copies to its replay flag (+0x6222); LoadStageData then calls
// LoadReplaySaveFile, and if that fails the flag is cleared and the stage runs
// live. Autoplay results are shown but never recorded: the result screen's
// resultSceneUpdateSaveParam (best score, rank, replay file) is skipped.
#include "autoplay.h"

#include <string.h>

#include "config.h"
#include "hook.h"
#include "log.h"

namespace {

// MarkInfo: [0] judged state (>0 once judged), [4] press time, [0x48] TuneMarkData*.
constexpr int kMarkJudged = 0x00;
constexpr int kMarkPressTime = 0x04;
constexpr int kMarkInfoData = 0x48;
// TuneMarkData: [8] start time (ms, int), [0xac] end time (ms, float).
constexpr int kDataStart = 0x08;
constexpr int kDataEnd = 0xac;

constexpr int kFlagShowPopup = 0x10;

constexpr int kMenuOutsideReplay = 0x08;
// ModeSelectMenu::Initialize: `cbnz w8, skip` around registering the REPLAY
// widget when (saveFlags & 0x12) != 0x12 (not cleared / no replay saved).
constexpr uintptr_t kReplayButtonCheck = 0x0dc400;
constexpr uint32_t kReplayButtonCheckInsn = 0x350003a8;
constexpr uint32_t kNop = 0xd503201f;

bool g_stage_auto;  // current stage was started from a REPLAY button

using GetMessageFn = const char* (*)(void*, int, int);
GetMessageFn orig_GetMessage;

using SceneFn = void (*)(void*);
using SceneBoolFn = bool (*)(void*);
using GetMenuOutsideDataFn = unsigned char* (*)();

SceneFn orig_LoadStageData;
SceneFn orig_UpdateSaveParam;
bool g_stage_was_auto;  // autoplay was used at any point in the current stage
GetMenuOutsideDataFn GetMenuOutsideData;

using CheckHitMarkFn = bool (*)(void*, void*, unsigned char*, int);
using IsMarkFn = bool (*)(void*, void*);
using ShotRankFn = int (*)(void*, void*, int);
using LongShotRankFn = int (*)(void*, void*, float, float);
using SetMarkRankFn = void (*)(void*, void*, int, int, int, bool);

CheckHitMarkFn orig_checkHitMark;
IsMarkFn IsMarkLongShot;
ShotRankFn CalcMarkShotRank;
LongShotRankFn CalcMarkLongShotRank;
SetMarkRankFn SetMarkRank;

template <typename T>
T& field(void* base, int off) {
  return *reinterpret_cast<T*>(static_cast<char*>(base) + off);
}

void hk_LoadStageData(void* scene) {
  // The menu flag survives retries, unlike the scene flag we let the game clear.
  g_stage_auto = GetMenuOutsideData()[kMenuOutsideReplay] != 0;
  g_stage_was_auto = false;
  LOGI("stage load: auto=%d", g_stage_auto);
  orig_LoadStageData(scene);
}

// The REPLAY button label comes from the sysmes*.dat string table; relabel it.
const char* hk_GetMessage(void* mgr, int table, int id) {
  const char* s = orig_GetMessage(mgr, table, id);
  return s && strcmp(s, "REPLAY") == 0 ? "AUTO" : s;
}

// Failing here makes LoadStageData clear the replay flag and run the stage live.
bool hk_LoadReplaySaveFile(void*) { return false; }

void hk_UpdateSaveParam(void* scene) {
  if (g_stage_was_auto) {
    LOGI("autoplay stage: result not recorded");
    return;
  }
  orig_UpdateSaveParam(scene);
}

bool hk_checkHitMark(void* scene, void* mark, unsigned char* flags, int time) {
  if (!autoplay_active() || !mark) return orig_checkHitMark(scene, mark, flags, time);
  g_stage_was_auto = true;

  void* data = field<void*>(mark, kMarkInfoData);
  int& judged = field<int>(mark, kMarkJudged);
  if (!data || judged > 0) return judged > 0;

  int start = field<int>(data, kDataStart);
  if (time < start) return false;

  if (IsMarkLongShot(scene, data)) {
    int& press = field<int>(mark, kMarkPressTime);
    if (press < 0) press = start;
    float end = field<float>(data, kDataEnd);
    if (time < end) return false;
    int rank = CalcMarkLongShotRank(scene, data, (float)start, end);
    SetMarkRank(scene, mark, time, rank | kFlagShowPopup, -1, true);
  } else {
    field<int>(mark, kMarkPressTime) = start;
    int rank = CalcMarkShotRank(scene, data, start);
    SetMarkRank(scene, mark, start, rank | kFlagShowPopup, -1, true);
  }
  return judged > 0;
}

template <typename T>
bool resolve(T& out, const char* sym) {
  out = reinterpret_cast<T>(gcp::find_symbol(sym));
  return out != nullptr;
}

}  // namespace

bool autoplay_active() { return g_stage_auto || gcp::config().autoplay; }

void install_autoplay() {
  // setMarkRank is called directly rather than through the GOT, so autoplay hits
  // never reach the FAST/LATE hook.
  bool ok = resolve(IsMarkLongShot, "_ZN9GameScene14IsMarkLongShotER12TuneMarkData") &&
            resolve(CalcMarkShotRank, "_ZN9GameScene16CalcMarkShotRankEP12TuneMarkDatai") &&
            resolve(CalcMarkLongShotRank, "_ZN9GameScene20CalcMarkLongShotRankEP12TuneMarkDataff") &&
            resolve(SetMarkRank, "_ZN9GameScene11setMarkRankEP8MarkInfoiiib") &&
            resolve(GetMenuOutsideData, "_Z18GetMenuOutsideDatav");
  if (!ok) return;
  orig_checkHitMark = reinterpret_cast<CheckHitMarkFn>(
      gcp::hook_got("_ZN9GameScene12checkHitMarkEP8MarkInfoRhi", reinterpret_cast<void*>(hk_checkHitMark)));

  if (gcp::config().autobutton && !gcp::patch_insn(kReplayButtonCheck, kReplayButtonCheckInsn, kNop)) return;
  orig_LoadStageData = reinterpret_cast<SceneFn>(
      gcp::hook_got("_ZN9GameScene13LoadStageDataEv", reinterpret_cast<void*>(hk_LoadStageData)));
  gcp::hook_got("_ZN9GameScene18LoadReplaySaveFileEv", reinterpret_cast<void*>(hk_LoadReplaySaveFile));
  orig_GetMessage = reinterpret_cast<GetMessageFn>(
      gcp::hook_got("_ZN17MtxMessageManager10GetMessageEii", reinterpret_cast<void*>(hk_GetMessage)));
  orig_UpdateSaveParam = reinterpret_cast<SceneFn>(gcp::hook_got(
      "_ZN9GameScene26resultSceneUpdateSaveParamEv", reinterpret_cast<void*>(hk_UpdateSaveParam)));
}
