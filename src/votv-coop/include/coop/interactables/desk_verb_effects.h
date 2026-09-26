// coop/interactables/desk_verb_effects.h -- what a desk press the host runs for a client makes for that client
// alone: the glossary entry its SAVE adds, and the sounds its branches play.
//
// The SAVE branch's lib_C::addGloss(name, 0, desk) writes getMainSave(), the save_main of the machine that runs
// it (lib.cpp:3472-3477, :4143), so run on the host for a client's press the entry would land in the host's
// profile. The branches' PlaySound2D clicks are 2D, heard at one level anywhere (analogDScreenTest.cpp:2611,
// :2696), so the host would hear a client's press and the client nothing, its own press refused. While a
// Replay is open the host refuses both, addGloss at the script-body gate and PlaySound2D at a pre hook armed
// only inside it (ue_wrap/core/ufunction_hook), and sends each to the presser, whose machine makes the call for
// itself. The scope is the replay's own rather than the gate's IsBodyActive(actionOptionIndex): the host's own
// press runs the same body and keeps its gloss and its clicks, the choice drive_sync makes for its replayed
// insert (drive_sync.cpp:108-132). Game thread.

#pragma once

#include <cstdint>

namespace coop::net {
class Session;
struct DeskVerbPayload;
}  // namespace coop::net

namespace coop::desk_verb_effects {

// Watch addGloss and install the PlaySound2D pre hook disarmed. Idempotent; retried until both settle. Game
// thread.
void Install(coop::net::Session* session);

// HOST: both seams stand, so a client's press can run here without its gloss or sounds landing on the host.
// Refused names a seam this process will never have. Game thread.
bool Ready();
bool Refused();

// HOST: while one lives, the press running is `slot`'s, press number `seq`: its gloss and its 2D sounds go
// there. Restores the scope it found, so a nested one cannot close an outer. Game thread.
class Replay {
  public:
    Replay(uint8_t slot, uint32_t seq);
    ~Replay();
    Replay(const Replay&) = delete;
    Replay& operator=(const Replay&) = delete;

  private:
    uint8_t  outerSlot_;
    uint32_t outerSeq_;
    bool     outerOpen_;
};

// CLIENT: the host's gloss or sound for this machine's press (DeskVerb ops 2 and 3): made here, as the press
// would have made it. Game thread.
void OnEffect(const coop::net::DeskVerbPayload& p);

// Session end: the counts are said and the scope is closed.
void OnDisconnect();

// This session's counts, for a drill: the glosses and 2D sounds this host refused inside a replay and sent to
// their presser, and the ones this presser made from the host's word. Game thread.
struct Counts {
    uint64_t glossesSent = 0, soundsSent = 0, glossesMade = 0, soundsMade = 0, lost = 0;
};
Counts CountsNow();

}  // namespace coop::desk_verb_effects
