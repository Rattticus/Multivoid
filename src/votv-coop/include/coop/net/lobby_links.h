// coop/net/lobby_links.h -- how a lobby's players reach its host, as the host measures them.
//
// The host counts its connected players by the link kind it measures on each (coop/net/link_kind.h),
// itself not counted, and sends the counts with every heartbeat; the master keeps them on the lobby's
// record and serves them on its browser row beside one word for the row. The counts say how other players
// reach this host, not the path a new joiner will get: ICE decides that per pair.

#pragma once

namespace coop::net::lobby {

struct LobbyLinks {
    int relayed = 0;
    int direct = 0;
    int lan = 0;
};

}  // namespace coop::net::lobby
