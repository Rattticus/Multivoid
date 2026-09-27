// coop/dev/desk_verb_drill.h -- drill: a client presses the main desk's save family through its own input
// (ini desk_verb_drill=run|join / env VOTVCOOP_DESK_VERB_DRILL; BOTH peers).
//
// run: the host arms a caught signal formed from a sky signal's object (the signal data, the download formed from
// it once the catch is on the wire, the detection needle once its render exists), and the client, walked to the
// desk by the director, aims its own trace at SAVE and presses it (useSelectedAction), lifting a cap in the way as
// a player does. Then DELETE on a second one, the deck's send on the saved row, the deck's drive button twice (the
// row exported onto a drive the host put in, then imported back) and the refiner's upload of a drive holding it;
// then the refiner's start, its stop, a start the host completes, and one at level 2 whose completion is the last
// level's. Both peers count the entries into the functions those branches call, the game's own (a Blueprint frame
// made the call) apart from ours and the lanes'. Per leg the host says it entered them, the client that it entered
// none, that its refiner never latched, and that SAVE's and each completion's gloss and the last level's processed
// point reached its profile. join: the host decodes on its refiner and saves a row as the client joins; the client
// finds the row with its photo, and its restore of the decode refused, mirroring the host's. Lines tagged
// [DESK-VERB-DRILL]; 'client DONE PASS|FAIL' ends it; ABANDONED is a step that could not run.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::desk_verb_drill {

// Advance this peer's legs. Every pump tick in a world; a latched read when off. Game thread.
void Tick(coop::net::Session* session);

// join, CLIENT: the census armed ahead of the world load, which runs the restore it counts. Every pump tick of a
// session, world or not; a latched read once live or when off. Game thread.
void TickLoad(coop::net::Session* session);

// The session ended: the legs start over.
void OnDisconnect();

}  // namespace coop::dev::desk_verb_drill
