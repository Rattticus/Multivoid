// coop/world/power_decay.h -- the generators' wear dice: generatorFuckuper_C's 30 s timer_transformers tick, which
// only the host rolls. A client in its announced world refuses its own tick at the body, so a generator wears only
// on the host, whose rows run each step on every client (coop/world/power_grid). Principle-7 gameplay layer.

#pragma once

namespace coop::net { class Session; }

namespace coop::power_decay {

// Register the tick's watch. Idempotent. Session install.
void Install(coop::net::Session* session);

// Say once whether the watch is live. Game thread, per frame.
void Tick();

// Say the session's counts and start them again. Session end.
void OnDisconnect();

}  // namespace coop::power_decay
