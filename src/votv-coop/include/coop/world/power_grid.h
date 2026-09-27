// coop/world/power_grid.h -- the base's power grid, whose simulation runs on the host (PowerGridState). The
// generators wear on the dice of generatorFuckuper's 30 s decay tick, which a client in its announced world
// refuses at the body; a client refuses its own generator break, wear and fullFix too, so a generator breaks,
// wears and mends only on the host, whose rows every client applies by running the same edges itself, a repair as
// the Activate route runs one, after the panel canonical the verbs produced; each row carries its generator's
// repair puzzle (coop/world/power_puzzle.h). A client's player acts on a generator as ops to the host, taken in
// its order from a player within reach; a refused op is answered to its author alone. Its Activate press, which
// the host judges on its own copy of the puzzle, its upgrade install and its puzzle inputs run on the client first
// and are reconciled like the panel's presses (coop/world/power_panel.h), the rows carrying the last op taken from
// each slot: the element-data shape. Its hit is the host's to run, the
// request-and-confirm shape, since a break blacks the whole base out:
// reference/mtasa-blue/Client/mods/deathmatch/logic/CClientPed.cpp:6711-6737 sends and waits,
// reference/mtasa-blue/Server/mods/deathmatch/logic/CGame.cpp:3079 validates. Every peer still arms the timers and
// draws the pole arcs of a broken generator; the coordinate towers' timer belongs to the towers.

#pragma once

#include <cstddef>
#include <cstdint>

namespace coop::net { class Session; struct PowerGridPayload; }

namespace coop::power_grid {

// A client's act on a generator is judged within this reach of it: the Activate button and the upgrade slot are
// pressed within the look-at trace, and a hit lands within a swing. Twice the default armLength, the door and
// keypad lanes' reach, which coop/element/intent_authority pads with the generator's bounds and the puppet's lag.
inline constexpr float kGeneratorReachUU = 400.0f;

// Register the decay tick's and the generators' gates. Idempotent. Session install.
void Install(coop::net::Session* session);

// Settle the gates' registration and say once whether they are live; take the ops that waited for the generators,
// their sender's body or their turn, apply rows that arrived before the generators resolved, and send a joiner the
// rows its world-ready was owed. Game thread, per frame.
void Tick();

// PowerGridState from the wire (router: event_dispatch_world.cpp): the rows on a client, an op on the host.
// Game thread.
void OnReliable(const coop::net::PowerGridPayload& payload, uint8_t senderSlot);

// HOST: the rows to a joiner at its world-ready, or as soon as the generators resolve. Game thread.
void QueueConnectBroadcastForSlot(int slot);

// HOST: a leaver's waiting ops go with it. Game thread.
void OnPeerLeft(uint8_t slot);

// The lane's two halves share one sequence and one send of the rows. CLIENT: the next op's seq, which
// power_puzzle's inputs take here, so the rows' acknowledgement covers both. HOST: a panel changed outside the
// generators' verbs; the rows go at most every 100 ms while changes continue, and the last change always goes.
// Game thread.
uint16_t NextSeq();
void HostPuzzleChanged();

// [dev] the grid drill's readings. CLIENT: my ops the host has not yet taken, those sent this session, and the
// host's last rows (false before they came). HOST: the ops it took this session, puzzle inputs not counted, and
// those it refused. Game thread.
size_t PendingOps();
uint64_t ClientOpsSent();
bool LastRows(coop::net::PowerGridPayload& out);
uint64_t HostOpsTaken();
uint64_t HostOpsRefused();

// [dev] the grid drill's upgrade legs. HOST: refuse installs as if another had filled the generator first. CLIENT:
// an install op with no insert behind it, what a forged client sends. Game thread.
void DevRefuseInstalls(bool on);
void DevSendInstall(int32_t index);

// Say the session's counts and start them again. Session end.
void OnDisconnect();

}  // namespace coop::power_grid
