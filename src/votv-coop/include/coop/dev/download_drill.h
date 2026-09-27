// coop/dev/download_drill.h -- [dev] the download machine's arm and reset are the host's, run once on each client by the
// game's own verbs with the host's values.
//   HOST   -- run: once a client is in its world, five cycles, each started on its own machine's state: a caught signal
//             written (a sky signal's object) and relayed, the desk's own arm (checkFordDishes, so formDownload), then
//             mainGamemode.deleteActiveSignal. join (with --rejoin): an arm while the client's first life is in, so
//             the rejoiner's world is taken armed; then, in its window, a reset, a second caught signal and its arm,
//             which reach it only as its connect rows, in the game's order.
//   CLIENT -- run: five arms and five resets replayed with the host's values (the lane's own check), one formDownload
//             body an arm and one reset line a reset (counted at the write), the caught signal cleared and the
//             download's frequency, quality and object type 0; then its own formDownload and deleteActiveSignal, both
//             refused. join: its first life says "[DOWNLOAD-DRILL] client LIFE 1 DONE" once the first arm replayed
//             (--rejoin-marker); its second ends armed on the second signal, reset and re-armed by its connect rows.
// "[DOWNLOAD-DRILL] FAIL" is the lane failing a step (--fail-marker), "[DOWNLOAD-DRILL] ABANDONED" the drill unable to
// do its part (--dead-marker). Run with download_drill=run|join and --done-marker "[DOWNLOAD-DRILL] client DONE".

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::download_drill {

// Game thread, once per pump tick; two short string compares when off.
void Tick(coop::net::Session* session);

// The per-session half: a rejoin's peers run their legs again.
void OnDisconnect();

}  // namespace coop::dev::download_drill
