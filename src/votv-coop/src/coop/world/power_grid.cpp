// coop/world/power_grid.cpp -- see coop/world/power_grid.h.

#include "coop/world/power_grid.h"

#include "coop/config/config.h"
#include "coop/config/config_registry.h"
#include "coop/element/intent_authority.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/player/hand_item.h"         // what the sender's hand holds, and held last
#include "coop/player/players_registry.h"
#include "coop/player/roster_ledger.h"
#include "coop/session/net_pump.h"  // IsInAnnouncedWorld: a client's own world load runs natively
#include "coop/world/power_panel.h"  // the canonical a client's own generator verbs rewrote

#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/devices/generator.h"
#include "ue_wrap/world/weapon_catalog.h"  // whether the held item swings

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace coop::power_grid {
namespace {

namespace EL  = coop::element;
namespace GEN = ue_wrap::generator;
namespace R   = ue_wrap::reflection;
namespace sg  = ue_wrap::script_gate;
using coop::net::PowerGridPayload;
using coop::net::kPowerGridGenerators;

static_assert(coop::net::kMaxPeers <= 4, "PowerGridPayload::ack holds four slots");
static_assert(coop::net::kMaxPeers <= 8, "the owed rows are one bit a slot");

constexpr wchar_t kDecayClass[] = L"generatorFuckuper_C";
constexpr wchar_t kGenClass[]   = L"generator_C";
constexpr wchar_t kUpgradeClass[] = L"prop_transformerUpgrade_C";  // what the insert takes from the hand
constexpr int32_t kMaxUpgrade   = 6;    // the insert's own gate, `upgradeLevel < 6`
constexpr int32_t kFullWear     = 100;  // what the Activate button's repair and service write the wear back to
// The Activate button and the upgrade slot are pressed within the look-at trace, and a hit lands within a swing:
// twice the default armLength, the door and keypad lanes' reach, which coop/element/intent_authority pads with
// the generator's bounds and the puppet's lag.
constexpr float  kGeneratorReachUU = 400.0f;
// A slot's ops wait, in arrival order, while the generators have not resolved or their sender has no body here
// yet (a joiner's first pose follows its world-ready by seconds, 3 s measured on the LAN rig). Each is taken as
// refused once it has waited this long, and answered while the host has generators to read: unlike a door verb,
// which waits for its body without a bound, a repair or an install is a prediction its sender already shows.
constexpr uint64_t kWaitMs = 10000;
constexpr size_t   kMaxQueued = 32;
// A player swings at a generator a few times a second at most; a sender's ops run at this rate, the rest waiting
// their turn (door_verb_intent's bound).
constexpr float kOpBurst = 4.0f;
constexpr float kOpPerSecond = 4.0f;

std::atomic<coop::net::Session*> g_session{nullptr};

coop::net::Session* Connected() {
    auto* s = g_session.load(std::memory_order_acquire);
    return (s && s->connected()) ? s : nullptr;
}

bool IsClient(coop::net::Session* s) { return s && s->role() == coop::net::Role::Client; }

uint64_t NowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// ---- the decay tick ------------------------------------------------------------------------------------------

uint64_t g_decayRefused = 0, g_decayRan = 0;

// [dev] power_decay_drill: `watch` says every tick's verdict and ends on the client's second tick; `red` lets a
// client's tick run, the negative control.
enum class Drill : uint8_t { Off, Watch, Red };
Drill DrillMode() {
    static const Drill d = [] {
        const std::string v = coop::config::ResolveString(::coop::config_registry::rows::power_decay_drill);
        return v == "watch" ? Drill::Watch : v == "red" ? Drill::Red : Drill::Off;
    }();
    return d;
}

sg::Verdict OnDecayTickPre(const sg::Call& call) {
    if (call.fromOurCode) return sg::Verdict::Run;
    auto* s = Connected();
    if (!s) return sg::Verdict::Run;
    const bool client = IsClient(s);
    const Drill drill = DrillMode();
    const bool refuse = client && drill != Drill::Red && coop::net_pump::IsInAnnouncedWorld(call.object);
    const uint64_t n = refuse ? ++g_decayRefused : ++g_decayRan;
    if (refuse && n == 1)
        UE_LOGI("power_grid: this client refuses its own decay tick -- only the host's dice wear the generators");
    if (drill != Drill::Off) {
        UE_LOGI("[POWER-DECAY] %s tick %s (%llu)", client ? "client" : "host", refuse ? "refused" : "ran",
                static_cast<unsigned long long>(n));
        if (client && g_decayRefused + g_decayRan == 2)
            UE_LOGI("[POWER-DECAY] DONE client refused=%llu ran=%llu",
                    static_cast<unsigned long long>(g_decayRefused), static_cast<unsigned long long>(g_decayRan));
    }
    return refuse ? sg::Verdict::Cancel : sg::Verdict::Run;
}

// ---- the rows and the ops ------------------------------------------------------------------------------------

// HOST: what the last broadcast said, the last predicted op taken from each slot, the ops that wait, each
// sender's rate, and the joiners owed their rows.
PowerGridPayload g_lastSent{};
bool g_haveSent = false;
coop::roster_ledger::PerSlotState<uint16_t> g_ack;
bool g_ackDirty = false;
struct Waiting { PowerGridPayload p; uint64_t arrivedMs; };
std::deque<Waiting> g_waiting[coop::net::kMaxPeers];
struct Bucket { float tokens = kOpBurst; uint64_t lastMs = 0; };
Bucket g_rate[coop::net::kMaxPeers];
uint8_t g_owed = 0;
uint64_t g_opsTaken = 0, g_opsRefused = 0, g_hitsAnswered = 0;

// The generator verbs whose exit sends the rows, recorded as each begins and held by slot and serial: only the
// outermost of them sends, so a break's rows follow the blackout canonical its solar() sent, and a client never
// applies a break before it. upd() is one, for its own call at a world's load.
enum Verb : int { kVerbBreak, kVerbDamage, kVerbFullFix, kVerbUpdUpgrades, kVerbActivate, kVerbUpd, kVerbCount };
ue_wrap::CachedObjRef g_verbFn[kVerbCount];

void RecordVerb(int verb, void* fn) {
    if (!g_verbFn[verb].Is(fn)) g_verbFn[verb].Set(fn);
}

// CLIENT: my predicted ops the host has not taken, oldest first; the host's last rows, and whether they still
// wait for this peer's generators; the Activate press in flight, its generator and its row as it began.
struct Pending { uint16_t seq; uint8_t op; uint8_t index; };
std::vector<Pending> g_pending;
uint16_t g_seq = 0;
PowerGridPayload g_rows{};
bool g_haveRows = false;
bool g_rowsWaiting = false;
void* g_pressGen = nullptr;
GEN::Row g_pressRow{};
uint64_t g_editsRefused = 0, g_opsSent = 0, g_hitsSent = 0, g_rowsApplied = 0;

// The hit's parameters, per function (one generator class, so one resolve).
struct HitParams { void* fn = nullptr; int32_t actor = -1; int32_t damage = -1; };
HitParams g_hitParams;

// `a` is later than `b` in a 16-bit sequence that wraps.
bool SeqAfter(uint16_t a, uint16_t b) { return static_cast<int16_t>(static_cast<uint16_t>(a - b)) > 0; }

uint8_t LocalSlot() { return coop::players::Registry::Get().LocalPeerId(); }

bool TakeToken(uint8_t slot) {
    Bucket& b = g_rate[slot];
    const uint64_t now = NowMs();
    if (b.lastMs != 0 && now > b.lastMs) {
        b.tokens += kOpPerSecond * static_cast<float>(now - b.lastMs) / 1000.0f;
        if (b.tokens > kOpBurst) b.tokens = kOpBurst;
    }
    b.lastMs = now;
    if (b.tokens < 1.0f) return false;
    b.tokens -= 1.0f;
    return true;
}

// A repair, a service and an install run on their client first, and the host's rows reconcile them; a hit
// breaks a generator, which only the host's world does.
bool Predicted(uint8_t op) {
    return op == coop::net::kPowerGridOpRepair || op == coop::net::kPowerGridOpService ||
           op == coop::net::kPowerGridOpUpgrade;
}

const char* OpName(uint8_t op) {
    switch (op) {
    case coop::net::kPowerGridOpRepair:  return "repair";
    case coop::net::kPowerGridOpUpgrade: return "upgrade";
    case coop::net::kPowerGridOpHit:     return "hit";
    case coop::net::kPowerGridOpService: return "service";
    default:                             return "?";
    }
}

bool ReadRows(PowerGridPayload& p) {
    std::vector<void*> gens;
    if (GEN::ReadGenerators(gens) == 0) return false;
    const size_t n = std::min(gens.size(), static_cast<size_t>(kPowerGridGenerators));
    p.op = coop::net::kPowerGridOpRows;
    p.count = static_cast<uint8_t>(n);
    for (size_t i = 0; i < n; ++i) {
        GEN::Row r{};
        coop::net::PowerGridRow& w = p.rows[i];
        w = {};
        if (!gens[i] || !GEN::ReadRow(gens[i], r)) continue;
        w.present = 1;
        w.broken = r.broken ? 1 : 0;
        w.cyc = r.cyc ? 1 : 0;
        w.upgradeLevel = static_cast<uint8_t>(std::clamp(r.upgradeLevel, 0, kMaxUpgrade));
        w.cycle = r.cycle;
    }
    return true;
}

bool SameRows(const PowerGridPayload& a, const PowerGridPayload& b) {
    if (a.count != b.count) return false;
    for (int i = 0; i < a.count && i < kPowerGridGenerators; ++i) {
        const auto& x = a.rows[i];
        const auto& y = b.rows[i];
        if (x.present != y.present || x.broken != y.broken || x.cyc != y.cyc || x.upgradeLevel != y.upgradeLevel ||
            x.cycle != y.cycle)
            return false;
    }
    return true;
}

// ---- the host ------------------------------------------------------------------------------------------------

// The rows with every slot's acknowledgement: to `onlySlot`, or to every peer when they or an acknowledgement
// changed since the last broadcast.
void HostSendRows(coop::net::Session* s, int onlySlot = -1) {
    PowerGridPayload p{};
    if (!ReadRows(p)) return;
    for (int i = 0; i < coop::net::kMaxPeers; ++i) p.ack[i] = g_ack[i];
    if (onlySlot >= 0) {
        s->SendReliableToSlot(onlySlot, coop::net::ReliableKind::PowerGridState, &p, sizeof(p));
        return;
    }
    if (!g_ackDirty && g_haveSent && SameRows(p, g_lastSent)) return;
    s->SendReliable(coop::net::ReliableKind::PowerGridState, &p, sizeof(p));
    g_lastSent = p;
    g_haveSent = true;
    g_ackDirty = false;
}

// A hit is a swing: the player's attack swings only an item whose list_weapons row carries a montage and the
// attack flag (door_verb_intent's hit, the precedent).
bool HitSwings(uint8_t slot, std::wstring& item) {
    item = coop::hand_item::HeldItem(slot);
    ue_wrap::weapon_catalog::Swing swing;
    return ue_wrap::weapon_catalog::Ready() && ue_wrap::weapon_catalog::Lookup(item, swing) && swing.canSwing;
}

enum class Take : uint8_t { Done, Wait };

Take HostTakeOp(coop::net::Session* s, const Waiting& wt, uint8_t sender) {
    const PowerGridPayload& p = wt.p;
    const bool waited = NowMs() - wt.arrivedMs >= kWaitMs;
    const bool resolved = GEN::EnsureResolved();
    if (!resolved && !waited) return Take::Wait;
    const bool predicted = Predicted(p.op);
    if (!predicted && p.op != coop::net::kPowerGridOpHit) return Take::Done;
    if (predicted && !SeqAfter(p.seq, g_ack[sender])) {
        // Already taken: its sender still shows it only if no rows have answered it yet.
        UE_LOGW("power_grid: host dropped slot %u's %s seq %u (last taken %u)", sender, OpName(p.op), p.seq,
                g_ack[sender]);
        HostSendRows(s, sender);
        return Take::Done;
    }
    std::vector<void*> gens;
    if (resolved) GEN::ReadGenerators(gens);
    void* gen = p.index < gens.size() ? gens[p.index] : nullptr;
    GEN::Row r{};
    const bool read = gen && GEN::ReadRow(gen, r);
    EL::IntentSubject reach{};
    if (read) reach = EL::IntentTarget::ForClientIntent(*s, sender, kGeneratorReachUU).Authorize(gen);
    if (reach.outcome == EL::IntentOutcome::NoBody && !waited) return Take::Wait;
    if (predicted) {
        g_ack[sender] = p.seq;
        g_ackDirty = true;
    }
    std::wstring item;
    const char* refusal = !read ? "that generator is not there" : !reach ? EL::OutcomeName(reach.outcome) : nullptr;
    if (!refusal) {
        switch (p.op) {
        case coop::net::kPowerGridOpService:
            if (r.broken) refusal = "it is broken, and a broken generator's Activate press is its repair";
            break;
        case coop::net::kPowerGridOpUpgrade:
            // The insert takes a held upgrade and spends it before the op comes; two players installing at the last
            // free place leave the second's item spent (the refund, with its pairing proof, is still to build).
            if (!coop::hand_item::LastHeldClassIs(sender, kUpgradeClass)) refusal = "the installer held no upgrade";
            else if (r.upgradeLevel >= kMaxUpgrade) refusal = "its upgrades are full";
            break;
        case coop::net::kPowerGridOpHit:
            if (!HitSwings(sender, item)) refusal = "the hitter's held item does not swing";
            break;
        default: break;
        }
    }
    const bool hit = p.op == coop::net::kPowerGridOpHit;
    const bool say = !hit || ++g_hitsAnswered <= 3 || g_hitsAnswered % 20 == 0;  // melee swings several a second
    if (refusal) {
        ++g_opsRefused;
        if (say)
            UE_LOGW("power_grid: host refused slot %u's %s of generator %u (seq %u) -- %s (%.0f uu, allowed %.0f)",
                    sender, OpName(p.op), p.index, p.seq, refusal, reach.distUU, reach.reachUU);
        if (predicted) HostSendRows(s, sender);  // its acknowledgement reverts the sender's prediction
        return Take::Done;
    }
    switch (p.op) {
    case coop::net::kPowerGridOpRepair:
        // Run as the Activate route runs it, which the host cannot press itself while its own copy of the puzzle
        // is unsolved. A second repair of one generator finds it mended: the presser's copy already reads as the
        // host's.
        if (r.broken) GEN::Repair(gen);
        break;
    case coop::net::kPowerGridOpService:
        GEN::WriteCycle(gen, kFullWear);  // all the Activate press writes to a whole generator but its 2D cue
        break;
    case coop::net::kPowerGridOpUpgrade:
        GEN::WriteUpgradeLevel(gen, r.upgradeLevel + 1);
        GEN::CallUpdUpgrades(gen);
        break;
    case coop::net::kPowerGridOpHit:
        GEN::CallBreak(gen);  // the whole body of the generator's addDamage
        break;
    default: break;
    }
    ++g_opsTaken;
    if (say)
        UE_LOGI("power_grid: host took slot %u's %s of generator %u (seq %u)%s%ls", sender, OpName(p.op), p.index,
                p.seq, hit ? " with " : "", item.c_str());
    // The change and the acknowledgement to every peer: nothing new when the verb's own exit already sent them.
    HostSendRows(s);
    return Take::Done;
}

// Every waiting op of `slot` refused with its newest, whose acknowledgement answers them all: its queue is full.
void HostRefuseQueue(coop::net::Session* s, uint8_t slot, uint16_t newestSeq) {
    const size_t n = g_waiting[slot].size();
    g_waiting[slot].clear();
    if (SeqAfter(newestSeq, g_ack[slot])) g_ack[slot] = newestSeq;
    g_ackDirty = true;
    g_opsRefused += n + 1;
    UE_LOGE("power_grid: host refused slot %u's %zu waiting ops and seq %u -- its queue is full", slot, n, newestSeq);
    HostSendRows(s, slot);
}

// An op from the wire is taken at once when its slot has none waiting and a token to spend.
void HostOffer(coop::net::Session* s, const PowerGridPayload& p, uint8_t sender) {
    if (sender == 0 || sender >= coop::net::kMaxPeers || p.op == coop::net::kPowerGridOpRows) return;
    auto& q = g_waiting[sender];
    const Waiting w{p, NowMs()};
    if (q.empty() && TakeToken(sender)) {
        if (HostTakeOp(s, w, sender) == Take::Done) return;
        g_rate[sender].tokens += 1.0f;  // a wait runs nothing, so it spends no token
    }
    if (q.size() >= kMaxQueued) {
        HostRefuseQueue(s, sender, p.seq);
        return;
    }
    q.push_back(w);
}

void HostDrainWaiting(coop::net::Session* s) {
    for (uint8_t slot = 1; slot < coop::net::kMaxPeers; ++slot) {
        auto& q = g_waiting[slot];
        while (!q.empty() && TakeToken(slot)) {
            if (HostTakeOp(s, q.front(), slot) == Take::Wait) {
                g_rate[slot].tokens += 1.0f;
                break;
            }
            q.pop_front();
        }
    }
}

// The rows each owed joiner waits for, once the generators resolve.
void HostServeOwed(coop::net::Session* s) {
    if (!g_owed || !GEN::EnsureResolved()) return;
    PowerGridPayload probe{};
    if (!ReadRows(probe)) return;
    for (int slot = 1; slot < coop::net::kMaxPeers; ++slot)
        if (g_owed & (1u << slot)) HostSendRows(s, slot);
    g_owed = 0;
}

// ---- a client ------------------------------------------------------------------------------------------------

void ClientSendOp(coop::net::Session* s, uint8_t op, int32_t index, float damage = 0.f) {
    PowerGridPayload p{};
    p.op = op;
    p.index = static_cast<uint8_t>(index);
    p.damage = damage;
    if (Predicted(op)) {
        p.seq = ++g_seq;
        g_pending.push_back({p.seq, op, p.index});
    }
    s->SendReliableToSlot(0, coop::net::ReliableKind::PowerGridState, &p, sizeof(p));
    ++g_opsSent;
    if (op != coop::net::kPowerGridOpHit || ++g_hitsSent <= 3 || g_hitsSent % 20 == 0)
        UE_LOGI("power_grid: local %s of generator %d (seq %u) -- sent to the host", OpName(op), index, p.seq);
}

// The host's row with my untaken ops on top: the row the host will send once it takes them.
GEN::Row Expected(const coop::net::PowerGridRow& w, int32_t index) {
    GEN::Row e{};
    e.broken = w.broken != 0;
    e.cyc = w.cyc != 0;
    e.cycle = w.cycle;
    e.upgradeLevel = w.upgradeLevel;
    for (const Pending& d : g_pending) {
        if (d.index != index) continue;
        if (d.op == coop::net::kPowerGridOpRepair && e.broken) {
            e.broken = false;
            e.cycle = kFullWear;
        } else if (d.op == coop::net::kPowerGridOpService && !e.broken) {
            e.cycle = kFullWear;
        } else if (d.op == coop::net::kPowerGridOpUpgrade && e.upgradeLevel < kMaxUpgrade) {
            ++e.upgradeLevel;
        }
    }
    return e;
}

// Bring each generator to the host's row with my untaken ops on top, through the game's own verbs, run as the
// host's so this client's gates pass them.
void ClientReconcile() {
    std::vector<void*> gens;
    if (GEN::ReadGenerators(gens) == 0) {
        g_rowsWaiting = true;
        return;
    }
    g_rowsWaiting = false;
    bool panelVerbs = false;
    const size_t n = std::min({gens.size(), static_cast<size_t>(g_rows.count),
                               static_cast<size_t>(kPowerGridGenerators)});
    for (size_t i = 0; i < n; ++i) {
        const coop::net::PowerGridRow& w = g_rows.rows[i];
        GEN::Row r{};
        if (!w.present || !gens[i] || !GEN::ReadRow(gens[i], r)) continue;
        const GEN::Row e = Expected(w, static_cast<int32_t>(i));
        if (e.broken != r.broken) {
            // A break blacks the base out through the panel and scrambles the puzzle; a repair runs as the host
            // runs a player's, its turn-on at the generator included.
            if (e.broken) GEN::CallBreak(gens[i]);
            else GEN::Repair(gens[i]);
            panelVerbs = true;
            UE_LOGI("power_grid: generator %zu %s as the host's", i, e.broken ? "broke" : "was repaired");
            GEN::ReadRow(gens[i], r);
        }
        if (e.cycle != r.cycle) {
            // One step of the host's wear runs as its damage(), whose scrambled sine page puts this copy's next
            // service a solved puzzle away, as the host's is; any other gap (a join, a service) is written.
            if (!e.broken && !r.broken && e.cycle >= 1 && e.cycle == r.cycle - 1)
                GEN::CallDamage(gens[i]);
            else
                GEN::WriteCycle(gens[i], e.cycle);
        }
        if (e.cyc != r.cyc) GEN::WriteCyc(gens[i], e.cyc);
        if (e.upgradeLevel != r.upgradeLevel) {
            GEN::WriteUpgradeLevel(gens[i], e.upgradeLevel);
            GEN::CallUpdUpgrades(gens[i]);
            UE_LOGI("power_grid: generator %zu upgrades %d -> %d as the host's", i, r.upgradeLevel, e.upgradeLevel);
        }
    }
    // A break's solar() cleared this panel's breakers along with the power; only the canonical writes them, and a
    // rolled-back repair's break has no canonical behind it.
    if (panelVerbs) coop::power_panel::ReassertCanonical();
    ++g_rowsApplied;
}

void ClientTakeRows(const PowerGridPayload& p) {
    const uint8_t me = LocalSlot();
    const uint16_t ack = me < coop::net::kMaxPeers ? p.ack[me] : 0;
    size_t kept = 0;
    for (const Pending& d : g_pending)
        if (SeqAfter(d.seq, ack)) g_pending[kept++] = d;
    g_pending.resize(kept);
    g_rows = p;
    g_haveRows = true;
    if (GEN::EnsureResolved()) ClientReconcile();
    else g_rowsWaiting = true;
}

// ---- the watches ---------------------------------------------------------------------------------------------

// A client's own break, wear and fullFix never run: only the host's world breaks and wears a generator, and the
// host's rows run each edge here as the host's.
sg::Verdict OnOwnEditPre(const sg::Call& call) {
    const int verb = call.tag == 0x50474252 /*'PGBR'*/ ? kVerbBreak : call.tag == 0x5047444D /*'PGDM'*/ ? kVerbDamage
                                                                                                      : kVerbFullFix;
    RecordVerb(verb, call.function);
    auto* s = Connected();
    if (!IsClient(s) || call.fromOurCode) return sg::Verdict::Run;
    if (!call.object || !coop::net_pump::IsInAnnouncedWorld(call.object)) return sg::Verdict::Run;
    if (++g_editsRefused == 1)
        UE_LOGI("power_grid: this client refuses its own generator break, wear and fullFix -- the host's "
                "generators are the author");
    return sg::Verdict::Cancel;
}

// The host's own edges: the outermost one's exit sends the rows when they changed.
void OnGenHostPost(const sg::Call& call) {
    auto* s = Connected();
    if (!s || s->role() != coop::net::Role::Host) return;
    for (const auto& ref : g_verbFn) {
        void* const fn = ref.Get();
        if (fn && fn != call.function && sg::IsBodyActive(fn)) return;  // an outer verb's exit sends them
    }
    HostSendRows(s);
}

// upd(): recorded only, for the nesting above; its exit is a host's rows.
sg::Verdict OnUpdPre(const sg::Call& call) {
    RecordVerb(kVerbUpd, call.function);
    return sg::Verdict::Run;
}

// A client's install: the insert raised the level and spent the held upgrade before its updUpgrades, a level
// above the one expected; each step goes to the host.
sg::Verdict OnUpdUpgradesPre(const sg::Call& call) {
    RecordVerb(kVerbUpdUpgrades, call.function);
    auto* s = Connected();
    if (!IsClient(s) || call.fromOurCode || !g_haveRows) return sg::Verdict::Run;
    if (!call.object || !coop::net_pump::IsInAnnouncedWorld(call.object)) return sg::Verdict::Run;
    const int32_t idx = GEN::IndexOf(call.object);
    GEN::Row r{};
    if (idx < 0 || idx >= kPowerGridGenerators || idx >= g_rows.count || !g_rows.rows[idx].present ||
        !GEN::ReadRow(call.object, r))
        return sg::Verdict::Run;
    const int32_t level = std::clamp(r.upgradeLevel, 0, kMaxUpgrade);
    for (int32_t lvl = Expected(g_rows.rows[idx], idx).upgradeLevel; lvl < level; ++lvl)
        ClientSendOp(s, coop::net::kPowerGridOpUpgrade, idx);
    return sg::Verdict::Run;
}

// A client's Activate press, as it begins: its generator and that generator's row.
sg::Verdict OnActivatePre(const sg::Call& call) {
    RecordVerb(kVerbActivate, call.function);
    if (!IsClient(Connected()) || !call.object) return sg::Verdict::Run;
    GEN::Row r{};
    if (GEN::ReadRow(call.object, r)) {
        g_pressGen = call.object;
        g_pressRow = r;
    }
    return sg::Verdict::Run;
}

// ...and as it ends. A broken generator it left whole was repaired, a whole one whose wear it restored was
// serviced: either ran here as the prediction, and goes to the host. The host's own press sends the rows.
void OnActivatePost(const sg::Call& call) {
    auto* s = Connected();
    if (!s || !call.object) return;
    if (s->role() == coop::net::Role::Host) {
        OnGenHostPost(call);
        return;
    }
    if (call.object != g_pressGen) return;
    g_pressGen = nullptr;
    if (!coop::net_pump::IsInAnnouncedWorld(call.object)) return;
    const int32_t idx = GEN::IndexOf(call.object);
    GEN::Row r{};
    if (idx < 0 || idx >= kPowerGridGenerators || !GEN::ReadRow(call.object, r)) return;
    if (g_pressRow.broken && !r.broken)
        ClientSendOp(s, coop::net::kPowerGridOpRepair, idx);
    else if (!g_pressRow.broken && !r.broken && r.cycle > g_pressRow.cycle)
        ClientSendOp(s, coop::net::kPowerGridOpService, idx);
}

// A client's own hit: its player's goes to the host, whose world breaks the generator; every other hit, a
// creature's or an explosion's, is the host's world's and runs there (door_verb_intent's hit, the precedent).
sg::Verdict OnHitPre(const sg::Call& call) {
    auto* s = Connected();
    if (!IsClient(s) || call.fromOurCode) return sg::Verdict::Run;
    if (!call.object || !coop::net_pump::IsInAnnouncedWorld(call.object)) return sg::Verdict::Run;
    if (g_hitParams.fn != call.function) {
        g_hitParams.fn = call.function;
        g_hitParams.actor = R::FindParamOffset(call.function, L"actor");
        g_hitParams.damage = R::FindParamOffset(call.function, L"damage");
        if (g_hitParams.actor < 0 || g_hitParams.damage < 0)
            UE_LOGW("power_grid: the generator's addDamage parameters did not resolve (actor=%d damage=%d) -- a "
                    "client's hit on a generator is lost", g_hitParams.actor, g_hitParams.damage);
    }
    if (g_hitParams.actor < 0 || g_hitParams.damage < 0) return sg::Verdict::Cancel;
    void* actor = *reinterpret_cast<void* const*>(call.locals + g_hitParams.actor);
    const float damage = *reinterpret_cast<const float*>(call.locals + g_hitParams.damage);
    const int32_t idx = GEN::IndexOf(call.object);
    if (coop::players::Registry::Get().IsLocal(actor) && idx >= 0 && idx < kPowerGridGenerators)
        ClientSendOp(s, coop::net::kPowerGridOpHit, idx, damage);
    return sg::Verdict::Cancel;
}

struct WatchDef {
    const wchar_t* cls;
    const wchar_t* fn;
    int tag;
    sg::PreFn pre;
    sg::PostFn post;
};
constexpr WatchDef kWatches[] = {
    { kDecayClass, L"timer_transformers", 0x50474454 /*'PGDT'*/, &OnDecayTickPre,   nullptr },
    { kGenClass,   L"break",              0x50474252 /*'PGBR'*/, &OnOwnEditPre,     &OnGenHostPost },
    { kGenClass,   L"damage",             0x5047444D /*'PGDM'*/, &OnOwnEditPre,     &OnGenHostPost },
    { kGenClass,   L"fullFix",            0x50474658 /*'PGFX'*/, &OnOwnEditPre,     &OnGenHostPost },
    { kGenClass,   L"updUpgrades",        0x50475550 /*'PGUP'*/, &OnUpdUpgradesPre, &OnGenHostPost },
    { kGenClass,   L"upd",                0x50475544 /*'PGUD'*/, &OnUpdPre,         &OnGenHostPost },
    { kGenClass,   L"actionOptionIndex",  0x5047414F /*'PGAO'*/, &OnActivatePre,    &OnActivatePost },
    { kGenClass,   L"addDamage",          0x50474144 /*'PGAD'*/, &OnHitPre,         nullptr },
};
constexpr int kWatchCount = static_cast<int>(sizeof(kWatches) / sizeof(kWatches[0]));
bool g_registered = false;
bool g_settled = false;

}  // namespace

void Install(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
    if (g_registered) return;
    g_registered = true;
    for (const WatchDef& w : kWatches)
        if (!sg::WatchClassName(w.cls, w.fn, w.tag, w.pre, w.post))
            UE_LOGE("power_grid: the script-body gate refused the watch on %ls::%ls -- that edge is each peer's own",
                    w.cls, w.fn);
}

void Tick() {
    if (auto* s = Connected(); s && s->role() == coop::net::Role::Host) {
        HostDrainWaiting(s);
        HostServeOwed(s);
    } else if (g_rowsWaiting && GEN::EnsureResolved()) {
        ClientReconcile();
    }
    if (g_settled || !g_registered) return;
    sg::ResolvePendingNames();
    int live = 0, settled = 0;
    for (const WatchDef& w : kWatches) {
        if (sg::ClassNameWatchLive(w.cls, w.fn, w.tag)) ++live;
        if (sg::ClassNameWatchSettled(w.cls, w.fn, w.tag)) ++settled;
    }
    if (live == kWatchCount) {
        g_settled = true;
        UE_LOGI("power_grid: the grid's gates are live (the decay tick, break, wear, fullFix, upgrades, upd, the "
                "Activate press, a hit)");
    } else if (settled == kWatchCount) {
        g_settled = true;
        UE_LOGE("power_grid: %d of %d grid gates are dead -- those edges stay each peer's own", kWatchCount - live,
                kWatchCount);
    }
}

void OnReliable(const coop::net::PowerGridPayload& payload, uint8_t senderSlot) {
    auto* s = Connected();
    if (!s) return;
    if (s->role() == coop::net::Role::Host) {
        HostOffer(s, payload, senderSlot);
        return;
    }
    if (senderSlot != 0 || payload.op != coop::net::kPowerGridOpRows) return;
    ClientTakeRows(payload);
}

void QueueConnectBroadcastForSlot(int slot) {
    auto* s = Connected();
    if (!s || s->role() != coop::net::Role::Host || slot <= 0 || slot >= coop::net::kMaxPeers) return;
    g_owed |= static_cast<uint8_t>(1u << slot);
    HostServeOwed(s);
    if (g_owed & (1u << slot))
        UE_LOGI("power_grid: slot %d's rows wait for the host's generators to resolve", slot);
}

void OnPeerLeft(uint8_t slot) {
    if (slot == 0 || slot >= coop::net::kMaxPeers) return;
    g_waiting[slot].clear();
    g_rate[slot] = Bucket{};
    g_owed = static_cast<uint8_t>(g_owed & ~(1u << slot));
}

void OnDisconnect() {
    if (g_decayRefused || g_decayRan)
        UE_LOGI("power_grid: session end -- decay ticks refused %llu, run %llu",
                static_cast<unsigned long long>(g_decayRefused), static_cast<unsigned long long>(g_decayRan));
    if (g_editsRefused || g_opsSent || g_rowsApplied || g_opsTaken || g_opsRefused)
        UE_LOGI("power_grid: session end -- own edits refused %llu, ops sent %llu, rows applied %llu; as host: ops "
                "taken %llu, refused %llu",
                static_cast<unsigned long long>(g_editsRefused), static_cast<unsigned long long>(g_opsSent),
                static_cast<unsigned long long>(g_rowsApplied), static_cast<unsigned long long>(g_opsTaken),
                static_cast<unsigned long long>(g_opsRefused));
    g_decayRefused = g_decayRan = 0;
    g_editsRefused = g_opsSent = g_hitsSent = g_rowsApplied = 0;
    g_opsTaken = g_opsRefused = g_hitsAnswered = 0;
    g_haveSent = g_ackDirty = false;
    for (int slot = 0; slot < coop::net::kMaxPeers; ++slot) {
        g_waiting[slot].clear();
        g_rate[slot] = Bucket{};
    }
    for (auto& ref : g_verbFn) ref.Reset();
    g_owed = 0;
    g_pending.clear();
    g_seq = 0;
    g_haveRows = g_rowsWaiting = false;
    g_pressGen = nullptr;
}

}  // namespace coop::power_grid
