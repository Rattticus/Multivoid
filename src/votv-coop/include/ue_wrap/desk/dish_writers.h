// ue_wrap/desk/dish_writers.h -- the two classes outside the dish that write its precision (dish.calibration) at a
// player's own verb. The toolgun's calibration tool: its init(toolgun) writes the tool's float parameter, unclamped,
// into the dish the toolgun's trace hit, the dish at the tool's index, or the dish of its name. The uncalibrator: its
// playerHandUse_LMB(player) zeroes the dish the player's hitResult names, the body's only effect. Principle-7
// engine-wrapper layer, no network logic: the names a watch registers.

#pragma once

namespace ue_wrap::dish_writers {

inline constexpr const wchar_t* kToolClass = L"tool_setDishCalibration_C";
inline constexpr const wchar_t* kToolVerb = L"init";
inline constexpr const wchar_t* kUncalibratorClass = L"prop_uncalibrator_C";
inline constexpr const wchar_t* kUncalibratorVerb = L"playerHandUse_LMB";

}  // namespace ue_wrap::dish_writers
