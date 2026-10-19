// Fix for the original game's avatar crash.
//
// TuneAppMain keeps one 12-byte PlayerSaveParam per avatar ({lock flags,
// version, exp}) in a fixed array at Main+0x40b98 with room for 256 avatars,
// but the game data defines 368 (Main+0x2b48). Every access is bounds-checked
// against the avatar count only, so avatars 256+ read and write the item table
// (Main+0x41798) and skin table behind it. Selecting one of them makes
// GetPlayerBottomInfo / GameScene::SetPlayerParam take an item count (~100000)
// as the avatar's version and loop over that many level records -> SIGSEGV.
//
// We move the array to new space at the end of TuneAppMain, sized for 1024
// avatars: operator new(sizeof(TuneAppMain)) is enlarged and every
// `mov wN, #0xb98/#0xb9c/#0xba0; movk wN, #4, lsl #16` that builds an offset
// into the array gets the new low half (the high half stays 4). The init loops
// and LoadGameData's version fix-up loop, which stopped at 256 entries, now
// cover the whole array. Save files keep their format: they still store the
// first 256 entries (memcpy of 0xc00 bytes); avatars 256+ get their lock flags
// and version derived from the game data on every boot, like before.
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "hook.h"
#include "log.h"

namespace {

struct Patch {
  uintptr_t vaddr;
  uint32_t expected;
  uint32_t value;
};

// Generated from libtune.so: player table offset 0x40b98 -> 0x4e300.
constexpr Patch kPatches[] = {
    {0x092938, 0x52817309, 0x529c6009},  // tuneViewController::wwicAvatarBuyDidFinish
    {0x0995d4, 0x52817308, 0x529c6008},  // TuneAppMain::Initialize
    {0x09acb0, 0x52817309, 0x529c6009},  // TuneAppMain::SaveGameData
    {0x09bf48, 0x52817319, 0x529c6019},  // TuneAppMain::LoadBootData
    {0x09c114, 0x5281730a, 0x529c600a},  // TuneAppMain::LoadBootData
    {0x09eacc, 0x52817308, 0x529c6008},  // TuneAppMain::InitGameData
    {0x09ed9c, 0x5281730d, 0x529c600d},  // TuneAppMain::InitGameData
    {0x09f164, 0x5281738a, 0x529c608a},  // TuneAppMain::GetPlayerInfo (version)
    {0x09f168, 0x5281730b, 0x529c600b},  // TuneAppMain::GetPlayerInfo
    {0x09f328, 0x5281738a, 0x529c608a},  // TuneAppMain::GetPlayerBottomInfo (version)
    {0x09facc, 0x52817308, 0x529c6008},  // TuneAppMain::LoadGameData
    {0x09ffe0, 0x5281738c, 0x529c608c},  // TuneAppMain::LoadGameData (version)
    {0x0a024c, 0x52817308, 0x529c6008},  // TuneAppMain::LoadGameDatav1004
    {0x0a060c, 0x52817308, 0x529c6008},  // TuneAppMain::LoadGameDatav2004
    {0x0a0a3c, 0x52817309, 0x529c6009},  // TuneAppMain::CreateBackupData
    {0x0a1220, 0x52817308, 0x529c6008},  // TuneAppMain::LoadBackupData
    {0x0a17c8, 0x52817308, 0x529c6008},  // TuneAppMain::LoadBackupDatav1004
    {0x0a1b0c, 0x52817308, 0x529c6008},  // TuneAppMain::LoadBackupDatav2004
    {0x0a20f8, 0x5281738a, 0x529c608a},  // TuneAppMain::SetPlayerVersion (version)
    {0x0a2130, 0x52817309, 0x529c6009},  // TuneAppMain::SetPlayerLockFlag
    {0x0a23f0, 0x5281730a, 0x529c600a},  // TuneAppMain::GetLevelUpBonus
    {0x0a24d0, 0x5281738b, 0x529c608b},  // TuneAppMain::GetLevelUpBonus (version)
    {0x0abb44, 0x52817309, 0x529c6009},  // TuneAppMain::makePostDataForComDataSync
    {0x0aebc0, 0x5281738e, 0x529c608e},  // GameScene::SetPlayerParam (version)
    {0x0c1b0c, 0x52817409, 0x529c6109},  // GameScene::resultSceneUpdateSaveParam (exp)
    {0x0c1b30, 0x52817409, 0x529c6109},  // GameScene::resultSceneUpdateSaveParam (exp)
    {0x0c1b4c, 0x5281740a, 0x529c610a},  // GameScene::resultSceneUpdateSaveParam (exp)
    {0x0c7d50, 0x52817309, 0x529c6009},  // BuyMenu::UpdateUpgradeNode
    {0x0c97c0, 0x52817309, 0x529c6009},  // BuyMenu::EffectProduct
    {0x0dc62c, 0x52817317, 0x529c6017},  // ModeSelectMenu::Initialize
    {0x0dc658, 0x5281731a, 0x529c601a},  // ModeSelectMenu::Initialize
    {0x0e2ad8, 0x52817318, 0x529c6018},  // StartMenu::Callback_ComDataSync
    {0x0e2c4c, 0x52817318, 0x529c6018},  // StartMenu::Callback_ComDataSync
    {0x0e5540, 0x52817309, 0x529c6009},  // StartMenu::Run
    {0x0e7bdc, 0x52817309, 0x529c6009},  // StartMenu::UpdateUpgradeNode
    // operator new(sizeof(TuneAppMain)): 0x4e2f8 -> 0x51300 (new table at 0x4e300, 1024 entries)
    {0x09203c, 0x529c5f00, 0x52826000},
    {0x092040, 0x72a00080, 0x72a000a0},
    {0x092230, 0x529c5f00, 0x52826000},
    {0x092234, 0x72a00080, 0x72a000a0},
    // PlayerSaveParam::Initialize loops (Initialize, InitGameData): cmp x20, #0xc00 -> #0x3000
    {0x0995f0, 0xf130029f, 0xf1400e9f},
    {0x09eae8, 0xf130029f, 0xf1400e9f},
    // LoadGameData version fix-up loop: cmp x11, #0xfe -> #0x3fe
    {0x0a0044, 0xf103f97f, 0xf10ff97f},
};

// --- allavatars option -----------------------------------------------------
// Bit 1 of an avatar's lock flags means "owned" (ModeSelectMenu lists only
// those). After each save load we set it for every avatar, remembering which
// ones we forced, and clear those again while the game writes its save or
// backup, so the save file keeps the real ownership.
constexpr int kTable = 0x4e300;
constexpr int kStride = 12;
constexpr int kMaxAvatars = 1024;
constexpr int kAvatarCount = 0x2b48;
constexpr uint8_t kOwned = 0x02;

bool g_forced[kMaxAvatars];

uint8_t& lock_flags(void* app, int i) { return *(static_cast<uint8_t*>(app) + kTable + i * kStride); }

int avatar_count(void* app) {
  int n = *reinterpret_cast<int*>(static_cast<char*>(app) + kAvatarCount);
  return n < 0 ? 0 : n > kMaxAvatars ? kMaxAvatars : n;
}

void unlock_all(void* app) {
  int n = avatar_count(app), forced = 0;
  for (int i = 1; i < n; i++) {
    if (lock_flags(app, i) & kOwned) continue;
    lock_flags(app, i) |= kOwned;
    g_forced[i] = true;
    forced++;
  }
  if (forced) LOGI("allavatars: unlocked %d of %d avatars", forced, n - 1);
}

// Clears the forced bits for the lifetime of the object.
struct RealOwnership {
  void* app;
  explicit RealOwnership(void* a) : app(a) {
    for (int i = 0; i < kMaxAvatars; i++)
      if (g_forced[i]) lock_flags(app, i) &= ~kOwned;
  }
  ~RealOwnership() {
    for (int i = 0; i < kMaxAvatars; i++)
      if (g_forced[i]) lock_flags(app, i) |= kOwned;
  }
};

using LoadGameDataFn = bool (*)(void*, char*, int);
using SaveGameDataFn = void (*)(void*);
using CreateBackupDataFn = void* (*)(void*, int*);
LoadGameDataFn orig_LoadGameData;
SaveGameDataFn orig_SaveGameData;
CreateBackupDataFn orig_CreateBackupData;

bool hk_LoadGameData(void* app, char* path, int arg) {
  bool r = orig_LoadGameData(app, path, arg);
  // A fresh load replaces the table, so start over.
  for (bool& f : g_forced) f = false;
  if (gcp::config().allavatars) unlock_all(app);
  return r;
}

void hk_SaveGameData(void* app) {
  RealOwnership real(app);
  orig_SaveGameData(app);
}

void* hk_CreateBackupData(void* app, int* size) {
  RealOwnership real(app);
  return orig_CreateBackupData(app, size);
}

}  // namespace

void install_avatar_fix() {
  // Verify everything first so a mismatched libtune is left untouched.
  for (const Patch& p : kPatches) {
    if (gcp::read_insn(p.vaddr) != p.expected) {
      LOGE("avatar fix: unexpected code at %#lx, not applied", (unsigned long)p.vaddr);
      return;
    }
  }
  for (const Patch& p : kPatches) gcp::patch_insn(p.vaddr, p.expected, p.value);
  LOGI("avatar fix: player table moved (%zu patches)", sizeof kPatches / sizeof kPatches[0]);

  // allavatars works on the moved table, so it is hooked only once that is in place.
  orig_LoadGameData = reinterpret_cast<LoadGameDataFn>(
      gcp::hook_got("_ZN11TuneAppMain12LoadGameDataEPci", reinterpret_cast<void*>(hk_LoadGameData)));
  orig_SaveGameData = reinterpret_cast<SaveGameDataFn>(
      gcp::hook_got("_ZN11TuneAppMain12SaveGameDataEv", reinterpret_cast<void*>(hk_SaveGameData)));
  orig_CreateBackupData = reinterpret_cast<CreateBackupDataFn>(
      gcp::hook_got("_ZN11TuneAppMain16CreateBackupDataEPi", reinterpret_cast<void*>(hk_CreateBackupData)));
}
