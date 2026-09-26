// coop/dev/desk_verb_drill_internal.h -- what the desk-verb drill's two files share: its legs, its census of the
// entries into the save family's functions, what a peer's desk holds, and how the host sets a leg up
// (coop/dev/desk_verb_drill_desk.cpp). Dev only; game thread.

#pragma once

#include "ue_wrap/desk/signal_dynamic.h"

#include <cstdint>
#include <string>
#include <vector>

namespace coop::dev::desk_verb_drill::detail {

// The drill's caught signals sit at coordinates no sky signal reaches, and a saved row keeps its signal's x and y
// (formDownload's initDownloadSignal), so the band names the drill's rows. Each leg arms its own x.
inline constexpr float kBandX = 913000.f;
inline constexpr float kBandY = 917000.f;
inline constexpr float kBandWidth = 16.f;

// The legs: SAVE, DELETE on a second caught signal, the deck's send on the saved row, the deck's drive button both
// ways (the row exported onto an empty drive, then imported back), the refiner's upload of a drive's row.
enum Leg : int { kSave = 0, kDelete, kSend, kExport, kImport, kUpload, kLegs };
inline constexpr const wchar_t* kLegButton[kLegs] = {L"button_downl_saveSig1", L"button_downl_delSig",
                                                     L"button_play_saveSig",   L"button_play_left",
                                                     L"button_play_left",      L"button_comp_upload"};
inline constexpr const char* kLegName[kLegs] = {"SAVE", "DELETE", "send", "export", "import", "upload"};

// The functions the census counts.
enum Fn : int { kSaveSignal, kGetSigObj, kSetSignalID, kAddGloss, kDeleteActive, kLaptopAdd, kDeleteSignal, kCompUpload,
                kFns };

// ---- the census: entries into the family's functions, the game's own apart --------------------------------------

struct Census {
    uint32_t game[kFns] = {};   // called from a Blueprint frame
    uint32_t other[kFns] = {};  // through ProcessEvent: our code, a lane's replay
    uint32_t ran[kFns] = {};    // bodies that ran: a refused call enters and does not run
};
const Census& CensusNow();
const wchar_t* CensusNotLive();  // every census watch live: null; else the first function that is not
Census Since(const Census& before);
std::string CensusLine(const Census& d);

// ---- what this peer's desk holds ---------------------------------------------------------------------------------

std::string PowerLine();  // the desk's four units' power: DELETE reads active_download, the drive button active_play
float CaughtX();          // the caught signal's x, or -1 with none caught
float Needle();
std::vector<int32_t> BandRows();
uint64_t BandHash();      // the band row's identity, 0 with none
uint64_t LaptopNewest();  // the laptop's newest row's identity, 0 with none: what a send just appended
void* SlotDrive(int role);
uint64_t DriveRow(void* drive);  // a drive's row identity, 0 for no drive or an empty one
uint64_t CompRow();              // the refiner's, the same way
void* Button(int leg);
bool CapOpened(void* desk);      // the desk's own cap over DELETE is open

// Whether this machine's profile glossary (gamemode.save_main.signalsGlossary, lib.cpp:3472-3477) holds `name`:
// 1, 0, or -1 when it does not read.
int GlossaryHas(const std::wstring& name);

// A saved row as the drill reports it: its identity, its photo's bytes and digest (the identity leaves the photo
// out), its template key (the SAVE's gloss).
struct RowSeen {
    uint64_t hash = 0;
    size_t photo = 0;
    uint64_t photoDigest = 0;
    std::wstring signal;
};
bool ReadRowSeen(int32_t index, RowSeen& out);

// ---- the host's fixtures -----------------------------------------------------------------------------------------

bool FindSource();                   // the sky signal the caught signals are formed from, kept; false with none
const std::wstring& SourceObject();  // its object
int PurgeBand();                     // the rows an earlier run left in the band go, by the game's own delete
bool WriteCaught(int leg);           // a caught signal at the leg's x
bool FormDownload();                 // its download, formed whole
bool Rendered();                     // the download's render exists: SAVE's photo is that render
bool Detect();                       // the needle at 100%
int ArmDrive(int role, bool withRow);  // 1 once the leg's drive sits in the slot, 0 while working, -1 failed
void ResetFixtures();

}  // namespace coop::dev::desk_verb_drill::detail
