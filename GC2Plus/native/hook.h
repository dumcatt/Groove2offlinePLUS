// Minimal PLT/GOT hooking for libtune.so.
// libtune calls even its own exported functions through the PLT, so swapping
// a GOT slot redirects every internal call without touching code pages.
#pragma once
#include <cstdint>

namespace gcp {

bool hook_init(const char* lib_suffix);

// Replace the GOT slot for `symbol`; returns the previous target (nullptr if not found).
void* hook_got(const char* symbol, void* replacement);

// Replace `method` in the vtable `vtable` (both symbol names); returns the
// original method (nullptr if not found). For virtual calls, which bypass the PLT.
void* hook_vtable(const char* vtable, const char* method, void* replacement);

// Address of an exported symbol in the target library (nullptr if not found).
void* find_symbol(const char* symbol);

// Current instruction word at `vaddr` (file virtual address).
uint32_t read_insn(uintptr_t vaddr);

// Overwrite one instruction at `vaddr` (file virtual address) if it currently
// equals `expected`. Must run before the code is first executed (i.e. from our
// constructor) so the ARM translator never caches the old instruction.
bool patch_insn(uintptr_t vaddr, uint32_t expected, uint32_t value);

}  // namespace gcp
