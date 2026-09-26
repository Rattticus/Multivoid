// coop/interactables/meadow_db_writers.h -- the meadow database's writers at the script-body gate. From the
// bytecode of every asset that names saveSlot.savedSignals_0: ui_laptop_C's addSignal (an Add), removeSignal
// (a Remove) and sortSignal (a move, a Remove then an Insert), and the rename window, whose ubergraph writes
// a row's name in place when its button is clicked. saveSlot_C::reset_days clears the store too, but only on
// a save the reset menu loads from disk, never on the live one. Each is watched on its own class; the lane
// (meadow_db_sync) supplies what the entry and the exit do. Game thread.

#pragma once

#include "ue_wrap/core/script_gate.h"

namespace coop::meadow_db_writers {

// Register the watches, once a process: the entry and the exit the lane runs around each writer's body.
// False when one was refused (said); asked again at the next session's install.
bool Watch(ue_wrap::script_gate::PreFn pre, ue_wrap::script_gate::PostFn post);

// Asked until true: whether the gate has settled every name watch. A watch refused at registration or dead
// in a full table never goes live, and asking after that would walk the gate's table every tick; a writer
// still waiting for its name is not dead yet. At true each unwatched writer has been named once.
bool Settle();

}  // namespace coop::meadow_db_writers
