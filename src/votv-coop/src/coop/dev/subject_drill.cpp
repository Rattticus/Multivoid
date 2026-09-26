// coop/dev/subject_drill.cpp -- see coop/dev/subject_drill.h.

#include "coop/dev/subject_drill.h"

#include "coop/config/config.h"
#include "coop/dev/director/aimed_grab.h"
#include "coop/dev/director/director.h"
#include "coop/element/registry.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/props/prop_snapshot.h"  // IsBracketClosed: a joiner's snapshot is over
#include "coop/props/remote_prop.h"    // IsActorUnderAnyDrive: a client's hold, seen on the host
#include "coop/session/join_progress.h"
#include "coop/session/net_pump.h"     // HasAnnouncedWorldReady

#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/desk/drive_chain.h"
#include "ue_wrap/engine/engine.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

namespace coop::dev::subject_drill {
namespace {

namespace DC = ue_wrap::drive_chain;
namespace E  = ue_wrap::engine;
namespace R  = ue_wrap::reflection;
namespace D  = coop::director;
using Clock = std::chrono::steady_clock;

enum class Arm : uint8_t { Off, Insert, Own };
enum class HostStep : uint8_t { Ready, Named, WaitHold, Done };
enum class ClientStep : uint8_t { Ready, Walk, Grab, Hold, Seated, Done };

constexpr float kReachCm = 150.f;                         // the use key's reach, stood off a little
constexpr int   kWalkDeadlineS = 120;
constexpr auto  kStepBound = std::chrono::seconds(60);    // the element lane names the drives; the insert arrives
constexpr auto  kJoinBound = std::chrono::seconds(600);   // a fresh client's boot, load, join and walk

HostStep   g_host = HostStep::Ready;
ClientStep g_client = ClientStep::Ready;
Clock::time_point g_stepAt{};
ue_wrap::CachedObjRef g_drives[2];                // the host's two (insert) or one (own) drives
ue_wrap::CachedObjRef g_held;                     // the client's grabbed drive
std::shared_ptr<D::BackgroundWalk> g_walk;
std::unique_ptr<D::AimedGrab> g_grab;

Arm ArmOf() {
    static const Arm a = [] {
        const std::string v = coop::config::ResolveEnum(::coop::config_registry::rows::subject_drill);
        return v == "insert" ? Arm::Insert : v == "own" ? Arm::Own : Arm::Off;
    }();
    return a;
}

const char* ArmName() { return ArmOf() == Arm::Insert ? "insert" : ArmOf() == Arm::Own ? "own" : "off"; }

void Enter(HostStep h) { g_host = h; g_stepAt = Clock::now(); }
void Enter(ClientStep c) { g_client = c; g_stepAt = Clock::now(); }
bool StepExpired(std::chrono::seconds bound) { return Clock::now() - g_stepAt >= bound; }

void Abandon(const char* who, const char* why) {
    UE_LOGW("[subject_drill] ABANDONED (%s, arm %s): %s", who, ArmName(), why);
    g_host = HostStep::Done;
    g_client = ClientStep::Done;
}

uint32_t EidOf(void* actor) {
    return actor ? static_cast<uint32_t>(coop::element::Registry::Get().EidForActor(actor)) : 0u;
}
bool Named(void* actor) { return actor && coop::element::Registry::Get().EidForActor(actor) != coop::element::kInvalidId; }

void* SpawnDriveBeside(void* player, float forwardCm, float sideCm) {
    ue_wrap::FVector at{};
    void* cls = DC::DriveClass();
    if (!cls || !E::TryGetActorLocation(player, at)) return nullptr;
    const ue_wrap::FVector fwd = E::GetActorForwardVector(player);
    const ue_wrap::FVector side{-fwd.Y, fwd.X, 0.f};
    return E::SpawnActor(cls, {at.X + fwd.X * forwardCm + side.X * sideCm, at.Y + fwd.Y * forwardCm + side.Y * sideCm,
                               at.Z + 40.f});
}

void* PlaySlot() { return DC::EnsureResolved() ? DC::SlotActor(DC::kRoleDeskPlay) : nullptr; }

// The drive a client's hold moves on this host, if any: a named drive the pose stream drives.
void* HeldDrive() {
    struct Find { void* found; } f{nullptr};
    ue_wrap::object_index::ForEachInstance(DC::DriveClass(), [](void* ctx, void* obj, int32_t) {
        auto& q = *static_cast<Find*>(ctx);
        if (!q.found && Named(obj) && coop::remote_prop::IsActorUnderAnyDrive(obj)) q.found = obj;
    }, &f);
    return f.found;
}

void HostTick(coop::net::Session& s) {
    void* player = coop::players::Registry::Get().Local();
    switch (g_host) {
    case HostStep::Ready: {
        // Before any client's world is ready, so a joiner meets the drives through the join.
        if (!player || !PlaySlot()) return;
        for (int slot = 1; slot < static_cast<int>(coop::net::kMaxPeers); ++slot)
            if (s.IsSlotWorldReady(slot)) {
                Abandon("host", "a client's world was ready before the host could bring its drives in");
                return;
            }
        const int n = ArmOf() == Arm::Insert ? 2 : 1;
        for (int i = 0; i < n; ++i) {
            g_drives[i].Set(SpawnDriveBeside(player, 150.f, i == 0 ? 0.f : 120.f));
            if (!g_drives[i].Get()) {
                Abandon("host", "a drive could not be spawned");
                return;
            }
        }
        UE_LOGI("[subject_drill] host spawned %d drive(s) in front of its player", n);
        Enter(HostStep::Named);
        return;
    }
    case HostStep::Named: {
        const int n = ArmOf() == Arm::Insert ? 2 : 1;
        for (int i = 0; i < n; ++i)
            if (!Named(g_drives[i].Get())) {
                if (StepExpired(kStepBound)) Abandon("host", "the element lane did not name the drives within 60 s");
                return;
            }
        UE_LOGI("[subject_drill] host armed (arm %s): drive eid=%u%s", ArmName(), EidOf(g_drives[0].Get()),
                n == 2 ? " and a second, the one to seat" : "");
        Enter(ArmOf() == Arm::Insert ? HostStep::WaitHold : HostStep::Done);
        return;
    }
    case HostStep::WaitHold: {
        // Once a client's join is over and a drive -- its nearest, one of these or the world's -- moves under its
        // hold, one of the host's own that is not held is seated.
        bool joined = false;
        for (int slot = 1; slot < static_cast<int>(coop::net::kMaxPeers) && !joined; ++slot)
            joined = s.IsSlotWorldReady(slot) && coop::prop_snapshot::IsBracketClosed(slot);
        void* held = joined ? HeldDrive() : nullptr;
        if (!held) {
            if (StepExpired(kJoinBound)) Abandon("host", "no client held a drive within 600 s");
            return;
        }
        void* seat = nullptr;
        for (auto& d : g_drives)
            if (void* own = d.Get(); own && own != held && !seat) seat = own;
        void* slotActor = PlaySlot();
        if (!seat || !slotActor || DC::SlotDrive(slotActor) || !DC::CallPutDriveIn(slotActor, seat)) {
            Abandon("host", "no free drive of its own, or the desk's play slot is gone, full, or did not take it");
            return;
        }
        UE_LOGI("[subject_drill] host seated drive eid=%u in the desk's play slot while a client held eid=%u",
                EidOf(seat), EidOf(held));
        Enter(HostStep::Done);
        return;
    }
    case HostStep::Done:
        return;
    }
}

// The nearest named drive on this peer, or null.
void* NearestNamedDrive(void* player) {
    ue_wrap::FVector me{};
    if (!E::TryGetActorLocation(player, me)) return nullptr;
    struct Best { ue_wrap::FVector me; void* drive; float d; } best{me, nullptr, 1e30f};
    ue_wrap::object_index::ForEachInstance(DC::DriveClass(), [](void* ctx, void* obj, int32_t) {
        auto& b = *static_cast<Best*>(ctx);
        ue_wrap::FVector at{};
        if (!Named(obj) || !E::TryGetActorLocation(obj, at)) return;
        const float d = std::hypot(at.X - b.me.X, at.Y - b.me.Y);
        if (d < b.d) { b.d = d; b.drive = obj; }
    }, &best);
    return best.drive;
}

void ClientTick() {
    void* player = coop::players::Registry::Get().Local();
    switch (g_client) {
    case ClientStep::Ready: {
        if (!player || !coop::net_pump::HasAnnouncedWorldReady() ||
            coop::join_progress::CurrentPhase() != coop::join_progress::Phase::Idle || !DC::EnsureResolved() ||
            !DC::DriveClass())
            return;
        if (g_stepAt == Clock::time_point{}) g_stepAt = Clock::now();
        void* drive = NearestNamedDrive(player);
        ue_wrap::FVector at{};
        if (!drive || !E::TryGetActorLocation(drive, at)) {
            if (StepExpired(kStepBound)) Abandon("client", "no named drive here within 60 s of the join");
            return;
        }
        g_held.Set(drive);
        g_walk = D::StartBackgroundWalk(at, kReachCm, kWalkDeadlineS);
        UE_LOGI("[subject_drill] client walks to drive eid=%u", EidOf(drive));
        Enter(ClientStep::Walk);
        return;
    }
    case ClientStep::Walk: {
        const int st = g_walk ? g_walk->state.load() : 2;
        if (st == 0) return;
        if (st == 2) {
            Abandon("client", "the walk to the drive did not arrive");
            return;
        }
        g_grab = std::make_unique<D::AimedGrab>(player, g_held.Get());
        Enter(ClientStep::Grab);
        return;
    }
    case ClientStep::Grab: {
        const D::GrabState gs = g_grab->Tick();
        if (gs == D::GrabState::Working) return;
        if (gs == D::GrabState::Failed) {
            Abandon("client", g_grab->Why());
            return;
        }
        void* slotActor = PlaySlot();
        if (!slotActor || DC::SlotDrive(slotActor)) {
            Abandon("client", "this copy's play slot is gone or already holds a drive");
            return;
        }
        UE_LOGI("[subject_drill] client holds drive eid=%u (aimed after %d fan pose(s))", EidOf(g_held.Get()),
                g_grab->AimPoses());
        if (ArmOf() == Arm::Own) {
            // Its own insert, as the port's overlap runs it while the drive is carried in.
            if (!DC::CallPutDriveIn(slotActor, g_held.Get())) {
                Abandon("client", "its own putDriveIn did not run");
                return;
            }
            Enter(ClientStep::Seated);
            return;
        }
        Enter(ClientStep::Hold);
        return;
    }
    case ClientStep::Hold: {
        // The host's insert, replayed on this copy: its play slot filled with the other drive.
        void* slotActor = PlaySlot();
        void* seated = slotActor ? DC::SlotDrive(slotActor) : nullptr;
        if (!seated) {
            if (D::Grabbing(player) != g_held.Get()) {
                Abandon("client", "the grab ended before any insert reached this copy");
                return;
            }
            if (StepExpired(kStepBound)) Abandon("client", "the host's insert did not reach this copy within 60 s");
            return;
        }
        const bool kept = D::Grabbing(player) == g_held.Get();
        if (kept)
            UE_LOGI("[subject_drill] client DONE -- the host seated drive eid=%u in this copy's play slot and this "
                    "client still holds eid=%u -- PASS", EidOf(seated), EidOf(g_held.Get()));
        else
            UE_LOGW("[subject_drill] client DONE -- the host seated drive eid=%u in this copy's play slot and this "
                    "client's grab of eid=%u ended with it -- FAIL", EidOf(seated), EidOf(g_held.Get()));
        if (kept) D::CallOnPlayer(player, L"dropGrabObject");  // the drill ends with an empty hand
        Enter(ClientStep::Done);
        return;
    }
    case ClientStep::Seated: {
        void* slotActor = PlaySlot();
        const bool seated = slotActor && DC::SlotDrive(slotActor) == g_held.Get();
        const bool ended = D::Grabbing(player) == nullptr;
        if (seated && ended)
            UE_LOGI("[subject_drill] client DONE -- its own insert seated drive eid=%u and ended its grab, as single "
                    "player's does -- PASS", EidOf(g_held.Get()));
        else
            UE_LOGW("[subject_drill] client DONE -- its own insert: seated=%d, grab ended=%d -- FAIL", seated ? 1 : 0,
                    ended ? 1 : 0);
        Enter(ClientStep::Done);
        return;
    }
    case ClientStep::Done:
        return;
    }
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (ArmOf() == Arm::Off || !session || !session->running()) return;
    if (session->role() == coop::net::Role::Host) HostTick(*session);
    else                                           ClientTick();
}

void OnDisconnect() {
    if (ArmOf() == Arm::Off) return;
    g_host = HostStep::Ready;
    g_client = ClientStep::Ready;
    g_stepAt = {};
    for (auto& d : g_drives) d.Reset();
    g_held.Reset();
    g_walk.reset();
    g_grab.reset();
}

}  // namespace coop::dev::subject_drill
