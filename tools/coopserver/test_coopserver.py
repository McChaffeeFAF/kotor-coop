"""Tests for the join-code service and the built-in relay: python -m unittest test_coopserver"""
import asyncio
import base64
import hashlib
import hmac
import json
import os
import socket
import struct
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coopserver  # noqa: E402
import joincode  # noqa: E402
import turnrelay as tr  # noqa: E402


def free_port(kind=socket.SOCK_STREAM):
    s = socket.socket(socket.AF_INET, kind)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class JoinCodeTest(unittest.TestCase):
    def test_generate(self):
        for _ in range(200):
            code = joincode.generate()
            self.assertEqual(len(code), 8)
            self.assertFalse(set(code) & set("01OIL"))
            self.assertEqual(joincode.normalize(code), code)

    def test_normalize(self):
        self.assertEqual(joincode.normalize("k7qm-4txr"), "K7QM4TXR")
        self.assertEqual(joincode.normalize("  K7QM4TXR "), "K7QM4TXR")
        self.assertIsNone(joincode.normalize("K7QM-4TX"))  # 7 characters
        self.assertIsNone(joincode.normalize("K7QM-4TX0"))  # look-alike
        self.assertIsNone(joincode.normalize("K7-QM-4TXR"))
        self.assertIsNone(joincode.normalize(None))
        self.assertIsNone(joincode.normalize("K7QM4TXRK7QM4TXRZ"))
        self.assertEqual(joincode.display("K7QM4TXR"), "K7QM-4TXR")


class Line:
    def __init__(self, reader, writer):
        self.reader, self.writer = reader, writer

    async def send(self, message):
        raw = message if isinstance(message, bytes) else (json.dumps(message) + "\n").encode()
        self.writer.write(raw)
        await self.writer.drain()

    async def recv(self, timeout=3.0):
        line = await asyncio.wait_for(self.reader.readline(), timeout)
        return json.loads(line) if line else None


class ServiceTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.port = free_port()
        self.args = coopserver.parse_args(["--listen", "127.0.0.1:%d" % self.port, "--turn-url", "relay.example:3478",
                                           "--turn-secret", "s3cret", "--stun-urls", "stun.example:3478",
                                           "--join-rate", "60", "--local-stats"])
        self.server = coopserver.Server(self.args)
        self.listener = await asyncio.start_server(self.server.serve, "127.0.0.1", self.port, limit=coopserver.MAX_LINE)

    async def asyncTearDown(self):
        self.listener.close()
        await self.listener.wait_closed()

    async def connect(self):
        reader, writer = await asyncio.open_connection("127.0.0.1", self.port)
        return Line(reader, writer)

    async def host(self, identity="kc-00000000000000aa", peers=2):
        h = await self.connect()
        await h.send({"op": "host", "v": 1, "id": identity, "max": peers})
        return h, await h.recv()

    async def test_host_join_signal(self):
        h, hosted = await self.host()
        self.assertEqual(hosted["op"], "hosted")
        code = hosted["code"]
        self.assertEqual(joincode.normalize(code), code)
        turn = hosted["ice"]["turn"][0]
        self.assertEqual(turn["url"], "relay.example:3478")
        expiry = int(turn["user"].split(":")[0])
        self.assertGreater(expiry, time.time() + 3600)
        expected = base64.b64encode(hmac.new(b"s3cret", turn["user"].encode(), hashlib.sha1).digest()).decode()
        self.assertEqual(turn["pass"], expected)
        self.assertEqual(hosted["ice"]["stun"], ["stun.example:3478"])

        c = await self.connect()
        typed = (code[:4] + "-" + code[4:]).lower()
        await c.send({"op": "join", "v": 1, "id": "kc-00000000000000bb", "code": typed})
        joined = await c.recv()
        self.assertEqual(joined["op"], "joined")
        self.assertEqual(joined["host"], "kc-00000000000000aa")
        peer = await h.recv()
        self.assertEqual(peer, {"op": "peer", "peer": joined["peer"], "id": "kc-00000000000000bb"})

        blob = base64.b64encode(b"\x01\x02rendezvous").decode()
        await c.send({"op": "signal", "data": blob})
        self.assertEqual(await h.recv(), {"op": "signal", "peer": joined["peer"], "data": blob})
        await h.send({"op": "signal", "peer": joined["peer"], "data": blob})
        self.assertEqual(await c.recv(), {"op": "signal", "data": blob})
        # A host's signal to a peer it does not have goes nowhere.
        await h.send({"op": "signal", "peer": 9999, "data": blob})
        await h.send({"op": "ping"})
        self.assertEqual(await h.recv(), {"op": "pong"})

        c.writer.close()
        self.assertEqual(await h.recv(), {"op": "peer_left", "peer": joined["peer"]})

    async def test_bad_code_and_errors(self):
        c = await self.connect()
        await c.send({"op": "join", "v": 1, "id": "kc-00000000000000bb", "code": "ZZZZ-ZZZZ"})
        self.assertEqual((await c.recv())["error"], "bad_code")
        self.assertIsNone(await c.recv())  # closed
        for message, reason in (({"op": "join", "v": 2, "id": "kc-00000000000000bb", "code": "x"}, "version"),
                                ({"op": "join", "v": 1, "id": "../etc", "code": "x"}, "bad_request"),
                                ({"op": "signal", "data": "AA=="}, "bad_request"),
                                ({"op": "host", "v": 1, "id": "kc-00000000000000aa", "max": 99}, "bad_request")):
            c = await self.connect()
            await c.send(message)
            self.assertEqual((await c.recv())["error"], reason, message)
        c = await self.connect()
        await c.send(b"this is not json\n")
        self.assertEqual((await c.recv())["error"], "bad_request")
        c = await self.connect()
        await c.send(b"x" * (coopserver.MAX_LINE + 10) + b"\n")
        self.assertEqual((await c.recv())["error"], "bad_request")

    async def test_full_lock_reissue_and_host_left(self):
        h, hosted = await self.host(peers=1)
        code = hosted["code"]
        c1 = await self.connect()
        await c1.send({"op": "join", "v": 1, "id": "kc-0000000000000001", "code": code})
        self.assertEqual((await c1.recv())["op"], "joined")
        await h.recv()
        c2 = await self.connect()
        await c2.send({"op": "join", "v": 1, "id": "kc-0000000000000002", "code": code})
        self.assertEqual((await c2.recv())["error"], "host_full")

        await h.send({"op": "reissue"})
        again = await h.recv()
        self.assertEqual(again["op"], "hosted")
        self.assertNotEqual(again["code"], code)
        c3 = await self.connect()
        await c3.send({"op": "join", "v": 1, "id": "kc-0000000000000003", "code": code})
        self.assertEqual((await c3.recv())["error"], "bad_code")  # the old code is dead

        await h.send({"op": "lock", "on": True})
        c1.writer.close()
        await h.recv()  # peer_left
        c4 = await self.connect()
        await c4.send({"op": "join", "v": 1, "id": "kc-0000000000000004", "code": again["code"]})
        self.assertEqual((await c4.recv())["error"], "host_full")
        await h.send({"op": "lock", "on": False})
        c5 = await self.connect()
        await c5.send({"op": "join", "v": 1, "id": "kc-0000000000000005", "code": again["code"]})
        self.assertEqual((await c5.recv())["op"], "joined")
        await h.recv()

        h.writer.close()
        self.assertEqual(await c5.recv(), {"op": "host_left"})
        c6 = await self.connect()
        await c6.send({"op": "join", "v": 1, "id": "kc-0000000000000006", "code": again["code"]})
        self.assertEqual((await c6.recv())["error"], "bad_code")  # the code died with the host

    async def test_join_rate_limit(self):
        self.server.limits.join_rate = 6
        reasons = []
        for i in range(12):
            c = await self.connect()
            await c.send({"op": "join", "v": 1, "id": "kc-%016x" % i, "code": "ZZZZZZZZ"})
            reasons.append((await c.recv())["error"])
        self.assertIn("rate_limited", reasons)
        self.assertEqual(reasons[0], "bad_code")

    async def test_code_expiry(self):
        self.server.limits.code_ttl = 0
        h, hosted = await self.host()
        for session in self.server.hosts:
            session.code_expires = time.monotonic() - 1
        self.server.expire_codes(time.monotonic())
        self.assertEqual(await h.recv(), {"op": "expired"})
        c = await self.connect()
        await c.send({"op": "join", "v": 1, "id": "kc-00000000000000bb", "code": hosted["code"]})
        self.assertEqual((await c.recv())["error"], "bad_code")


class Udp(asyncio.DatagramProtocol):
    def __init__(self):
        self.queue = asyncio.Queue()

    def datagram_received(self, data, addr):
        self.queue.put_nowait((data, addr))


class RelayTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.port = free_port(socket.SOCK_DGRAM)
        low = free_port(socket.SOCK_DGRAM)
        self.relay = tr.TurnRelay(tr.RelayConfig(secret="s3cret", listen_host="127.0.0.1", listen_port=self.port,
                                                 relay_bind="127.0.0.1", relay_ports=(low, low),
                                                 allow_private_peers=True, max_per_user=1))
        await self.relay.start()
        loop = asyncio.get_running_loop()
        self.client_t, self.client = await loop.create_datagram_endpoint(Udp, local_addr=("127.0.0.1", 0))
        self.peer_t, self.peer = await loop.create_datagram_endpoint(Udp, local_addr=("127.0.0.1", 0))

    async def asyncTearDown(self):
        self.client_t.close()
        self.peer_t.close()
        self.relay.close()

    async def request(self, method, attrs, key=None):
        tid = os.urandom(12)
        self.client_t.sendto(tr.build(method, tid, attrs, key), ("127.0.0.1", self.port))
        data, _ = await asyncio.wait_for(self.client.queue.get(), 3)
        msg = tr.parse(data)
        self.assertIsNotNone(msg)
        self.assertEqual(msg.tid, tid)
        return msg

    def error_code(self, msg):
        value = msg.get(tr.A_ERROR_CODE)
        return value[2] * 100 + value[3] if value else None

    async def test_binding(self):
        msg = await self.request(tr.BINDING, [])
        self.assertEqual(msg.cls, tr.SUCCESS)
        mapped = tr.decode_xor_address(msg.get(tr.A_XOR_MAPPED_ADDRESS))
        self.assertEqual(mapped, self.client_t.get_extra_info("sockname"))

    async def test_allocate_permission_send_data(self):
        transport = tr.attr(tr.A_REQUESTED_TRANSPORT, bytes([17, 0, 0, 0]))
        first = await self.request(tr.ALLOCATE, [transport])
        self.assertEqual(self.error_code(first), 401)
        realm, nonce = first.get(tr.A_REALM), first.get(tr.A_NONCE)
        user, password = tr.make_turn_credentials("s3cret", 600)
        key = hashlib.md5(("%s:%s:%s" % (user, realm.decode(), password)).encode()).digest()
        auth = [tr.attr(tr.A_USERNAME, user.encode()), tr.attr(tr.A_REALM, realm), tr.attr(tr.A_NONCE, nonce)]

        wrong = hashlib.md5(("%s:%s:%s" % (user, realm.decode(), "nope")).encode()).digest()
        self.assertEqual(self.error_code(await self.request(tr.ALLOCATE, [transport] + auth, wrong)), 401)

        ok = await self.request(tr.ALLOCATE, [transport] + auth, key)
        self.assertEqual(ok.cls, tr.SUCCESS, self.error_code(ok))
        self.assertTrue(tr.check_integrity(ok, key))
        relay_addr = tr.decode_xor_address(ok.get(tr.A_XOR_RELAYED_ADDRESS))
        self.assertEqual(relay_addr[0], "127.0.0.1")

        # A second allocation on the same 5-tuple is a mismatch.
        self.assertEqual(self.error_code(await self.request(tr.ALLOCATE, [transport] + auth, key)), 437)

        peer_addr = self.peer_t.get_extra_info("sockname")
        # Without a permission nothing is relayed.
        self.client_t.sendto(tr.build(tr.SEND | tr.INDICATION, os.urandom(12),
                                      [tr.xor_address(tr.A_XOR_PEER_ADDRESS, *peer_addr), tr.attr(tr.A_DATA, b"early")]),
                             ("127.0.0.1", self.port))
        perm = await self.request(tr.CREATE_PERMISSION, [tr.xor_address(tr.A_XOR_PEER_ADDRESS, *peer_addr)] + auth, key)
        self.assertEqual(perm.cls, tr.SUCCESS)
        self.client_t.sendto(tr.build(tr.SEND | tr.INDICATION, os.urandom(12),
                                      [tr.xor_address(tr.A_XOR_PEER_ADDRESS, *peer_addr), tr.attr(tr.A_DATA, b"hello")]),
                             ("127.0.0.1", self.port))
        data, src = await asyncio.wait_for(self.peer.queue.get(), 3)
        self.assertEqual(data, b"hello")
        self.assertEqual(src, relay_addr)

        self.peer_t.sendto(b"back", relay_addr)
        data, _ = await asyncio.wait_for(self.client.queue.get(), 3)
        indication = tr.parse(data)
        self.assertEqual(indication.type, tr.DATA | tr.INDICATION)
        self.assertEqual(indication.get(tr.A_DATA), b"back")
        self.assertEqual(tr.decode_xor_address(indication.get(tr.A_XOR_PEER_ADDRESS)), peer_addr)

        # Channels: bind, then ChannelData both ways.
        bind = await self.request(tr.CHANNEL_BIND, [tr.attr(tr.A_CHANNEL_NUMBER, struct.pack("!HH", 0x4001, 0)),
                                                    tr.xor_address(tr.A_XOR_PEER_ADDRESS, *peer_addr)] + auth, key)
        self.assertEqual(bind.cls, tr.SUCCESS)
        self.client_t.sendto(struct.pack("!HH", 0x4001, 3) + b"abc\x00", ("127.0.0.1", self.port))
        data, _ = await asyncio.wait_for(self.peer.queue.get(), 3)
        self.assertEqual(data, b"abc")
        self.peer_t.sendto(b"xyz", relay_addr)
        data, _ = await asyncio.wait_for(self.client.queue.get(), 3)
        self.assertEqual(data, struct.pack("!HH", 0x4001, 3) + b"xyz")

        refresh = await self.request(tr.REFRESH, [tr.attr(tr.A_LIFETIME, struct.pack("!I", 0))] + auth, key)
        self.assertEqual(refresh.cls, tr.SUCCESS)
        self.assertEqual(self.relay.stats["allocations_now"], 0)
        self.assertGreaterEqual(self.relay.stats["bytes_to_peer"], 8)

    async def test_expired_credentials(self):
        transport = tr.attr(tr.A_REQUESTED_TRANSPORT, bytes([17, 0, 0, 0]))
        first = await self.request(tr.ALLOCATE, [transport])
        realm, nonce = first.get(tr.A_REALM), first.get(tr.A_NONCE)
        user = "%d:old" % (int(time.time()) - 10)
        password = base64.b64encode(hmac.new(b"s3cret", user.encode(), hashlib.sha1).digest()).decode()
        key = hashlib.md5(("%s:%s:%s" % (user, realm.decode(), password)).encode()).digest()
        auth = [tr.attr(tr.A_USERNAME, user.encode()), tr.attr(tr.A_REALM, realm), tr.attr(tr.A_NONCE, nonce)]
        self.assertEqual(self.error_code(await self.request(tr.ALLOCATE, [transport] + auth, key)), 401)

    async def test_private_peers_refused_by_default(self):
        self.relay.config.allow_private_peers = False
        self.relay.config.relay_public_ip = "203.0.113.5"
        self.assertFalse(self.relay._peer_allowed("192.168.1.10"))
        self.assertFalse(self.relay._peer_allowed("127.0.0.1"))
        self.assertTrue(self.relay._peer_allowed("203.0.113.5"))
        self.assertTrue(self.relay._peer_allowed("8.8.8.8"))
        self.assertFalse(self.relay._peer_allowed("224.0.0.1"))

    def test_parse_rejects_garbage(self):
        self.assertIsNone(tr.parse(b""))
        self.assertIsNone(tr.parse(b"\x00\x01\x00\x04" + tr.MAGIC_BYTES + b"x" * 12))  # length past the end
        good = tr.build(tr.BINDING, b"t" * 12, [])
        self.assertIsNotNone(tr.parse(good))
        bad = bytearray(good)
        bad[-1] ^= 0xFF  # fingerprint broken
        self.assertIsNone(tr.parse(bytes(bad)))


if __name__ == "__main__":
    unittest.main()
