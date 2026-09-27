// coop/dev/grid_drill_checks.cpp -- see grid_drill_checks.h.

#include "grid_drill_checks.h"

#include "coop/world/power_grid.h"
#include "coop/world/power_panel.h"

#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/desk/console_desk.h"
#include "ue_wrap/devices/generator.h"
#include "ue_wrap/devices/power_control.h"
#include "ue_wrap/engine/engine_component.h"  // GetComponentLocation

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace coop::dev::grid_drill {

namespace PC  = ue_wrap::power_control;
namespace GEN = ue_wrap::generator;
namespace CD  = ue_wrap::console_desk;

float Dist(const ue_wrap::FVector& a, const ue_wrap::FVector& b) {
    const float dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ---- what a peer says of its grid ----------------------------------------------------------------------------

namespace {

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

}  // namespace

// Say the grid at `step`, and FAIL when it does not hold. False on a FAIL.
bool Say(const char* role, const char* step) {
    UE_LOGI("[GRID-DRILL] %s %s: %s", role, step, GridLine().c_str());
    std::string why;
    if (GridHolds(why)) return true;
    UE_LOGW("[GRID-DRILL] FAIL on the %s at '%s': %s", role, step, why.c_str());
    return false;
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
namespace {
ue_wrap::CachedObjRef g_drillGen;
}  // namespace

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

void ForgetDrillGen() { g_drillGen.Reset(); }

}  // namespace coop::dev::grid_drill
