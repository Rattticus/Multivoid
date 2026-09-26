// ue_wrap/desk/desk_press.cpp -- see ue_wrap/desk/desk_press.h.

#include "ue_wrap/desk/desk_press.h"

#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/hit_result.h"

namespace ue_wrap::desk_press {
namespace {

namespace R = reflection;

}  // namespace

void* Member(void* desk, const wchar_t* member) {
    void* cls = desk ? R::ClassOf(desk) : nullptr;
    const int32_t off = cls ? R::FindPropertyOffset(cls, member) : -1;
    if (off < 0) return nullptr;
    void* comp = *reinterpret_cast<void* const*>(static_cast<const uint8_t*>(desk) + off);
    return (comp && R::IsLive(comp)) ? comp : nullptr;
}

bool Press(void* desk, void* player, void* component) {
    void* cls = desk ? R::ClassOf(desk) : nullptr;
    void* lookFn = cls ? R::FindDispatchFunctionCached(cls, L"lookAt") : nullptr;
    void* pressFn = cls ? R::FindDispatchFunctionCached(cls, L"actionOptionIndex") : nullptr;
    if (!lookFn || !pressFn || !component || !player) {
        UE_LOGW("desk_press: press not made (lookAt=%d actionOptionIndex=%d component=%d player=%d)",
                lookFn ? 1 : 0, pressFn ? 1 : 0, component ? 1 : 0, player ? 1 : 0);
        return false;
    }
    const FVector at = engine::GetComponentLocation(component);
    ParamFrame look(lookFn);
    if (!look.valid() || !look.Set<void*>(L"player", player) ||
        !hit_result::Write(look, L"hit", desk, component, at) || !Call(desk, look)) {
        UE_LOGW("desk_press: press not made: the desk's lookAt with a hit on the component did not run");
        return false;
    }
    struct { void* data; int32_t num; int32_t max; } text{};
    if (look.GetRaw(L"text", &text, sizeof(text)) && text.data) R::EngineFree(text.data);  // the engine wrote it
    ParamFrame press(pressFn);
    if (!press.valid() || !press.Set<void*>(L"player", player) ||
        !hit_result::Write(press, L"hit", desk, component, at) ||
        !press.Set<void*>(L"lookAtComponent", component) || !Call(desk, press)) {
        UE_LOGW("desk_press: press not made: actionOptionIndex with a hit on the component did not run");
        return false;
    }
    return true;
}

void* Pressed(void* pressFunction, const uint8_t* locals) {
    const int32_t off = (pressFunction && locals) ? R::FindParamOffset(pressFunction, L"hit") : -1;
    return off < 0 ? nullptr : hit_result::Component(locals + off);
}

}  // namespace ue_wrap::desk_press
