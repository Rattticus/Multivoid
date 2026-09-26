// coop/dev/director/aim_fan.cpp -- see coop/dev/director/aim_fan.h.

#include "coop/dev/director/aim_fan.h"

#include "coop/dev/director/director.h"  // LookAt

#include "ue_wrap/engine/engine_pawn.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace coop::director {
namespace {

namespace E = ue_wrap::engine;

constexpr int   kTicksPerPose = 6;  // the trace runs on the player's own tick: hold each pose
constexpr int   kFanHalf      = 5;  // an 11 x 11 fan of 3 degrees round the point
constexpr float kFanStepDeg   = 3.f;

const std::vector<std::pair<int, int>>& Fan() {
    static const std::vector<std::pair<int, int>> fan = [] {
        std::vector<std::pair<int, int>> v;
        for (int p = -kFanHalf; p <= kFanHalf; ++p)
            for (int y = -kFanHalf; y <= kFanHalf; ++y) v.emplace_back(p, y);
        std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
            return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second;
        });
        return v;
    }();
    return fan;
}

}  // namespace

AimFan::State AimFan::Tick(void* player, const ue_wrap::FVector& at, bool aimed) {
    if (aimed) return State::Aimed;
    if (!player) return State::Failed;
    if (ticks_++ % kTicksPerPose != 0) return State::Working;
    const auto& fan = Fan();
    if (poses_ >= static_cast<int>(fan.size())) return State::Failed;
    ue_wrap::FRotator r = LookAt(E::GetCameraLocation(), at);
    r.Pitch += kFanStepDeg * static_cast<float>(fan[poses_].first);
    r.Yaw   += kFanStepDeg * static_cast<float>(fan[poses_].second);
    E::SetControlRotation(E::GetController(player), r);
    ++poses_;
    return State::Working;
}

}  // namespace coop::director
