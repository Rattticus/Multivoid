// ue_wrap/core/script_loop.h -- where the VM's local script loop is, found two ways. The loop is the
// one engine function every script body runs inside (script_gate detours it). Its shape: within its
// body, the return-opcode and nothing-opcode compares and a rip-relative reference to the exec-handler
// table (GNatives).
//   ByCode      -- from the image alone: the table's address from the dispatch site's lea, then every
//                  function that references the table by a rip-relative lea, the one with the loop's
//                  shape. Valid before the exe's static initializers have filled the table.
//   ByHandlers  -- from the filled table: the local-virtual and local-final handlers each hand
//                  ProcessScriptFunction the loop by a rip-relative lea, and the two must agree. Needs
//                  the static initializers.
// Engine-wrapper layer (principle 7): no gameplay or network logic.

#pragma once

#include <cstddef>
#include <cstdint>

namespace ue_wrap::script_loop {

// The exec-handler table's address, from the dispatch site `lea r9,[GNatives]; ... call [r9+rax*8]`.
// Reads code only. Null when the site is not found.
std::uintptr_t* ResolveGNatives();

// Whether the table is filled: at least 200 of its 256 entries point into the main module. False
// before the static initializers have run.
bool TableFilled(const std::uintptr_t* gnatives);

// The loop from the image alone; 0 unless exactly one function passes. `candidates` receives how
// many distinct functions reference the table, for the log.
std::uintptr_t ByCode(std::uintptr_t gnatives, int& candidates);

// The loop from the filled table's two local-call handlers; 0 when either names none, or they
// disagree (`virt` and `fin` receive each one's answer, for the log).
std::uintptr_t ByHandlers(const std::uintptr_t* gnatives, std::uintptr_t& virt, std::uintptr_t& fin);

}  // namespace ue_wrap::script_loop
