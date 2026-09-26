// ue_wrap/desk/desk_press.h -- a player's press on the main desk (analogDScreenTest_C), as the game makes it: the
// player's useSelectedAction runs the desk's lookAt(player, hit), which sets the panel under the player's eye, and
// then actionOptionIndex(player, hit, action, lookAtComponent), whose body takes the pressed button from the hit
// (analogDScreenTest.cpp:2355). A press made here replays that pair with a hit on the chosen component; a press the
// game makes is read back from its actionOptionIndex frame. Engine-wrapper layer (principle 7), no network or coop
// state. Game thread, every function. Implementation: src/ue_wrap/desk/desk_press.cpp.

#pragma once

#include <cstdint>

namespace ue_wrap::desk_press {

// The desk's component variable `member` (a button, as `button_downl_saveSig1`), or null when it does not read.
void* Member(void* desk, const wchar_t* member);

// `player` presses `component` on `desk`: lookAt, then actionOptionIndex, each with a hit on the component. A look
// comes first because a press with a panel under the player's eye goes to that panel. True when both dispatched.
bool Press(void* desk, void* player, void* component);

// The component an actionOptionIndex call's frame took its press from: its `hit` parameter's component, read from
// `locals` (the call's parameters) as the body's BreakHitResult reads it. Null when it does not resolve.
void* Pressed(void* pressFunction, const uint8_t* locals);

}  // namespace ue_wrap::desk_press
