// coop/net/ice_config.h -- ICE (STUN/TURN) configuration for P2P connections.
//
// Co-located net-internal header (src tree, not include/ -- same as session_lanes.h): session_start.cpp
// applies a P2P session's config, the lobby announcer hands the host's renewed TURN credential over, and the
// net thread writes it and counts the credential's lifetime. Keeps the GNS config-value enums out of
// session.h: callers describe ICE in our own vocabulary; the .cpp maps it to SetGlobalConfigValue*.
//
// One session per process, so the config is applied GLOBALLY (the shape the working test_p2p example
// proves), not per-connection. GNS reads the TURN lists when each connection's ICE starts, at its accept,
// so a renewal written mid-session reaches every connection accepted after it.

#pragma once

#include "coop/net/turn_credential.h"

#include <cstdint>
#include <string>

namespace coop::net {

struct IceConfig {
    std::string stunList;   // "host:port,host2:port" -- "" disables STUN (rung 2)
    std::string turnList;   // "turn:host:port,..."   -- "" disables TURN (rung 3)
    std::string turnUser;   // parallel to turnList (coturn REST creds)
    std::string turnPass;   // parallel to turnList
    int         turnTtlS = 0;  // the credential's lifetime its master stated; 0 when none was
    // Which candidates to gather and share: every kind (private, public, relay: rungs 1-3), or the
    // TURN relay's alone, so the peer is shown the relay's address instead of ours.
    bool        relayOnly = false;
};

// Apply the ICE configuration to GNS as GLOBAL config values: the candidate policy, the STUN list
// and all three TURN lists, written every time, empty included, so after a true return nothing of
// a previous session in this process carries into the next, a queued renewal included. False when
// GNS refused a value: the writes stop there and the ones after it keep the previous session's
// values, so the caller must end the start, which is what keeps them unused. Call after
// GameNetworkingSockets_Init and before CreateListenSocketP2P / Connect, on a thread where
// SteamNetworkingUtils() is valid (post-init).
bool ApplyGlobalIceConfig(const IceConfig& ice);

// The net thread's pass over the TURN credential, between its passes over the transport's callbacks: writes
// a queued renewal (below), then counts the credential's lifetime from its apply or renewal, printing once,
// at or past the lapse, that a connection whose ICE starts from then on gets no relay candidate from it. A
// credential with no stated lifetime never lapses here. Called every pass with NowMs (net_clock.h): an
// acquire and a relaxed load until there is something to do.
void TickTurnCredential(uint64_t nowMs);

// The username of the TURN credential the running session holds, or "" with none. Any thread.
std::string AppliedTurnUser();

// The session ended: it holds no credential any more, a queued renewal is dropped and nothing counts
// down. Called from Session::Stop once its net thread has been joined.
void ForgetTurnCredential();

// A renewal of the host's credential, handed to the net thread, which writes the three TURN lists together
// (an accept between two separate writes would pair a new user with an old password) and only while the
// session still holds `replaces`; a later renewal replaces an unwritten one. False, with a line, for a
// credential with a field missing, which is never queued. Any thread.
bool QueueTurnRenewal(const std::string& replaces, const TurnCredential& fresh);

}  // namespace coop::net
