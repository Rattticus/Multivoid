// coop/interactables/meadow_db_park.h -- the meadow lane's inbound holding pen, for lines that cannot apply
// yet: deletes that came before their row, and every line that came while this peer's database was away.
// The lane (meadow_db_sync) owns the parse and the apply and hands them in through callbacks, so nothing
// here reads the database. Game thread throughout.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

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

// The lines that came while this peer's database or its laptop widget was away -- a travel between the
// gamemode that held them and the next, or a load -- kept in the order they came, an append, a delete and
// an order line alike, since each is a step of the database the next one assumes. Past the bound a line is
// refused (said), not an older one evicted; a travel's worth of edits is a handful. `hash` names an
// append's or a delete's row. False when refused.
enum class Kind : uint8_t { Append, Delete, Order };
bool Park(Kind kind, std::vector<uint8_t>&& blob, uint64_t hash, uint8_t senderSlot);

// Hand the parked lines back in order until `replay` answers false -- the database went again -- keeping
// that line and the ones after it. Returns the lines replayed.
using ReplayFn = bool (*)(Kind kind, const std::vector<uint8_t>& blob, uint64_t hash, uint8_t senderSlot);
size_t Drain(ReplayFn replay);

size_t Parked();

// [dev] Whether an append of this row waits among the parked lines.
bool ParkedAppend(uint64_t hash);

// Both kinds of waiting line go: a new database, or the session's end.
void Clear();

}  // namespace coop::meadow_db_park
