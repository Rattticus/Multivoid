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
// rate: past the bound a delete is refused, not the oldest evicted, since eviction would let a flood push
// out the legitimate not-yet-arrived-row delete the pen exists to hold; the first refusal of an episode is
// said, and the count when a hold is taken again or the pen clears. Legitimate entries are the deletes
// racing an append that has not landed yet, a handful within one hold. False when refused.
bool HoldDelete(uint64_t hash, uint8_t senderSlot, Clock::time_point now);

// Whether an arriving append of this content meets a held delete; the delete goes with it.
bool ConsumeDelete(uint64_t hash);

// The held deletes past their time go, each said.
void ExpireDeletes(Clock::time_point now);

// A held delete's clock stands still while its append may be waiting for this peer's database: the lane
// stops expiring while anything is parked or the database is away, and on its way back gives every held
// delete a full hold again, from now.
void RestampDeletes(Clock::time_point now);

// Offer each held delete to `apply` again: true when it found its row and applied, which retires it.
using ApplyDeleteFn = bool (*)(uint64_t hash);
void RetryDeletes(ApplyDeleteFn apply);

size_t HeldDeletes();

// The lines that came while this peer's database or its laptop widget was away -- a travel between the
// gamemode that held them and the next (a load is a new database, which drops them) -- kept in the order
// they came, an append, a delete and an order line alike, since each is a step of the database the next one
// assumes. An order line right behind one of the same sender's replaces it, as the newer order the older
// one. Past the bounds (lines and bytes) a line is refused, not an older one evicted; the first refusal of
// an episode is said, and the count when the episode ends. A travel's worth of edits is a handful. `hash`
// names an append's or a delete's row. False when refused.
enum class Kind : uint8_t { Append, Delete, Order };
bool Park(Kind kind, std::vector<uint8_t>&& blob, uint64_t hash, uint8_t senderSlot);

// Hand at most `budget` parked lines back in order, each taken out before its replay, until `replay`
// answers false -- the database went again -- which puts that line back at the head. A Clear inside a
// replay ends the drain with nothing put back. Returns the lines replayed.
using ReplayFn = bool (*)(Kind kind, const std::vector<uint8_t>& blob, uint64_t hash, uint8_t senderSlot);
size_t Drain(ReplayFn replay, size_t budget);

size_t Parked();

// Visit the first `upTo` parked lines in the order they came, without taking them out.
using VisitFn = void (*)(void* ctx, Kind kind, const std::vector<uint8_t>& blob, uint64_t hash, uint8_t senderSlot);
void ForEachParked(size_t upTo, VisitFn fn, void* ctx);

// [dev] Whether an append of this row waits among the parked lines.
bool ParkedAppend(uint64_t hash);

// Both kinds of waiting line go: a new database, or the session's end. Returns the parked lines dropped.
size_t Clear();

}  // namespace coop::meadow_db_park
