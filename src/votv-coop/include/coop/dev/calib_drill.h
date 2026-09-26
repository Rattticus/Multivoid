// coop/dev/calib_drill.h -- [dev] a dish's precision is the host's, and a client player's own verbs reach the host.
//   HOST   -- holds dishes 0 to 3 at 0.3131, 0.5757, 1.4242 and 0.6262, values no dish rests at, and fails if dish 0
//             ever takes the client's deviation. run: once a client is in its world (the values cross in its batch);
//             join: before any client connects (they cross with the join). Its DONE line says it performed the
//             client's two verbs (dish 1 reads 0, dish 2 reads 2.5) and refused the third, whose dish keeps its value.
//   CLIENT -- in its world, its join over, and the four values in. The poll leg: 0.8686 written into its copy of
//             dish 0, the stand-in for the writers no seam can refuse, must be put back by the lane within 3 s. The
//             reader leg: the same deviation again, then mainGamemode.setPrec at once, whose average must be the
//             host's. The uncalibrator leg: its copy of dish 1 zeroed, a deviation the verb's entry must put back,
//             then its player's hit aimed at dish 1 and an uncalibrator's use run, as the LMB does. The tool leg: a
//             toolgun's calibration tool set to 2.5, a value the old wire clamped away, run on dish 2 as its RMB
//             does. The refusal leg: the tool set to NaN on dish 3, which the host refuses and whose answer alone can
//             put the copy back. Then 3 s in which all four hold; its DONE line, the run's last, ends it.
// "[CALIB-DRILL] FAIL" is the lane failing a step (--fail-marker), "[CALIB-DRILL] ABANDONED" the drill unable to do
// its part (--dead-marker). Run with calib_drill=run|join and --done-marker "[CALIB-DRILL] client DONE".

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::calib_drill {

// Game thread, once per pump tick; two short string compares when off.
void Tick(coop::net::Session* session);

// The per-session half: a rejoin's peers run their legs again.
void OnDisconnect();

}  // namespace coop::dev::calib_drill
