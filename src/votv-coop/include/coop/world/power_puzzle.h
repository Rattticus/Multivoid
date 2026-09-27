// coop/world/power_puzzle.h -- the generators' repair puzzles, the grid lane's second half: coop/world/power_grid
// owns the wire, the ops' order and their acknowledgement. The targets are the host's world, and a client never
// rolls them: the panel's three randomizers are refused on a client. The values belong to the player inside the
// panel, one at a time (coop/interactables/device_occupancy): a client's input is its prediction, sent as op 4
// with the field's absolute value and taken by the host in the sender's order, so an Activate press is judged
// after the inputs it rests on. Every row carries its generator's puzzle, which a client writes after the rows'
// verbs with its untaken inputs on top. Game thread throughout.

#pragma once

#include "coop/net/protocol.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace coop::net { class Session; }

namespace coop::power_puzzle {

// The panel's watches: a client's inputs and a change of the host's panel at the exit of the panel's event
// graph, and a client's refused rolls at the three randomizers. Idempotent.
void Install(coop::net::Session* session);

// HOST: the row's puzzle read from `gen`'s panel; `valid` 0 while the panel is not readable.
void Fill(void* gen, coop::net::PowerGridPuzzle& out);

// Whether two rows' puzzles say the same.
bool SamePuzzle(const coop::net::PowerGridPuzzle& a, const coop::net::PowerGridPuzzle& b);

// HOST: whether `gen`'s panel reads solved, the Activate button's own check, and whether a click's move on it is still
// running, until whose end the check trails the values.
bool Solved(void* gen);
bool Settling(void* gen);

// HOST: a client's op 4 on `gen`, whose reach power_grid has checked. Null when taken: the field written and
// drawn. Otherwise the refusal, or, with `wait` set, not yet judged: nobody holds the panel's claim, which rides
// another kind and can arrive after the op; `claimWaited` says the op has waited out that allowance.
const char* HostTake(uint8_t sender, const coop::net::PowerGridPayload& p, void* gen, bool broken, bool claimWaited,
                     bool& wait);

// CLIENT: the host's rows arrived: my inputs up to `ack` are taken, and each row's puzzle is the canonical.
void OnRows(const coop::net::PowerGridPayload& p, uint16_t ack);

// CLIENT: each generator's panel written to its canonical puzzle with my untaken inputs on top.
void Reconcile(const std::vector<void*>& gens);

// CLIENT: every input still held back by the send rate, sent now: before an op that rests on them.
void FlushInputs();

// Per tick. CLIENT: the held-back inputs that are due. HOST: nothing yet (power_grid sends the coalesced rows).
void Tick();

void OnDisconnect();

// [dev] the drill's reads: the canonical of generator `index` this client holds, the inputs it has sent, and those
// the host has not yet taken (held back ones included).
bool Canonical(int32_t index, coop::net::PowerGridPuzzle& out);
uint64_t InputsSent();
size_t UntakenInputs();

}  // namespace coop::power_puzzle
