//! What a lobby's host or joiner is handed to reach its peer: the signaling relay, a STUN server and a
//! TURN relay credential (coturn's REST scheme), the credential's lifetime per holder, and how the host's
//! heartbeat renews its own.

use crate::common::now_unix;
use crate::master_config::CFG;
use base64::engine::general_purpose::STANDARD as B64;
use base64::Engine;
use hmac::{Hmac, Mac};
use serde_json::{json, Value};
use sha1::Sha1;
use std::time::{Duration, Instant};

type HmacSha1 = Hmac<Sha1>;

// A TURN credential's lifetime, per holder. The joiner's is spent within seconds of its join, on the one
// allocation its ICE makes. The host allocates once per joiner for as long as its lobby lives, so its
// heartbeat renews it once half of it is spent; 600 s keeps a join's worst case inside the lifetime: the
// renewal age (under 300 s after the last beat that got through; a renewing answer the host missed is
// re-sent at its next beat) plus how long a lobby stays joinable without a beat (LOBBY_TTL and the 30 s
// sweep).
pub const TURN_TTL_JOIN: u64 = 120;
pub const TURN_TTL_HOST: u64 = 600;

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
pub fn ice_block(turn_label: &str, turn_ttl: u64) -> serde_json::Map<String, Value> {
    let mut m = serde_json::Map::new();
    m.insert("signalingUrl".into(), json!(CFG.signaling_url));
    m.insert("signalingToken".into(), json!(CFG.signaling_token));
    m.insert("stun".into(), json!(CFG.stun_uri));
    let turn = turn_creds(&CFG.turn_uri, &CFG.turn_secret, turn_label, turn_ttl).unwrap_or_else(|| json!({}));
    m.insert("turn".into(), turn);
    m
}

/// The credential a lobby's host holds by the master's record: the block it was handed, its username, and when
/// it was minted.
#[derive(Clone)]
pub struct HostTurn {
    pub username: String,
    pub block: Value,
    pub minted: Instant,
}

impl HostTurn {
    /// The record of a block just minted, or None for the empty block of a master with no TURN.
    pub fn minted_now(block: &Value, now: Instant) -> Option<HostTurn> {
        let username = block.get("user")?.as_str()?.to_string();
        Some(HostTurn { username, block: block.clone(), minted: now })
    }
}

/// What a heartbeat answers about the host's credential, given the username of the one its session holds.
#[derive(Debug, PartialEq, Eq)]
pub enum HostTurnAnswer {
    /// The record's credential is half spent: a new one, the only mint -- one per lobby per TTL / 2.
    Mint,
    /// The host holds another than the record's (it missed the answer that carried it): the record's own
    /// again. Not a mint.
    Resend,
    Nothing,
}

pub fn host_turn_answer(record: &HostTurn, held: &str, now: Instant) -> HostTurnAnswer {
    if now.saturating_duration_since(record.minted) >= Duration::from_secs(TURN_TTL_HOST / 2) {
        HostTurnAnswer::Mint
    } else if held != record.username {
        HostTurnAnswer::Resend
    } else {
        HostTurnAnswer::Nothing
    }
}

/// The turn block a heartbeat answers with, leaving the record behind it: `held` is the username the host's
/// session reports (None from a mod that takes no renewal), `mint` makes a new block (None with no TURN). A
/// re-sent record states the lifetime it has left, not the one it was minted with.
pub fn heartbeat_turn(record: &mut Option<HostTurn>, held: Option<&str>, now: Instant,
                      mint: impl FnOnce() -> Option<Value>) -> Option<Value> {
    let held = held.filter(|h| !h.is_empty())?;
    let answer = host_turn_answer(record.as_ref()?, held, now);
    match answer {
        HostTurnAnswer::Mint => {
            let block = mint()?;
            *record = HostTurn::minted_now(&block, now);
            Some(block)
        }
        HostTurnAnswer::Resend => {
            let r = record.as_ref()?;
            let mut block = r.block.clone();
            let spent = now.saturating_duration_since(r.minted).as_secs();
            block["ttl"] = json!(TURN_TTL_HOST.saturating_sub(spent));
            Some(block)
        }
        HostTurnAnswer::Nothing => None,
    }
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

    fn record(minted: Instant) -> HostTurn {
        HostTurn { username: "1700000600:203.0.113.7".into(), block: json!({}), minted }
    }

    #[test]
    fn a_host_credential_is_minted_anew_once_half_spent() {
        let t0 = Instant::now();
        let half = Duration::from_secs(TURN_TTL_HOST / 2);
        let r = record(t0);
        assert_eq!(host_turn_answer(&r, &r.username, t0), HostTurnAnswer::Nothing);
        assert_eq!(host_turn_answer(&r, &r.username, t0 + half - Duration::from_millis(1)), HostTurnAnswer::Nothing);
        assert_eq!(host_turn_answer(&r, &r.username, t0 + half), HostTurnAnswer::Mint);
        // Due is due whatever the host holds: the mint is the record's, and a stale host takes the new one.
        assert_eq!(host_turn_answer(&r, "older", t0 + half), HostTurnAnswer::Mint);
        // A clock read before the mint is never due (saturating, not a panic).
        assert_eq!(host_turn_answer(&record(t0 + half), &r.username, t0), HostTurnAnswer::Nothing);
    }

    #[test]
    fn a_host_that_missed_its_renewal_is_sent_the_record_again() {
        let t0 = Instant::now();
        let r = record(t0);
        assert_eq!(host_turn_answer(&r, "1700000000:203.0.113.7", t0 + Duration::from_secs(40)),
                   HostTurnAnswer::Resend);
    }

    fn minted(name: &str) -> Value {
        json!({"user": name, "pass": "p", "ttl": TURN_TTL_HOST, "uris": []})
    }

    #[test]
    fn a_beat_that_reports_no_credential_is_answered_without_one() {
        let t0 = Instant::now();
        let mut rec = HostTurn::minted_now(&minted("u0"), t0);
        let due = t0 + Duration::from_secs(TURN_TTL_HOST);
        assert!(heartbeat_turn(&mut rec, None, due, || Some(minted("u1"))).is_none());
        assert!(heartbeat_turn(&mut rec, Some(""), due, || Some(minted("u1"))).is_none());
        let mut none: Option<HostTurn> = None;
        assert!(heartbeat_turn(&mut none, Some("u0"), due, || Some(minted("u1"))).is_none());
        assert_eq!(rec.as_ref().map(|r| r.username.as_str()), Some("u0"));
    }

    #[test]
    fn a_due_beat_mints_and_the_record_follows() {
        let t0 = Instant::now();
        let mut rec = HostTurn::minted_now(&minted("u0"), t0);
        let due = t0 + Duration::from_secs(TURN_TTL_HOST / 2);
        let block = heartbeat_turn(&mut rec, Some("u0"), due, || Some(minted("u1"))).expect("minted");
        assert_eq!(block["user"], "u1");
        assert_eq!(rec.as_ref().map(|r| (r.username.as_str(), r.minted)), Some(("u1", due)));
        // A master with no TURN mints nothing and keeps its record.
        let mut rec = HostTurn::minted_now(&minted("u0"), t0);
        assert!(heartbeat_turn(&mut rec, Some("u0"), due, || None).is_none());
        assert_eq!(rec.as_ref().map(|r| r.username.as_str()), Some("u0"));
    }

    #[test]
    fn a_missed_renewal_is_re_sent_with_the_life_it_has_left() {
        let t0 = Instant::now();
        let mut rec = HostTurn::minted_now(&minted("u1"), t0);
        let later = t0 + Duration::from_secs(40);
        let block = heartbeat_turn(&mut rec, Some("u0"), later, || panic!("a re-send mints nothing"))
            .expect("re-sent");
        assert_eq!(block["user"], "u1");
        assert_eq!(block["ttl"], TURN_TTL_HOST - 40);
        assert!(heartbeat_turn(&mut rec, Some("u1"), later, || panic!("nothing to mint")).is_none());
    }

    #[test]
    fn only_a_minted_block_is_a_record() {
        let t0 = Instant::now();
        assert!(HostTurn::minted_now(&json!({}), t0).is_none());
        let b = json!({"user": "1700000600:x", "pass": "p", "ttl": 600, "uris": []});
        assert_eq!(HostTurn::minted_now(&b, t0).map(|r| r.username), Some("1700000600:x".to_string()));
    }
}
