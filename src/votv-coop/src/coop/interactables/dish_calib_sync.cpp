// coop/interactables/dish_calib_sync.cpp -- see coop/interactables/dish_calib_sync.h.

#include "coop/interactables/dish_calib_sync.h"

#include "coop/net/session.h"

#include "ue_wrap/core/log.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/core/sdk_profile_names.h"
#include "ue_wrap/desk/dish.h"
#include "ue_wrap/desk/dish_writers.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

namespace coop::dish_calib_sync {
namespace {

namespace D = ue_wrap::dish;
namespace DW = ue_wrap::dish_writers;
namespace P = ue_wrap::profile;
namespace sg = ue_wrap::script_gate;
using coop::net::kMaxDishes;

constexpr int kTagTool = 0x44435754;          // 'DCWT'
constexpr int kTagUncalibrator = 0x44435755;  // 'DCWU'
constexpr int kTagSetPrec = 0x44435350;       // 'DCSP'

// A client's intents, per peer: a player's verbs come a few a second at most.
constexpr float    kIntentBurst = 8.f;
constexpr float    kIntentsPerSecond = 4.f;
constexpr uint64_t kRefusalSayMs = 10000;

std::atomic<coop::net::Session*> g_session{nullptr};

// HOST: what it last sent, the baseline its poll diffs against.
float g_sent[kMaxDishes] = {};
bool  g_haveSent = false;
// CLIENT: the host's values it holds, each dish from the seed or a batch, or from this player's own verb, for the
// session.
float g_host[kMaxDishes] = {};
bool  g_held[kMaxDishes] = {};
bool  g_saidRefusedBatch = false;  // a batch not the host's to be sent, said once a session
Counts g_counts;

struct Bucket {
    float    tokens = kIntentBurst;
    uint64_t lastMs = 0;
    uint64_t nextSayMs = 0;
};
Bucket g_budget[coop::net::kMaxPeers];

// A stored value changed: bit for bit, so a NaN the host's tool wrote is one value, not a change every poll.
bool Same(float a, float b) {
    uint32_t x = 0, y = 0;
    std::memcpy(&x, &a, sizeof(x));
    std::memcpy(&y, &b, sizeof(y));
    return x == y;
}

bool IsClient(coop::net::Session* s) {
    return s && s->running() && s->connected() && s->role() != coop::net::Role::Host;
}

bool TakeToken(uint8_t slot) {
    Bucket& b = g_budget[slot];
    const uint64_t now = ::GetTickCount64();
    if (b.lastMs != 0 && now > b.lastMs) {
        b.tokens += kIntentsPerSecond * static_cast<float>(now - b.lastMs) / 1000.0f;
        if (b.tokens > kIntentBurst) b.tokens = kIntentBurst;
    }
    b.lastMs = now;
    if (b.tokens < 1.0f) return false;
    b.tokens -= 1.0f;
    return true;
}

void SendAll(coop::net::Session* s, const D::DishCalibration* rows, int32_t n, int toSlot) {
    coop::net::DishCalibPayload p{};
    for (int32_t i = 0; i < n; ++i) {
        if (rows[i].index < 0 || rows[i].index >= kMaxDishes) continue;
        auto& e = p.entries[p.count++];
        e.index = static_cast<uint8_t>(rows[i].index);
        e.value = rows[i].value;
    }
    if (p.count == 0) return;
    if (toSlot < 0) s->SendReliable(coop::net::ReliableKind::DishCalib, &p, sizeof(p));
    else s->SendReliableToSlot(toSlot, coop::net::ReliableKind::DishCalib, &p, sizeof(p));
}

// CLIENT: every held dish that moved off the host's value is put back to it.
void PutBack(const D::DishCalibration* rows, int32_t n) {
    int reverted = 0;
    for (int32_t i = 0; i < n; ++i) {
        const int32_t idx = rows[i].index;
        if (idx < 0 || idx >= kMaxDishes || !g_held[idx] || Same(rows[i].value, g_host[idx])) continue;
        if (D::WriteCalibration(idx, g_host[idx])) ++reverted;
    }
    if (reverted == 0) return;
    g_counts.putBack += static_cast<uint64_t>(reverted);
    UE_LOGI("dish_calib_sync: CLIENT %d dish precision(s) put back to the host's (n=%llu)", reverted,
            static_cast<unsigned long long>(g_counts.putBack));
}

// CLIENT: setPrec averages the dishes into the rate the desk's download reads every tick (its speed text, its
// completion test), so the copy is put back first; a deviation the poll has not reached yet would otherwise stand
// in that rate for the timer's whole ten seconds.
sg::Verdict OnSetPrecPre(const sg::Call&) {
    if (!IsClient(g_session.load(std::memory_order_acquire))) return sg::Verdict::Run;
    D::DishCalibration rows[kMaxDishes];
    PutBack(rows, D::ReadCalibrations(rows, kMaxDishes));
    return sg::Verdict::Run;
}

// The two verbs, each body's entry reading every dish and its exit sending the ones it changed. A body can nest in
// another watched body, so after a send every enclosing body's reading takes the values as sent, and a change is
// sent once. The gate's own chain is the scope: an entry at `depth` or deeper whose body ended without its exit
// (another watcher's Cancel, a fault the firewall absorbed) is dropped at the next entry or exit.
struct InFlight {
    int   depth;
    void* stack;
    float before[kMaxDishes];
    bool  read[kMaxDishes];
};
std::vector<InFlight> g_inFlight;  // game thread only

void DropFrom(int depth) {
    while (!g_inFlight.empty() && g_inFlight.back().depth >= depth) g_inFlight.pop_back();
}

sg::Verdict OnVerbPre(const sg::Call& call) {
    DropFrom(call.depth);
    if (!IsClient(g_session.load(std::memory_order_acquire))) return sg::Verdict::Run;
    D::DishCalibration rows[kMaxDishes];
    const int32_t n = D::ReadCalibrations(rows, kMaxDishes);
    if (n <= 0) return sg::Verdict::Run;
    InFlight f{call.depth, call.stack, {}, {}};
    for (int32_t i = 0; i < n; ++i) {
        if (rows[i].index < 0 || rows[i].index >= kMaxDishes) continue;
        f.before[rows[i].index] = rows[i].value;
        f.read[rows[i].index] = true;
    }
    g_inFlight.push_back(f);
    return sg::Verdict::Run;
}

void OnVerbPost(const sg::Call& call) {
    DropFrom(call.depth + 1);
    if (g_inFlight.empty() || g_inFlight.back().depth != call.depth || g_inFlight.back().stack != call.stack)
        return;  // its entry read nothing
    const InFlight f = g_inFlight.back();
    g_inFlight.pop_back();
    auto* s = g_session.load(std::memory_order_acquire);
    if (!IsClient(s)) return;
    D::DishCalibration rows[kMaxDishes];
    const int32_t n = D::ReadCalibrations(rows, kMaxDishes);
    coop::net::DishCalibPayload p{};
    for (int32_t i = 0; i < n; ++i) {
        const int32_t idx = rows[i].index;
        if (idx < 0 || idx >= kMaxDishes || !f.read[idx] || Same(rows[i].value, f.before[idx])) continue;
        auto& e = p.entries[p.count++];
        e.index = static_cast<uint8_t>(idx);
        e.value = rows[i].value;
        for (InFlight& outer : g_inFlight) outer.before[idx] = rows[i].value;
    }
    if (p.count == 0) return;
    if (!s->SendReliableToSlot(0, coop::net::ReliableKind::DishCalibIntent, &p, sizeof(p))) {
        UE_LOGW("dish_calib_sync: CLIENT %u dish precision(s) this player set were not sent (the session refused "
                "them); the host's values stand", static_cast<unsigned>(p.count));
        return;
    }
    // Held once sent: the host performs it and sends it back to all, or sends its own value in its place.
    for (uint8_t i = 0; i < p.count; ++i) {
        g_host[p.entries[i].index] = p.entries[i].value;
        g_held[p.entries[i].index] = true;
    }
    g_counts.intentsSent += p.count;
    UE_LOGI("dish_calib_sync: CLIENT this player's verb set %u dish precision(s), dish %u = %.4f first -- intent to "
            "the host", static_cast<unsigned>(p.count), static_cast<unsigned>(p.entries[0].index),
            p.entries[0].value);
}

// A watch's registration, once a process, and its one settle line.
enum class Reg : uint8_t { Pending, Registered, Refused };
struct Watch {
    const wchar_t* cls;
    const wchar_t* fn;
    int tag;
    sg::PreFn pre;
    sg::PostFn post;
    Reg reg = Reg::Pending;
    bool settled = false;
};
Watch g_watches[] = {
    {DW::kToolClass, DW::kToolVerb, kTagTool, &OnVerbPre, &OnVerbPost},
    {DW::kUncalibratorClass, DW::kUncalibratorVerb, kTagUncalibrator, &OnVerbPre, &OnVerbPost},
    {P::name::GamemodeClass, D::kSetPrec, kTagSetPrec, &OnSetPrecPre, nullptr},
};

}  // namespace

void Install(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
    for (Watch& w : g_watches) {
        if (w.reg != Reg::Pending) continue;
        if (sg::WatchClassName(w.cls, w.fn, w.tag, w.pre, w.post)) {
            w.reg = Reg::Registered;
            continue;
        }
        w.reg = Reg::Refused;  // the gate refuses a watch it cannot keep for the process, an uninstalled gate included
    }
}

void Tick() {
    bool pending = false;
    for (Watch& w : g_watches) pending |= !w.settled && w.reg != Reg::Pending;
    if (!pending) return;
    sg::ResolvePendingNames();
    for (Watch& w : g_watches) {
        if (w.settled || w.reg == Reg::Pending) continue;
        if (w.reg == Reg::Registered && sg::ClassNameWatchLive(w.cls, w.fn, w.tag)) {
            w.settled = true;
            UE_LOGI("dish_calib_sync: the watch on %ls::%ls is live", w.cls, w.fn);
        } else if (w.reg == Reg::Refused || sg::ClassNameWatchSettled(w.cls, w.fn, w.tag)) {
            w.settled = true;
            UE_LOGE("dish_calib_sync: the watch on %ls::%ls is dead -- %ls", w.cls, w.fn,
                    w.pre == &OnSetPrecPre ? L"a client's download rate can read a deviation for up to ten seconds"
                                           : L"a client's use of it is put back and never reaches the host");
        }
    }
}

// HOST: the changed dishes to every client, and all of them when the baseline primes (the session's first poll, or a
// desk reload on the host). CLIENT: the put-back.
void Poll(coop::net::Session* s) {
    D::DishCalibration rows[kMaxDishes];
    const int32_t n = D::ReadCalibrations(rows, kMaxDishes);
    if (n <= 0) return;
    if (s->role() != coop::net::Role::Host) {
        PutBack(rows, n);
        return;
    }
    if (!g_haveSent) {
        for (int32_t i = 0; i < n; ++i)
            if (rows[i].index >= 0 && rows[i].index < kMaxDishes) g_sent[rows[i].index] = rows[i].value;
        g_haveSent = true;
        SendAll(s, rows, n, -1);
        return;
    }
    coop::net::DishCalibPayload p{};
    for (int32_t i = 0; i < n; ++i) {
        const int32_t idx = rows[i].index;
        if (idx < 0 || idx >= kMaxDishes || Same(rows[i].value, g_sent[idx])) continue;
        auto& e = p.entries[p.count++];
        e.index = static_cast<uint8_t>(idx);
        e.value = rows[i].value;
        g_sent[idx] = rows[i].value;
    }
    if (p.count > 0) s->SendReliable(coop::net::ReliableKind::DishCalib, &p, sizeof(p));
}

void QueueConnectBroadcastForSlot(int peerSlot) {
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || s->role() != coop::net::Role::Host || !D::EnsureResolved()) return;
    D::DishCalibration rows[kMaxDishes];
    SendAll(s, rows, D::ReadCalibrations(rows, kMaxDishes), peerSlot);
}

void OnDishCalib(const coop::net::DishCalibPayload& p, uint8_t senderSlot) {
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s) return;
    // The host authors every dish's precision; a batch from anyone else is not theirs to send.
    if (s->role() == coop::net::Role::Host || senderSlot != 0) {
        if (!g_saidRefusedBatch) {
            g_saidRefusedBatch = true;
            UE_LOGW("dish_calib_sync: a precision batch from slot %u refused -- the host authors the precision",
                    static_cast<unsigned>(senderSlot));
        }
        return;
    }
    // Held as the host's values even before this copy's dishes resolve; the poll puts them in once they do.
    const bool resolved = D::EnsureResolved();
    const int32_t n = p.count <= kMaxDishes ? p.count : kMaxDishes;
    for (int32_t i = 0; i < n; ++i) {
        const auto& e = p.entries[i];
        if (e.index >= kMaxDishes) continue;
        g_host[e.index] = e.value;
        g_held[e.index] = true;
        if (resolved) D::WriteCalibration(e.index, e.value);
    }
}

// HOST: performs what it can of a client's intent and sends every named dish's value, as it now holds it, to all:
// the author learns the outcome whatever the host did, and no poll diff has to happen to carry it.
void OnDishCalibIntent(const coop::net::DishCalibPayload& p, uint8_t senderSlot) {
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || s->role() != coop::net::Role::Host || senderSlot == 0 || senderSlot >= coop::net::kMaxPeers) return;
    if (!D::EnsureResolved()) {
        UE_LOGW("dish_calib_sync: slot %u's precision intent dropped -- the dishes are not resolved here",
                static_cast<unsigned>(senderSlot));
        return;
    }
    const bool within = TakeToken(senderSlot);
    D::DishCalibration rows[kMaxDishes];
    const int32_t m = D::ReadCalibrations(rows, kMaxDishes);
    coop::net::DishCalibPayload out{};
    int performed = 0, refused = 0;
    const int32_t n = p.count <= kMaxDishes ? p.count : kMaxDishes;
    for (int32_t i = 0; i < n; ++i) {
        const auto& e = p.entries[i];
        const D::DishCalibration* row = nullptr;
        for (int32_t j = 0; j < m && !row; ++j)
            if (rows[j].index == e.index) row = &rows[j];
        if (!row) {  // no live dish at that index here: nothing to answer with
            ++refused;
            continue;
        }
        float value = row->value;
        if (within && std::isfinite(e.value) && D::WriteCalibration(e.index, e.value)) {
            value = e.value;
            ++performed;
            g_counts.lastIndex = e.index;
            g_counts.lastValue = e.value;
        } else {
            ++refused;
        }
        auto& a = out.entries[out.count++];
        a.index = e.index;
        a.value = value;
        g_sent[e.index] = value;
    }
    g_counts.intentsApplied += static_cast<uint64_t>(performed);
    g_counts.intentsRefused += static_cast<uint64_t>(refused);
    if (out.count > 0) s->SendReliable(coop::net::ReliableKind::DishCalib, &out, sizeof(out));
    if (performed > 0)
        UE_LOGI("dish_calib_sync: HOST performed %d of slot %u's dish precision(s), dish %d = %.4f last", performed,
                static_cast<unsigned>(senderSlot), g_counts.lastIndex, g_counts.lastValue);
    if (refused > 0) {
        Bucket& b = g_budget[senderSlot];
        const uint64_t now = ::GetTickCount64();
        if (now >= b.nextSayMs) {
            b.nextSayMs = now + kRefusalSayMs;
            UE_LOGW("dish_calib_sync: HOST refused %d of slot %u's dish precision(s) (%s) -- sent its own values",
                    refused, static_cast<unsigned>(senderSlot),
                    within ? "not a finite value, or no live dish there" : "past its intent budget");
        }
    }
}

void OnDeskReplaced() {
    g_haveSent = false;
    g_inFlight.clear();
}

void OnSessionEnd() {
    g_haveSent = false;
    for (bool& h : g_held) h = false;
    g_saidRefusedBatch = false;
    g_inFlight.clear();
    g_counts = Counts{};
    for (Bucket& b : g_budget) b = Bucket{};
}

Counts LaneCounts() { return g_counts; }

}  // namespace coop::dish_calib_sync
