// ue_wrap/world/red_sky.cpp -- see ue_wrap/world/red_sky.h.

#include "ue_wrap/world/red_sky.h"

#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/sdk_profile.h"
#include "ue_wrap/world/world_singleton.h"

#include <cstdint>

namespace ue_wrap::red_sky {
namespace {

namespace P = profile;
namespace R = reflection;

// The event's isred. Its place is the class's layout, the same in every world, so it is found once, off the first
// live event; the event itself is read through the gamemode per call.
int32_t g_isRedOff = -2;  // -2 not asked yet, -1 did not resolve
uint8_t g_isRedMask = 0;

void* GamemodeOr(void* gamemode) { return gamemode ? gamemode : world_singleton::Gamemode(); }

// The event the gamemode's redSky holds, when it is live.
void* LiveEvent(void* gamemode) {
    void* event = *reinterpret_cast<void* const*>(static_cast<const char*>(gamemode) + P::off::AmainGamemode_redSky);
    return event && R::IsLive(event) ? event : nullptr;
}

}  // namespace

bool ReadIsRed(void* event, bool& red) {
    if (!event) return false;
    if (g_isRedOff == -2) {
        int32_t off = -1;
        g_isRedOff = R::FindBoolProperty(R::ClassOf(event), L"isred", off, g_isRedMask) ? off : -1;
        if (g_isRedOff < 0) UE_LOGW("red_sky: redSkyEvent_C.isred did not resolve -- a red sky cannot be read");
    }
    if (g_isRedOff < 0) return false;
    red = (*(static_cast<const uint8_t*>(event) + g_isRedOff) & g_isRedMask) != 0;
    return true;
}

bool ReadLive(void* gamemode, bool& live) {
    void* gm = GamemodeOr(gamemode);
    if (!gm) return false;
    live = LiveEvent(gm) != nullptr;
    return true;
}

bool Read(void* gamemode, bool& red) {
    void* gm = GamemodeOr(gamemode);
    if (!gm) return false;
    void* event = LiveEvent(gm);
    if (!event) {
        red = false;
        return true;
    }
    return ReadIsRed(event, red);
}

bool CallSpawn() {
    void* gm = world_singleton::Gamemode();
    void* fn = gm ? R::FindDispatchFunctionCached(R::ClassOf(gm), P::name::MainGamemode_SpawnRedSkyFn) : nullptr;
    if (!fn) return false;
    ParamFrame f(fn);
    if (!f.valid()) return false;
    return Call(gm, f);
}

}  // namespace ue_wrap::red_sky
