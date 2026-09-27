// coop/dev/keypad_drill.h -- the keypad lane's drill: a client's input reaches the host, the host
// judges it, and the client's copy lands on the host's verdict. CLIENT -- once its join is over,
// walks with the director to the keypad its navmesh reaches first among the named ones that gate a
// door and have a code of digits, and runs five legs there, each started in one tick and ended by its
// own copy's state: PRESS the door while unlocked; ACCEPT the code on the numpad; CANCEL two numpad
// digits with "-"; DENY a wrong code on the keys, pressed through the click that needs the light's
// power; TAIL, two digits more held through the deny's 0.2 s tail. Straight after each start its own
// copy reads as before. HOST -- logs every change of every keypad that gates a door. LATE: before the
// joiner is ready the host negates the verdict of a keypad whose code the keys cannot type, which only
// the snapshot carries. keypad_drill_blackout: a blackout before the walk and the power restored before
// the typing, or then the client's light power written off (stale, the RED). Both census the keypads,
// door beside keypad. A leg not landed in 10 s fails. Lines are tagged [KEYPAD-DRILL]; run on both
// peers (keypad_drill=1); the client's DONE ends it.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::keypad_drill {

bool IsEnabled();

// Game thread, once per pump tick; a single bool read when off. A walk thread of its own, then one
// keypad read a tick on the client; the host reads the keypads that gate a door.
void Tick(coop::net::Session* session);

// The keypad and the legs belong to one world and one session.
void OnDisconnect();

}  // namespace coop::dev::keypad_drill
