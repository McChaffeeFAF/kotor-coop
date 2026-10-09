"""A small STUN/TURN relay (RFC 5389 STUN, RFC 5766 TURN over UDP, IPv4).

It exists so the whole co-op service runs from one Python process on a
machine without Docker (local tests on Windows). A cloud deployment uses
coturn instead (docker-compose.yml); both check the same short-lived
credentials, the "TURN REST API" scheme coturn calls use-auth-secret:

    username = "<unix expiry>:<anything>"
    password = base64(HMAC-SHA1(shared secret, username))

The signalling server hands those out with each join code, so nothing is
stored and no account exists. What is implemented is what ICE clients use:
Binding, Allocate, Refresh, CreatePermission, ChannelBind, Send and Data
indications and ChannelData, with long-term credentials (401 / 438 nonces),
MESSAGE-INTEGRITY and FINGERPRINT on responses.

Limits: allocations in total and per user, a lifetime cap, a byte rate per
allocation, and a peer-address policy (private, loopback and link-local
peers are refused unless allow_private_peers, which a LAN or same-machine
test needs).
"""
import asyncio
import base64
import binascii
import hashlib
import hmac
import ipaddress
import logging
import os
import socket
import struct
import time

log = logging.getLogger("coopserver.turn")

MAGIC = 0x2112A442
MAGIC_BYTES = struct.pack("!I", MAGIC)
FINGERPRINT_XOR = 0x5354554E

# Methods (all below 0x10, so class bits are plain ORs).
BINDING = 0x001
ALLOCATE = 0x003
REFRESH = 0x004
SEND = 0x006
DATA = 0x007
CREATE_PERMISSION = 0x008
CHANNEL_BIND = 0x009

REQUEST = 0x0000
INDICATION = 0x0010
SUCCESS = 0x0100
ERROR = 0x0110

# Attributes
A_MAPPED_ADDRESS = 0x0001
A_USERNAME = 0x0006
A_MESSAGE_INTEGRITY = 0x0008
A_ERROR_CODE = 0x0009
A_UNKNOWN_ATTRIBUTES = 0x000A
A_CHANNEL_NUMBER = 0x000C
A_LIFETIME = 0x000D
A_XOR_PEER_ADDRESS = 0x0012
A_DATA = 0x0013
A_REALM = 0x0014
A_NONCE = 0x0015
A_XOR_RELAYED_ADDRESS = 0x0016
A_REQUESTED_ADDRESS_FAMILY = 0x0017
A_EVEN_PORT = 0x0018
A_REQUESTED_TRANSPORT = 0x0019
A_DONT_FRAGMENT = 0x001A
A_XOR_MAPPED_ADDRESS = 0x0020
A_RESERVATION_TOKEN = 0x0022
A_PRIORITY = 0x0024
A_USE_CANDIDATE = 0x0025
A_SOFTWARE = 0x8022
A_FINGERPRINT = 0x8028
A_ICE_CONTROLLED = 0x8029
A_ICE_CONTROLLING = 0x802A

# Comprehension-required attributes we understand (anything else below 0x8000 gets a 420).
KNOWN_REQUIRED = {A_MAPPED_ADDRESS, A_USERNAME, A_MESSAGE_INTEGRITY, A_ERROR_CODE, A_UNKNOWN_ATTRIBUTES,
                  A_CHANNEL_NUMBER, A_LIFETIME, A_XOR_PEER_ADDRESS, A_DATA, A_REALM, A_NONCE, A_XOR_RELAYED_ADDRESS,
                  A_REQUESTED_ADDRESS_FAMILY, A_EVEN_PORT, A_REQUESTED_TRANSPORT, A_DONT_FRAGMENT, A_XOR_MAPPED_ADDRESS,
                  A_RESERVATION_TOKEN, A_PRIORITY, A_USE_CANDIDATE}

SOFTWARE = b"kotor-coop relay"
DEFAULT_LIFETIME = 600
# RFC 5766 says 300 s, refreshed by CreatePermission. GameNetworkingSockets'
# ICE client never refreshes one, so a relayed game would stop after five
# minutes: permissions here last as long as an allocation can.
PERMISSION_LIFETIME = 3600
CHANNEL_LIFETIME = 600
NONCE_LIFETIME = 3600


def make_turn_credentials(secret, ttl, tag="coop"):
    """Short-lived TURN credentials (coturn use-auth-secret / TURN REST API)."""
    expiry = int(time.time()) + int(ttl)
    username = "%d:%s" % (expiry, tag)
    password = base64.b64encode(hmac.new(secret.encode(), username.encode(), hashlib.sha1).digest()).decode()
    return username, password


class StunMessage:
    __slots__ = ("type", "tid", "attrs", "raw", "mi_offset", "has_fingerprint")

    def __init__(self):
        self.type = 0
        self.tid = b""
        self.attrs = []
        self.raw = b""
        self.mi_offset = None
        self.has_fingerprint = False

    @property
    def method(self):
        t = self.type
        return (t & 0x000F) | ((t & 0x00E0) >> 1) | ((t & 0x3E00) >> 2)

    @property
    def cls(self):
        return self.type & 0x0110

    def get(self, attr):
        for t, v in self.attrs:
            if t == attr:
                return v
        return None

    def get_all(self, attr):
        return [v for t, v in self.attrs if t == attr]


def parse(data):
    """A STUN message, or None for anything that is not a well-formed one."""
    if len(data) < 20 or data[0] & 0xC0:
        return None
    mtype, length, magic = struct.unpack_from("!HHI", data)
    if magic != MAGIC or length != len(data) - 20 or length % 4:
        return None
    msg = StunMessage()
    msg.type = mtype
    msg.tid = data[8:20]
    msg.raw = data
    off = 20
    while off < len(data):
        if off + 4 > len(data):
            return None
        atype, alen = struct.unpack_from("!HH", data, off)
        start = off + 4
        end = start + alen
        if end > len(data):
            return None
        if msg.has_fingerprint:
            return None  # nothing may follow FINGERPRINT
        if atype == A_MESSAGE_INTEGRITY:
            if alen != 20 or msg.mi_offset is not None:
                return None
            msg.mi_offset = off
        elif atype == A_FINGERPRINT:
            if alen != 4:
                return None
            crc = (binascii.crc32(data[:off]) & 0xFFFFFFFF) ^ FINGERPRINT_XOR
            if struct.unpack_from("!I", data, start)[0] != crc:
                return None
            msg.has_fingerprint = True
        elif msg.mi_offset is not None:
            # Only FINGERPRINT may follow MESSAGE-INTEGRITY; attributes after it are ignored (RFC 5389 15.4).
            off = end + (-alen % 4)
            continue
        msg.attrs.append((atype, data[start:end]))
        off = end + (-alen % 4)
    return msg


def attr(atype, value):
    return struct.pack("!HH", atype, len(value)) + value + b"\x00" * (-len(value) % 4)


def xor_address(atype, ip, port):
    xport = port ^ (MAGIC >> 16)
    xip = struct.unpack("!I", socket.inet_aton(ip))[0] ^ MAGIC
    return attr(atype, struct.pack("!BBHI", 0, 1, xport, xip))


def plain_address(atype, ip, port):
    return attr(atype, struct.pack("!BBH", 0, 1, port) + socket.inet_aton(ip))


def decode_xor_address(value):
    """(ip, port) of an IPv4 XOR address attribute, or None."""
    if value is None or len(value) != 8 or value[1] != 1:
        return None
    xport, xip = struct.unpack_from("!HI", value, 2)
    return socket.inet_ntoa(struct.pack("!I", xip ^ MAGIC)), xport ^ (MAGIC >> 16)


def error_attr(code, reason):
    return attr(A_ERROR_CODE, struct.pack("!HBB", 0, code // 100, code % 100) + reason.encode())


def build(mtype, tid, attrs, key=None, fingerprint=True):
    body = b"".join(attrs)
    if key is not None:
        header = struct.pack("!HH", mtype, len(body) + 24) + MAGIC_BYTES + tid
        mac = hmac.new(key, header + body, hashlib.sha1).digest()
        body += attr(A_MESSAGE_INTEGRITY, mac)
    if fingerprint:
        header = struct.pack("!HH", mtype, len(body) + 8) + MAGIC_BYTES + tid
        crc = (binascii.crc32(header + body) & 0xFFFFFFFF) ^ FINGERPRINT_XOR
        body += attr(A_FINGERPRINT, struct.pack("!I", crc))
    return struct.pack("!HH", mtype, len(body)) + MAGIC_BYTES + tid + body


def check_integrity(msg, key):
    if msg.mi_offset is None:
        return False
    prefix = bytearray(msg.raw[:msg.mi_offset])
    struct.pack_into("!H", prefix, 2, msg.mi_offset - 20 + 24)
    expected = hmac.new(key, bytes(prefix), hashlib.sha1).digest()
    got = msg.raw[msg.mi_offset + 4:msg.mi_offset + 24]
    return hmac.compare_digest(expected, got)


class TokenBucket:
    def __init__(self, rate, burst):
        self.rate = float(rate)
        self.burst = float(burst)
        self.tokens = float(burst)
        self.stamp = time.monotonic()

    def take(self, amount=1.0):
        now = time.monotonic()
        self.tokens = min(self.burst, self.tokens + (now - self.stamp) * self.rate)
        self.stamp = now
        if self.tokens >= amount:
            self.tokens -= amount
            return True
        return False


class RelayConfig:
    def __init__(self, secret, realm="kotor-coop", listen_host="127.0.0.1", listen_port=3478,
                 relay_bind="127.0.0.1", relay_public_ip=None, relay_ports=(49160, 49200),
                 max_allocations=64, max_per_user=4, max_lifetime=3600, byte_rate=2_000_000,
                 allow_private_peers=False):
        self.secret = secret
        self.realm = realm
        self.listen_host = listen_host
        self.listen_port = listen_port
        self.relay_bind = relay_bind
        self.relay_public_ip = relay_public_ip or relay_bind
        self.relay_ports = relay_ports
        self.max_allocations = max_allocations
        self.max_per_user = max_per_user
        self.max_lifetime = max_lifetime
        self.byte_rate = byte_rate
        self.allow_private_peers = allow_private_peers


class _RelaySocket(asyncio.DatagramProtocol):
    def __init__(self, allocation):
        self.allocation = allocation
        self.transport = None

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        self.allocation.server.from_peer(self.allocation, data, addr)


class Allocation:
    def __init__(self, server, client, username, key, lifetime):
        self.server = server
        self.client = client
        self.username = username
        self.key = key
        self.expiry = time.monotonic() + lifetime
        self.permissions = {}  # peer ip -> expiry
        self.channels = {}  # number -> ((ip, port), expiry)
        self.peer_channels = {}  # (ip, port) -> number
        self.relay = None  # _RelaySocket
        self.port = 0
        self.last_tid = None
        self.last_response = None
        self.bucket = TokenBucket(server.config.byte_rate, server.config.byte_rate * 2)

    def permitted(self, ip, now):
        expiry = self.permissions.get(ip)
        return expiry is not None and expiry > now


class TurnRelay(asyncio.DatagramProtocol):
    """The listening STUN/TURN socket and every allocation."""

    def __init__(self, config):
        self.config = config
        self.transport = None
        self.allocations = {}  # client (ip, port) -> Allocation
        self.by_relay_port = {}
        self.free_ports = list(range(config.relay_ports[0], config.relay_ports[1] + 1))
        self._nonce_key = os.urandom(16)
        self.stats = {"bindings": 0, "allocations": 0, "allocations_now": 0, "relayed_to_peer": 0,
                      "relayed_to_client": 0, "bytes_to_peer": 0, "bytes_to_client": 0, "auth_failures": 0,
                      "refused_peers": 0, "rate_dropped": 0}
        self._cleaner = None

    # --- lifecycle -------------------------------------------------------

    async def start(self):
        loop = asyncio.get_running_loop()
        await loop.create_datagram_endpoint(lambda: self, local_addr=(self.config.listen_host, self.config.listen_port))
        self._cleaner = asyncio.ensure_future(self._clean_loop())
        log.info("STUN/TURN relay on udp %s:%d, relay ports %d-%d on %s (advertised %s)", self.config.listen_host,
                 self.config.listen_port, self.config.relay_ports[0], self.config.relay_ports[1],
                 self.config.relay_bind, self.config.relay_public_ip)

    def close(self):
        if self._cleaner:
            self._cleaner.cancel()
        for allocation in list(self.allocations.values()):
            self._delete(allocation)
        if self.transport:
            self.transport.close()

    def connection_made(self, transport):
        self.transport = transport

    async def _clean_loop(self):
        while True:
            await asyncio.sleep(5)
            now = time.monotonic()
            for allocation in list(self.allocations.values()):
                if allocation.expiry <= now:
                    self._delete(allocation)
                    continue
                allocation.permissions = {ip: e for ip, e in allocation.permissions.items() if e > now}
                for number, (peer, expiry) in list(allocation.channels.items()):
                    if expiry <= now:
                        del allocation.channels[number]
                        allocation.peer_channels.pop(peer, None)

    # --- nonces and credentials -----------------------------------------

    def _nonce(self):
        stamp = "%x" % int(time.time())
        mac = hmac.new(self._nonce_key, stamp.encode(), hashlib.sha256).hexdigest()[:16]
        return (stamp + ":" + mac).encode()

    def _nonce_state(self, nonce):
        """'ok', 'stale' or 'bad'."""
        try:
            text = nonce.decode()
            stamp, mac = text.split(":")
            expected = hmac.new(self._nonce_key, stamp.encode(), hashlib.sha256).hexdigest()[:16]
            if not hmac.compare_digest(mac, expected):
                return "bad"
            return "ok" if time.time() - int(stamp, 16) < NONCE_LIFETIME else "stale"
        except (ValueError, UnicodeDecodeError):
            return "bad"

    def _key_for(self, username):
        """The long-term key for a REST username, or None when expired or malformed."""
        try:
            text = username.decode()
            expiry = int(text.split(":", 1)[0])
        except (ValueError, UnicodeDecodeError):
            return None
        if expiry < time.time() or len(text) > 128:
            return None
        password = base64.b64encode(hmac.new(self.config.secret.encode(), text.encode(), hashlib.sha1).digest()).decode()
        return hashlib.md5(("%s:%s:%s" % (text, self.config.realm, password)).encode()).digest()

    def _authenticate(self, msg, addr):
        """(key, username) for an authenticated request; otherwise sends the challenge and returns None."""
        method = msg.method
        username = msg.get(A_USERNAME)
        realm = msg.get(A_REALM)
        nonce = msg.get(A_NONCE)
        if msg.mi_offset is None or username is None or realm is None or nonce is None:
            self._challenge(msg, addr, 401, "Unauthorized")
            return None
        state = self._nonce_state(nonce)
        if state != "ok":
            self._challenge(msg, addr, 438, "Stale Nonce")
            return None
        if realm.decode(errors="replace") != self.config.realm:
            self._challenge(msg, addr, 401, "Unauthorized")
            return None
        key = self._key_for(username)
        if key is None or not check_integrity(msg, key):
            self.stats["auth_failures"] += 1
            self._challenge(msg, addr, 401, "Unauthorized")
            return None
        allocation = self.allocations.get(addr)
        if allocation and method != ALLOCATE and allocation.username != username:
            self._error(msg, addr, 441, "Wrong Credentials", key)
            return None
        return key, username

    def _challenge(self, msg, addr, code, reason):
        attrs = [error_attr(code, reason), attr(A_REALM, self.config.realm.encode()), attr(A_NONCE, self._nonce()),
                 attr(A_SOFTWARE, SOFTWARE)]
        self._send(build(msg.method | ERROR, msg.tid, attrs), addr)

    def _error(self, msg, addr, code, reason, key=None, extra=()):
        attrs = [error_attr(code, reason)] + list(extra) + [attr(A_SOFTWARE, SOFTWARE)]
        self._send(build(msg.method | ERROR, msg.tid, attrs, key), addr)

    def _send(self, data, addr):
        if self.transport:
            self.transport.sendto(data, addr)

    # --- peers -----------------------------------------------------------

    def _peer_allowed(self, ip):
        try:
            address = ipaddress.ip_address(ip)
        except ValueError:
            return False
        if address.is_unspecified or address.is_multicast or address.is_reserved or address.version != 4:
            return False
        if ip == "255.255.255.255":
            return False
        if not self.config.allow_private_peers and (address.is_private or address.is_loopback or address.is_link_local):
            # Our own relay addresses are fine even when they are private (both peers relayed).
            return ip == self.config.relay_public_ip
        return True

    # --- datagrams from clients -----------------------------------------

    def datagram_received(self, data, addr):
        if not data:
            return
        if 0x40 <= data[0] <= 0x7F:
            self._channel_data(data, addr)
            return
        msg = parse(data)
        if msg is None:
            return
        method, cls = msg.method, msg.cls
        if cls == REQUEST:
            unknown = [t for t, _ in msg.attrs if t < 0x8000 and t not in KNOWN_REQUIRED]
            if unknown:
                self._error(msg, addr, 420, "Unknown Attribute",
                            extra=[attr(A_UNKNOWN_ATTRIBUTES, b"".join(struct.pack("!H", t) for t in unknown[:8]))])
                return
            if method == BINDING:
                self.stats["bindings"] += 1
                attrs = [xor_address(A_XOR_MAPPED_ADDRESS, addr[0], addr[1]), plain_address(A_MAPPED_ADDRESS, addr[0], addr[1]),
                         attr(A_SOFTWARE, SOFTWARE)]
                self._send(build(BINDING | SUCCESS, msg.tid, attrs), addr)
            elif method == ALLOCATE:
                self._allocate(msg, addr)
            elif method == REFRESH:
                self._refresh(msg, addr)
            elif method == CREATE_PERMISSION:
                self._create_permission(msg, addr)
            elif method == CHANNEL_BIND:
                self._channel_bind(msg, addr)
            else:
                self._error(msg, addr, 400, "Bad Request")
        elif cls == INDICATION and method == SEND:
            self._send_indication(msg, addr)

    def _allocate(self, msg, addr):
        existing = self.allocations.get(addr)
        if existing and existing.last_tid == msg.tid:
            if existing.last_response:
                self._send(existing.last_response, addr)  # a retransmission
            return  # still opening: the answer is on its way
        auth = self._authenticate(msg, addr)
        if auth is None:
            return
        key, username = auth
        if existing:
            self._error(msg, addr, 437, "Allocation Mismatch", key)
            return
        transport = msg.get(A_REQUESTED_TRANSPORT)
        if transport is None or len(transport) != 4:
            self._error(msg, addr, 400, "Bad Request", key)
            return
        if transport[0] != 17:
            self._error(msg, addr, 442, "Unsupported Transport Protocol", key)
            return
        family = msg.get(A_REQUESTED_ADDRESS_FAMILY)
        if family is not None and (len(family) != 4 or family[0] != 1):
            self._error(msg, addr, 440, "Address Family not Supported", key)
            return
        if msg.get(A_RESERVATION_TOKEN) is not None:
            self._error(msg, addr, 508, "Insufficient Capacity", key)
            return
        per_user = sum(1 for a in self.allocations.values() if a.username == username)
        if len(self.allocations) >= self.config.max_allocations or per_user >= self.config.max_per_user:
            self._error(msg, addr, 486, "Allocation Quota Reached", key)
            return
        lifetime = self._lifetime(msg) or min(DEFAULT_LIFETIME, self.config.max_lifetime)
        allocation = Allocation(self, addr, username, key, lifetime)
        allocation.last_tid = msg.tid
        asyncio.ensure_future(self._open_relay(allocation, msg, key, lifetime))
        self.allocations[addr] = allocation  # holds the 5-tuple while the socket opens

    async def _open_relay(self, allocation, msg, key, lifetime):
        loop = asyncio.get_running_loop()
        opened = None
        while self.free_ports:
            port = self.free_ports.pop(0)
            try:
                transport, protocol = await loop.create_datagram_endpoint(
                    lambda: _RelaySocket(allocation), local_addr=(self.config.relay_bind, port))
                opened = (port, protocol)
                break
            except OSError:
                continue  # busy: try the next one (it is not returned to the pool)
        if opened is None:
            self.allocations.pop(allocation.client, None)
            self._error(msg, allocation.client, 508, "Insufficient Capacity", key)
            return
        if self.allocations.get(allocation.client) is not allocation:
            protocol.transport.close()
            self.free_ports.append(opened[0])
            return
        allocation.port, allocation.relay = opened
        self.by_relay_port[allocation.port] = allocation
        self.stats["allocations"] += 1
        self.stats["allocations_now"] = len(self.by_relay_port)
        attrs = [xor_address(A_XOR_RELAYED_ADDRESS, self.config.relay_public_ip, allocation.port),
                 xor_address(A_XOR_MAPPED_ADDRESS, allocation.client[0], allocation.client[1]),
                 attr(A_LIFETIME, struct.pack("!I", lifetime)), attr(A_SOFTWARE, SOFTWARE)]
        response = build(ALLOCATE | SUCCESS, msg.tid, attrs, key)
        allocation.last_tid = msg.tid
        allocation.last_response = response
        log.info("relay allocation %d opened (lifetime %d s)", allocation.port, lifetime)
        self._send(response, allocation.client)

    def _lifetime(self, msg):
        value = msg.get(A_LIFETIME)
        requested = struct.unpack("!I", value)[0] if value is not None and len(value) == 4 else DEFAULT_LIFETIME
        return max(0, min(requested, self.config.max_lifetime))

    def _delete(self, allocation):
        self.allocations.pop(allocation.client, None)
        if allocation.port:
            self.by_relay_port.pop(allocation.port, None)
            if allocation.relay and allocation.relay.transport:
                allocation.relay.transport.close()
            self.free_ports.append(allocation.port)
            log.info("relay allocation %d closed", allocation.port)
            allocation.port = 0
        self.stats["allocations_now"] = len(self.by_relay_port)

    def _owned(self, msg, addr):
        """The caller's live allocation after authentication, or None (an error was sent)."""
        auth = self._authenticate(msg, addr)
        if auth is None:
            return None, None
        key, _ = auth
        allocation = self.allocations.get(addr)
        if allocation is None or not allocation.relay:
            self._error(msg, addr, 437, "Allocation Mismatch", key)
            return None, key
        return allocation, key

    def _refresh(self, msg, addr):
        allocation, key = self._owned(msg, addr)
        if allocation is None:
            return
        lifetime = self._lifetime(msg)
        if lifetime == 0:
            self._delete(allocation)
        else:
            allocation.expiry = time.monotonic() + lifetime
        self._send(build(REFRESH | SUCCESS, msg.tid, [attr(A_LIFETIME, struct.pack("!I", lifetime)),
                                                       attr(A_SOFTWARE, SOFTWARE)], key), addr)

    def _create_permission(self, msg, addr):
        allocation, key = self._owned(msg, addr)
        if allocation is None:
            return
        peers = [decode_xor_address(v) for v in msg.get_all(A_XOR_PEER_ADDRESS)]
        if not peers or any(p is None for p in peers):
            self._error(msg, addr, 400, "Bad Request", key)
            return
        if not all(self._peer_allowed(ip) for ip, _ in peers):
            self.stats["refused_peers"] += 1
            self._error(msg, addr, 403, "Forbidden", key)
            return
        expiry = time.monotonic() + PERMISSION_LIFETIME
        for ip, _ in peers:
            allocation.permissions[ip] = expiry
        self._send(build(CREATE_PERMISSION | SUCCESS, msg.tid, [attr(A_SOFTWARE, SOFTWARE)], key), addr)

    def _channel_bind(self, msg, addr):
        allocation, key = self._owned(msg, addr)
        if allocation is None:
            return
        number_attr = msg.get(A_CHANNEL_NUMBER)
        peer = decode_xor_address(msg.get(A_XOR_PEER_ADDRESS))
        if number_attr is None or len(number_attr) != 4 or peer is None:
            self._error(msg, addr, 400, "Bad Request", key)
            return
        number = struct.unpack("!H", number_attr[:2])[0]
        if not 0x4000 <= number <= 0x7FFE:
            self._error(msg, addr, 400, "Bad Request", key)
            return
        if not self._peer_allowed(peer[0]):
            self.stats["refused_peers"] += 1
            self._error(msg, addr, 403, "Forbidden", key)
            return
        bound = allocation.channels.get(number)
        other = allocation.peer_channels.get(peer)
        if (bound and bound[0] != peer) or (other is not None and other != number):
            self._error(msg, addr, 400, "Bad Request", key)
            return
        now = time.monotonic()
        allocation.channels[number] = (peer, now + CHANNEL_LIFETIME)
        allocation.peer_channels[peer] = number
        allocation.permissions[peer[0]] = now + PERMISSION_LIFETIME
        self._send(build(CHANNEL_BIND | SUCCESS, msg.tid, [attr(A_SOFTWARE, SOFTWARE)], key), addr)

    def _send_indication(self, msg, addr):
        allocation = self.allocations.get(addr)
        if allocation is None or not allocation.relay:
            return
        peer = decode_xor_address(msg.get(A_XOR_PEER_ADDRESS))
        payload = msg.get(A_DATA)
        if peer is None or payload is None:
            return
        self._to_peer(allocation, payload, peer)

    def _channel_data(self, data, addr):
        allocation = self.allocations.get(addr)
        if allocation is None or not allocation.relay or len(data) < 4:
            return
        number, length = struct.unpack_from("!HH", data)
        if 4 + length > len(data):
            return
        bound = allocation.channels.get(number)
        if bound is None:
            return
        self._to_peer(allocation, data[4:4 + length], bound[0])

    def _to_peer(self, allocation, payload, peer):
        now = time.monotonic()
        if not allocation.permitted(peer[0], now):
            return
        if not allocation.bucket.take(len(payload)):
            self.stats["rate_dropped"] += 1
            return
        allocation.relay.transport.sendto(payload, peer)
        self.stats["relayed_to_peer"] += 1
        self.stats["bytes_to_peer"] += len(payload)

    # --- datagrams from peers -------------------------------------------

    def from_peer(self, allocation, payload, peer):
        now = time.monotonic()
        if allocation.expiry <= now or not allocation.permitted(peer[0], now):
            return
        if not allocation.bucket.take(len(payload)):
            self.stats["rate_dropped"] += 1
            return
        number = allocation.peer_channels.get(peer)
        if number is not None:
            message = struct.pack("!HH", number, len(payload)) + payload
        else:
            message = build(DATA | INDICATION, os.urandom(12),
                            [xor_address(A_XOR_PEER_ADDRESS, peer[0], peer[1]), attr(A_DATA, payload)], fingerprint=False)
        self._send(message, allocation.client)
        self.stats["relayed_to_client"] += 1
        self.stats["bytes_to_client"] += len(payload)
