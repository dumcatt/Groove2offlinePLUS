// Debug aid: log the faulting ARM (guest) context on SIGSEGV/SIGBUS, with
// addresses inside libtune.so shown as offsets, then let the previous handler
// run (normal crash/tombstone). Native-bridge tombstones only show the host
// translator's state, so this is the only way to see which game code crashed.
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>

#include "hook.h"
#include "log.h"

namespace {

struct sigaction g_old_segv, g_old_bus;
uintptr_t g_base, g_end;

void log_addr(const char* name, uintptr_t v) {
  if (v >= g_base && v < g_end)
    LOGE("  %-4s = libtune+%#lx", name, (unsigned long)(v - g_base));
  else
    LOGE("  %-4s = %#lx", name, (unsigned long)v);
}

void handler(int sig, siginfo_t* info, void* ctx) {
  auto* uc = static_cast<ucontext_t*>(ctx);
  auto& mc = uc->uc_mcontext;
  LOGE("CRASH sig=%d fault=%p", sig, info->si_addr);
  log_addr("pc", mc.pc);
  log_addr("lr", mc.regs[30]);
  log_addr("sp", mc.sp);
  char name[8];
  for (int i = 0; i < 29; i++) {
    snprintf(name, sizeof name, "x%d", i);
    log_addr(name, mc.regs[i]);
  }
  // Walk saved frame pointers for a short backtrace.
  auto fp = reinterpret_cast<uintptr_t*>(mc.regs[29]);
  for (int i = 0; i < 12 && fp; i++) {
    uintptr_t ret = fp[1];
    snprintf(name, sizeof name, "#%d", i);
    log_addr(name, ret);
    auto next = reinterpret_cast<uintptr_t*>(fp[0]);
    if (next <= fp) break;
    fp = next;
  }
  struct sigaction& old = sig == SIGSEGV ? g_old_segv : g_old_bus;
  sigaction(sig, &old, nullptr);
  raise(sig);
}

}  // namespace

void install_crashlog() {
  void* any = gcp::find_symbol("_Z7GetMainv");
  if (!any) return;
  // libtune spans roughly base .. base+2MB; good enough to tag addresses.
  g_base = reinterpret_cast<uintptr_t>(any) - 0x976e4;
  g_end = g_base + 0x200000;
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = handler;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGSEGV, &sa, &g_old_segv);
  sigaction(SIGBUS, &sa, &g_old_bus);
  LOGI("crash logger installed");
}
