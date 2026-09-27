// ue_wrap/world/votv_lib.cpp -- see header.

#include "ue_wrap/world/votv_lib.h"

#include "ue_wrap/core/cached_obj_ref.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/fname_utils.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"

namespace ue_wrap::votv_lib {
namespace {

namespace R = ue_wrap::reflection;

// One-time latched resolution (no per-call GUObjectArray walks). lib_C is a
// BlueprintFunctionLibrary -- static BP calls dispatch on its CDO.
// The default object is held by slot and serial, so a lib_C loaded again is found again rather than served dead.
ue_wrap::CachedObjRef g_libCdo;
void* g_stepFn  = nullptr;
void* g_glossFn = nullptr;
void* g_buoyantFn = nullptr;  // found on its own call, so step and addGloss never wait on it

bool Resolve() {
    if (g_libCdo.Alive() && g_stepFn && g_glossFn) return true;
    void* cls = R::FindClass(L"lib_C");
    if (!cls) return false;  // not loaded yet (menu) -- retry on a later call
    if (!g_libCdo.Alive()) {
        g_libCdo.Set(R::FindClassDefaultObject(L"lib_C"));
        g_stepFn = g_glossFn = g_buoyantFn = nullptr;  // found again on the class the new default object is of
    }
    if (!g_stepFn) g_stepFn = R::FindFunction(cls, L"step");
    if (!g_glossFn) g_glossFn = R::FindFunction(cls, L"addGloss");
    if (g_libCdo.Alive() && g_stepFn && g_glossFn) {
        UE_LOGI("votv_lib: resolved lib_C step and addGloss (cdo=%p)", g_libCdo.Raw());
        return true;
    }
    return false;
}

}  // namespace

bool CharacterStep(void* character, float volume) {
    if (!character || !Resolve()) return false;
    // Signature: step(ACharacter* Character, float Z_offset, AActor* callActor, float Volume, float
    // Pitch, float speedVolume, UAudioComponent* AudioComponent, UObject* __WorldContext,
    // FHitResult& OutHit). Volume is the caller's -- a puppet's steps are presentation and read too
    // loud at the native 1.0 -- and callActor is us, where the local player's own call passes null.
    // lib_C::step tests callActor for int_objects and calls its `stepped`; mainPlayer_C implements
    // the interface with an empty event, so ours costs one dispatch and does nothing.
    ue_wrap::ParamFrame f(g_stepFn);
    f.Set<void*>(L"Character", character);
    f.Set<float>(L"Z_offset", 0.f);
    f.Set<void*>(L"callActor", character);
    f.Set<float>(L"Volume", volume);
    f.Set<float>(L"Pitch", 1.0f);
    f.Set<float>(L"speedVolume", 400.0f);
    f.Set<void*>(L"AudioComponent", nullptr);
    f.Set<void*>(L"__WorldContext", character);
    // OutHit stays zeroed in the frame; the BP writes it, we ignore it.
    return ue_wrap::Call(g_libCdo.Raw(), f);
}

bool AddGloss(const std::wstring& name, int32_t level, void* worldContext) {
    if (name.empty() || !worldContext || !Resolve()) return false;
    const R::FName fname = ue_wrap::fname_utils::StringToFName(name);
    if (fname.ComparisonIndex == 0) return false;
    ue_wrap::ParamFrame f(g_glossFn);
    return f.valid() && f.Set<R::FName>(L"name", fname) && f.Set<int32_t>(L"level", level) &&
           f.Set<void*>(L"__WorldContext", worldContext) && ue_wrap::Call(g_libCdo.Raw(), f);
}

bool CheatsAllowed(void* worldContext, bool& allowed) {
    if (!worldContext || !Resolve()) return false;
    if (!g_buoyantFn) g_buoyantFn = R::FindFunction(R::ClassOf(g_libCdo.Raw()), L"isBuoyant");
    if (!g_buoyantFn) return false;
    ue_wrap::ParamFrame f(g_buoyantFn);
    if (!f.valid() || !f.Set<void*>(L"actor", nullptr) || !f.Set<void*>(L"__WorldContext", worldContext) ||
        !ue_wrap::Call(g_libCdo.Raw(), f))
        return false;
    allowed = f.Get<bool>(L"buoyant");
    return true;
}

}  // namespace ue_wrap::votv_lib
