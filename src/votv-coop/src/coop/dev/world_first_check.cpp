// coop/dev/world_first_check.cpp -- see coop/dev/world_first_check.h.

#include "coop/dev/world_first_check.h"

#include "coop/session/net_pump.h"
#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/engine/world_identity.h"

namespace coop::dev::world_first_check {
namespace {

// By identity, not address: the menu's world or the host's can be handed the booted one's address.
bool g_armed = false;
ue_wrap::CachedObjRef g_booted;

}  // namespace

void Arm() {
    void* const world = ue_wrap::world_identity::CurrentWorld();
    g_booted.Set(world);
    g_armed = true;
    UE_LOGI("[WORLD-FIRST] armed: this client stands in a world of its own (%p)", world);
}

void Tick() {
    if (!g_armed || !coop::net_pump::HasAnnouncedWorldReady()) return;
    g_armed = false;
    void* const world = ue_wrap::world_identity::CurrentWorld();
    if (!world)
        UE_LOGE("[WORLD-FIRST] world-ready came where the world reader names no world: no verdict -- FAIL");
    else if (g_booted.Is(world))
        UE_LOGE("[WORLD-FIRST] world-ready came in the world this client booted (%p): the host's world was never "
                "loaded -- FAIL", world);
    else
        UE_LOGI("[WORLD-FIRST] world-ready came in another world (%p) than the one this client booted -- PASS", world);
}

}  // namespace coop::dev::world_first_check
