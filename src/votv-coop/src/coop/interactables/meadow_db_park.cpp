// coop/interactables/meadow_db_park.cpp -- see coop/interactables/meadow_db_park.h.

#include "coop/interactables/meadow_db_park.h"

#include "ue_wrap/core/log.h"

#include <deque>
#include <vector>

namespace coop::meadow_db_park {
namespace {

constexpr auto kHoldTime = std::chrono::seconds(20);
// About 4 KB at the bound; legitimate entries are a handful.
constexpr size_t kHeldCap = 256;

struct Tomb { uint64_t hash; Clock::time_point until; };
std::vector<Tomb> g_tombs;

constexpr size_t kParkCap = 256;
// A row's blob is bounded by the wire (tens of KB); 2 MB holds any travel's worth of edits.
constexpr size_t kParkBytesCap = size_t{2} << 20;
struct Line { Kind kind; std::vector<uint8_t> blob; uint64_t hash; uint8_t senderSlot; };
std::deque<Line> g_lines;
size_t g_bytes = 0;         // the parked blobs' bytes
size_t g_refused = 0;       // the lines refused this episode, said when it ends
uint32_t g_clears = 0;      // Clear's count, which a drain reads around each replay

void SayRefused() {
    if (g_refused == 0) return;
    UE_LOGW("meadow_db: %zu line(s) were refused while the database was away -- the pen was full", g_refused);
    g_refused = 0;
}

}  // namespace

bool HoldDelete(uint64_t hash, uint8_t senderSlot, Clock::time_point now) {
    if (g_tombs.size() >= kHeldCap) {
        UE_LOGW("meadow_db: tombstone REFUSED (hash=%016llx, from slot %u) -- at the %zu-entry bound",
                static_cast<unsigned long long>(hash), static_cast<unsigned>(senderSlot), kHeldCap);
        return false;
    }
    g_tombs.push_back({hash, now + kHoldTime});
    return true;
}

bool ConsumeDelete(uint64_t hash) {
    for (auto it = g_tombs.begin(); it != g_tombs.end(); ++it) {
        if (it->hash == hash) {
            g_tombs.erase(it);
            return true;
        }
    }
    return false;
}

void ExpireDeletes(Clock::time_point now) {
    for (auto it = g_tombs.begin(); it != g_tombs.end();) {
        if (now >= it->until) {
            UE_LOGW("meadow_db: delete for hash %016llx expired unmatched", static_cast<unsigned long long>(it->hash));
            it = g_tombs.erase(it);
        } else {
            ++it;
        }
    }
}

void RestampDeletes(Clock::time_point now) {
    for (Tomb& t : g_tombs) t.until = now + kHoldTime;
}

void RetryDeletes(ApplyDeleteFn apply) {
    for (auto it = g_tombs.begin(); it != g_tombs.end();) {
        if (apply(it->hash)) it = g_tombs.erase(it);
        else ++it;
    }
}

size_t HeldDeletes() {
    return g_tombs.size();
}

bool Park(Kind kind, std::vector<uint8_t>&& blob, uint64_t hash, uint8_t senderSlot) {
    if (kind == Kind::Order && !g_lines.empty() && g_lines.back().kind == Kind::Order &&
        g_lines.back().senderSlot == senderSlot) {
        g_bytes = g_bytes - g_lines.back().blob.size() + blob.size();
        g_lines.back().blob = std::move(blob);
        return true;
    }
    if (g_lines.size() >= kParkCap || g_bytes + blob.size() > kParkBytesCap) {
        if (g_refused++ == 0)
            UE_LOGW("meadow_db: a line from slot %u REFUSED while the database is away -- %zu line(s), %zu bytes "
                    "already wait", static_cast<unsigned>(senderSlot), g_lines.size(), g_bytes);
        return false;
    }
    g_bytes += blob.size();
    g_lines.push_back({kind, std::move(blob), hash, senderSlot});
    return true;
}

size_t Drain(ReplayFn replay, size_t budget) {
    size_t n = 0;
    while (!g_lines.empty() && n < budget) {
        Line l = std::move(g_lines.front());
        g_lines.pop_front();
        g_bytes -= l.blob.size();
        const uint32_t clears = g_clears;
        const bool applied = replay(l.kind, l.blob, l.hash, l.senderSlot);
        if (clears != g_clears) break;  // the database moved inside the replay: the pen is already empty
        if (!applied) {
            g_bytes += l.blob.size();
            g_lines.push_front(std::move(l));
            break;
        }
        ++n;
    }
    if (g_lines.empty()) SayRefused();
    return n;
}

size_t Parked() {
    return g_lines.size();
}

bool ParkedAppend(uint64_t hash) {
    for (const Line& l : g_lines)
        if (l.kind == Kind::Append && l.hash == hash) return true;
    return false;
}

size_t Clear() {
    const size_t dropped = g_lines.size();
    ++g_clears;
    SayRefused();
    g_tombs.clear();
    g_lines.clear();
    g_bytes = 0;
    return dropped;
}

}  // namespace coop::meadow_db_park
