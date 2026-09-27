// ue_wrap/devices/generator_panel.h -- engine access for a generator's repair puzzle (AtransformerMGPanel_C, the
// generator's child actor `panelObj`). Principle-7 engine-wrapper layer: no network or coop state;
// coop/world/power_puzzle drives the puzzle's crossing through here. The Activate button checks three pages: the
// sines (offset, frequency and amplitude against their targets), eight switches against a target byte, and nine
// rotators whose colored edges must meet (the color grid is built solved, and the rotators turn its cells). A
// setter redraws its page and sets that page's complete flag; a rotator's or a switch's move is its click's
// animation and sound under the panel's `isMoving` latch, and the move's end runs the setters. The grid's colors
// are drawn only where the grid is built, so a grid written from outside is drawn here with the same calls.

#pragma once

#include <cstdint>

namespace ue_wrap::generator_panel {

inline constexpr int kSwitches = 8;
inline constexpr int kRotators = 9;

struct Puzzle {
    uint8_t targetSine[3] = {};           // offset, frequency, amplitude
    uint8_t switchesTarget = 0;
    uint8_t colors[kRotators][4] = {};    // a cell's edges: top, right, bottom, left
    uint8_t sine[3] = {};
    uint8_t switches = 0;                 // bit i: switch i on
    uint8_t rotators[kRotators] = {};     // a quarter turn a step, 0..3
};

// Resolve transformerMGPanel_C's fields and verbs by name. Idempotent; false until the class has loaded (retried
// a second later), or for good when a field is missing (said once). Game thread.
bool EnsureResolved();

// The generator's panel (its `panelObj`), live, or null. Game thread.
void* PanelOf(void* gen);

// The panel's generator (its `transformer`), live, or null. Game thread.
void* GeneratorOf(void* panel);

// The puzzle as the panel holds it. False before the panel's own initiate has sized its arrays. Game thread.
bool Read(void* panel, Puzzle& out);

// The three complete flags the Activate button reads (generator actionOptionIndex @3044). Game thread.
bool Solved(void* panel);

// Whether a click's move is still running: its end runs the setters, so until then the complete flags do not yet
// say what the values do. Game thread.
bool IsMoving(void* panel);

// `in` written: the targets raw, the values raw, then what changed drawn by the panel's own calls. One rotator or
// one switch changed on a panel that is not mid-move takes its move (the click's animation and sound); more, or
// a moving panel, take the setter; the sines take setKnobs, and a written grid is drawn. False when the panel is
// not readable. Game thread.
bool Write(void* panel, const Puzzle& in);

// [dev] the grid drill's player inputs, through the panel's own handlers, so they run in its event graph as a
// player's do: a rotator's click (clicked_rotataors with rotator `i`'s button), a switch's (clicked_switchers),
// and the mouse wheel over the button at `button` in the panel's hover order (mouseDelta, whose addVal turns knob
// `button` -- 0 offset, 1 frequency, 2 amplitude -- by `delta`); and the use that enters the panel's interface
// (actionOptionIndex, action 4). A click waits for the move before it (IsMoving). Game thread.
bool ClickRotator(void* panel, int i);
bool ClickSwitch(void* panel, int i);
bool Scroll(void* panel, int32_t button, float delta);
bool Enter(void* panel, void* player);

}  // namespace ue_wrap::generator_panel
