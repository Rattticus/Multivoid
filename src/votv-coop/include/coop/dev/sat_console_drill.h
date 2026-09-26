// coop/dev/sat_console_drill.h -- [dev] the SAT console's shared commands, typed on a client, run on the host.
//   HOST   -- once a client is in: lowers the first dish whose server works to 0.4242, a value no dish rests
//             at, the fixture the calibration lane carries to the client; counts where calibratteDish runs.
//             Its DONE line says it ran the client's lines on that client's terminal, one gift box more.
//   CLIENT -- in its world, its join over: points its terminal at the fixture dish, known by its value, as a
//             panel's init does, and types through enterCommand, where both of the terminal's inputs go:
//             "sd.cal", waiting for the host's echo then "Completed" and the dish at full precision here;
//             "sauce.get", one gift box more here; "sv.hash", the host's answer. calibratteDish running
//             here is the RED.
// sat_console_drill=rejoin: the host zeroes five dishes and the client in slot 1 types "sd.calall"; its first
// life leaves once the run is busy and printing ("[SAT-DRILL] client: leaving mid-command", the
// --rejoin-marker), its second must be told "The terminal is busy" and get the run's lines; run it with
// --observer, since the last client's leave ends the session and every terminal. "[SAT-DRILL] FAIL" is the
// lane failing a step (--fail-marker), "[SAT-DRILL] ABANDONED" the drill unable to do its part
// (--dead-marker). Run with sat_console_drill=run or rejoin.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::sat_console_drill {

// Game thread, once per pump tick; two short string compares when off.
void Tick(coop::net::Session* session);

// The per-session half: a rejoin's peers run their legs again.
void OnDisconnect();

}  // namespace coop::dev::sat_console_drill
