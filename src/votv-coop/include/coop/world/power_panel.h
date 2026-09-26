// coop/world/power_panel.h -- the base's power panel, one author: the host (PowerControlState). A press on any
// peer, a lever or the laptop's breaker page, runs locally as its prediction; a client's goes to the host as the
// breakers it flipped. The host takes presses in arrival order, a few a second, from a presser within reach of the
// lever or of the terminal the page was worked through. It runs a lever press as the game's own press by the
// presser's puppet and applies a page press's flip, whose wait the presser already served. It sends every peer the
// canonical breakers and `disabled` with the last press taken from each; a refused press goes to its author
// alone. A client puts its untaken presses on top of the canonical, runs the panel's apply only on a difference,
// and re-asserts the canonical after its own generator verbs rewrote the breakers. A client never blacks out or
// locks the panel on its own: the desk virus's lockout runs on the host, and a client runs its two halves (the
// servers off; then the servers, the calc breaker's, and the turn-on cue) as the canonical's edges arrive.
// The shape is MTA's element data, whose set runs on the client first and which the server relays to all but its
// source, answering a cancelled set with its own value to the source alone:
//   reference/mtasa-blue/Client/mods/deathmatch/logic/CStaticFunctionDefinitions.cpp:1045-1050
//   reference/mtasa-blue/Server/mods/deathmatch/logic/CGame.cpp:2767-2788
// A press is a flip taken in order, not a value, so the canonical reaches the presser too, with its last press.

#pragma once

#include <cstdint>

namespace coop::net { class Session; struct PowerPanelPayload; }

namespace coop::power_panel {

// Register the panel's watches. Idempotent. Session install.
void Install(coop::net::Session* session);

// Settle the watches' registration and say once whether they are live; take the presses that waited for the
// panel, their presser's body or their turn, apply a canonical that arrived before the panel resolved, and send a
// joiner the canonical its world-ready was owed. Game thread, per frame.
void Tick();

// PowerControlState from the wire (router: event_dispatch_state.cpp): a press on the host, the canonical on a
// client. Game thread.
void OnReliable(const coop::net::PowerPanelPayload& payload, uint8_t senderSlot);

// HOST: the canonical to a joiner at its world-ready, or as soon as the panel resolves. Game thread.
void QueueConnectBroadcastForSlot(int slot);

// HOST: a leaver's waiting presses go with it. Game thread.
void OnPeerLeft(uint8_t slot);

// CLIENT: put the panel back to the last canonical with my untaken presses on top, after a verb of this peer's
// own wrote its breakers (a generator's break blacks the panel out through solar()). Game thread.
void ReassertCanonical();

// Forget the session's presses, canonical and acknowledgements. Session end.
void OnDisconnect();

}  // namespace coop::power_panel
