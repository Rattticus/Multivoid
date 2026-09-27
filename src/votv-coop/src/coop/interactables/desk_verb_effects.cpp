// coop/interactables/desk_verb_effects.cpp -- see coop/interactables/desk_verb_effects.h.

#include "coop/interactables/desk_verb_effects.h"

#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/player/players_registry.h"

#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/script_gate.h"
#include "ue_wrap/core/ufunction_hook.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/world/votv_lib.h"

#include <atomic>
#include <string>

namespace coop::desk_verb_effects {
namespace {

namespace R  = ue_wrap::reflection;
namespace sg = ue_wrap::script_gate;
namespace UH = ue_wrap::ufunction_hook;
namespace E  = ue_wrap::engine;
namespace DV = coop::net::desk_verb;
using coop::net::DeskVerbPayload;

constexpr int kTagGloss = 0x44564701;  // 'DVG' 1
// One pointer per name: the gate knows a watch by the literals it was registered with.
constexpr const wchar_t* kGlossClass = L"lib_C";
constexpr const wchar_t* kGlossName = L"addGloss";

std::atomic<coop::net::Session*> g_session{nullptr};

// The gloss seam: the gate's watch on lib_C::addGloss, and its parameters' offsets. Refused for this process when
// the watch cannot be registered or settles dead, or when addGloss or its parameters do not read on a loaded lib_C.
bool    g_glossWatched = false;
bool    g_glossLive = false;
bool    g_glossRefused = false;
int32_t g_offGlossName = -1, g_offGlossLevel = -1;

// The sound seam: the pre hook on GameplayStatics::PlaySound2D, and its parameters' offsets.
void*   g_soundFn = nullptr;
bool    g_soundHooked = false;
bool    g_soundRefused = false;
int32_t g_offSound = -1, g_offVolume = -1, g_offPitch = -1, g_offStart = -1, g_offUi = -1;

// The press running for a client, while a Replay is open.
bool     g_open = false;
uint8_t  g_slot = 0;
uint32_t g_seq = 0;

Counts g_counts;

// `text` into the payload, ASCII; false for a name the wire cannot carry.
bool PutText(DeskVerbPayload& p, const std::wstring& text) {
    if (text.empty() || text.size() > DV::kTextCap) return false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] < 0x20 || text[i] > 0x7E) return false;
        p.text[i] = static_cast<char>(text[i]);
    }
    p.textLen = static_cast<uint8_t>(text.size());
    return true;
}

std::wstring TextOf(const DeskVerbPayload& p) {
    const size_t n = p.textLen < DV::kTextCap ? p.textLen : DV::kTextCap;
    return std::wstring(p.text, p.text + n);
}

void SendToPresser(DeskVerbPayload& p) {
    auto* s = g_session.load(std::memory_order_acquire);
    p.seq = g_seq;
    if (!s || !s->connected() ||
        !s->SendReliableToSlot(g_slot, coop::net::ReliableKind::DeskVerb, &p, static_cast<int>(sizeof(p))))
        ++g_counts.lost;
}

// The gloss a replayed SAVE adds is the presser's: refused here, sent there.
sg::Verdict OnGlossPre(const sg::Call& c) {
    if (!g_open) return sg::Verdict::Run;
    DeskVerbPayload p{};
    p.op = DV::kOpGloss;
    p.level = *reinterpret_cast<const int32_t*>(c.locals + g_offGlossLevel);
    const std::wstring name = R::ToString(*reinterpret_cast<const R::FName*>(c.locals + g_offGlossName));
    if (!PutText(p, name)) {
        ++g_counts.lost;
        UE_LOGW("desk_verb: slot %u's gloss '%ls' cannot cross the wire -- refused here, not sent",
                static_cast<unsigned>(g_slot), name.c_str());
        return sg::Verdict::Cancel;
    }
    SendToPresser(p);
    if (++g_counts.glossesSent <= 3)
        UE_LOGI("desk_verb: HOST refused the replayed press's gloss '%ls' (level %d) and sent it to slot %u (#%u)",
                name.c_str(), p.level, static_cast<unsigned>(g_slot), g_seq);
    return sg::Verdict::Cancel;
}

// A 2D sound a replayed press plays is heard by its presser: refused here, sent there.
sg::Verdict OnSound2DPre(void*, void*, const uint8_t* parms) {
    if (!g_open) return sg::Verdict::Run;
    void* sound = *reinterpret_cast<void* const*>(parms + g_offSound);
    DeskVerbPayload p{};
    p.op = DV::kOpSound;
    p.volume = *reinterpret_cast<const float*>(parms + g_offVolume);
    p.pitch = *reinterpret_cast<const float*>(parms + g_offPitch);
    p.startTime = *reinterpret_cast<const float*>(parms + g_offStart);
    p.uiSound = parms[g_offUi] ? 1 : 0;
    if (!sound || !PutText(p, E::SoundName(sound))) {
        ++g_counts.lost;
        return sg::Verdict::Cancel;
    }
    SendToPresser(p);
    if (++g_counts.soundsSent <= 3)
        UE_LOGI("desk_verb: HOST refused the replayed press's 2D sound and sent it to slot %u (#%u)",
                static_cast<unsigned>(g_slot), g_seq);
    return sg::Verdict::Cancel;
}

void ResolveGloss() {
    if (g_glossRefused || (g_offGlossName >= 0 && g_offGlossLevel >= 0)) return;
    void* cls = ue_wrap::object_index::ClassByName(L"lib_C");
    if (!cls) return;  // loading: a later tick finds it
    void* fn = R::FindFunction(cls, L"addGloss");
    g_offGlossName = fn ? R::FindParamOffset(fn, L"name") : -1;
    g_offGlossLevel = fn ? R::FindParamOffset(fn, L"level") : -1;
    if (g_offGlossName < 0 || g_offGlossLevel < 0) {
        g_glossRefused = true;
        UE_LOGE("desk_verb: lib_C's addGloss or its parameters do not read (fn=%p name=%d level=%d) -- a client's "
                "desk press will not run on the host", fn, g_offGlossName, g_offGlossLevel);
    }
}

void InstallSound() {
    if (g_soundHooked || g_soundRefused) return;
    void* cls = ue_wrap::object_index::ClassByName(L"GameplayStatics");
    g_soundFn = cls ? R::FindFunction(cls, L"PlaySound2D") : nullptr;
    if (!g_soundFn) return;
    g_offSound = R::FindParamOffset(g_soundFn, L"Sound");
    g_offVolume = R::FindParamOffset(g_soundFn, L"VolumeMultiplier");
    g_offPitch = R::FindParamOffset(g_soundFn, L"PitchMultiplier");
    g_offStart = R::FindParamOffset(g_soundFn, L"StartTime");
    g_offUi = R::FindParamOffset(g_soundFn, L"bIsUISound");
    if (g_offSound < 0 || g_offVolume < 0 || g_offPitch < 0 || g_offStart < 0 || g_offUi < 0 ||
        !UH::InstallPreHook(g_soundFn, &OnSound2DPre, /*armed=*/false)) {
        g_soundRefused = true;
        UE_LOGE("desk_verb: no pre hook on PlaySound2D (offsets %d/%d/%d/%d/%d) -- a client's desk press will not "
                "run on the host", g_offSound, g_offVolume, g_offPitch, g_offStart, g_offUi);
        return;
    }
    g_soundHooked = true;
}

}  // namespace

void Install(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
    if (!g_glossWatched && !g_glossRefused) {
        g_glossWatched = sg::WatchClassName(kGlossClass, kGlossName, kTagGloss, &OnGlossPre, nullptr);
        if (!g_glossWatched) {
            g_glossRefused = true;  // a full watch table or no gate: said by the gate, and final
            UE_LOGE("desk_verb: the gate took no watch on lib_C::addGloss -- a client's desk press will not run on "
                    "the host");
        }
    }
    if (g_glossWatched && !g_glossLive && !g_glossRefused) {
        sg::ResolvePendingNames();
        g_glossLive = sg::ClassNameWatchLive(kGlossClass, kGlossName, kTagGloss);
        if (!g_glossLive && sg::ClassNameWatchSettled(kGlossClass, kGlossName, kTagGloss)) {
            g_glossRefused = true;
            UE_LOGE("desk_verb: the watch on lib_C::addGloss settled dead -- a client's desk press will not run on "
                    "the host");
        }
    }
    ResolveGloss();
    InstallSound();
}

bool Ready() {
    return g_glossLive && g_offGlossName >= 0 && g_offGlossLevel >= 0 && g_soundHooked;
}

bool Refused() { return g_soundRefused || g_glossRefused; }

Counts CountsNow() { return g_counts; }

Replay::Replay(uint8_t slot, uint32_t seq) : outerSlot_(g_slot), outerSeq_(g_seq), outerOpen_(g_open) {
    g_slot = slot;
    g_seq = seq;
    g_open = true;
    if (g_soundHooked) UH::SetArmed(g_soundFn, &OnSound2DPre, true);
}

Replay::~Replay() {
    g_slot = outerSlot_;
    g_seq = outerSeq_;
    g_open = outerOpen_;
    if (g_soundHooked && !g_open) UH::SetArmed(g_soundFn, &OnSound2DPre, false);
}

void OnEffect(const DeskVerbPayload& p) {
    void* local = coop::players::Registry::Get().Local();
    if (!local) return;
    const std::wstring text = TextOf(p);
    if (p.op == DV::kOpGloss) {
        if (!ue_wrap::votv_lib::AddGloss(text, p.level, local)) {
            ++g_counts.lost;
            UE_LOGW("desk_verb: the host's gloss '%ls' for press #%u did not run here", text.c_str(), p.seq);
            return;
        }
        if (++g_counts.glossesMade <= 3)
            UE_LOGI("desk_verb: CLIENT added the gloss '%ls' (level %d) of press #%u to this profile", text.c_str(),
                    p.level, p.seq);
        return;
    }
    if (p.op == DV::kOpSound) {
        void* sound = E::FindSound(text);
        if (!sound) {
            ++g_counts.lost;
            UE_LOGW("desk_verb: the sound '%ls' of press #%u is not loaded here", text.c_str(), p.seq);
            return;
        }
        E::PlaySound2D(local, sound, p.volume, p.pitch, p.startTime, p.uiSound != 0);
        if (++g_counts.soundsMade <= 3) UE_LOGI("desk_verb: CLIENT played press #%u's sound '%ls'", p.seq, text.c_str());
    }
}

void OnDisconnect() {
    const Counts& c = g_counts;
    if (c.glossesSent || c.soundsSent || c.glossesMade || c.soundsMade || c.lost)
        UE_LOGI("desk_verb: session end -- glosses sent=%llu made=%llu, sounds sent=%llu played=%llu, lost=%llu",
                c.glossesSent, c.glossesMade, c.soundsSent, c.soundsMade, c.lost);
    g_counts = Counts{};
    g_open = false;
    g_slot = 0;
    g_seq = 0;
    if (g_soundHooked) UH::SetArmed(g_soundFn, &OnSound2DPre, false);
}

}  // namespace coop::desk_verb_effects
