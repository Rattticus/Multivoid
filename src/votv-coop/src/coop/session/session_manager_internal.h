// coop/session/session_manager_internal.h -- what the session manager's host lane
// (session_manager.cpp) and join lane (session_join.cpp) share: the one-action-at-a-time latch, the
// pending-start hand-off and a master answer's P2P config. Co-located, not a public header.

#pragma once

#include "coop/net/session.h"  // net::Config

#include <atomic>

namespace coop::session_manager::internal {

// Serialises the session-start actions (host, join, direct connect): one in flight at a time.
// Refresh is not gated.
extern std::atomic<bool> g_actionBusy;

// One queued session start (last action wins until the harness consumes it).
void QueueStart(const net::Config& cfg);

// A master lobby's P2P session Config, for the host and the joiner alike: the master's answer
// names the rendezvous (its signaling relay and token) and the ICE servers (STUN, the TURN
// credential it minted for this peer). lobby::HostInfo and lobby::JoinInfo carry the three fields
// and the credential this reads alike. The candidate policy is the player's, read at every session
// start (coop/net/ice_policy.h).
template <typename MasterAnswer>
net::Config LobbyP2PConfig(net::Role role, const MasterAnswer& info) {
    net::Config cfg;
    cfg.role = role;
    cfg.topology = net::Topology::P2P;
    cfg.signalingUrl = info.signalingUrl;
    cfg.signalingToken = info.signalingToken;
    cfg.stunList = info.stun;
    cfg.turnList = info.turn.uri;
    cfg.turnUser = info.turn.user;
    cfg.turnPass = info.turn.pass;
    cfg.turnTtlS = info.turn.ttlS;
    return cfg;
}

}  // namespace coop::session_manager::internal
