// coop/dev/calib_drill.h -- [dev] a dish's precision is the host's, and a client player's own verbs reach the host.
//   HOST   -- holds dish 0 at 0.3131, dish 1 at 0.5757 and dish 2 at 1.4242, values no dish rests at, and fails if
//             dish 0 ever takes the client's deviation. run: once a client is in its world, so the values cross in
//             the host's batch. join: hosting, before any client connects, so they cross with the join itself (its
//             save and its seed). Its DONE line, the run's end, says it performed both of the client's verbs: dish 1
//             reads 0 and dish 2 reads 2.5 on its copy.
//   CLIENT -- in its world, its join over, and the three values in. The poll leg: 0.8686 written into its copy of
//             dish 0, the stand-in for the writers no seam can refuse, must be put back by the lane within 3 s. The
//             reader leg: the same deviation again, then mainGamemode.setPrec at once, whose average must be the
//             host's. The uncalibrator leg: its player's hit aimed at dish 1 and an uncalibrator's use run, as the
//             LMB does. The tool leg: a toolgun's calibration tool set to 2.5, a value the old wire clamped away, run
//             on dish 2 as its RMB does. Then 3 s in which all three hold.
// "[CALIB-DRILL] FAIL" is the lane failing a step (--fail-marker), "[CALIB-DRILL] ABANDONED" the drill unable to
// do its part (--dead-marker). Run with calib_drill=run or calib_drill=join, --done-marker "[CALIB-DRILL] host DONE".

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::calib_drill {

// Game thread, once per pump tick; two short string compares when off.
void Tick(coop::net::Session* session);

// The per-session half: a rejoin's peers run their legs again.
void OnDisconnect();

}  // namespace coop::dev::calib_drill
