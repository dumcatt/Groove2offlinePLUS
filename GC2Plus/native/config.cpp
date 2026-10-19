#include "config.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "log.h"

namespace gcp {
namespace {

constexpr const char* kPath =
    "/storage/emulated/0/Android/data/jp.co.taito.groovecoasterzero/files/gc2plus.ini";

Config g_config;
time_t g_mtime = -1;
time_t g_last_check = 0;

void load() {
  Config c;
  if (FILE* f = fopen(kPath, "r")) {
    char line[128];
    while (fgets(line, sizeof line, f)) {
      char key[64];
      int value;
      if (sscanf(line, " %63[a-z0-9_] = %d", key, &value) != 2) continue;
      if (!strcmp(key, "autoplay")) c.autoplay = value != 0;
      else if (!strcmp(key, "fastlate")) c.fastlate = value != 0;
      else if (!strcmp(key, "interpolate")) c.interpolate = value != 0;
      else if (!strcmp(key, "gc2plus")) c.enabled = value != 0;
      else if (!strcmp(key, "autobutton")) c.autobutton = value != 0;
      else if (!strcmp(key, "allavatars")) c.allavatars = value != 0;
    }
    fclose(f);
  }
  g_config = c;
  LOGI("config: autoplay=%d fastlate=%d interpolate=%d gc2plus=%d autobutton=%d allavatars=%d", c.autoplay,
       c.fastlate, c.interpolate, c.enabled, c.autobutton, c.allavatars);
}

}  // namespace

const Config& config() {
  time_t now = time(nullptr);
  if (now != g_last_check) {
    g_last_check = now;
    struct stat st;
    time_t mtime = stat(kPath, &st) == 0 ? st.st_mtime : 0;
    if (mtime != g_mtime) {
      g_mtime = mtime;
      load();
    }
  }
  return g_config;
}

}  // namespace gcp
