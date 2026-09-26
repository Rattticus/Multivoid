// coop/net/ice_config.cpp -- see ice_config.h.

#include "ice_config.h"

#include "coop/net/net_clock.h"  // NowMs, the net layer's one steady clock
#include "ue_wrap/core/log.h"

#include <atomic>

#pragma warning(push)
#pragma warning(disable: 4100 4127 4191 4244 4245 4267 4310 4324 4458)
#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingtypes.h>
#pragma warning(pop)

namespace coop::net {
namespace {

// The applied credential's lapse on NowMs, and its stated lifetime for the line; 0 when the applied
// TURN list has no credential with a stated lifetime. Process-global, as the values GNS holds are.
std::atomic<uint64_t> g_turnLapseAtMs{0};
std::atomic<int>      g_turnTtlS{0};

}  // namespace

bool ApplyGlobalIceConfig(const IceConfig& ice) {
    auto* utils = SteamNetworkingUtils();
    if (!utils) {
        UE_LOGE("ice: SteamNetworkingUtils() null -- GNS not initialized");
        return false;
    }

    // Every value is written, because these are process-global and a session must not run on its
    // predecessor's: a previous session's relay-only policy, or a TURN credential minted for a
    // lobby that ended, stays in effect until something overwrites it. An empty STUN list is
    // meaningful to GNS ("NAT piercing will not be attempted"), and an empty TURN list offers no
    // relay candidate. Each write is checked by name; the first refusal ends the chain and the
    // start, since the values after it are still the previous session's.
    const char* refused = nullptr;
    if (!utils->SetGlobalConfigValueInt32(
            k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable,
            ice.relayOnly ? k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Relay
                          : k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_All))
        refused = "the candidate policy";
    else if (!utils->SetGlobalConfigValueString(
                 k_ESteamNetworkingConfig_P2P_STUN_ServerList, ice.stunList.c_str()))
        refused = "the STUN list";
    else if (!utils->SetGlobalConfigValueString(
                 k_ESteamNetworkingConfig_P2P_TURN_ServerList, ice.turnList.c_str()))
        refused = "the TURN list";
    else if (!utils->SetGlobalConfigValueString(
                 k_ESteamNetworkingConfig_P2P_TURN_UserList, ice.turnUser.c_str()))
        refused = "the TURN user list";
    else if (!utils->SetGlobalConfigValueString(
                 k_ESteamNetworkingConfig_P2P_TURN_PassList, ice.turnPass.c_str()))
        refused = "the TURN password list";
    if (refused) {
        UE_LOGE("ice: GNS refused %s -- not applied (policy=%s)", refused,
                ice.relayOnly ? "relay" : "all");
        return false;
    }

    UE_LOGI("ice: applied policy=%s stun='%s' turn='%s'",
            ice.relayOnly ? "relay" : "all",
            ice.stunList.empty() ? "(none)" : ice.stunList.c_str(),
            ice.turnList.empty() ? "(none)" : ice.turnList.c_str());
    // Counted from here, not from the mint: the mint came first, so a lapse printed by this count
    // is never early.
    const bool timed = !ice.turnList.empty() && ice.turnTtlS > 0;
    g_turnTtlS.store(timed ? ice.turnTtlS : 0, std::memory_order_relaxed);
    g_turnLapseAtMs.store(timed ? NowMs() + uint64_t(ice.turnTtlS) * 1000 : 0,
                          std::memory_order_relaxed);
    if (timed)
        UE_LOGI("ice: turn credential valid %d s", ice.turnTtlS);
    else if (!ice.turnList.empty())
        UE_LOGI("ice: turn credential with no stated lifetime");
    return true;
}

void TickTurnCredential(uint64_t nowMs) {
    uint64_t at = g_turnLapseAtMs.load(std::memory_order_relaxed);
    if (at == 0 || nowMs < at) return;
    // The exchange makes the line once per apply, whichever thread gets there first.
    if (!g_turnLapseAtMs.compare_exchange_strong(at, 0, std::memory_order_relaxed)) return;
    UE_LOGI("ice: turn credential lapsed, %d s after its apply -- a connection whose ICE starts "
            "from now gets no relay candidate from it", g_turnTtlS.load(std::memory_order_relaxed));
}

}  // namespace coop::net
