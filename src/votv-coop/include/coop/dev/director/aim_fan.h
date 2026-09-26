// coop/dev/director/aim_fan.h -- the camera turned through a fan round a point, nearest pose first, each pose held a
// few ticks for the player's own interaction trace, until the caller's reading of that trace says it took what the
// drill wants. A drill ticks it from its step. DEV-ONLY, like the rest of the director; game thread.

#pragma once

#include "ue_wrap/core/types.h"

namespace coop::director {

class AimFan {
public:
    enum class State { Working, Aimed, Failed };
    // `aimed`: the caller's reading of this tick, true once the trace is where it wants it. Failed once every pose
    // of the fan was held without it.
    State Tick(void* player, const ue_wrap::FVector& at, bool aimed);
    int Poses() const { return poses_; }

private:
    int ticks_ = 0;
    int poses_ = 0;
};

}  // namespace coop::director
