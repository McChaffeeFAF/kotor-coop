#!/usr/bin/env python3
"""kotor-coop join-code and signalling service (design note coop_online).

One small process, Python 3.9+ standard library only:

  * Join codes. A host registers and gets an 8-character code (joincode.py).
    The code lives only while the host stays registered; the host can
    reissue it (the old one stops working at once) and lock it while the
    session is full. Codes also expire after --code-ttl.
  * Signalling. A client resolves a code; from then on the service relays
    GameNetworkingSockets' P2P rendezvous blobs between that client and its
    host ("custom signalling"). It never sees game traffic.
  * ICE settings. Every host and client gets the STUN server list and
    short-lived TURN relay credentials (coturn's use-auth-secret scheme), so
    a relay can carry the game when a direct path fails.
  * Optionally (--builtin-turn) the STUN/TURN relay itself (turnrelay.py),
    for machines without coturn.

Nothing is stored beyond live sessions: no accounts, no database, no files.
Logs carry no addresses and only a redacted code.

Wire protocol: TCP, one JSON object per line (UTF-8, at most 16 KiB).

  client -> service
    {"op":"host","v":1,"id":"kc-<16 hex>","max":<1..7>}
    {"op":"reissue"}                       host: a new code, the old one dies
    {"op":"lock","on":true|false}          host: a full session refuses joins
    {"op":"join","v":1,"id":"kc-<16 hex>","code":"<code>"}
    {"op":"signal","peer":<n>,"data":"<base64>"}   (a client omits peer)
    {"op":"ping"}
    {"op":"bye"}
  service -> client
    {"op":"hosted","code":"XXXXXXXX","expires":<unix>,"ice":ICE}
    {"op":"joined","peer":<n>,"host":"kc-...","ice":ICE}
    {"op":"peer","peer":<n>,"id":"kc-..."}      to the host: a client resolved the code
    {"op":"peer_left","peer":<n>}
    {"op":"signal","peer":<n>,"data":"..."}     (to a client without peer)
    {"op":"expired"}                            to the host: the code timed out
    {"op":"host_left"}                          to clients
    {"op":"pong"}
    {"op":"error","error":<reason>,"detail":"..."}
  ICE = {"stun":["host:port",...],"turn":[{"url":"host:port","user":"...","pass":"..."}]}
  reasons: bad_request, version, bad_code, host_full, rate_limited, server_full, timeout

Run `python coopserver.py --help` for the settings; each has an environment
variable for Docker (COOP_*).
"""
import argparse
import asyncio
import base64
import binascii
import json
import logging
import os
import re
import secrets
import signal
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import joincode  # noqa: E402
import turnrelay  # noqa: E402

PROTOCOL = 1
MAX_LINE = 16 * 1024
MAX_SIGNAL_BYTES = 8 * 1024
MAX_PEERS = 7  # Free Mode: 8 players
IDENTITY = re.compile(r"^kc-[0-9a-f]{16}$")

log = logging.getLogger("coopserver")


class Limits:
    def __init__(self, args):
        self.max_sessions = args.max_sessions
        self.max_connections = args.max_connections
        self.max_per_ip = args.max_per_ip
        self.join_rate = args.join_rate  # attempts per minute per address
        self.host_rate = args.host_rate  # registrations and reissues per minute per address
        self.code_ttl = args.code_ttl
        self.first_message_timeout = 10.0
        self.idle_timeout = args.idle_timeout
        self.message_rate = 50.0
        self.message_burst = 200.0
        self.byte_rate = 64 * 1024.0
        self.byte_burst = 256 * 1024.0


class Connection:
    def __init__(self, server, reader, writer, ip):
        self.server = server
        self.reader = reader
        self.writer = writer
        self.ip = ip
        self.role = None  # "host" or "join"
        self.session = None
        self.peer = 0  # a client's peer number
        self.identity = None
        self.messages = turnrelay.TokenBucket(server.limits.message_rate, server.limits.message_burst)
        self.bytes = turnrelay.TokenBucket(server.limits.byte_rate, server.limits.byte_burst)
        self.closed = False

    def send(self, message):
        if self.closed:
            return
        try:
            self.writer.write((json.dumps(message, separators=(",", ":")) + "\n").encode())
        except (ConnectionError, RuntimeError):
            self.closed = True

    def error(self, reason, detail=""):
        self.send({"op": "error", "error": reason, "detail": detail[:200]})

    def close(self):
        if not self.closed:
            self.closed = True
            try:
                self.writer.close()
            except (ConnectionError, RuntimeError):
                pass


class Session:
    def __init__(self, host, identity, max_peers, ice):
        self.host = host
        self.identity = identity
        self.max_peers = max_peers
        self.code = None
        self.code_expires = 0.0
        self.locked = False
        self.peers = {}  # peer number -> Connection
        self.ice = ice


def redact(code):
    return (code[:2] + "******") if code else "-"


class Server:
    def __init__(self, args, relay=None):
        self.args = args
        self.limits = Limits(args)
        self.relay = relay
        self.sessions = {}  # code -> Session
        self.hosts = set()  # every live Session, with or without a code
        self.connections = set()
        self.per_ip = {}
        self.join_buckets = {}
        self.host_buckets = {}
        self.next_peer = 1
        self.counters = {"hosted": 0, "joined": 0, "bad_codes": 0, "rate_limited": 0, "signals": 0}

    # --- ICE settings ----------------------------------------------------

    def ice(self):
        turn = []
        if self.args.turn_url and self.args.turn_secret:
            user, password = turnrelay.make_turn_credentials(self.args.turn_secret, self.args.turn_ttl,
                                                             "kc" + secrets.token_hex(4))
            for url in self.args.turn_url.split(","):
                url = url.strip()
                if url:
                    turn.append({"url": url, "user": user, "pass": password})
        stun = [u.strip() for u in (self.args.stun_urls or "").split(",") if u.strip()]
        return {"stun": stun, "turn": turn}

    # --- rate limits -----------------------------------------------------

    def _bucket(self, table, ip, per_minute):
        bucket = table.get(ip)
        if bucket is None:
            bucket = table[ip] = turnrelay.TokenBucket(per_minute / 60.0, max(3.0, per_minute / 2.0))
        return bucket

    def allow_join(self, ip):
        return self._bucket(self.join_buckets, ip, self.limits.join_rate).take()

    def allow_host(self, ip):
        return self._bucket(self.host_buckets, ip, self.limits.host_rate).take()

    async def housekeeping(self):
        while True:
            await asyncio.sleep(30)
            now = time.monotonic()
            for table in (self.join_buckets, self.host_buckets):
                for ip in [ip for ip, b in table.items() if now - b.stamp > 600 and ip not in self.per_ip]:
                    del table[ip]
            self.expire_codes(now)

    def expire_codes(self, now):
        for session in list(self.hosts):
            if session.code and now >= session.code_expires:
                log.info("code %s expired", redact(session.code))
                self.sessions.pop(session.code, None)
                session.code = None
                session.host.send({"op": "expired"})

    # --- sessions --------------------------------------------------------

    def issue_code(self, session):
        if session.code:
            self.sessions.pop(session.code, None)
        for _ in range(100):
            code = joincode.generate()
            if code not in self.sessions:
                break
        else:
            raise RuntimeError("no free join code")
        session.code = code
        session.code_expires = time.monotonic() + self.limits.code_ttl
        self.sessions[code] = session
        return code

    def hosted_message(self, session):
        return {"op": "hosted", "code": session.code,
                "expires": int(time.time() + (session.code_expires - time.monotonic())), "ice": session.ice}

    def drop(self, conn):
        """A connection went away: tidy its session or its place in one."""
        if conn.role == "host" and conn.session:
            session = conn.session
            self.hosts.discard(session)
            if session.code:
                self.sessions.pop(session.code, None)
            for peer in list(session.peers.values()):
                peer.send({"op": "host_left"})
                peer.session = None
                peer.close()
            session.peers.clear()
            log.info("host left (code %s)", redact(session.code))
        elif conn.role == "join" and conn.session:
            session = conn.session
            if session.peers.get(conn.peer) is conn:
                del session.peers[conn.peer]
                session.host.send({"op": "peer_left", "peer": conn.peer})
        conn.session = None

    # --- messages --------------------------------------------------------

    def handle(self, conn, message):
        op = message.get("op")
        if not isinstance(op, str):
            conn.error("bad_request", "op")
            return False
        if op == "ping":
            conn.send({"op": "pong"})
            return True
        if op == "bye":
            return False
        if conn.role is None:
            if op == "host":
                return self.on_host(conn, message)
            if op == "join":
                return self.on_join(conn, message)
            conn.error("bad_request", "host or join first")
            return False
        if op == "signal":
            return self.on_signal(conn, message)
        if conn.role == "host" and op == "reissue":
            if not self.allow_host(conn.ip):
                self.counters["rate_limited"] += 1
                conn.error("rate_limited", "too many new codes; wait a minute")
                return True
            old = conn.session.code
            self.issue_code(conn.session)
            log.info("code reissued (%s -> %s)", redact(old), redact(conn.session.code))
            conn.send(self.hosted_message(conn.session))
            return True
        if conn.role == "host" and op == "lock":
            on = message.get("on")
            if not isinstance(on, bool):
                conn.error("bad_request", "lock needs on: true or false")
                return False
            conn.session.locked = on
            return True
        conn.error("bad_request", "unexpected op " + op[:20])
        return False

    def _version(self, conn, message):
        if message.get("v") != PROTOCOL:
            conn.error("version", "service protocol %d" % PROTOCOL)
            return False
        identity = message.get("id")
        if not isinstance(identity, str) or not IDENTITY.match(identity):
            conn.error("bad_request", "id")
            return False
        return True

    def on_host(self, conn, message):
        if not self._version(conn, message):
            return False
        max_peers = message.get("max", 2)
        if not isinstance(max_peers, int) or isinstance(max_peers, bool) or not 1 <= max_peers <= MAX_PEERS:
            conn.error("bad_request", "max")
            return False
        if not self.allow_host(conn.ip):
            self.counters["rate_limited"] += 1
            conn.error("rate_limited", "too many sessions from this address; wait a minute")
            return False
        if len(self.hosts) >= self.limits.max_sessions:
            conn.error("server_full", "the service is at its session limit")
            return False
        session = Session(conn, message["id"], max_peers, self.ice())
        conn.role = "host"
        conn.session = session
        conn.identity = message["id"]
        self.hosts.add(session)
        self.issue_code(session)
        self.counters["hosted"] += 1
        log.info("host registered (code %s, up to %d peers)", redact(session.code), max_peers)
        conn.send(self.hosted_message(session))
        return True

    def on_join(self, conn, message):
        if not self._version(conn, message):
            return False
        if not self.allow_join(conn.ip):
            self.counters["rate_limited"] += 1
            conn.error("rate_limited", "too many join attempts; wait a minute")
            return False
        code = joincode.normalize(message.get("code"))
        session = self.sessions.get(code) if code else None
        if session is None:
            self.counters["bad_codes"] += 1
            conn.error("bad_code", "no session has that code")
            return False
        if session.locked or len(session.peers) >= session.max_peers:
            conn.error("host_full", "the session is full")
            return False
        if message["id"] == session.identity:
            conn.error("bad_request", "id")
            return False
        conn.role = "join"
        conn.session = session
        conn.identity = message["id"]
        conn.peer = self.next_peer
        self.next_peer += 1
        session.peers[conn.peer] = conn
        self.counters["joined"] += 1
        log.info("peer %d resolved code %s", conn.peer, redact(code))
        session.host.send({"op": "peer", "peer": conn.peer, "id": conn.identity})
        conn.send({"op": "joined", "peer": conn.peer, "host": session.identity, "ice": self.ice()})
        return True

    def on_signal(self, conn, message):
        data = message.get("data")
        if not isinstance(data, str) or len(data) > (MAX_SIGNAL_BYTES * 4) // 3 + 4:
            conn.error("bad_request", "data")
            return False
        try:
            raw = base64.b64decode(data, validate=True)
        except (binascii.Error, ValueError):
            conn.error("bad_request", "data")
            return False
        if not raw or len(raw) > MAX_SIGNAL_BYTES:
            conn.error("bad_request", "data")
            return False
        session = conn.session
        if session is None:
            return False
        if conn.role == "host":
            peer = message.get("peer")
            target = session.peers.get(peer) if isinstance(peer, int) and not isinstance(peer, bool) else None
            if target is None:
                return True  # the peer left meanwhile: best effort, like the signals themselves
            target.send({"op": "signal", "data": data})
        else:
            session.host.send({"op": "signal", "peer": conn.peer, "data": data})
        self.counters["signals"] += 1
        return True

    # --- connections -----------------------------------------------------

    async def serve(self, reader, writer):
        peer = writer.get_extra_info("peername") or ("?", 0)
        ip = peer[0]
        if len(self.connections) >= self.limits.max_connections or self.per_ip.get(ip, 0) >= self.limits.max_per_ip:
            writer.write(b'{"op":"error","error":"server_full","detail":"too many connections"}\n')
            writer.close()
            return
        conn = Connection(self, reader, writer, ip)
        self.connections.add(conn)
        self.per_ip[ip] = self.per_ip.get(ip, 0) + 1
        try:
            first = True
            while not conn.closed:
                timeout = self.limits.first_message_timeout if first else self.limits.idle_timeout
                try:
                    line = await asyncio.wait_for(reader.readline(), timeout)
                except asyncio.TimeoutError:
                    conn.error("timeout", "no message for %d s" % timeout)
                    break
                except (ValueError, asyncio.LimitOverrunError):
                    conn.error("bad_request", "line too long")
                    break
                if not line:
                    break
                if not conn.messages.take() or not conn.bytes.take(len(line)):
                    conn.error("rate_limited", "too many messages")
                    break
                try:
                    message = json.loads(line.decode("utf-8"))
                except (UnicodeDecodeError, ValueError):
                    conn.error("bad_request", "not JSON")
                    break
                if not isinstance(message, dict):
                    conn.error("bad_request", "not an object")
                    break
                if message.get("op") == "stats" and self.args.local_stats and ip in ("127.0.0.1", "::1"):
                    conn.send({"op": "stats", "sessions": len(self.hosts), "codes": len(self.sessions),
                               "connections": len(self.connections), "counters": self.counters,
                               "turn": self.relay.stats if self.relay else None})
                    continue
                first = False
                if not self.handle(conn, message):
                    break
                await writer.drain()
        except (ConnectionError, OSError):
            pass
        finally:
            self.drop(conn)
            conn.close()
            self.connections.discard(conn)
            count = self.per_ip.get(ip, 1) - 1
            if count <= 0:
                self.per_ip.pop(ip, None)
            else:
                self.per_ip[ip] = count


def env(name, default):
    return os.environ.get(name, default)


def parse_args(argv=None):
    ap = argparse.ArgumentParser(description="kotor-coop join-code and signalling service")
    ap.add_argument("--listen", default=env("COOP_LISTEN", "127.0.0.1:27200"),
                    help="TCP address for the game's signalling link (COOP_LISTEN)")
    ap.add_argument("--stun-urls", default=env("COOP_STUN_URLS", ""),
                    help="comma-separated STUN host:port list handed to games (COOP_STUN_URLS)")
    ap.add_argument("--turn-url", default=env("COOP_TURN_URL", ""),
                    help="TURN relay host:port handed to games; empty disables the relay (COOP_TURN_URL)")
    ap.add_argument("--turn-secret", default=env("COOP_TURN_SECRET", ""),
                    help="shared secret with the TURN server (coturn static-auth-secret) (COOP_TURN_SECRET)")
    ap.add_argument("--turn-ttl", type=int, default=int(env("COOP_TURN_TTL", "86400")),
                    help="seconds a relay credential stays valid (COOP_TURN_TTL)")
    ap.add_argument("--code-ttl", type=int, default=int(env("COOP_CODE_TTL", "43200")),
                    help="seconds a join code lives at most; the host can reissue (COOP_CODE_TTL)")
    ap.add_argument("--idle-timeout", type=int, default=int(env("COOP_IDLE_TIMEOUT", "90")),
                    help="seconds without a message before a link is closed; games ping every 20 s")
    ap.add_argument("--max-sessions", type=int, default=int(env("COOP_MAX_SESSIONS", "200")))
    ap.add_argument("--max-connections", type=int, default=int(env("COOP_MAX_CONNECTIONS", "1000")))
    ap.add_argument("--max-per-ip", type=int, default=int(env("COOP_MAX_PER_IP", "16")))
    ap.add_argument("--join-rate", type=float, default=float(env("COOP_JOIN_RATE", "20")),
                    help="join attempts per minute per address (COOP_JOIN_RATE)")
    ap.add_argument("--host-rate", type=float, default=float(env("COOP_HOST_RATE", "10")),
                    help="new sessions and reissued codes per minute per address (COOP_HOST_RATE)")
    ap.add_argument("--local-stats", action="store_true", default=env("COOP_LOCAL_STATS", "0") == "1",
                    help="answer {\"op\":\"stats\"} from 127.0.0.1 (tests)")
    ap.add_argument("--builtin-turn", default=env("COOP_BUILTIN_TURN", ""),
                    help="run the built-in STUN/TURN relay on this UDP host:port (e.g. 127.0.0.1:27210)")
    ap.add_argument("--relay-ports", default=env("COOP_RELAY_PORTS", "49160-49200"),
                    help="built-in relay: UDP port range for relay addresses")
    ap.add_argument("--relay-bind", default=env("COOP_RELAY_BIND", ""),
                    help="built-in relay: address relay sockets bind to (default: the relay's own host)")
    ap.add_argument("--relay-public-ip", default=env("COOP_RELAY_PUBLIC_IP", ""),
                    help="built-in relay: IPv4 address games are told to send to (default: the bind address)")
    ap.add_argument("--allow-private-peers", action="store_true", default=env("COOP_ALLOW_PRIVATE_PEERS", "0") == "1",
                    help="built-in relay: relay to private/loopback addresses (same-machine and LAN tests only)")
    ap.add_argument("--realm", default=env("COOP_REALM", "kotor-coop"), help="TURN realm (coturn realm)")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)
    if args.builtin_turn and not args.turn_secret:
        args.turn_secret = secrets.token_urlsafe(24)  # in-process: nobody else needs it
    return args


def split_address(text, default_port):
    host, _, port = text.rpartition(":")
    if not host:
        return text, default_port
    return host.strip("[]"), int(port)


async def main_async(args):
    relay = None
    if args.builtin_turn:
        host, port = split_address(args.builtin_turn, 3478)
        low, high = (int(x) for x in args.relay_ports.split("-"))
        bind = args.relay_bind or host
        relay = turnrelay.TurnRelay(turnrelay.RelayConfig(
            secret=args.turn_secret, realm=args.realm, listen_host=host, listen_port=port, relay_bind=bind,
            relay_public_ip=args.relay_public_ip or bind, relay_ports=(low, high),
            allow_private_peers=args.allow_private_peers))
        await relay.start()
        if not args.turn_url:
            args.turn_url = "%s:%d" % (args.relay_public_ip or host, port)
        if not args.stun_urls:
            args.stun_urls = "%s:%d" % (args.relay_public_ip or host, port)
    server = Server(args, relay)
    host, port = split_address(args.listen, 27200)
    listener = await asyncio.start_server(server.serve, host, port, limit=MAX_LINE)
    log.info("signalling on tcp %s:%d; STUN %s; TURN %s", host, port, args.stun_urls or "none",
             args.turn_url or "none (no relay fallback)")
    housekeeping = asyncio.ensure_future(server.housekeeping())
    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    for name in ("SIGINT", "SIGTERM"):
        if hasattr(signal, name):
            try:
                loop.add_signal_handler(getattr(signal, name), stop.set)
            except (NotImplementedError, RuntimeError):
                pass  # Windows: Ctrl+C raises KeyboardInterrupt instead
    print("coopserver ready", flush=True)
    try:
        await stop.wait()
    finally:
        housekeeping.cancel()
        listener.close()
        if relay:
            relay.close()


def main(argv=None):
    args = parse_args(argv)
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(name)s %(levelname)s %(message)s", stream=sys.stdout)
    try:
        asyncio.run(main_async(args))
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
