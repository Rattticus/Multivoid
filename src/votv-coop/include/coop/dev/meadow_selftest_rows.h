// coop/dev/meadow_selftest_rows.h -- [dev] the meadow selftest's rows (coop/dev/meadow_selftest), each built
// alike on both peers so its content hash is the same on each, and the database read as those hashes.
// Game thread.

#pragma once

#include "ue_wrap/desk/signal_dynamic.h"

#include <cstdint>
#include <vector>

namespace coop::dev::meadow_selftest_rows {

// The rename keeps a row's id and changes its name, so A and A2 are one row of the database under two
// contents. X and Y are the race's: X the client's, held, Y the host's. X2 is the client's row added and
// removed while held, whose two lines must reach the host in their order. W is the client's cue while its
// database reads as away, Z the host's row that must wait for it there.
enum RowId : int { kA, kB, kA2, kC, kX, kY, kX2, kW, kZ, kRowCount };
inline constexpr RowId kAllRows[] = {kA, kB, kA2, kC, kX, kY, kX2, kW, kZ};

const wchar_t* Name(RowId r);
ue_wrap::signal_dynamic::Row MakeRow(RowId r);
uint64_t HashOf(RowId r);

// The database as its rows' content hashes, in its order. False while it cannot be read.
bool ReadSequence(std::vector<uint64_t>& out);
int32_t IndexOf(const std::vector<uint64_t>& seq, uint64_t h);
int32_t IndexOf(RowId r);

// The lane's own digest: the rows' hashes summed, whatever their order.
uint64_t Digest(const std::vector<uint64_t>& seq);

// Takes a drill row out if it is there; true once it is not. TakeOutAll: whether every drill row is out,
// taking out the ones still there.
bool TakeOut(RowId r);
bool TakeOutAll();

}  // namespace coop::dev::meadow_selftest_rows
