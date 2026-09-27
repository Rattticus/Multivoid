// coop/dev/director/standpoints.h -- where a drill's walker stands to act on something no route ends at, as a
// desk's button: the points of a ring about it that a NavMesh route reaches. Dev only (the drills' walks); game thread.

#pragma once

#include "ue_wrap/core/types.h"

#include <vector>

namespace coop::director {

// The `count` points of a ring of `ringCm` about `about`, at the height `player` stands at, whose NavMesh route from
// `player` ends within `reachCm` of them, flat, shortest route first: the director's own test for a reachable pile
// (PickReachablePile). Empty when `player` does not read or no route reaches one.
std::vector<ue_wrap::FVector> ReachableStandpoints(void* player, const ue_wrap::FVector& about, float ringCm, int count,
                                                   float reachCm);

}  // namespace coop::director
