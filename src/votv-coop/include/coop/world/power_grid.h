// coop/world/power_grid.h -- the base's power grid, whose simulation runs on the host. The generators wear on
// the dice of generatorFuckuper's 30 s decay tick: the host rolls them, and a client in its announced world
// refuses its own tick at the body, so a generator breaks only where the host's dice broke it. Every peer still
// arms the timers and runs the 5 s sendElec tick, which only draws the pole arcs of a broken generator; the
// coordinate towers' timer belongs to the towers.

#pragma once

namespace coop::net { class Session; }

namespace coop::power_grid {

// Register the decay tick's gate. Idempotent. Session install.
void Install(coop::net::Session* session);

// Settle the gate's registration and say once whether it is live. Game thread, per frame.
void Tick();

// Say the session's refusals and runs, and start the counts again. Session end.
void OnDisconnect();

}  // namespace coop::power_grid
