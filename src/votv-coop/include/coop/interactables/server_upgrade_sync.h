// coop/interactables/server_upgrade_sync.h -- the signal servers' physical upgrades (ServerUpgradeState).
// Gameplay/network layer (principle 7): it reaches the engine only through ue_wrap::serverbox.
//
// A box's level (serverBox_C.upgrades, 0..3) changes only at its two verbs, the install (playerUsedOn with
// a held upgrade) and the take-out (actionOptionIndex, action 4), and both are watched on the box's class
// at the script gate: the entry reads the box's level and the exit sends what the body changed. A client
// sends the op, install or take-out, by the box's servers[] index; the host applies it inside the game's
// own 0..3, runs the box's updUpgrades and broadcasts every box's level, and a peer adopts that canonical
// wholesale. An op that lost a race goes back to its author with the canonical (the physmods shape, MTA's
// refused element data): a refused install is refunded by a host spawn of the upgrade at the box, and a
// refused take-out's handed-over prop is destroyed on its author and reaped at the host. The host's own
// verbs broadcast at their exit. Late join: every box's level at the joiner's world-ready. The residuals:
// only a peer's last take-out is tracked, so a second one inside a round trip, or a refused one's upgrade
// dropped inside it, is not taken back. Game thread.

#pragma once

#include <cstdint>

namespace coop::net {
class Session;
struct ServerUpgradeStatePayload;
}  // namespace coop::net

namespace coop::server_upgrade_sync {

void Install(coop::net::Session* session);

// Per net-pump tick: a canonical that arrived before the servers resolved applies once they have.
void Tick();

// ServerUpgradeState from the wire (router: event_dispatch_world.cpp).
void OnState(const coop::net::ServerUpgradeStatePayload& p, uint8_t senderSlot);

// HOST: every box's level to a joiner, at its world-ready.
void QueueConnectBroadcastForSlot(int slot);

// HOST, from the client birth author: whether a new upgrade prop from this sender is the handed-over prop
// of a take-out this host just refused. True consumes the refusal and refuses the birth.
bool HostShouldReapUpgradeBirth(uint8_t senderSlot);

// [dev] How many canonicals this peer has adopted: a client counting them knows the host answered.
uint64_t CanonicalsAdopted();

// [dev] The client ops this host applied and refused this session, for a drill's verdict.
struct HostCounts { uint64_t installs = 0, takeOuts = 0, refused = 0; };
HostCounts HostOpCounts();

// [dev] HOST: refuse every client op as if it had lost its race, so a drill reaches the refusal paths --
// the refund of an install, the removal of a take-out's upgrade -- without timing two peers against each other.
void DebugRefuseOps(bool on);

void OnDisconnect();

}  // namespace coop::server_upgrade_sync
