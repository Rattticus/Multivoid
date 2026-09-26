// ui/link_format.h -- the ONE renderer for a player's connection facts.
//
// Three surfaces show "how is this player connected to the session": the tilde scoreboard,
// the host's admin panel, and the world nameplate. Each one used to hand-copy the same
// `>0 / ==0 / else` cascade, so the vocabulary could drift a panel at a time -- and it had,
// silently. One fact, one renderer.
//
// TWO DISTINCT TOKENS, deliberately:
//   "n/a"  -- NOT APPLICABLE. The host has no network link to the session; they
//             ARE it. There is no number to report and never will be.
//   "--"   -- NOT KNOWN YET. A sample has not landed (a row younger than the
//             host's ~1 Hz RTT sampler, or a connection that just dropped).
// One glyph for both would say "empty" where the truth is "there is nothing to measure",
// which is how "VIA HOST" read as information. ASCII on purpose: our fonts fall back to '?'
// on missing glyphs, so an em dash is not safe here.

#pragma once

#include "coop/net/link_kind.h"

#include <string>

namespace ui::link_format {

// Short column label for a transport. Never returns nullptr or an empty string:
// a blank cell is the "it's empty" complaint this lane exists to remove.
const char* LinkLabel(coop::net::LinkKind kind);

// Render `pingMs` into `out` ("42ms" / "<1ms" / "--" / "n/a"). `kind` decides
// the not-applicable case, NOT a magic ping value -- the semantics belong to the
// link, and a sentinel ping would have to be a number the renderer then has to
// un-mean. (A published 0 was rejected for exactly this: the cascade turns 0
// into "<1ms", a measured-looking latency nobody measured.)
void FormatPing(int pingMs, coop::net::LinkKind kind, char* out, int outLen);

// A lobby's word for how its players reach its host, as the master serves it on a browser row ("relay",
// "direct", "lan", or empty), in the label both browsers show: "Relay", "Direct", "LAN", or "--" when the
// host reports no one.
const char* LobbyLinkLabel(const std::string& word);

}  // namespace ui::link_format
