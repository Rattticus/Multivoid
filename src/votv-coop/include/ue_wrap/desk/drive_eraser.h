// ue_wrap/desk/drive_eraser.h -- the signal-drive eraser (signalDriveEraser_C): its delete press and the presentation
// of that press. The press (actionOptionIndex, action 4) plays the button; with a drive seated and the eraser idle it
// sets `processing`, speeds the wiper up, plays the begin sound and waits 3 s. Its resume (ubergraph entry 15) then
// wipes a drive that holds data, plays the done sound, slows the wiper and clears `processing`, or plays the deny
// sound when the seated drive is gone or empty. Engine-wrapper layer (principle 7): no gameplay or network logic.

#pragma once

#include <cstdint>

namespace ue_wrap::drive_eraser {

inline constexpr const wchar_t* kClassName = L"signalDriveEraser_C";
inline constexpr uint8_t kActionDelete = 4;     // the delete button's interaction action
inline constexpr int32_t kResumeEntry = 15;     // the ubergraph entry the press's 3 s Delay resumes at

// The running world's eraser (a level singleton), or null.
void* Instance();

// The delete press, as the button does: the reflected actionOptionIndex with `player` and action 4. The press body
// never reads its player. False when unresolved or the call failed. Game thread.
bool PressDelete(void* eraser, void* player);

// The press body's busy flag, set for the 3 s between a press and its resume. False when unresolved.
bool ReadProcessing(void* eraser, bool& out);

// What a peer that did not run the press shows of it, through the eraser's own components.
enum class Show : uint8_t {
    Click = 1,    // the button alone: nothing seated, or the eraser busy
    Start = 2,    // the button, the wiper at speed, the begin sound
    Done = 3,     // the done sound, the wiper slowed
    Deny = 4,     // the deny sound: the resume found no drive with data
    Refused = 5,  // the button and the deny sound: a press the host did not run
};
bool Present(void* eraser, Show what);

}  // namespace ue_wrap::drive_eraser
