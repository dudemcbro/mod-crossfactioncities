"""A minimal headless WotLK 3.3.5a (build 12340) client for testing AzerothCore modules.

It logs in through the authserver (SRP6), connects to the worldserver, creates and logs in
characters, runs chat commands and records the packets the server sends. It does not
move, render or parse object updates: tests use GM commands, a few packets and the
database. Python 3.8 or newer, standard library only.
"""

import hashlib
import hmac
import os
import select
import socket
import struct
import time

BUILD = 12340

# Opcodes (3.3.5a)
CMSG_CHAR_CREATE = 0x036
CMSG_CHAR_ENUM = 0x037
SMSG_CHAR_CREATE = 0x03A
SMSG_CHAR_ENUM = 0x03B
CMSG_PLAYER_LOGIN = 0x03D
SMSG_NEW_WORLD = 0x03E
CMSG_ITEM_QUERY_SINGLE = 0x056
SMSG_ITEM_QUERY_SINGLE_RESPONSE = 0x058
CMSG_MESSAGECHAT = 0x095
SMSG_MESSAGECHAT = 0x096
MSG_MOVE_TELEPORT_ACK = 0x0C7
MSG_MOVE_WORLDPORT_ACK = 0x0DC
CMSG_AUTOEQUIP_ITEM = 0x10A
SMSG_INVENTORY_CHANGE_FAILURE = 0x112
CMSG_NPC_TEXT_QUERY = 0x17F
SMSG_NPC_TEXT_UPDATE = 0x180
SMSG_AUTH_CHALLENGE = 0x1EC
CMSG_AUTH_SESSION = 0x1ED
SMSG_AUTH_RESPONSE = 0x1EE
SMSG_LOGIN_VERIFY_WORLD = 0x236
SMSG_SET_FORCED_REACTIONS = 0x2A5
SMSG_ACHIEVEMENT_EARNED = 0x468

AUTH_OK = 0x0C
CHAR_CREATE_SUCCESS = 0x2F

CHAT_MSG_SYSTEM = 0x00
CHAT_MSG_SAY = 0x01
CHAT_MSG_MONSTER_SAY = 0x0C

LANG_ORCISH = 1
LANG_COMMON = 7

SRP_N = int("894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7", 16)
SRP_G = 7


class ClientError(Exception):
    pass


def sha1(*parts):
    digest = hashlib.sha1()
    for part in parts:
        digest.update(part)
    return digest.digest()


def int_le(data):
    return int.from_bytes(data, "little")


def bytes_le(value, length=32):
    return value.to_bytes(length, "little")


def make_verifier(username, password):
    """Salt and verifier for the auth.account table, as AzerothCore computes them."""
    salt = os.urandom(32)
    x = int_le(sha1(salt, sha1(("%s:%s" % (username.upper(), password.upper())).encode())))
    return salt, bytes_le(pow(SRP_G, x, SRP_N))


def sha1_interleave(s_bytes):
    buf0, buf1 = s_bytes[0::2], s_bytes[1::2]
    p = 0
    while p < len(s_bytes) and not s_bytes[p]:
        p += 1
    if p & 1:
        p += 1
    p //= 2
    hash0, hash1 = sha1(buf0[p:]), sha1(buf1[p:])
    return bytes(b for pair in zip(hash0, hash1) for b in pair)


def recv_exact(sock, size):
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ClientError("connection closed")
        data += chunk
    return data


def auth_login(host, port, username, password):
    """Logs in to the authserver and returns the 40-byte session key."""
    user = username.upper().encode()
    with socket.create_connection((host, port), timeout=10) as sock:
        body = struct.pack("<4sBBBH4s4s4sII", b"WoW\0", 3, 3, 5, BUILD, b"68x\0", b"niW\0", b"SUne", 0, 0x0100007F)
        body += struct.pack("<B", len(user)) + user
        sock.sendall(struct.pack("<BBH", 0x00, 0x08, len(body)) + body)

        cmd, _, error = struct.unpack("<BBB", recv_exact(sock, 3))
        if cmd != 0 or error != 0:
            raise ClientError("logon challenge failed (error %d)" % error)
        b_bytes = recv_exact(sock, 32)
        g_len = recv_exact(sock, 1)[0]
        g_bytes = recv_exact(sock, g_len)
        n_len = recv_exact(sock, 1)[0]
        n_bytes = recv_exact(sock, n_len)
        salt = recv_exact(sock, 32)
        recv_exact(sock, 16 + 1)  # version challenge, security flags

        n, g, b = int_le(n_bytes), int_le(g_bytes), int_le(b_bytes)
        x = int_le(sha1(salt, sha1(user + b":" + password.upper().encode())))
        a = int_le(os.urandom(19))
        a_pub = pow(g, a, n)
        a_bytes = bytes_le(a_pub)
        u = int_le(sha1(a_bytes, b_bytes))
        s = pow((b - 3 * pow(g, x, n)) % n, a + u * x, n)
        key = sha1_interleave(bytes_le(s))
        ng = bytes(p ^ q for p, q in zip(sha1(n_bytes), sha1(g_bytes)))
        m1 = sha1(ng, sha1(user), salt, a_bytes, b_bytes, key)

        sock.sendall(struct.pack("<B", 0x01) + a_bytes + m1 + b"\0" * 20 + b"\0\0")
        cmd, error = struct.unpack("<BB", recv_exact(sock, 2))
        if cmd != 1 or error != 0:
            raise ClientError("logon proof failed (error %d), wrong password?" % error)
        m2 = recv_exact(sock, 20)
        recv_exact(sock, 10)
        if m2 != sha1(a_bytes, m1, key):
            raise ClientError("server proof mismatch")
        return key


class Arc4:
    def __init__(self, key):
        state = list(range(256))
        j = 0
        for i in range(256):
            j = (j + state[i] + key[i % len(key)]) & 0xFF
            state[i], state[j] = state[j], state[i]
        self.state, self.i, self.j = state, 0, 0
        self.process(bytes(1024))

    def process(self, data):
        state, i, j = self.state, self.i, self.j
        out = bytearray(len(data))
        for index, byte in enumerate(data):
            i = (i + 1) & 0xFF
            j = (j + state[i]) & 0xFF
            state[i], state[j] = state[j], state[i]
            out[index] = byte ^ state[(state[i] + state[j]) & 0xFF]
        self.i, self.j = i, j
        return bytes(out)


SERVER_ENCRYPTION_KEY = bytes([0xCC, 0x98, 0xAE, 0x04, 0xE8, 0x97, 0xEA, 0xCA, 0x12, 0xDD, 0xC0, 0x93, 0x42, 0x91, 0x53, 0x57])
SERVER_DECRYPTION_KEY = bytes([0xC2, 0xB3, 0x72, 0x3C, 0xC6, 0xAE, 0xD9, 0xB5, 0x34, 0x3C, 0x53, 0xEE, 0x2F, 0x43, 0x67, 0xCE])


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, fmt):
        values = struct.unpack_from("<" + fmt, self.data, self.pos)
        self.pos += struct.calcsize("<" + fmt)
        return values if len(values) > 1 else values[0]

    def cstring(self):
        end = self.data.index(b"\0", self.pos)
        value = self.data[self.pos:end].decode("utf-8", "replace")
        self.pos = end + 1
        return value

    def packed_guid(self):
        mask = self.take("B")
        guid = 0
        for i in range(8):
            if mask & (1 << i):
                guid |= self.take("B") << (i * 8)
        return guid


def parse_chat(data):
    """(type, language, sender guid, text) of an SMSG_MESSAGECHAT."""
    reader = Reader(data)
    chat_type, language, sender, _flags = reader.take("BIQI")
    if chat_type in (0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x29, 0x2A, 0x2B):   # monster types
        reader.take("I")
        reader.cstring()
        receiver = reader.take("Q")
        if receiver and (receiver >> 48) not in (0x0000, 0xF140):
            reader.take("I")
            reader.cstring()
    else:
        reader.take("Q")
    length = reader.take("I")
    text = reader.data[reader.pos:reader.pos + length - 1].decode("utf-8", "replace")
    return chat_type, language, sender, text


class WorldClient:
    def __init__(self, host, port, username, session_key, log=print):
        self.username = username.upper()
        self.key = session_key
        self.log = log
        self.sock = socket.create_connection((host, port), timeout=10)
        self.buffer = b""
        self.header = b""
        self.encrypt = self.decrypt = None
        self.packets = []           # (time, opcode, data) of every packet received
        self.player_guid = None
        self._authenticate()

    # -- transport -------------------------------------------------------------------
    def _parse_packet(self):
        """Returns one complete (opcode, data) from the buffer, or None. Header bytes are
        decrypted as they arrive, so the cipher state never runs ahead of the stream."""
        while True:
            needed = 5 if self.header and self.header[0] & 0x80 else 4
            if len(self.header) >= needed:
                break
            if not self.buffer:
                return None
            byte, self.buffer = self.buffer[:1], self.buffer[1:]
            self.header += self.decrypt.process(byte) if self.decrypt else byte
        header = self.header
        if needed == 5:
            size = (header[0] & 0x7F) << 16 | header[1] << 8 | header[2]
        else:
            size = header[0] << 8 | header[1]
        opcode = struct.unpack_from("<H", header, needed - 2)[0]
        if len(self.buffer) < size - 2:
            return None
        data, self.buffer = self.buffer[:size - 2], self.buffer[size - 2:]
        self.header = b""
        return opcode, data

    def _read_packet(self, timeout):
        deadline = time.time() + timeout
        while True:
            packet = self._parse_packet()
            if packet:
                return packet
            remaining = deadline - time.time()
            if remaining <= 0:
                return None
            readable, _, _ = select.select([self.sock], [], [], remaining)
            if not readable:
                return None
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ClientError("world connection closed")
            self.buffer += chunk

    def send(self, opcode, body=b""):
        header = struct.pack(">H", len(body) + 4) + struct.pack("<I", opcode)
        if self.encrypt:
            header = self.encrypt.process(header)
        self.sock.sendall(header + body)

    def pump(self, seconds=0.0):
        """Reads packets for the given time, answering teleports. Returns the new packets."""
        deadline = time.time() + seconds
        new = []
        while True:
            packet = self._read_packet(max(0.0, deadline - time.time()))
            if packet is None:
                return new
            opcode, data = packet
            self.packets.append((time.time(), opcode, data))
            new.append((opcode, data))
            if opcode == SMSG_NEW_WORLD:
                self.send(MSG_MOVE_WORLDPORT_ACK)
            elif opcode == MSG_MOVE_TELEPORT_ACK:
                reader = Reader(data)
                guid = reader.packed_guid()
                counter = reader.take("I")
                self.send(MSG_MOVE_TELEPORT_ACK, pack_guid(guid) + struct.pack("<II", counter, int(time.time() * 1000) & 0xFFFFFFFF))

    def wait_for(self, opcode, timeout=10.0, predicate=None):
        deadline = time.time() + timeout
        while time.time() < deadline:
            for got, data in self.pump(min(0.5, max(0.0, deadline - time.time()))):
                if got == opcode and (predicate is None or predicate(data)):
                    return data
        return None

    # -- session ---------------------------------------------------------------------
    def _authenticate(self):
        opcode, data = self._read_packet(10) or (None, None)
        if opcode != SMSG_AUTH_CHALLENGE:
            raise ClientError("no auth challenge")
        server_seed = data[4:8]
        client_seed = os.urandom(4)
        digest = sha1(self.username.encode(), b"\0\0\0\0", client_seed, server_seed, self.key)
        body = struct.pack("<II", BUILD, 0) + self.username.encode() + b"\0"
        body += struct.pack("<I", 0) + client_seed + struct.pack("<IIIQ", 0, 0, 1, 0) + digest
        body += struct.pack("<I", 0)    # no addon data
        self.send(CMSG_AUTH_SESSION, body)
        self.decrypt = Arc4(hmac.new(SERVER_ENCRYPTION_KEY, self.key, hashlib.sha1).digest())
        self.encrypt = Arc4(hmac.new(SERVER_DECRYPTION_KEY, self.key, hashlib.sha1).digest())
        data = self.wait_for(SMSG_AUTH_RESPONSE, 15)
        if data is None or data[0] != AUTH_OK:
            raise ClientError("world auth failed: %r" % (data[:1] if data else None))

    def characters(self):
        self.send(CMSG_CHAR_ENUM)
        data = self.wait_for(SMSG_CHAR_ENUM)
        reader = Reader(data)
        result = []
        for _ in range(reader.take("B")):
            guid = reader.take("Q")
            name = reader.cstring()
            race, cls, gender = reader.take("BBB")
            reader.take("BBBBB")
            level = reader.take("B")
            reader.take("IIfffIIIBIII")
            reader.pos += 23 * 9
            result.append({"guid": guid, "name": name, "race": race, "class": cls, "level": level})
        return result

    def create_character(self, name, race, cls, gender=0):
        self.send(CMSG_CHAR_CREATE, name.encode() + b"\0" + bytes([race, cls, gender, 0, 0, 0, 0, 0, 0]))
        data = self.wait_for(SMSG_CHAR_CREATE)
        if not data or data[0] != CHAR_CREATE_SUCCESS:
            raise ClientError("character create failed: %r" % data)

    def login(self, guid):
        self.send(CMSG_PLAYER_LOGIN, struct.pack("<Q", guid))
        if self.wait_for(SMSG_LOGIN_VERIFY_WORLD, 20) is None:
            raise ClientError("login failed")
        self.player_guid = guid
        self.pump(2)

    def say(self, text, language):
        self.send(CMSG_MESSAGECHAT, struct.pack("<II", CHAT_MSG_SAY, language) + text.encode() + b"\0")

    def command(self, text, language, wait=2.0):
        """Runs a chat command and returns the system messages that came back."""
        start = len(self.packets)
        self.say(text, language)
        self.pump(wait)
        return [parse_chat(data)[3] for _, opcode, data in self.packets[start:]
                if opcode == SMSG_MESSAGECHAT and data[0] == CHAT_MSG_SYSTEM]

    def received(self, opcode, since=0.0):
        return [data for when, got, data in self.packets if got == opcode and when >= since]

    def close(self):
        self.sock.close()


def pack_guid(guid):
    mask, body = 0, b""
    for i in range(8):
        byte = (guid >> (i * 8)) & 0xFF
        if byte:
            mask |= 1 << i
            body += bytes([byte])
    return bytes([mask]) + body
