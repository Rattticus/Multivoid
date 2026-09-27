// harness/join_leave.h -- a client join that starts while a gameplay world stands leaves that world first.
//
// The menu join assumes the menu: it connects there, downloads the host's save and loads it. Started inside a
// world, its world-ready is announced in that world at the connect, and its load of the host's slot answers
// "already in gameplay" without opening it, so the player plays the session in a world of their own. The leave
// travels to the menu through the game mode's own `transition`, the verb the game's quit to menu runs, and hands
// the join back once the menu stands.
//
// A phase of the play loop, not a wait inside it: nothing of the session runs until the join starts, the loop
// keeps its idle tick and its cancel drain, and a join cancelled meanwhile is dropped here. TimelineThread.

#pragma once

namespace coop::net { struct Config; }

namespace harness::join_leave {

// Takes the join's config when a gameplay world stands, and answers true: the join waits at the leave, which
// Step drives. False when no gameplay world stands, so the join starts at once; also false when the world reader
// is degraded and cannot tell, said once by name (a gate that acts on the world does not act on Unknown).
bool Begin(const coop::net::Config& cfg);

enum class Outcome {
    Idle,          // no leave under way
    Waiting,       // the travel is asked for, or under way
    AtMenu,        // the menu stands: the join's config is handed back
    Cancelled,     // the join ended meanwhile (a cancel, a failure, a shutdown) with its world still standing
    CancelledLeft, // the join ended meanwhile after its travel to the menu dispatched: the player ends at the menu
    Failed,        // the travel was refused or did not dispatch within 30 s, or the menu never stood after it
};

// One step, from each pass of the play loop. The travel is asked of the game thread every 250 ms until the game mode
// takes it: the game mode is missing from our object index for a few batches after its world appears. Then it waits
// for the menu, the bound counting only while the game thread runs tasks (a level load holds it). A world left by
// other means before the travel goes on as at the menu. On AtMenu `out` holds the join's config; every other outcome
// leaves it alone.
Outcome Step(coop::net::Config& out);

// Whether a leave is under way: the play loop's host drain waits for it, and its cancel drain names it.
bool Active();

}  // namespace harness::join_leave
