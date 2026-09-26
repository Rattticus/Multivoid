// coop/dev/server_upgrade_drill.h -- [dev] a server box's physical upgrades across both peers.
//   CLIENT -- once its world is ready and its join over: walks (the director) to the nearest server box that
//             can take an upgrade, spawns an upgrade and takes it into its hand with the game's own pickup,
//             and runs the box's install with it, as the player's use does; once a canonical from the host
//             shows the box one higher, it aims until the box's own look reads the player on its take-out
//             and runs the take-out (action 4), as the E press does, and waits for the canonical to show
//             the box back where it began. Its DONE line says the host's canonical followed both.
//   HOST   -- its DONE line says it applied the client's install and take-out.
// With server_upgrade_drill=refuse the host refuses both ops, as if each had lost its race: the client picks a
// box that holds one or two, sees the canonical put the box back after each op, a refund upgrade come after its
// install and the take-out's upgrade leave its hand. "[SRV-UPG-DRILL] FAIL" is the lane failing a step
// (--fail-marker), "[SRV-UPG-DRILL] ABANDONED" the drill unable to do its part (--dead-marker). Run with
// server_upgrade_drill=apply or refuse and --posed, the rig client at the base, where the server room is.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::server_upgrade_drill {

// Game thread, once per pump tick; a single bool read when off.
void Tick(coop::net::Session* session);

// The per-session half: a rejoin's peers run their legs again.
void OnDisconnect();

}  // namespace coop::dev::server_upgrade_drill
