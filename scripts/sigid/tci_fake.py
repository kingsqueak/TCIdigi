#!/usr/bin/env python3
# tci_fake.py -- a stand-in TCI server for silent tests
# Copyright (C) 2026
# This file is part of fldigi. GPL-3.0-or-later.
"""A minimal TCI 2.x server on localhost, standard library only.

It lets TCIdigi run with no radio and no sound card. TX audio that the
client sends back for each TX_CHRONO is recorded in memory, and RX audio is
streamed from a WAV file or silence. Nothing is played on the computer and
nothing reaches a radio.

Audio on the wire is 48 kHz float32 stereo, 2048 frames per block, one
block every 42.67 ms in real time. TX_CHRONO asks for the same block size.

As a library (see gen_corpus.py):

    srv = FakeTci(port=50701); srv.start()
    srv.start_recording(); ...; samples = srv.stop_recording()

From the shell, to feed a WAV to TCIdigi as receive audio:

    tci_fake.py --port 50701 --play clip.wav [--loop]
"""

import argparse
import array
import base64
import hashlib
import socket
import struct
import sys
import threading
import time
import wave

RATE = 48000
BLOCK = 2048
RX_AUDIO, TX_AUDIO, TX_CHRONO = 1, 2, 3
FLOAT32 = 3
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

GREETING = (
    "protocol:ExpertSDR3,2.0;device:SunSDR2PRO;receive_only:false;"
    "trx_count:1;channels_count:2;vfo_limits:10000,30000000;"
    "if_limits:-24000,24000;"
    "modulations_list:AM,SAM,DSB,LSB,USB,CW,NFM,DIGL,DIGU,WFM,DRM;"
    "vfo:0,0,14070000;modulation:0,DIGU;rx_filter_band:0,100,3000;"
    "audio_samplerate:48000;audio_stream_sample_type:float32;"
    "audio_stream_channels:2;audio_stream_samples:2048;ready;"
)


def read_wav_mono(path):
    """Return (rate, list of floats in -1..1) from a 16-bit PCM WAV."""
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            raise ValueError("%s: only 16-bit PCM is supported" % path)
        ch = w.getnchannels()
        rate = w.getframerate()
        data = array.array("h", w.readframes(w.getnframes()))
    if sys.byteorder != "little":
        data.byteswap()
    return rate, [data[i] / 32768.0 for i in range(0, len(data), ch)]


def resample_linear(samples, rate_in, rate_out):
    """Resample for feeding test audio. An integer upsampling ratio (8 kHz
    to 48 kHz) gets a band-limited windowed-sinc interpolator, flat within
    0.2 dB to 0.875 of the input's Nyquist (3.5 kHz for 8 kHz audio); linear interpolation would tilt the
    passband by about 8 dB, which a real receiver does not. Other ratios
    fall back to linear interpolation."""
    if rate_out % rate_in == 0 and rate_out > rate_in:
        return upsample_bandlimited(samples, rate_out // rate_in)
    if rate_in == rate_out or not samples:
        return list(samples)
    step = rate_in / rate_out
    n = int(len(samples) / step)
    out = []
    last = len(samples) - 1
    for i in range(n):
        x = i * step
        k = int(x)
        f = x - k
        a = samples[k]
        b = samples[k + 1] if k < last else a
        out.append(a + (b - a) * f)
    return out


def upsample_bandlimited(samples, factor, taps_per_phase=48):
    """Polyphase windowed-sinc interpolation by an integer factor."""
    import math
    n = taps_per_phase * factor
    cutoff = 0.475 / factor         # of the output rate: 0.95 x input Nyquist
    proto = []
    for i in range(n):
        m = i - (n - 1) / 2.0
        s = 2 * cutoff if m == 0 else math.sin(2 * math.pi * cutoff * m) / (math.pi * m)
        w = 0.42 - 0.5 * math.cos(2 * math.pi * i / (n - 1)) + 0.08 * math.cos(4 * math.pi * i / (n - 1))
        proto.append(s * w * factor)
    phases = [proto[p::factor] for p in range(factor)]
    half = taps_per_phase // 2
    padded = [0.0] * half + list(samples) + [0.0] * half
    out = []
    for k in range(len(samples)):
        window = padded[k:k + taps_per_phase][::-1]
        for p in range(factor):
            out.append(sum(a * b for a, b in zip(phases[p], window)))
    return out


class FakeTci:
    def __init__(self, host="127.0.0.1", port=50701, rx_source=None, loop=False, log=None):
        if host not in ("127.0.0.1", "localhost", "::1"):
            raise ValueError("the fake TCI server only listens on localhost")
        self.host, self.port = host, port
        self.rx = rx_source or []  # mono floats at 48 kHz
        self.loop = loop
        self.rx_pos = 0
        self.log = log or (lambda msg: None)
        self.lock = threading.Lock()
        self.conn = None
        self.audio_on = False
        self.tx_on = False
        self.recording = None
        self.tx_frames = 0
        self.state = {"vfo": "0,0,14070000", "modulation": "0,DIGU",
            "rx_filter_band": "0,100,3000"}
        self.quit = False
        self.connected = threading.Event()

    # --- public ---------------------------------------------------------

    def start(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((self.host, self.port))
        self.sock.listen(1)
        threading.Thread(target=self._accept_loop, daemon=True).start()
        threading.Thread(target=self._pump, daemon=True).start()

    def stop(self):
        self.quit = True
        try:
            self.sock.close()
        except OSError:
            pass

    def start_recording(self):
        with self.lock:
            self.recording = array.array("f")

    def stop_recording(self):
        with self.lock:
            rec, self.recording = self.recording, None
        return rec if rec is not None else array.array("f")

    # --- websocket ------------------------------------------------------

    def _accept_loop(self):
        while not self.quit:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                return
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            try:
                self._serve(conn)
            except (OSError, ValueError) as e:
                self.log("connection ended: %s" % e)
            finally:
                with self.lock:
                    self.conn = None
                    self.audio_on = False
                    self.tx_on = False
                self.connected.clear()
                conn.close()

    def _serve(self, conn):
        req = b""
        while b"\r\n\r\n" not in req:
            chunk = conn.recv(4096)
            if not chunk:
                return
            req += chunk
        head, rest = req.split(b"\r\n\r\n", 1)
        key = None
        for line in head.decode("latin-1").split("\r\n"):
            if line.lower().startswith("sec-websocket-key:"):
                key = line.split(":", 1)[1].strip()
        if not key:
            raise ValueError("no Sec-WebSocket-Key")
        accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode()).digest()).decode()
        conn.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
            "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n" % accept).encode())
        with self.lock:
            self.conn = conn
        self.log("client connected")
        self._send_text(GREETING)
        self.connected.set()
        buf = bytearray(rest)
        while not self.quit:
            frame = self._next_frame(conn, buf)
            if frame is None:
                return
            opcode, payload = frame
            if opcode == 1:
                self._on_text(payload.decode("utf-8", "replace"))
            elif opcode == 2:
                self._on_binary(payload)
            elif opcode == 8:
                return
            elif opcode == 9:
                self._send(0xA, payload)

    def _next_frame(self, conn, buf):
        def need(n):
            while len(buf) < n:
                chunk = conn.recv(65536)
                if not chunk:
                    return False
                buf.extend(chunk)
            return True
        if not need(2):
            return None
        b0, b1 = buf[0], buf[1]
        n = b1 & 0x7F
        pos = 2
        if n == 126:
            if not need(4):
                return None
            n = struct.unpack(">H", buf[2:4])[0]
            pos = 4
        elif n == 127:
            if not need(10):
                return None
            n = struct.unpack(">Q", buf[2:10])[0]
            pos = 10
        masked = b1 & 0x80
        if masked:
            if not need(pos + 4):
                return None
            mask = buf[pos:pos + 4]
            pos += 4
        if not need(pos + n):
            return None
        payload = bytearray(buf[pos:pos + n])
        del buf[:pos + n]
        if masked:
            for i in range(n):
                payload[i] ^= mask[i & 3]
        return b0 & 0x0F, bytes(payload)

    def _send(self, opcode, data):
        n = len(data)
        if n < 126:
            hdr = struct.pack(">BB", 0x80 | opcode, n)
        elif n <= 0xFFFF:
            hdr = struct.pack(">BBH", 0x80 | opcode, 126, n)
        else:
            hdr = struct.pack(">BBQ", 0x80 | opcode, 127, n)
        with self.lock:
            conn = self.conn
        if conn is None:
            return
        try:
            conn.sendall(hdr + data)
        except OSError:
            pass

    def _send_text(self, s):
        self._send(1, s.encode())

    # --- TCI ------------------------------------------------------------

    def _on_text(self, msg):
        replies = []
        for cmd in msg.split(";"):
            cmd = cmd.strip()
            if not cmd:
                continue
            name, _, args = cmd.partition(":")
            name = name.lower()
            av = [a.strip() for a in args.split(",")] if args else []
            if name == "trx" and len(av) >= 2:
                on = av[1].lower() == "true"
                with self.lock:
                    self.tx_on = on
                self.log("trx %s" % ("on" if on else "off"))
                replies.append("trx:%s,%s;" % (av[0], "true" if on else "false"))
            elif name in ("audio_start", "line_out_start"):
                with self.lock:
                    self.audio_on = True
                replies.append(cmd + ";")
            elif name in ("audio_stop", "line_out_stop"):
                with self.lock:
                    self.audio_on = False
                replies.append(cmd + ";")
            elif name == "vfo":
                if len(av) >= 3:
                    self.state["vfo"] = ",".join(av[:3])
                replies.append("vfo:%s;" % self.state["vfo"])
            elif name in ("modulation", "rx_filter_band"):
                if len(av) >= (2 if name == "modulation" else 3):
                    self.state[name] = ",".join(av)
                replies.append("%s:%s;" % (name, self.state[name]))
            elif name == "modulations_list":
                replies.append("modulations_list:AM,SAM,DSB,LSB,USB,CW,NFM,DIGL,DIGU,WFM,DRM;")
            elif name.startswith("audio_"):
                replies.append(cmd + ";")
        if replies:
            self._send_text("".join(replies))

    def _on_binary(self, data):
        if len(data) < 64:
            return
        hdr = struct.unpack("<16I", data[:64])
        if hdr[6] != TX_AUDIO:
            return
        fmt, chans = hdr[2], hdr[7] or 2
        body = data[64:]
        if fmt == FLOAT32:
            samples = array.array("f")
            samples.frombytes(body[:len(body) - len(body) % 4])
        elif fmt == 0:
            raw = array.array("h")
            raw.frombytes(body[:len(body) - len(body) % 2])
            samples = array.array("f", (v / 32768.0 for v in raw))
        else:
            self.log("TX audio format %d not handled" % fmt)
            return
        if sys.byteorder != "little":
            samples.byteswap()
        mono = samples[0::chans]
        with self.lock:
            self.tx_frames += 1
            if self.recording is not None:
                self.recording.extend(mono)

    def _rx_block(self):
        out = array.array("f", bytes(4 * BLOCK * 2))
        if self.rx:
            for i in range(BLOCK):
                if self.rx_pos >= len(self.rx):
                    if not self.loop:
                        break
                    self.rx_pos = 0
                v = self.rx[self.rx_pos]
                self.rx_pos += 1
                out[2 * i] = v
                out[2 * i + 1] = v
        return out

    def _header(self, kind, length):
        return struct.pack("<16I", 0, RATE, FLOAT32, 0, 0, length, kind, 2,
            0, 0, 0, 0, 0, 0, 0, 0)

    def _pump(self):
        period = BLOCK / RATE
        due = time.monotonic()
        while not self.quit:
            due += period
            with self.lock:
                audio, tx, live = self.audio_on, self.tx_on, self.conn is not None
            if live and audio:
                block = self._rx_block()
                if sys.byteorder != "little":
                    block.byteswap()
                self._send(2, self._header(RX_AUDIO, BLOCK * 2) + block.tobytes())
            if live and tx:
                self._send(2, self._header(TX_CHRONO, BLOCK * 2))
            delay = due - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            else:
                due = time.monotonic()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--port", type=int, default=50701)
    ap.add_argument("--play", help="16-bit WAV to stream as receive audio")
    ap.add_argument("--loop", action="store_true", help="repeat the WAV")
    args = ap.parse_args()
    rx = []
    if args.play:
        rate, mono = read_wav_mono(args.play)
        rx = resample_linear(mono, rate, RATE)
    srv = FakeTci(port=args.port, rx_source=rx, loop=args.loop,
        log=lambda m: print("tci_fake: " + m, flush=True))
    srv.start()
    print("tci_fake: listening on 127.0.0.1:%d" % args.port, flush=True)
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        srv.stop()


if __name__ == "__main__":
    main()
