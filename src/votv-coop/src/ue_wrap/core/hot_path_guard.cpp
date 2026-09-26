// ue_wrap/core/hot_path_guard.cpp -- see ue_wrap/core/hot_path_guard.h.

#include "ue_wrap/core/hot_path_guard.h"

#include <windows.h>

#include <cstdint>

extern "C" IMAGE_DOS_HEADER __ImageBase;  // the linker's symbol for this module's base

namespace ue_wrap::hot_path {

unsigned long long CallerRva(const void* returnAddress) {
    return static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(returnAddress) -
                                           reinterpret_cast<uintptr_t>(&__ImageBase));
}

unsigned long CurrentThreadId() { return ::GetCurrentThreadId(); }

}  // namespace ue_wrap::hot_path
