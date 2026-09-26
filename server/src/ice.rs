//! What a lobby's host or joiner is handed to reach its peer: the signaling relay, a STUN server and a
//! TURN relay credential (coturn's REST scheme), and the credential's lifetime.

use crate::common::now_unix;
use crate::master_config::CFG;
use base64::engine::general_purpose::STANDARD as B64;
use base64::Engine;
use hmac::{Hmac, Mac};
use serde_json::{json, Value};
use sha1::Sha1;

type HmacSha1 = Hmac<Sha1>;

pub const TURN_TTL: u64 = 120;

/// The base64(HMAC-SHA1(secret, username)) coturn password. Split out so the
/// byte-exact spot is unit-testable against the Python reference independent of the
/// time-based expiry.
pub fn turn_password(turn_secret: &str, username: &str) -> String {
    let mut mac = HmacSha1::new_from_slice(turn_secret.as_bytes()).expect("HMAC accepts any key length");
    mac.update(username.as_bytes());
    B64.encode(mac.finalize().into_bytes())
}

/// A coturn REST time-limited credential (design 7), byte-for-byte identical to the
/// Python `turn_creds()`:
///   username = "<unixExpiry>:<label>"
///   password = base64( HMAC-SHA1( TURN_SECRET, username ) )
/// coturn validates it via `use-auth-secret` / `static-auth-secret=TURN_SECRET`.
/// **Byte-exact spot:** the HMAC digest, the base64 alphabet (STANDARD, with `=`
/// padding), and the `"exp:label"` username format must all match or coturn auth
/// fails. Returns `None` (→ omitted from the JSON, same as the Python empty dict)
/// when TURN is not configured.
pub fn turn_creds(turn_uri: &str, turn_secret: &str, label: &str, ttl: u64) -> Option<serde_json::Value> {
    if turn_uri.is_empty() || turn_secret.is_empty() {
        return None;
    }
    let exp = now_unix() + ttl;
    let username = format!("{exp}:{label}");
    let password = turn_password(turn_secret, &username);
    let uris = vec![
        format!("{turn_uri}?transport=udp"),
        format!("{turn_uri}?transport=tcp"),
    ];
    Some(serde_json::json!({
        "user": username,
        "pass": password,
        "ttl": ttl,
        "uris": uris,
    }))
}

/// The connectivity block every host/join response carries. `turn_label` is the
/// coarse per-client identity the TURN username is bound to (audit M2). NOTE: coturn's
/// REST username is "<exp>:<label>" with a per-mint expiry, so coturn cannot aggregate
/// `user-quota` on the label — the EFFECTIVE per-source bound on cred minting is the
/// master's per-/64 rate limit on /v1/join (RL_JOIN) plus coturn's global total-quota
/// + per-session max-bps + aggregate bps-capacity. Binding the label to the IP bucket
/// (vs a fresh-random per-mint identity) removes the "unique identity per mint" faucet
/// framing and gives coherent per-source attribution; it is not the quota enforcer.
pub fn ice_block(turn_label: &str) -> serde_json::Map<String, Value> {
    let mut m = serde_json::Map::new();
    m.insert("signalingUrl".into(), json!(CFG.signaling_url));
    m.insert("signalingToken".into(), json!(CFG.signaling_token));
    m.insert("stun".into(), json!(CFG.stun_uri));
    let turn = turn_creds(&CFG.turn_uri, &CFG.turn_secret, turn_label, TURN_TTL).unwrap_or_else(|| json!({}));
    m.insert("turn".into(), turn);
    m
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn turn_password_matches_python_reference() {
        // Reference produced by the Python turn_creds() HMAC path:
        //   python -c "import hmac,hashlib,base64; u='1700000000:h0011223344556677';
        //   print(base64.b64encode(hmac.new(b'testsecret_abc123', u.encode(),
        //   hashlib.sha1).digest()).decode())"  -> c7pJt+2pR4aVy8LJIi6NtjympwM=
        let pw = turn_password("testsecret_abc123", "1700000000:h0011223344556677");
        assert_eq!(pw, "c7pJt+2pR4aVy8LJIi6NtjympwM=");
    }
}
