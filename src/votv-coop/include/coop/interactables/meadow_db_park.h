// coop/interactables/meadow_db_park.h -- the meadow lane's inbound holding pen: deletes that came before
// their row. One entry per outstanding count, held until an append of that content consumes it or its
// time runs out -- the delete-beats-append race cover. The lane (meadow_db_sync) owns the apply and hands
// it in through a callback, so nothing here reads the database. Game thread throughout.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace coop::meadow_db_park {

using Clock = std::chrono::steady_clock;

// Hold a delete that found no row of its content. A delete is held exactly when it did not match, so a
// stream of attacker-chosen hashes grows the pen at line rate, and the time bounds it in time but not in
// rate: past the bound a delete is refused (said), not the oldest evicted, since eviction would let a
// flood push out the legitimate not-yet-arrived-row delete the pen exists to hold. Legitimate entries are
// the deletes racing an append that has not landed yet, a handful within one hold. False when refused.
bool HoldDelete(uint64_t hash, uint8_t senderSlot, Clock::time_point now);

// Whether an arriving append of this content meets a held delete; the delete goes with it.
bool ConsumeDelete(uint64_t hash);

// The held deletes past their time go, each said.
void ExpireDeletes(Clock::time_point now);

// Offer each held delete to `apply` again: true when it found its row and applied, which retires it.
using ApplyDeleteFn = bool (*)(uint64_t hash);
void RetryDeletes(ApplyDeleteFn apply);

size_t HeldDeletes();

void Clear();

}  // namespace coop::meadow_db_park
