// ue_wrap/world/red_sky.h -- the red sky: the gamemode's toggle (mainGamemode_C.spawnRedSky) and the event actor it
// makes (redSkyEvent_C). The toggle destroys a live event held in the gamemode's redSky, whose destroyed event runs
// set(false), and otherwise spawns one, whose begin-play runs set(true); set records its argument in isred and writes
// four colour curves reached through the day cycle, its ambient curve and its sky sphere's bottom, clouds and top: the
// red set on true, the summer set on false. The day cycle's noon (outside the Ambient game mode) calls the toggle
// whenever a red sky lives, and on a 1% roll when none does; the cheat menu calls it too. Engine access only. Game
// thread.
#pragma once

namespace ue_wrap::red_sky {

// Whether `gamemode`'s red sky shows: its redSky holds a live event whose isred is set. A null `gamemode` reads
// the running world's. False when there is no gamemode, or a live event's isred did not resolve.
bool Read(void* gamemode, bool& red);

// Whether `gamemode`'s redSky holds a live event, the test the toggle itself makes. A null `gamemode` reads the
// running world's. False when there is no gamemode.
bool ReadLive(void* gamemode, bool& live);

// The running world's gamemode's own spawnRedSky(), the function the day cycle's noon calls, reached here through
// ProcessEvent rather than the noon's Blueprint route. False when there is no gamemode or the verb did not resolve;
// true when it was called, whatever its body then did (a refusal included).
bool CallSpawn();

// An event actor's isred, as its last set left it. False when `event` is null or the field did not resolve.
bool ReadIsRed(void* event, bool& red);

}  // namespace ue_wrap::red_sky
