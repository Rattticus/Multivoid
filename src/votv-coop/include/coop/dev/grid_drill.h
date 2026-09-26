// coop/dev/grid_drill.h -- [dev] the power grid across both peers: the panel's presses and the generators' rows.
//   run  -- the client walks to the base panel by the director and presses its light lever as the E dispatch
//           does, each press waiting for the host's answer to leave the lever flipped; the host breaks the drill's
//           generator (the one nearest the panel) after the first. After the second the client repairs it at its
//           Activate button from where it stands, the puzzle solved as the drill's shortcut: within the button's
//           reach the host must take it, beyond it (the generators stand 470-620 m out) roll it back; after a third,
//           the host repairs it at its own button, which the client must run with its turn-on.
//   join -- the host breaks the drill's generator as it hosts; the joiner must find the blackout at its world-ready.
//   lockout, lockjoin -- the host runs the desk virus's lockout once the client's world is ready, or as it joins:
//           the client's panel locks with the canonical and unlocks 60 s later, its servers on as calc says.
//   red  -- a client applies the host's canonical and rows raw, as the old mirror did: the negative control.
// Each peer says its grid at every step on a "[GRID-DRILL]" line and FAILs when its unit flags lag its breakers;
// the client's last check reads this copy against the host's last canonical and rows. "[GRID-DRILL] ABANDONED" is
// the drill unable to do its part (--dead-marker); the client's DONE line ends the run.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::grid_drill {

// Game thread, once per pump tick; a latched read when off.
void Tick(coop::net::Session* session);

// The per-session half: a rejoin's peers run their legs again.
void OnDisconnect();

}  // namespace coop::dev::grid_drill
