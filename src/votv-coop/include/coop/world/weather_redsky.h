// coop/world/weather_redsky.h -- the red sky, owned by the host at its verb.
//
// The red sky is the gamemode's toggle (ue_wrap/world/red_sky): the day cycle's noon calls spawnRedSky on every
// peer, ending a live red sky and, on a 1% roll, starting one; the cheat menu calls it too. The noon's call runs
// inside the day cycle's graph, where no ProcessEvent hook sees it; the script gate sees it on every route. The HOST
// sends its red sky as each toggle leaves it, both edges, as MTA's server sends a weather change at the call that
// makes it (Server/mods/deathmatch/logic/CStaticFunctionDefinitions.cpp, SetWeather), and a joiner is seeded at its
// world-ready (coop/world/weather_sync), as MTA's join packet carries the weather (packets/CMapInfoPacket.cpp). A
// CLIENT refuses its own toggle at the gate, both halves -- its noon would otherwise end the host's red sky it
// shows -- and applies the host's through the same verb, which its gate lets run.

#pragma once

namespace coop::net { class Session; struct RedSkyPayload; }

namespace coop::weather_redsky {

// Cache the session and, once, watch the gamemode's spawnRedSky at the script gate. Called from
// weather_sync::Install on every re-entry.
void Install(coop::net::Session* session);

// Whether this world shows a red sky: the seed a joiner's world-ready gets (principle 8). Game thread.
bool LocalRedSkyActive();

// The host's red sky on this client: the gamemode's own toggle, when this copy's sky differs. Game thread.
void Apply(const coop::net::RedSkyPayload& payload);

// The session ended: its counts are said and cleared.
void OnDisconnect();

}  // namespace coop::weather_redsky
