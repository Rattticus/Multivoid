// coop/dev/grid_drill.cpp -- see coop/dev/grid_drill.h.

#include "coop/dev/grid_drill.h"

#include "coop/config/config.h"
#include "coop/config/config_registry.h"
#include "coop/dev/director/director.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"
#include "coop/session/join_progress.h"
#include "coop/session/net_pump.h"  // HasAnnouncedWorldReady
#include "coop/world/power_grid.h"
#include "coop/world/power_panel.h"

#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/desk/console_desk.h"
#include "ue_wrap/devices/generator.h"
#include "ue_wrap/devices/laptop.h"
#include "ue_wrap/devices/power_control.h"
#include "ue_wrap/engine/engine.h"            // TryGetActorLocation
#include "ue_wrap/engine/engine_component.h"  // GetComponentLocation
#include "ue_wrap/engine/engine_nav.h"        // FindNavPath

#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace coop::dev::grid_drill {
namespace {

namespace PC  = ue_wrap::power_control;
namespace GEN = ue_wrap::generator;
namespace CD  = ue_wrap::console_desk;

constexpr int      kLightBit      = 4;       // the panel's light lever
constexpr uint8_t  kCalcBit       = 0x08;    // the calc breaker: the lockout's end switches the servers with it
constexpr float    kPressReachCm  = 150.f;   // a lever, within the look-at trace's 200
// The host's reach for a generator op (coop/world/power_grid): a repair pressed within it must be taken; one pressed
// beyond anything its pads add (the generator's bounds and the puppet's lag, a few hundred uu) must be refused.
constexpr float    kGeneratorReachUU = 400.f;
constexpr float    kBeyondPadsUU     = 5000.f;
constexpr uint64_t kLockPollMs       = 250;     // the lock legs read the servers at this cadence
constexpr int      kWalkDeadlineS = 240;
// Failure bounds only: each leg ends on the state it waits for, and these say it never came.
constexpr uint64_t kAckBoundMs    = 15000;
constexpr uint64_t kRowBoundMs    = 30000;
constexpr uint64_t kLockBoundMs   = 90000;   // the desk virus's lockout lasts 60 s on the host

enum class Arm : uint8_t { Off, Run, Red, Join, Lockout, LockJoin };
Arm Mode() {
    static const Arm a = [] {
        const std::string v = coop::config::ResolveString(::coop::config_registry::rows::grid_drill);
        return v == "run" ? Arm::Run : v == "red" ? Arm::Red : v == "join" ? Arm::Join : v == "lockout" ? Arm::Lockout
             : v == "lockjoin" ? Arm::LockJoin : Arm::Off;
    }();
    return a;
}
bool LockArm() { return Mode() == Arm::Lockout || Mode() == Arm::LockJoin; }

uint64_t NowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

float Dist(const ue_wrap::FVector& a, const ue_wrap::FVector& b) {
    const float dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ---- what a peer says of its grid ----------------------------------------------------------------------------

std::string GridLine() {
    char buf[512];
    std::string out;
    void* panel = PC::Panel();
    uint8_t mask = 0;
    bool disabled = false;
    if (panel && PC::ReadPress(panel, mask) && PC::ReadDisabled(panel, disabled)) {
        std::snprintf(buf, sizeof(buf), "mask=0x%02X disabled=%d", mask, disabled ? 1 : 0);
        out += buf;
    } else {
        out += "mask=(unread) disabled=(unread)";
    }
    PC::UnitPower u{};
    if (PC::ReadUnitPower(u)) {
        std::snprintf(buf, sizeof(buf), " usesp(calc,downl,coords,play,light)=%d%d%d%d%d powerUsage=%.3f", u.calc,
                      u.downl, u.coords, u.play, u.light, u.usage);
        out += buf;
    }
    PC::ServerState sv{};
    if (panel && PC::ReadServers(panel, sv)) {
        std::snprintf(buf, sizeof(buf), " servers=%d/%d hum=%s", sv.active, sv.total,
                      sv.hum < 0 ? "(none)" : sv.hum ? "1" : "0");
        out += buf;
    }
    CD::Scalars sc{};
    if (CD::ReadScalars(sc)) {
        std::snprintf(buf, sizeof(buf), " desk(play,downl,coords,comp)=%d%d%d%d", sc.activePlay ? 1 : 0,
                      sc.activeDownload ? 1 : 0, sc.activeCoords ? 1 : 0, sc.activeComp ? 1 : 0);
        out += buf;
    }
    std::vector<void*> gens;
    GEN::ReadGenerators(gens);
    out += " gens=";
    for (void* g : gens) {
        GEN::Row r{};
        if (!g || !GEN::ReadRow(g, r)) { out += "(unread)"; continue; }
        std::snprintf(buf, sizeof(buf), "[b%d c%d u%d]", r.broken ? 1 : 0, r.cycle, r.upgradeLevel);
        out += buf;
    }
    return out;
}

// The grid's own invariant, read on this peer: with the generators whole and the panel not disabled, the game
// mode's unit flags are the breakers (setPower takes them) and the desk's four follow them; with a generator
// broken, the blackout's sendPower left every flag off. A peer whose flags lag its breakers says FAIL.
bool GridHolds(std::string& why) {
    void* panel = PC::Panel();
    uint8_t mask = 0;
    bool disabled = false;
    PC::UnitPower u{};
    if (!panel || !PC::ReadPress(panel, mask) || !PC::ReadDisabled(panel, disabled) || !PC::ReadUnitPower(u))
        return true;  // nothing to read yet
    if (disabled) return true;  // a lockout leaves the flags where they were: its own leg reads the servers
    bool broken = false;
    std::vector<void*> gens;
    GEN::ReadGenerators(gens);
    for (void* g : gens) {
        GEN::Row r{};
        if (g && GEN::ReadRow(g, r) && r.broken) broken = true;
    }
    // The breakers in the flags' order: calc (bit3), downl (bit1), coords (bit0), play (bit2), light (bit4).
    static constexpr int kBitOf[5] = {3, 1, 0, 2, 4};
    static constexpr const char* kName[5] = {"usesp_calc", "usesp_downl", "usesp_coords", "usesp_play",
                                             "usesp_light"};
    const bool usesp[5] = {u.calc, u.downl, u.coords, u.play, u.light};
    for (int i = 0; i < 5; ++i) {
        const bool want = !broken && (mask & (1u << kBitOf[i])) != 0;
        if (usesp[i] != want) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s is %d while %s", kName[i], usesp[i] ? 1 : 0,
                          broken ? "a generator is broken" : want ? "its breaker is on" : "its breaker is off");
            why = buf;
            return false;
        }
    }
    CD::Scalars sc{};
    if (CD::ReadScalars(sc) && (sc.activePlay != u.play || sc.activeDownload != u.downl ||
                                sc.activeCoords != u.coords || sc.activeComp != u.calc)) {
        why = "the desk's unit flags lag the game mode's";
        return false;
    }
    return true;
}

// Say the grid at `step`, and FAIL when it does not hold. False on a FAIL.
bool Say(const char* role, const char* step) {
    UE_LOGI("[GRID-DRILL] %s %s: %s", role, step, GridLine().c_str());
    std::string why;
    if (GridHolds(why)) return true;
    UE_LOGW("[GRID-DRILL] FAIL on the %s at '%s': %s", role, step, why.c_str());
    return false;
}

// CLIENT: this copy against the host's last word, the canonical breakers and lockout and every generator's row;
// with no press or op of mine untaken they must be equal. False with `why` on a difference.
bool MatchesHost(std::string& why) {
    char buf[200];
    void* panel = PC::Panel();
    uint8_t mask = 0, canon = 0;
    bool disabled = false, canonDisabled = false;
    if (coop::power_panel::PendingPresses() != 0 || coop::power_grid::PendingOps() != 0) {
        why = "a press or an op of mine is still untaken";
        return false;
    }
    if (!panel || !PC::ReadPress(panel, mask) || !PC::ReadDisabled(panel, disabled) ||
        !coop::power_panel::LastCanonical(canon, canonDisabled)) {
        why = "the panel or the host's canonical is unread";
        return false;
    }
    if (mask != canon || disabled != canonDisabled) {
        std::snprintf(buf, sizeof(buf), "the panel is 0x%02X%s, the host's canonical 0x%02X%s", mask,
                      disabled ? " disabled" : "", canon, canonDisabled ? " disabled" : "");
        why = buf;
        return false;
    }
    coop::net::PowerGridPayload rows{};
    std::vector<void*> gens;
    if (!coop::power_grid::LastRows(rows) || GEN::ReadGenerators(gens) == 0) {
        why = "the generators or the host's rows are unread";
        return false;
    }
    for (size_t i = 0; i < gens.size() && i < rows.count; ++i) {
        const coop::net::PowerGridRow& w = rows.rows[i];
        GEN::Row r{};
        if (!w.present || !gens[i] || !GEN::ReadRow(gens[i], r)) continue;
        if (r.broken != (w.broken != 0) || r.cycle != w.cycle || r.upgradeLevel != w.upgradeLevel) {
            std::snprintf(buf, sizeof(buf), "generator %zu is [b%d c%d u%d], the host's row [b%d c%d u%d]", i,
                          r.broken ? 1 : 0, r.cycle, r.upgradeLevel, w.broken, w.cycle, w.upgradeLevel);
            why = buf;
            return false;
        }
    }
    return true;
}

// The client's last line: the grid holds and this copy reads as the host last said.
void SayDone() {
    if (!Say("client", "at the end")) return;
    std::string why;
    if (!MatchesHost(why)) {
        UE_LOGW("[GRID-DRILL] FAIL on the client at the end: %s", why.c_str());
        return;
    }
    UE_LOGI("[GRID-DRILL] client DONE: this copy reads as the host's canonical and rows");
}

// The drill's generator: the one whose Activate button stands nearest the panel's light lever. The generators
// are placed in the level, so both peers find the same one; found once a session, held by slot and serial.
ue_wrap::CachedObjRef g_drillGen;

void* DrillGen() {
    if (void* g = g_drillGen.Get()) return g;
    void* panel = PC::Panel();
    void* lever = panel ? PC::Lever(panel, kLightBit) : nullptr;
    std::vector<void*> gens;
    if (!lever || GEN::ReadGenerators(gens) == 0) return nullptr;
    const ue_wrap::FVector at = ue_wrap::engine::GetComponentLocation(lever);
    void* best = nullptr;
    float bestDist = 0.f;
    for (void* g : gens) {
        void* button = GEN::ActivateButton(g);
        if (!button) continue;
        const float d = Dist(at, ue_wrap::engine::GetComponentLocation(button));
        if (!best || d < bestDist) { best = g; bestDist = d; }
    }
    g_drillGen.Set(best);
    return best;
}

bool DrillGenBroken(bool& broken) {
    GEN::Row r{};
    void* gen = DrillGen();
    if (!gen || !GEN::ReadRow(gen, r)) return false;
    broken = r.broken;
    return true;
}

// ---- the client's legs -----------------------------------------------------------------------------------------

enum class Step : uint8_t {
    Arm, WalkPanel, Press, Ack, WaitBreak, Repair, RepairAck, HostRepair, LockStart, LockEnd, Done
};
Step     g_step = Step::Arm;
uint64_t g_stepMs = 0;
uint64_t g_nextLockReadMs = 0;
int      g_presses = 0;
uint8_t  g_maskBefore = 0;   // the breakers as the last press went
bool     g_repairFar = false;  // the repair was pressed beyond the Activate button's reach
std::shared_ptr<coop::director::BackgroundWalk> g_walk;  // the director blocks, so the walk runs on a worker

// A walk by the director to `to`, polled: 0 while it walks, 1 once there, 2 when it failed.
int WalkTo(const ue_wrap::FVector& to) {
    if (!g_walk) {
        g_walk = coop::director::StartBackgroundWalk(to, kPressReachCm, kWalkDeadlineS);
        return 0;
    }
    const int st = g_walk->state.load();
    if (st != 0) g_walk.reset();
    return st;
}

// One target in the census: where it stands, how far from this client, and where the navmesh route toward it
// ends (a route that ends short of it names the nearest ground a walker reaches).
void SayTarget(void* player, const ue_wrap::FVector& me, const char* what, bool read, const ue_wrap::FVector& at) {
    if (!read) {
        UE_LOGI("[GRID-DRILL] client census: %s is unread", what);
        return;
    }
    std::vector<ue_wrap::FVector> route;
    const bool routed = ue_wrap::engine::FindNavPath(player, me, at, route) && !route.empty();
    const ue_wrap::FVector end = routed ? route.back() : ue_wrap::FVector{};
    UE_LOGI("[GRID-DRILL] client census: %s at (%.0f, %.0f, %.0f), %.0f uu away; the route toward it %s "
            "(%.0f, %.0f, %.0f), %.0f uu short of it", what, at.X, at.Y, at.Z, Dist(me, at),
            routed ? "ends at" : "does not exist", end.X, end.Y, end.Z, routed ? Dist(end, at) : -1.f);
}

// Where this client stands against what it presses: the panel's levers, the laptop that holds the breaker page,
// the drill's generator's Activate button. The host judges each press's reach from its puppet of this player.
void SayCensus(void* player, void* panel) {
    ue_wrap::FVector me{}, at{};
    ue_wrap::engine::TryGetActorLocation(player, me);
    UE_LOGI("[GRID-DRILL] client census: this client at (%.0f, %.0f, %.0f)", me.X, me.Y, me.Z);
    void* lever = PC::Lever(panel, kLightBit);
    SayTarget(player, me, "the panel's light lever", lever != nullptr,
              lever ? ue_wrap::engine::GetComponentLocation(lever) : ue_wrap::FVector{});
    void* laptop = ue_wrap::laptop::Instance();
    SayTarget(player, me, "the laptop", laptop && ue_wrap::engine::TryGetActorLocation(laptop, at), at);
    void* button = GEN::ActivateButton(DrillGen());
    SayTarget(player, me, "the drill's generator's Activate button", button != nullptr,
              button ? ue_wrap::engine::GetComponentLocation(button) : ue_wrap::FVector{});
}

void Go(Step s) {
    g_step = s;
    g_stepMs = NowMs();
}

void Abandon(const char* why) {
    UE_LOGW("[GRID-DRILL] ABANDONED on the client: %s", why);
    g_step = Step::Done;
}

void Fail(const char* why) {
    UE_LOGW("[GRID-DRILL] FAIL on the client: %s", why);
    g_step = Step::Done;
}

// The lockout on this copy: the panel disabled while the host's lockout runs; after it, enabled with the servers
// on exactly when the calc breaker is. The servers are not judged during it: the lockout's start switches them
// off, and each server's own check re-lights its loop from usesp_calc within seconds, on the host as here (the
// host's read 30/30 servers 27 s into its lockout). False with `why` otherwise.
bool LockoutHolds(void* panel, bool locked, std::string& why) {
    uint8_t mask = 0;
    bool disabled = false;
    PC::ServerState sv{};
    if (!PC::ReadPress(panel, mask) || !PC::ReadDisabled(panel, disabled) || !PC::ReadServers(panel, sv)) {
        why = "the panel or its servers are unread";
        return false;
    }
    const bool on = (mask & kCalcBit) != 0;
    char buf[160];
    if (disabled != locked || (!locked && (sv.total == 0 || sv.active != (on ? sv.total : 0)))) {
        std::snprintf(buf, sizeof(buf), "disabled=%d servers %d/%d, wanted disabled=%d%s", disabled, sv.active,
                      sv.total, locked ? 1 : 0, locked ? "" : on ? " servers all on" : " servers all off");
        why = buf;
        return false;
    }
    return true;
}

void ClientTick(void* player) {
    const uint64_t now = NowMs();
    void* panel = PC::Panel();
    switch (g_step) {
    case Step::Arm: {
        if (!coop::net_pump::HasAnnouncedWorldReady() ||
            coop::join_progress::CurrentPhase() != coop::join_progress::Phase::Idle)
            return;
        if (!panel || !PC::Lever(panel, kLightBit)) return;  // the panel resolves with the world
        if (g_stepMs == 0) g_stepMs = now;
        if (Mode() == Arm::Join) {
            // The host broke the drill's generator before this client came in: the rows and the canonical at its
            // world-ready must already hold the blackout here.
            bool broken = false;
            if (!DrillGenBroken(broken) || !broken) {
                if (now - g_stepMs > kRowBoundMs) Fail("joined into the host's blackout, its generator is whole");
                return;
            }
            if (Say("client", "joined into the blackout")) SayDone();
            g_step = Step::Done;
            return;
        }
        if (LockArm()) {
            Say("client", "armed");
            Go(Step::LockStart);
            return;
        }
        Say("client", "armed");
        SayCensus(player, panel);
        Go(Step::WalkPanel);
        return;
    }
    case Step::LockStart:
    case Step::LockEnd: {
        // The host ran the desk virus's lockout, as this client's session began (lockout) or as it was joining
        // (lockjoin): this copy locks with the canonical, and unlocks with it 60 s after the host's start.
        const bool locked = g_step == Step::LockStart;
        if (now < g_nextLockReadMs) return;
        g_nextLockReadMs = now + kLockPollMs;
        std::string why;
        if (!LockoutHolds(panel, locked, why)) {
            if (now - g_stepMs > kLockBoundMs) {
                const std::string what = std::string(locked ? "the lockout never held here: " : "the lockout never "
                                                     "ended here: ") + why;
                Fail(what.c_str());
            }
            return;
        }
        if (!Say("client", locked ? "locked out" : "the lockout ended")) { g_step = Step::Done; return; }
        if (locked) {
            Go(Step::LockEnd);
        } else {
            SayDone();
            g_step = Step::Done;
        }
        return;
    }
    case Step::WalkPanel: {
        // Down to the panel by the director: a lever is pressed where a player stands.
        const int st = WalkTo(ue_wrap::engine::GetComponentLocation(PC::Lever(panel, kLightBit)));
        if (st == 0) return;
        if (st != 1) { Abandon("the walk to the panel failed (no route, or the deadline)"); return; }
        SayCensus(player, panel);
        Go(Step::Press);
        return;
    }
    case Step::Press: {
        // The press as the E dispatch makes it: the panel's own actionOptionIndex with a hit on the lever.
        const uint64_t sent0 = coop::power_panel::ClientPressesSent();
        PC::ReadPress(panel, g_maskBefore);
        if (!PC::PressLever(panel, player, kLightBit)) { Abandon("the lever's press did not run"); return; }
        if (coop::power_panel::ClientPressesSent() == sent0) {
            Fail("the lever's press sent the host nothing");
            return;
        }
        ++g_presses;
        UE_LOGI("[GRID-DRILL] client: pressed the light lever (press %d)", g_presses);
        Go(Step::Ack);
        return;
    }
    case Step::Ack: {
        if (coop::power_panel::PendingPresses() != 0) {
            if (now - g_stepMs > kAckBoundMs) Abandon("the host never answered the press");
            return;
        }
        // The answer acknowledges a refused press too, and reverts it: a taken one leaves the lever flipped.
        uint8_t mask = 0;
        PC::ReadPress(panel, mask);
        if (((mask ^ g_maskBefore) & (1u << kLightBit)) == 0) {
            Fail("the host refused a press (its log says why)");
            return;
        }
        char step[32];
        std::snprintf(step, sizeof(step), "press %d taken", g_presses);
        if (!Say("client", step)) { g_step = Step::Done; return; }
        // The first press is the host's cue to break the drill's generator, the third its cue to repair it.
        Go(g_presses == 1 ? Step::WaitBreak : g_presses == 2 ? Step::Repair : Step::HostRepair);
        return;
    }
    case Step::WaitBreak: {
        bool broken = false;
        if (!DrillGenBroken(broken) || !broken) {
            if (now - g_stepMs > kRowBoundMs) Abandon("the host's break never reached this copy");
            return;
        }
        if (!Say("client", "the drill's generator broke")) { g_step = Step::Done; return; }
        Go(Step::Press);
        return;
    }
    case Step::Repair: {
        // The repair as a player makes it: the puzzle solved on this copy (the drill's shortcut through its three
        // pages), then the Activate button pressed as the E dispatch presses it, from where this client stands.
        // It mends this copy at once, as the prediction, and goes to the host. The generators stand 470-620 m out
        // and the director's walk there stalls on the terrain, so a repair from the panel is out of the button's
        // reach, and the host's refusal must roll the prediction back; one pressed within reach must be taken.
        void* gen = DrillGen();
        void* button = GEN::ActivateButton(gen);
        ue_wrap::FVector me{};
        if (!button || !ue_wrap::engine::TryGetActorLocation(player, me)) {
            Abandon("the drill's generator or this client is unread");
            return;
        }
        const float dist = Dist(me, ue_wrap::engine::GetComponentLocation(button));
        if (dist > kGeneratorReachUU && dist <= kBeyondPadsUU) {
            Abandon("the repair would be pressed between the button's reach and the host's pads");
            return;
        }
        g_repairFar = dist > kBeyondPadsUU;
        UE_LOGI("[GRID-DRILL] client: the repair is pressed %.0f uu from the Activate button, %s its reach", dist,
                g_repairFar ? "beyond" : "within");
        const uint64_t sent0 = coop::power_grid::ClientOpsSent();
        if (!GEN::WritePuzzleSolved(gen)) { Abandon("the drill's generator's puzzle is unread"); return; }
        if (!GEN::PressActivate(gen, player)) { Abandon("the Activate press did not run"); return; }
        bool broken = true;
        if (!DrillGenBroken(broken) || broken) {
            Fail("the Activate press left its generator broken on this copy");
            return;
        }
        if (coop::power_grid::ClientOpsSent() == sent0) { Fail("the Activate press sent the host nothing"); return; }
        UE_LOGI("[GRID-DRILL] client: repaired the drill's generator at its Activate button");
        Go(Step::RepairAck);
        return;
    }
    case Step::RepairAck: {
        if (coop::power_grid::PendingOps() != 0) {
            if (now - g_stepMs > kAckBoundMs) Abandon("the host never answered the repair");
            return;
        }
        bool broken = false;
        if (!DrillGenBroken(broken) || broken != g_repairFar) {
            Fail(g_repairFar ? "the host's answer to a repair beyond reach left its generator whole"
                             : "the host's answer to a repair within reach left its generator broken");
            return;
        }
        if (!Say("client", g_repairFar ? "repair refused, rolled back" : "repair taken")) {
            g_step = Step::Done;
            return;
        }
        if (!g_repairFar) {
            SayDone();
            g_step = Step::Done;
            return;
        }
        Go(Step::Press);  // the third press, made after the rollback held here
        return;
    }
    case Step::HostRepair: {
        // On the third press the host mends the generator itself at its own Activate button: this copy must run
        // that repair as the host's, its turn-on at the generator included.
        bool broken = true;
        if (!DrillGenBroken(broken) || broken) {
            if (now - g_stepMs > kRowBoundMs) Abandon("the host's own repair never reached this copy");
            return;
        }
        bool turnOn = false;
        if (!GEN::ReadLastCue(DrillGen(), turnOn)) { Abandon("the drill's generator's cue is unread"); return; }
        if (!turnOn) {
            Fail("the host's repair ran on this copy without its turn-on at the generator");
            return;
        }
        if (Say("client", "the host's own repair came, with its turn-on")) SayDone();
        g_step = Step::Done;
        return;
    }
    case Step::Done:
        return;
    }
}

// ---- the host's legs -------------------------------------------------------------------------------------------

uint64_t g_hostSeen = 0;
uint64_t g_hostOpsSeen = 0;
bool     g_hostSaidArm = false;
bool     g_hostActed = false;  // the join arms' break, the lockout arms' lockout

void HostTick(coop::net::Session* s) {
    void* panel = PC::Panel();
    if (!g_hostActed) {
        if (Mode() == Arm::Join) {
            // The blackout the joiner must find at its world-ready.
            void* gen = DrillGen();
            if (!gen) return;
            g_hostActed = true;
            GEN::CallBreak(gen);
            Say("host", "broke the drill's generator before the join");
            return;
        }
        // lockjoin: the lockout as the client joins, so it is on at its world-ready and ends 60 s later; lockout:
        // once the client's world is ready.
        const bool now = (Mode() == Arm::LockJoin && s->IsSlotConnected(1) && !s->IsSlotWorldReady(1)) ||
                         (Mode() == Arm::Lockout && s->IsSlotWorldReady(1));
        if (LockArm() && now && panel) {
            g_hostActed = true;
            if (!PC::CallVirusLockout(panel)) UE_LOGW("[GRID-DRILL] ABANDONED on the host: virus_pb did not run");
            Say("host", "ran the desk virus's lockout");
            return;
        }
    }
    if (!s->IsSlotWorldReady(1)) return;
    if (!g_hostSaidArm) {
        g_hostSaidArm = true;
        Say("host", "armed");
    }
    if (LockArm()) return;
    const uint64_t ops = coop::power_grid::HostOpsTaken();
    if (ops != g_hostOpsSeen) {
        g_hostOpsSeen = ops;
        Say("host", "the client's repair taken");
    }
    const uint64_t taken = coop::power_panel::HostPressesTaken();
    if (taken == g_hostSeen) return;
    g_hostSeen = taken;
    char step[32];
    std::snprintf(step, sizeof(step), "press %llu taken", static_cast<unsigned long long>(taken));
    Say("host", step);
    if (taken != 1 && taken != 3) return;
    void* gen = DrillGen();
    if (!gen) {
        UE_LOGW("[GRID-DRILL] ABANDONED on the host: the drill's generator is unread");
        return;
    }
    if (taken == 1) {
        GEN::CallBreak(gen);
        Say("host", "broke the drill's generator");
        return;
    }
    // The client's third press follows its rolled-back repair: the host's own player mends the generator, its
    // puzzle solved as the drill's shortcut, and the rows carry the repair to the client.
    void* me = coop::players::Registry::Get().Local();
    if (!me || !GEN::WritePuzzleSolved(gen) || !GEN::PressActivate(gen, me)) {
        UE_LOGW("[GRID-DRILL] ABANDONED on the host: its own repair did not run");
        return;
    }
    Say("host", "repaired the drill's generator itself");
}

}  // namespace

void Tick(coop::net::Session* session) {
    if (Mode() == Arm::Off || !session || !session->connected()) return;
    if (!PC::EnsureResolved() || !GEN::EnsureResolved()) return;
    if (session->role() == coop::net::Role::Host) {
        HostTick(session);
        return;
    }
    if (g_step == Step::Done) return;
    void* player = coop::players::Registry::Get().Local();
    if (player) ClientTick(player);
}

void OnDisconnect() {
    g_drillGen.Reset();
    g_step = Step::Arm;
    g_stepMs = 0;
    g_nextLockReadMs = 0;
    g_presses = 0;
    g_walk.reset();
    g_hostSeen = 0;
    g_hostOpsSeen = 0;
    g_hostSaidArm = false;
    g_hostActed = false;
}

}  // namespace coop::dev::grid_drill
