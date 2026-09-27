// ue_wrap/desk/desk_detector.cpp -- see ue_wrap/desk/desk_detector.h.

#include "ue_wrap/desk/desk_detector.h"

#include "ue_wrap/core/call.h"
#include "ue_wrap/core/fname_utils.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/sdk_profile.h"

#include <cstring>

namespace ue_wrap::desk_detector {
namespace {

namespace P = profile;
namespace R = reflection;

R::InstanceOffset g_multiplier{L"DL_detectorMultiplier"};
R::InstanceOffset g_needle{L"DL_resDetecPercent"};
R::InstanceOffset g_canDL{L"canDL"};
R::InstanceOffset g_gamemode{L"gamemode"};
R::InstanceOffset g_objectRenderer{L"objectRenderer"};
R::InstanceOffset g_signalObject{L"signalObjectActor"};
R::InstanceOffset g_fullyProcessed{L"fullyProcessed"};

// The layout the entry rests on, from the desk's CFG: the statement after the step pushes the flow that
// setFullyProcessedSignalObject's call opens, then falls into downloadTexts. The opcodes are UE4.27's EExprToken.
constexpr int32_t kPushTarget = 4587;
constexpr uint8_t kExPushExecutionFlow = 0x4C;
constexpr uint8_t kExLocalVirtualFunction = 0x45;

template <typename T>
T* Field(void* desk, R::InstanceOffset& member) {
    const int32_t off = desk ? member.Of(desk) : -1;
    return off < 0 ? nullptr : reinterpret_cast<T*>(static_cast<uint8_t*>(desk) + off);
}

// The live object in `obj`'s object member, or null.
void* ObjectAt(void* obj, R::InstanceOffset& member) {
    void* const* f = Field<void*>(obj, member);
    void* o = f ? *f : nullptr;
    return (o && R::IsLive(o)) ? o : nullptr;
}

void* Function(void* desk, const wchar_t* name) {
    void* cls = desk ? R::ClassOf(desk) : nullptr;
    return cls ? R::FindDispatchFunctionCached(cls, name) : nullptr;
}

bool CallNoArgs(void* desk, const wchar_t* name) {
    void* fn = Function(desk, name);
    if (!fn) return false;
    ParamFrame f(fn);
    return f.valid() && Call(desk, f);
}

// One ubergraph's answers: its EntryPoint parameter's offset and whether its layout holds. The desk's class is a
// Blueprint's, found again after a world change, so the answers are kept per UFunction and read again for a new one.
struct UbergraphFacts {
    void*   fn = nullptr;
    int32_t entryOff = -1;
    bool    layoutHolds = false;
};
UbergraphFacts g_facts;

bool LayoutOf(void* fn) {
    const auto* base = static_cast<const uint8_t*>(fn);
    const uint8_t* script = *reinterpret_cast<const uint8_t* const*>(base + P::off::UStruct_Script);
    const int32_t num = *reinterpret_cast<const int32_t*>(base + P::off::UStruct_ScriptNum);
    if (!script || num < kPushTarget + 5) {
        UE_LOGW("desk_detector: the ubergraph's bytecode holds %d bytes -- the post-step entry stays shut", num);
        return false;
    }
    uint32_t target = 0;
    std::memcpy(&target, script + kPostStepEntry + 1, sizeof(target));
    int32_t named = 0;
    std::memcpy(&named, script + kPushTarget + 1, sizeof(named));
    const int32_t want = ue_wrap::fname_utils::StringToFName(L"setFullyProcessedSignalObject").ComparisonIndex;
    const bool holds = script[kPostStepEntry] == kExPushExecutionFlow && target == static_cast<uint32_t>(kPushTarget) &&
                       script[kPushTarget] == kExLocalVirtualFunction && want != 0 && named == want;
    if (holds)
        UE_LOGI("desk_detector: the post-step entry %d holds (a push of %d, whose call is setFullyProcessedSignalObject)",
                kPostStepEntry, kPushTarget);
    else
        UE_LOGW("desk_detector: the post-step entry %d does not hold (op 0x%02X target %u, op 0x%02X name %d, want "
                "0x%02X %d 0x%02X %d) -- it stays shut", kPostStepEntry, script[kPostStepEntry], target,
                script[kPushTarget], named, kExPushExecutionFlow, kPushTarget, kExLocalVirtualFunction, want);
    return holds;
}

UbergraphFacts* FactsOf(void* fn) {
    if (!fn) return nullptr;
    if (g_facts.fn != fn) {
        g_facts = UbergraphFacts{};
        g_facts.fn = fn;
        g_facts.entryOff = R::FindParamOffset(fn, L"EntryPoint");
        g_facts.layoutHolds = LayoutOf(fn);
    }
    return &g_facts;
}

}  // namespace

bool ReadMultiplier(void* desk, float& out) {
    const float* f = Field<float>(desk, g_multiplier);
    if (!f) return false;
    out = *f;
    return true;
}

bool WriteMultiplier(void* desk, float value) {
    float* f = Field<float>(desk, g_multiplier);
    if (!f) return false;
    *f = value;
    return true;
}

bool ReadNeedle(void* desk, float& out) {
    const float* f = Field<float>(desk, g_needle);
    if (!f) return false;
    out = *f;
    return true;
}

bool WriteNeedle(void* desk, float value) {
    float* f = Field<float>(desk, g_needle);
    if (!f) return false;
    *f = value;
    return true;
}

bool ReadCanDL(void* desk, bool& out) {
    const uint8_t* f = Field<uint8_t>(desk, g_canDL);
    if (!f) return false;
    out = *f != 0;
    return true;
}

bool ReadFullyProcessed(void* desk, bool& out) {
    void* gamemode = ObjectAt(desk, g_gamemode);
    void* renderer = gamemode ? ObjectAt(gamemode, g_objectRenderer) : nullptr;
    void* signal = renderer ? ObjectAt(renderer, g_signalObject) : nullptr;
    const uint8_t* f = signal ? Field<uint8_t>(signal, g_fullyProcessed) : nullptr;
    if (!f) return false;
    out = *f != 0;
    return true;
}

bool CallCanSaveSignal(void* desk) { return CallNoArgs(desk, L"canSaveSignal"); }

bool CallSetFullyProcessedSignalObject(void* desk) { return CallNoArgs(desk, L"setFullyProcessedSignalObject"); }

void* Ubergraph(void* desk) { return Function(desk, L"ExecuteUbergraph_analogDScreenTest"); }

bool ReadEntry(void* ubergraph, const uint8_t* locals, int32_t& out) {
    UbergraphFacts* f = FactsOf(ubergraph);
    if (!f || f->entryOff < 0 || !locals) return false;
    std::memcpy(&out, locals + f->entryOff, sizeof(out));
    return true;
}

bool EnterPostStep(void* desk) {
    void* fn = Ubergraph(desk);
    UbergraphFacts* facts = FactsOf(fn);
    if (!facts || !facts->layoutHolds) return false;
    ParamFrame f(fn);
    return f.valid() && f.Set<int32_t>(L"EntryPoint", kPostStepEntry) && Call(desk, f);
}

}  // namespace ue_wrap::desk_detector
