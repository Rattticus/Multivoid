// coop/net/turn_credential.h -- a TURN relay credential as a master hands one out.
//
// The master mints a coturn REST credential for each peer, at the host's announce and at a join: a
// user naming its own expiry and the HMAC password over it, valid for the lifetime the master states
// beside them. The relay refuses a new allocation under an expired credential, so the lifetime is
// part of the credential: a relayed connection that has its allocation keeps it, and a connection
// whose ICE starts after the lapse gets no relay candidate from it.

#pragma once

#include <string>

namespace coop::net {

struct TurnCredential {
    std::string uri;   // "turn:host:port" (the first uri, its transport stripped) or ""
    std::string user;
    std::string pass;
    int ttlS = 0;      // the lifetime from the mint, as the master states it; 0 when it states none
};

}  // namespace coop::net
