// ue_wrap/devices/generator_panel.cpp -- see ue_wrap/devices/generator_panel.h. Offsets are the class's layout,
// the same in every world, and resolve once by name; verbs resolve on the instance in hand through the dispatch
// cache.

#include "ue_wrap/devices/generator_panel.h"

#include "ue_wrap/core/call.h"
#include "ue_wrap/core/component_calls.h"
#include "ue_wrap/core/field_io.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/object_index.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/engine/engine_component.h"  // SetComponentMaterial

#include <chrono>
#include <cstdint>

namespace ue_wrap::generator_panel {
namespace {

namespace R = reflection;

bool     g_resolved = false;
bool     g_refused = false;  // the class loaded but a field did not: permanent for this build
uint64_t g_nextTryMs = 0;
int32_t  g_offTargetSine[3] = {-1, -1, -1};
int32_t  g_offSine[3] = {-1, -1, -1};
int32_t  g_offSwitchesTarget = -1;
int32_t  g_offSwitches = -1;       // TArray<bool>
int32_t  g_offRotators = -1;       // TArray<uint8>
int32_t  g_offGrid = -1;           // TArray<Fstruct_generatorRotator>
int32_t  g_offRotatorButtons = -1;  // TArray<UPrimitiveComponent*>
int32_t  g_offGenerator = -1;      // transformer
int32_t  g_gridStride = 0;
int32_t  g_offEdge[4] = {-1, -1, -1, -1};  // within a grid cell: top, right, bottom, left
int32_t  g_offMoving = -1;
uint8_t  g_maskMoving = 0;
int32_t  g_offComplete[3] = {-1, -1, -1};
uint8_t  g_maskComplete[3] = {};
int32_t  g_offPanelObj = -1;       // generator_C.panelObj
int32_t  g_offSwitchButtons = -1;  // TArray<UPrimitiveComponent*>, the drill's clicks only

constexpr const wchar_t* kTargetSine[3] = { L"targetSine_offset", L"targetSine_frequency", L"targetSine_amplitude" };
constexpr const wchar_t* kSine[3] = { L"sine_offset", L"sine_frequency", L"sine_amplitude" };
constexpr const wchar_t* kEdge[4] = { L"top_", L"right_", L"bottom_", L"left_" };
constexpr const wchar_t* kComplete[3] = { L"isSineComplete", L"isSwitchesComplete", L"isRotatorsComplete" };
constexpr const wchar_t* kVerbs[] = { L"setKnobs", L"setSwitches", L"setRotators", L"moveRotator", L"moveSwitch",
                                      L"byteToColor" };

uint64_t NowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

uint8_t* At(void* obj, int32_t off) { return reinterpret_cast<uint8_t*>(obj) + off; }

bool ReadBit(void* obj, int32_t off, uint8_t mask) { return (*At(obj, off) & mask) != 0; }

void* ObjectAt(void* obj, int32_t off) { return off < 0 ? nullptr : *reinterpret_cast<void* const*>(At(obj, off)); }

const field_io::TArrayView* ArrayAt(void* obj, int32_t off) {
    return reinterpret_cast<const field_io::TArrayView*>(At(obj, off));
}

// The panel's arrays, as its own init sizes them; null while they are not.
uint8_t* Elements(void* panel, int32_t off, int32_t count) {
    const field_io::TArrayView* a = ArrayAt(panel, off);
    return (a->data && a->num == count) ? a->data : nullptr;
}

int32_t Clamp15(int32_t v) { return v < 0 ? 0 : v > 15 ? 15 : v; }

bool CallVerb(void* panel, const wchar_t* verb) { return component_calls::CallParamlessNamed(panel, verb); }

bool CallIndexed(void* panel, const wchar_t* verb, int32_t index) {
    void* fn = R::FindDispatchFunctionCached(R::ClassOf(panel), verb);
    ParamFrame f(fn);
    return fn && f.valid() && f.Set<int32_t>(L"index", index) && Call(panel, f);
}

// The grid's colors onto the rotator meshes, as the grid's builder draws them (solveColorGrid's last loop): each
// cell's edges into material slots 1 to 4 through byteToColor.
void DrawColors(void* panel, const uint8_t* grid, void* const* buttons) {
    void* fn = R::FindDispatchFunctionCached(R::ClassOf(panel), L"byteToColor");
    for (int i = 0; i < kRotators; ++i) {
        void* mesh = buttons[i];
        if (!mesh || !R::IsLive(mesh)) continue;
        for (int e = 0; e < 4; ++e) {
            ParamFrame f(fn);
            const uint8_t color = grid[i * g_gridStride + g_offEdge[e]];
            if (!fn || !f.valid() || !f.Set<uint8_t>(L"byte", color) || !Call(panel, f)) return;
            engine::SetComponentMaterial(mesh, e + 1, f.Get<void*>(L"mat"));
        }
    }
}

}  // namespace

bool EnsureResolved() {
    if (g_resolved) return true;
    if (g_refused) return false;
    const uint64_t now = NowMs();
    if (now < g_nextTryMs) return false;
    g_nextTryMs = now + 1000;
    void* cls = object_index::ClassByName(L"transformerMGPanel_C");
    void* genCls = object_index::ClassByName(L"generator_C");
    if (!cls || !genCls) return false;  // not streamed in yet; retried a second later

    auto refuse = [&](const wchar_t* what) {
        g_refused = true;
        UE_LOGE("generator_panel: %ls did not resolve on a loaded transformerMGPanel_C -- the repair puzzle is each "
                "peer's own for this game build", what);
        return false;
    };
    int32_t offTargetSine[3], offSine[3];
    for (int k = 0; k < 3; ++k) {
        if ((offTargetSine[k] = R::FindPropertyOffset(cls, kTargetSine[k])) < 0) return refuse(kTargetSine[k]);
        if ((offSine[k] = R::FindPropertyOffset(cls, kSine[k])) < 0) return refuse(kSine[k]);
    }
    const int32_t offSwitchesTarget = R::FindPropertyOffset(cls, L"switches_target");
    if (offSwitchesTarget < 0) return refuse(L"switches_target");
    const int32_t offSwitches = R::FindPropertyOffset(cls, L"switches_states");
    if (offSwitches < 0) return refuse(L"switches_states");
    const int32_t offRotators = R::FindPropertyOffset(cls, L"rotators_states");
    if (offRotators < 0) return refuse(L"rotators_states");
    const int32_t offGrid = R::FindPropertyOffset(cls, L"rotators_colorGrid");
    if (offGrid < 0) return refuse(L"rotators_colorGrid");
    const int32_t offButtons = R::FindPropertyOffset(cls, L"buttons_rotators");
    if (offButtons < 0) return refuse(L"buttons_rotators");
    const int32_t offGenerator = R::FindPropertyOffset(cls, L"transformer");
    if (offGenerator < 0) return refuse(L"transformer");
    const int32_t offPanelObj = R::FindPropertyOffset(genCls, L"panelObj");
    if (offPanelObj < 0) return refuse(L"generator_C::panelObj");
    // The grid cell's struct, reached through a function that takes one by value.
    void* rotate = R::FindDispatchFunctionCached(cls, L"rotateColor");
    void* cell = rotate ? R::PropertyInnerStruct(rotate, L"in") : nullptr;
    const int32_t stride = cell ? R::StructSize(cell) : 0;
    if (stride <= 0) return refuse(L"Fstruct_generatorRotator");
    int32_t offEdge[4];
    for (int e = 0; e < 4; ++e)
        if ((offEdge[e] = R::FindPropertyOffsetByPrefix(cell, kEdge[e])) < 0 || offEdge[e] >= stride)
            return refuse(kEdge[e]);
    int32_t offMoving = -1, offComplete[3];
    uint8_t maskMoving = 0, maskComplete[3];
    if (!R::FindBoolProperty(cls, L"isMoving", offMoving, maskMoving)) return refuse(L"isMoving");
    for (int k = 0; k < 3; ++k)
        if (!R::FindBoolProperty(cls, kComplete[k], offComplete[k], maskComplete[k])) return refuse(kComplete[k]);
    for (const wchar_t* verb : kVerbs)
        if (!R::FindDispatchFunctionCached(cls, verb)) return refuse(verb);
    g_offSwitchButtons = R::FindPropertyOffset(cls, L"buttons_switches");  // the drill's only: a miss is no refusal

    for (int k = 0; k < 3; ++k) {
        g_offTargetSine[k] = offTargetSine[k];
        g_offSine[k] = offSine[k];
        g_offComplete[k] = offComplete[k];
        g_maskComplete[k] = maskComplete[k];
    }
    for (int e = 0; e < 4; ++e) g_offEdge[e] = offEdge[e];
    g_offSwitchesTarget = offSwitchesTarget;
    g_offSwitches = offSwitches;
    g_offRotators = offRotators;
    g_offGrid = offGrid;
    g_offRotatorButtons = offButtons;
    g_offGenerator = offGenerator;
    g_offPanelObj = offPanelObj;
    g_gridStride = stride;
    g_offMoving = offMoving;
    g_maskMoving = maskMoving;
    g_resolved = true;
    UE_LOGI("generator_panel: resolved sines@0x%X/0x%X/0x%X targets@0x%X/0x%X/0x%X switches@0x%X/0x%X "
            "rotators@0x%X grid@0x%X (cell %d bytes, edges %d/%d/%d/%d)", offSine[0], offSine[1], offSine[2],
            offTargetSine[0], offTargetSine[1], offTargetSine[2], offSwitchesTarget, offSwitches, offRotators, offGrid,
            stride, offEdge[0], offEdge[1], offEdge[2], offEdge[3]);
    return true;
}

void* PanelOf(void* gen) {
    if (!gen || !EnsureResolved()) return nullptr;
    void* panel = ObjectAt(gen, g_offPanelObj);
    return (panel && R::IsLive(panel)) ? panel : nullptr;
}

void* GeneratorOf(void* panel) {
    if (!panel || !EnsureResolved()) return nullptr;
    void* gen = ObjectAt(panel, g_offGenerator);
    return (gen && R::IsLive(gen)) ? gen : nullptr;
}

bool Read(void* panel, Puzzle& out) {
    if (!panel || !EnsureResolved()) return false;
    const uint8_t* switches = Elements(panel, g_offSwitches, kSwitches);
    const uint8_t* rotators = Elements(panel, g_offRotators, kRotators);
    const uint8_t* grid = Elements(panel, g_offGrid, kRotators);
    if (!switches || !rotators || !grid) return false;
    for (int k = 0; k < 3; ++k) {
        out.targetSine[k] = static_cast<uint8_t>(Clamp15(*reinterpret_cast<int32_t*>(At(panel, g_offTargetSine[k]))));
        out.sine[k] = static_cast<uint8_t>(Clamp15(*reinterpret_cast<int32_t*>(At(panel, g_offSine[k]))));
    }
    out.switchesTarget = *At(panel, g_offSwitchesTarget);
    out.switches = 0;
    for (int i = 0; i < kSwitches; ++i)
        if (switches[i]) out.switches = static_cast<uint8_t>(out.switches | (1u << i));
    for (int i = 0; i < kRotators; ++i) {
        out.rotators[i] = static_cast<uint8_t>(rotators[i] & 3);
        for (int e = 0; e < 4; ++e) out.colors[i][e] = grid[i * g_gridStride + g_offEdge[e]];
    }
    return true;
}

bool Solved(void* panel) {
    if (!panel || !EnsureResolved()) return false;
    for (int k = 0; k < 3; ++k)
        if (!ReadBit(panel, g_offComplete[k], g_maskComplete[k])) return false;
    return true;
}

bool IsMoving(void* panel) { return panel && EnsureResolved() && ReadBit(panel, g_offMoving, g_maskMoving); }

bool Write(void* panel, const Puzzle& in) {
    Puzzle cur;
    if (!Read(panel, cur)) return false;
    uint8_t* switches = Elements(panel, g_offSwitches, kSwitches);
    uint8_t* rotators = Elements(panel, g_offRotators, kRotators);
    uint8_t* grid = Elements(panel, g_offGrid, kRotators);
    void* const* buttons = reinterpret_cast<void* const*>(Elements(panel, g_offRotatorButtons, kRotators));
    bool sines = false, switchTarget = false, colors = false;
    for (int k = 0; k < 3; ++k) {
        if (cur.targetSine[k] != in.targetSine[k]) {
            *reinterpret_cast<int32_t*>(At(panel, g_offTargetSine[k])) = in.targetSine[k];
            sines = true;
        }
        if (cur.sine[k] != in.sine[k]) {
            *reinterpret_cast<int32_t*>(At(panel, g_offSine[k])) = in.sine[k];
            sines = true;
        }
    }
    if (cur.switchesTarget != in.switchesTarget) {
        *At(panel, g_offSwitchesTarget) = in.switchesTarget;
        switchTarget = true;
    }
    int switchesChanged = 0, lastSwitch = -1, rotatorsChanged = 0, lastRotator = -1;
    for (int i = 0; i < kSwitches; ++i) {
        const bool on = (in.switches >> i) & 1u;
        if (((cur.switches >> i) & 1u) == static_cast<unsigned>(on)) continue;
        switches[i] = on ? 1 : 0;
        ++switchesChanged;
        lastSwitch = i;
    }
    for (int i = 0; i < kRotators; ++i) {
        for (int e = 0; e < 4; ++e) {
            if (cur.colors[i][e] == in.colors[i][e]) continue;
            grid[i * g_gridStride + g_offEdge[e]] = in.colors[i][e];
            colors = true;
        }
        if (cur.rotators[i] == (in.rotators[i] & 3)) continue;
        rotators[i] = static_cast<uint8_t>(in.rotators[i] & 3);
        ++rotatorsChanged;
        lastRotator = i;
    }
    if (colors && buttons) DrawColors(panel, grid, buttons);
    if (sines) CallVerb(panel, L"setKnobs");
    if (switchesChanged || switchTarget) {
        if (switchesChanged == 1 && !switchTarget && !ReadBit(panel, g_offMoving, g_maskMoving))
            CallIndexed(panel, L"moveSwitch", lastSwitch);
        else
            CallVerb(panel, L"setSwitches");
    }
    if (rotatorsChanged || colors) {
        if (rotatorsChanged == 1 && !colors && !ReadBit(panel, g_offMoving, g_maskMoving))
            CallIndexed(panel, L"moveRotator", lastRotator);
        else
            CallVerb(panel, L"setRotators");
    }
    return true;
}

namespace {
// A click on button `i` of the panel's `offButtons` array through `handler`; the key pressed stays zeroed, since
// the handlers never read it.
bool ClickButton(void* panel, int32_t offButtons, int count, int i, const wchar_t* handler) {
    if (!panel || !EnsureResolved() || offButtons < 0 || i < 0 || i >= count) return false;
    void* const* buttons = reinterpret_cast<void* const*>(Elements(panel, offButtons, count));
    void* fn = R::FindDispatchFunctionCached(R::ClassOf(panel), handler);
    ParamFrame f(fn);
    return buttons && buttons[i] && fn && f.valid() && f.Set<void*>(L"TouchedComponent", buttons[i]) &&
           Call(panel, f);
}
}  // namespace

bool ClickRotator(void* panel, int i) {
    return ClickButton(panel, g_offRotatorButtons, kRotators, i, L"clicked_rotataors");
}

bool ClickSwitch(void* panel, int i) {
    return ClickButton(panel, g_offSwitchButtons, kSwitches, i, L"clicked_switchers");
}

bool Enter(void* panel, void* player) {
    void* fn = panel && EnsureResolved() ? R::FindDispatchFunctionCached(R::ClassOf(panel), L"actionOptionIndex")
                                         : nullptr;
    ParamFrame f(fn);
    return fn && player && f.valid() && f.Set<void*>(L"player", player) && f.Set<uint8_t>(L"action", 4) &&
           Call(panel, f);
}

bool Scroll(void* panel, int32_t button, float delta) {
    if (!panel || !EnsureResolved()) return false;
    const int32_t off = R::FindPropertyOffset(R::ClassOf(panel), L"buttonUnderCursor");
    void* fn = R::FindDispatchFunctionCached(R::ClassOf(panel), L"mouseDelta");
    ParamFrame f(fn);
    if (off < 0 || !fn || !f.valid()) return false;
    *reinterpret_cast<int32_t*>(At(panel, off)) = button;  // what the tick's findButtonUnderCursor writes
    return f.Set<float>(L"delta", delta) && Call(panel, f);
}

}  // namespace ue_wrap::generator_panel
