// coop/dev/subject_drill.h -- [dev] a remote action never takes the local player as its subject (ini
// subject_drill=off|insert|own|carry|carryred / env VOTVCOOP_SUBJECT_DRILL; BOTH peers, a fresh host world). The desk's drive slot
// drops the local player's physics grab as it seats a drive, which is its own inserter's hand in single player.
//   insert -- HOST: before any client's world is ready, spawns two drives in front of its player, so a joiner meets
//             them; once a client's join is over and one of them moves under a client's hold, seats the other in
//             the desk's play slot. CLIENT: walks to the nearer drive, grabs it with the use key's chain, and holds
//             it; once the host's insert has reached this copy's play slot, its grab must still hold -- the verdict.
//   own    -- HOST: spawns one drive the same way. CLIENT: grabs it the same way and seats it in its own play slot,
//             as the port's overlap does; its grab must end there, as single player's does.
//   carry  -- on the saved world: the HOST empties its play slot once a client's world stands; the CLIENT carries
//             a drive to the play port, latches its own slot and holds the drive in its port, so the host's mirror
//             enters the host's port while it is still carried. The host judges that entry: refused, not inserted,
//             the slot empty. carryred: the control, the host's port left to take the mirror.
// "[subject_drill] ABANDONED" is the drill unable to do its part (a run's --dead-marker); the client's
// "[subject_drill] client DONE" line ends insert and own, the host's "[subject_drill] host DONE" ends carry.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::subject_drill {

// Game thread, once per pump tick; a single enum read when off.
void Tick(coop::net::Session* session);

// The drives and the legs belong to one world.
void OnDisconnect();

}  // namespace coop::dev::subject_drill
