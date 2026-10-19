// libgcp.so: loaded by the patched libtune.so as a DT_NEEDED dependency.
// Constructors of dependencies run after the whole group is relocated, so
// libtune's GOT is already filled in when we get here.
#include "config.h"
#include "hook.h"
#include "log.h"

void install_fast_late();
void install_autoplay();
void install_framerate();
void install_search();
void install_crashlog();
void install_avatar_fix();

__attribute__((constructor)) static void gcp_init() {
  LOGI("GC2Plus loaded");
  if (!gcp::config().enabled) {
    LOGI("gc2plus=0: running unmodified");
    return;
  }
  if (!gcp::hook_init("/libtune.so")) return;
  install_fast_late();
  install_autoplay();
  install_framerate();
  install_search();
  install_avatar_fix();
  install_crashlog();
}
