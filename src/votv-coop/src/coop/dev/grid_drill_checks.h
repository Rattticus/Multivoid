// coop/dev/grid_drill_checks.h -- [dev] the grid drill's readings, shared by its legs: what a peer says of its
// grid on a "[GRID-DRILL]" line and whether its unit flags hold the grid's invariant, the client's end check against
// the host's last canonical, rows, and the drill's generator (the one whose Activate button
// stands nearest the panel's light lever, so both peers find the same one). Src-local, beside grid_drill.cpp.

#pragma once

#include "coop/net/protocol.h"
#include "ue_wrap/core/types.h"  // FVector

#include <string>

namespace coop::dev::grid_drill {

constexpr int kLightBit = 4;  // the panel's light lever

float Dist(const ue_wrap::FVector& a, const ue_wrap::FVector& b);

// Say the grid at `step` on a "[GRID-DRILL]" line, and FAIL when it does not hold. False on a FAIL.
bool Say(const char* role, const char* step);

// The client's last line: the grid holds and this copy reads as the host last said, or FAIL.
void SayDone();

// The drill's generator, found once a session; `broken` its state. False while unread.
void* DrillGen();
bool DrillGenBroken(bool& broken);
void ForgetDrillGen();

}  // namespace coop::dev::grid_drill
