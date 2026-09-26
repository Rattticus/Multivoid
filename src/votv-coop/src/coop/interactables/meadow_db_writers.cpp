// coop/interactables/meadow_db_writers.cpp -- see coop/interactables/meadow_db_writers.h.

#include "coop/interactables/meadow_db_writers.h"

#include "ue_wrap/core/log.h"

#include <iterator>

namespace coop::meadow_db_writers {
namespace {

namespace sg = ue_wrap::script_gate;

struct Writer { const wchar_t* cls; const wchar_t* fn; };
constexpr Writer kWriters[] = {
    {L"ui_laptop_C", L"addSignal"},
    {L"ui_laptop_C", L"removeSignal"},
    {L"ui_laptop_C", L"sortSignal"},
    {L"ui_signalName_C", L"ExecuteUbergraph_ui_signalName"},
};
constexpr int kTagWriter = 0x4D445742;  // 'MDWB'
bool g_watched = false;  // registered, once a process

}  // namespace

bool Watch(sg::PreFn pre, sg::PostFn post) {
    if (g_watched) return true;
    bool ok = true;
    for (const Writer& w : kWriters)
        ok = sg::WatchClassName(w.cls, w.fn, kTagWriter, pre, post) && ok;
    g_watched = ok;
    if (!ok) UE_LOGW("meadow_db: a writer's watch was refused -- a change it makes is not sent");
    return ok;
}

bool Settle() {
    sg::ResolvePendingNames();
    size_t live = 0;
    for (const Writer& w : kWriters)
        if (sg::ClassNameWatchLive(w.cls, w.fn, kTagWriter)) ++live;
    if (live < std::size(kWriters) && sg::PendingNameCount() > 0) return false;
    if (live == std::size(kWriters)) {
        UE_LOGI("meadow_db: the database's %zu writers are watched at the script-body gate", std::size(kWriters));
        return true;
    }
    for (const Writer& w : kWriters)
        if (!sg::ClassNameWatchLive(w.cls, w.fn, kTagWriter))
            UE_LOGW("meadow_db: the writer %ls::%ls is NOT watched -- a change it makes goes out only with the "
                    "next watched writer's", w.cls, w.fn);
    return true;
}

}  // namespace coop::meadow_db_writers
