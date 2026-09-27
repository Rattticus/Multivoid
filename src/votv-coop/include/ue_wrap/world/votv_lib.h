// ue_wrap/world/votv_lib.h -- thunks into VOTV's shared BP function library (lib_C).
//
// Engine-wrapper layer (principle 7): resolves the lib_C CDO + UFunctions once
// and exposes typed calls. No coop/network state.

#pragma once

#include <cstdint>
#include <string>

namespace ue_wrap::votv_lib {

// Dispatch lib_C::step(Character, ...) -- THE shared footstep verb every VOTV
// walker funnels into (the local player's tick accumulator at mainPlayer uber
// @70973 and kerfurOmega.step both call it). It sphere-traces to the ground,
// picks the surface-material step cue (foot_def/grass/metal/snow/water/...),
// SpawnSoundAttached's it with att_default + conc_footsteps, and fires the
// steppedOn world reactions -- fully spatialized, possession-agnostic, nothing
// for us to re-implement. Silent on its own when airborne (no ground hit).
// Args mirror the local player's call (Z_offset=0, pitch 1.0, speedVolume=400)
// but for two. `volume` is the caller's: the BP multiplies it onto its internal
// clamp(MaxWalkSpeed/speedVolume, 0.5, 2.0) loudness, and the puppet stride
// emitter passes its own tuning. `callActor` is the character, where the local
// player's call passes null; lib_C tests it for int_objects and calls its
// `stepped`, which mainPlayer_C implements as an empty event. Returns false
// until lib_C resolves. Game thread (ProcessEvent).
bool CharacterStep(void* character, float volume);

// Dispatch lib_C::addGloss(name, level, worldContext): the signal glossary of the machine's own profile gains
// `name` at `level`, or raises it (lib.cpp:4143-4175, its save_main through getMainSave). False until lib_C
// resolves, or when the name does not convert. Game thread (ProcessEvent).
bool AddGloss(const std::wstring& name, int32_t level, void* worldContext);

// Dispatch lib_C::isBuoyant(null, worldContext): whether this machine's game lets its player cheat, the check the
// cheat menu opens on and each of its commands runs (mainPlayer.cpp:2457-2460, ui_cheatMenu.cpp:732-735). It is the
// gamemode's hasWeapon, set in Sandbox (mainGamemode.cpp:2879-2883), or, with isFlying, a developer key file matching
// a hashed secret (lib.cpp:1652-1686). False when it does not dispatch. Game thread (ProcessEvent).
bool CheatsAllowed(void* worldContext, bool& allowed);

}  // namespace ue_wrap::votv_lib
