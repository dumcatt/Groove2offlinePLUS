#include "hook.h"

#include <elf.h>
#include <link.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "log.h"

namespace gcp {
namespace {

struct Target {
  ElfW(Addr) bias = 0;
  const ElfW(Phdr)* phdr = nullptr;
  ElfW(Half) phnum = 0;
  const ElfW(Sym)* symtab = nullptr;
  const char* strtab = nullptr;
  uint32_t nsyms = 0;
  const ElfW(Rela)* jmprel = nullptr;
  size_t jmprel_count = 0;
} g;

const char* g_suffix;

int find_cb(dl_phdr_info* info, size_t, void*) {
  const char* name = info->dlpi_name;
  if (!name) return 0;
  size_t n = strlen(name), s = strlen(g_suffix);
  if (n < s || strcmp(name + n - s, g_suffix) != 0) return 0;
  g.bias = info->dlpi_addr;
  g.phdr = info->dlpi_phdr;
  g.phnum = info->dlpi_phnum;
  return 1;
}

}  // namespace

bool hook_init(const char* lib_suffix) {
  g_suffix = lib_suffix;
  if (!dl_iterate_phdr(find_cb, nullptr)) {
    LOGE("%s not found", lib_suffix);
    return false;
  }
  const ElfW(Dyn)* dyn = nullptr;
  for (int i = 0; i < g.phnum; i++)
    if (g.phdr[i].p_type == PT_DYNAMIC)
      dyn = reinterpret_cast<const ElfW(Dyn)*>(g.bias + g.phdr[i].p_vaddr);
  if (!dyn) return false;

  size_t pltrelsz = 0;
  for (; dyn->d_tag != DT_NULL; dyn++) {
    switch (dyn->d_tag) {
      case DT_SYMTAB: g.symtab = reinterpret_cast<const ElfW(Sym)*>(g.bias + dyn->d_un.d_ptr); break;
      case DT_STRTAB: g.strtab = reinterpret_cast<const char*>(g.bias + dyn->d_un.d_ptr); break;
      case DT_JMPREL: g.jmprel = reinterpret_cast<const ElfW(Rela)*>(g.bias + dyn->d_un.d_ptr); break;
      case DT_PLTRELSZ: pltrelsz = dyn->d_un.d_val; break;
      case DT_HASH: g.nsyms = reinterpret_cast<const uint32_t*>(g.bias + dyn->d_un.d_ptr)[1]; break;
    }
  }
  g.jmprel_count = pltrelsz / sizeof(ElfW(Rela));
  LOGI("target base=%p syms=%u plt=%zu", (void*)g.bias, g.nsyms, g.jmprel_count);
  return g.symtab && g.strtab && g.jmprel && g.nsyms;
}

void* find_symbol(const char* symbol) {
  for (uint32_t i = 0; i < g.nsyms; i++) {
    const ElfW(Sym)& s = g.symtab[i];
    if (s.st_value && s.st_shndx != SHN_UNDEF && strcmp(g.strtab + s.st_name, symbol) == 0)
      return reinterpret_cast<void*>(g.bias + s.st_value);
  }
  LOGE("symbol not found: %s", symbol);
  return nullptr;
}

uint32_t read_insn(uintptr_t vaddr) { return *reinterpret_cast<const uint32_t*>(g.bias + vaddr); }

bool patch_insn(uintptr_t vaddr, uint32_t expected, uint32_t value) {
  auto insn = reinterpret_cast<uint32_t*>(g.bias + vaddr);
  if (*insn != expected) {
    LOGE("patch %#lx: found %#x, expected %#x", (unsigned long)vaddr, *insn, expected);
    return false;
  }
  long page = sysconf(_SC_PAGESIZE);
  auto start = reinterpret_cast<uintptr_t>(insn) & ~(uintptr_t)(page - 1);
  if (mprotect(reinterpret_cast<void*>(start), page, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
    LOGE("mprotect failed for patch %#lx", (unsigned long)vaddr);
    return false;
  }
  *insn = value;
  mprotect(reinterpret_cast<void*>(start), page, PROT_READ | PROT_EXEC);
  __builtin___clear_cache(reinterpret_cast<char*>(insn), reinterpret_cast<char*>(insn + 1));
  LOGI("patched %#lx", (unsigned long)vaddr);
  return true;
}

void* hook_got(const char* symbol, void* replacement) {
  for (size_t i = 0; i < g.jmprel_count; i++) {
    const ElfW(Rela)& r = g.jmprel[i];
    if (strcmp(g.strtab + g.symtab[ELF64_R_SYM(r.r_info)].st_name, symbol) != 0) continue;

    auto slot = reinterpret_cast<void**>(g.bias + r.r_offset);
    long page = sysconf(_SC_PAGESIZE);
    auto start = reinterpret_cast<uintptr_t>(slot) & ~(uintptr_t)(page - 1);
    // The GOT sits inside PT_GNU_RELRO, which is read-only after relocation.
    if (mprotect(reinterpret_cast<void*>(start), page, PROT_READ | PROT_WRITE) != 0) {
      LOGE("mprotect failed for %s", symbol);
      return nullptr;
    }
    void* old = *slot;
    *slot = replacement;
    mprotect(reinterpret_cast<void*>(start), page, PROT_READ);
    __builtin___clear_cache(reinterpret_cast<char*>(slot), reinterpret_cast<char*>(slot + 1));
    LOGI("hooked %s (%p -> %p)", symbol, old, replacement);
    return old;
  }
  LOGE("no PLT slot for %s", symbol);
  return nullptr;
}

void* hook_vtable(const char* vtable, const char* method, void* replacement) {
  auto table = static_cast<void**>(find_symbol(vtable));
  void* target = find_symbol(method);
  if (!table || !target) return nullptr;
  // Virtual calls never go through the PLT; swap the method's vtable entry.
  for (int i = 0; i < 64; i++) {
    void** slot = table + i;
    if (*slot != target) continue;
    long page = sysconf(_SC_PAGESIZE);
    auto start = reinterpret_cast<uintptr_t>(slot) & ~(uintptr_t)(page - 1);
    if (mprotect(reinterpret_cast<void*>(start), page, PROT_READ | PROT_WRITE) != 0) {
      LOGE("mprotect failed for %s", method);
      return nullptr;
    }
    *slot = replacement;
    mprotect(reinterpret_cast<void*>(start), page, PROT_READ);
    LOGI("hooked %s in %s[%d]", method, vtable, i);
    return target;
  }
  LOGE("%s not found in %s", method, vtable);
  return nullptr;
}

}  // namespace gcp
