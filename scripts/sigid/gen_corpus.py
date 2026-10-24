#!/usr/bin/env python3
# gen_corpus.py -- clean test WAVs of every modem, made with no radio
# Copyright (C) 2026
# This file is part of fldigi. GPL-3.0-or-later.
"""Generate one clean WAV per modem for SigID tests.

Each worker starts fldigi with a throwaway configuration directory, with TCI
pointed at tci_fake.py on 127.0.0.1. No rig control, no PTT line, and no RsID
or other ID are set. fldigi's transmit audio comes back over the fake TCI
link and is written to disk: nothing reaches a sound card or a radio.

The script drives fldigi over XML-RPC: set the mode and carrier, queue the
text, transmit for --seconds, then abort. Output is 8 kHz 16-bit mono, plus
manifest.jsonl with one line per clip.

    gen_corpus.py --fldigi src/fldigi --out ~/sigid-corpus/clean --jobs 4
"""

import argparse
import array
import json
import math
import operator
import os
import random
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import wave
import xmlrpc.client

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tci_fake import FakeTci  # noqa: E402

OUT_RATE = 8000
DECIM = 6  # 48 kHz to 8 kHz

# Not signals, or nothing to send without extra input.
SKIP = {"NULL", "SSB", "WWV", "ANALYSIS", "FMT", "EOT", "WEFAX576", "WEFAX288",
    "OLIVIA", "CONTESTIA", "RSID_EOT",
    # Listed in the menu, but init_modem() has them commented out "pending
    # development work", so selecting one gives BPSK31.
    "OFDM500F", "OFDM750F", "OFDM3500"}

# FSQ always transmits at 1500 Hz, whatever the carrier is set to.
TX_FIXED_HZ = {"FSQ": 1500}

# Slow modes need longer clips to show their structure.
SLOW_SECONDS = {"THROB1": 60, "THROB2": 45, "THRBX1": 60, "THRBX2": 45,
    "DOMEXM": 60, "THORM": 60, "FSQ": 45, "MFSK4": 45,
    "SLOWHELL": 45, "SCFSKVSL": 45, "NAVTEX": 40, "SITORB": 40}

# fldigi's get_bandwidth() for each mode, in Hz, from SIGID-MODES.md. fldigi
# has no XML-RPC call for it. Used to keep each signal inside the 8 kHz
# band and, by check_corpus.py, to catch a clip of the wrong mode.
def _same(names, bw):
    return dict((n, bw) for n in names)


BANDWIDTH = {
    "CW": 150, "NAVTEX": 170, "SITORB": 170, "RTTY": 170, "FSQ": 290, "IFKP": 387,
    "MT63-500S": 500, "MT63-500L": 500, "MT63-1KS": 1000, "MT63-1KL": 1000,
    "MT63-2KS": 2000, "MT63-2KL": 2000,
    "DOMEXM": 36, "DOMEX4": 141, "DOMEX5": 194, "DOMEX8": 281, "DOMX11": 194,
    "DOMX16": 281, "DOMX22": 388, "DOMX44": 1550, "DOMX88": 1550,
    "THORM": 36, "THOR4": 141, "THOR5": 194, "THOR8": 281, "THOR11": 194,
    "THOR16": 281, "THOR22": 388, "THOR25": 450, "THOR32": 563, "THOR44": 775,
    "THOR56": 993, "THOR25x4": 1800, "THOR50x1": 900, "THOR50x2": 1800, "THOR100": 1800,
    "FELDHELL": 245, "SLOWHELL": 31, "HELLX5": 1225, "HELLX9": 2205,
    "FSKH245": 123, "FSKH105": 55, "HELL80": 300,
    "MFSK4": 121, "MFSK8": 242, "MFSK11": 162, "MFSK16": 234, "MFSK22": 323,
    "MFSK31": 219, "MFSK32": 469, "MFSK64": 938, "MFSK128": 1875,
    "MFSK64L": 938, "MFSK128L": 1875,
    "THROB1": 64, "THROB2": 64, "THROB4": 128, "THRBX1": 78, "THRBX2": 78, "THRBX4": 156,
    "SCAMPFSK": 133, "SCAMPOOK": 63, "SCFSKFST": 333, "SCFSKSLW": 69,
    "SCOOKSLW": 28, "SCFSKVSL": 35,
    "BPSK31": 31, "BPSK63": 63, "BPSK63F": 63, "BPSK125": 125, "BPSK250": 250,
    "BPSK500": 500, "BPSK1000": 1000,
    "QPSK31": 31, "QPSK63": 63, "QPSK125": 125, "QPSK250": 250, "QPSK500": 500,
    "PSK125R": 125, "PSK250R": 250, "PSK500R": 500, "PSK1000R": 1000,
    "PSK125C12": 2050, "PSK250C6": 2000, "PSK500C2": 1200, "PSK500C4": 2600,
    "PSK800C2": 1920, "PSK1000C2": 2400,
    "PSK63RC4": 325, "PSK63RC5": 413, "PSK63RC10": 850, "PSK63RC20": 1725,
    "PSK63RC32": 2775, "PSK125RC4": 650, "PSK125RC5": 825, "PSK125RC10": 1700,
    "PSK125RC12": 2050, "PSK125RC16": 2750, "PSK250RC2": 600, "PSK250RC3": 950,
    "PSK250RC5": 1650, "PSK250RC6": 2000, "PSK250RC7": 2350, "PSK500RC2": 1200,
    "PSK500RC3": 1900, "PSK500RC4": 2600, "PSK800RC2": 1920, "PSK1000RC2": 2400,
    "8PSK1200F": 1231, "OFDM500F": 438, "OFDM750F": 625, "OFDM3500": 3250,
}
BANDWIDTH.update(_same(("8PSK125", "8PSK125FL", "8PSK125F"), 125))
BANDWIDTH.update(_same(("8PSK250", "8PSK250FL", "8PSK250F"), 250))
BANDWIDTH.update(_same(("8PSK500", "8PSK500F"), 500))
BANDWIDTH.update(_same(("8PSK1000", "8PSK1000F"), 1000))
# Olivia and Contestia: (tones - 1) x nominal / tones.
for _t in (4, 8, 16, 32, 64):
    for _nom, _tag in ((125, "125"), (250, "250"), (500, "500"), (1000, "1K"), (2000, "2K")):
        _bw = round((_t - 1) * _nom / _t)
        BANDWIDTH["OLIVIA-%d/%s" % (_t, _tag)] = _bw
        BANDWIDTH["Cont-%d/%s" % (_t, _tag)] = _bw

TEXT = ("CQ CQ CQ de KC2RGW KC2RGW pse k  The quick brown fox jumps over the "
    "lazy dog 0123456789. Rig is a TS-590SG at 50 W into a dipole, WX is fair "
    "and 15 C here in FN30. Name Chris, QTH Long Island NY. 73 and good DX. ")

DEF_XML = """<FLDIGI_DEFS>
<CONFIRMEXIT>0</CONFIRMEXIT>
<TCI_ENABLE>1</TCI_ENABLE>
<TCI_AUDIO>1</TCI_AUDIO>
<TCI_HOST>127.0.0.1</TCI_HOST>
<TCI_PORT>{port}</TCI_PORT>
<TCI_RX>0</TCI_RX>
<RECEIVERSID>0</RECEIVERSID>
<TRANSMITRSID>0</TRANSMITRSID>
<CHKUSEHAMLIBIS>0</CHKUSEHAMLIBIS>
<CHKUSERIGCATIS>0</CHKUSERIGCATIS>
<CHKUSEXMLRPCIS>0</CHKUSEXMLRPCIS>
<RTSPTT>0</RTSPTT>
<DTRPTT>0</DTRPTT>
<USEUHROUTERPTT>0</USEUHROUTERPTT>
<MYCALL>KC2RGW</MYCALL>
</FLDIGI_DEFS>
"""


def lowpass_taps(n=191, cutoff=4500.0, rate=48000.0):
    """Blackman-windowed sinc, unity DC gain. Flat to about 3.8 kHz and
    about 74 dB down from 5.2 kHz, so nothing folds into the 8 kHz band."""
    m = n - 1
    fc = cutoff / rate
    taps = []
    for i in range(n):
        x = i - m / 2
        s = 2 * fc if x == 0 else math.sin(2 * math.pi * fc * x) / (math.pi * x)
        w = 0.42 - 0.5 * math.cos(2 * math.pi * i / m) + 0.08 * math.cos(4 * math.pi * i / m)
        taps.append(s * w)
    g = sum(taps)
    return [t / g for t in taps]


TAPS = lowpass_taps()


def decimate(samples):
    """48 kHz float samples to 8 kHz, filtered."""
    n = len(TAPS)
    return [sum(map(operator.mul, TAPS, samples[k:k + n]))
        for k in range(0, len(samples) - n, DECIM)]


def trim(samples, floor=1e-4):
    """Drop the silence TCI padding leaves at either end."""
    start = next((i for i, v in enumerate(samples) if abs(v) > floor), len(samples))
    end = len(samples)
    while end > start and abs(samples[end - 1]) <= floor:
        end -= 1
    return samples[start:end]


def write_wav(path, samples):
    peak = max((abs(v) for v in samples), default=0.0) or 1.0
    scale = 0.89 / peak  # about -1 dBFS
    data = array.array("h", (int(round(v * scale * 32767)) for v in samples))
    if sys.byteorder != "little":
        data.byteswap()
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(OUT_RATE)
        w.writeframes(data.tobytes())


class Worker:
    def __init__(self, index, args):
        self.index = index
        self.args = args
        self.tci_port = args.base_port + index * 10
        self.rpc_port = args.base_port + index * 10 + 1
        self.arq_port = args.base_port + index * 10 + 2
        self.proc = None
        self.stuck = False

    def log(self, msg):
        print("[w%d] %s" % (self.index, msg), flush=True)

    def start(self):
        self.cfg = tempfile.mkdtemp(prefix="sigid-gen-%d-" % self.index)
        self.srv = FakeTci(port=self.tci_port, log=self.log)
        self.stuck = False
        with open(os.path.join(self.cfg, "fldigi_def.xml"), "w") as f:
            f.write(DEF_XML.format(port=self.tci_port))
        self.srv.start()
        cmd = [self.args.fldigi, "--config-dir", self.cfg,
            "--xmlrpc-server-port", str(self.rpc_port),
            "--arq-server-port", str(self.arq_port)]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
            stderr=open(os.path.join(self.cfg, "stderr.txt"), "w"))
        self.rpc = xmlrpc.client.ServerProxy("http://127.0.0.1:%d/" % self.rpc_port)
        for _ in range(60):
            try:
                self.version = self.rpc.fldigi.version()
                break
            except (OSError, xmlrpc.client.Error):
                time.sleep(0.5)
        else:
            raise RuntimeError("fldigi did not answer XML-RPC on %d" % self.rpc_port)
        if not self.srv.connected.wait(20):
            raise RuntimeError("fldigi did not connect to the fake TCI server")
        time.sleep(2)  # let the audio subscription settle
        self.log("fldigi %s up, TCI %d, XML-RPC %d" % (self.version, self.tci_port, self.rpc_port))

    def stop(self):
        try:
            self.rpc.main.abort()
            self.rpc.fldigi.terminate(0)
        except (OSError, xmlrpc.client.Error):
            pass
        if self.proc:
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self.srv.stop()
        shutil.rmtree(self.cfg, ignore_errors=True)

    def restart(self):
        self.log("restarting fldigi")
        self.stop()
        self.start()

    def wait_for(self, what, test, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if test():
                return True
            time.sleep(0.2)
        self.log("timed out waiting for %s" % what)
        return False

    def clip(self, mode, rng):
        r = self.rpc
        r.main.abort()
        r.main.rx()
        if not self.wait_for("RX", lambda: r.main.get_trx_state() == "RX", 5):
            raise Stuck("still transmitting before %s" % mode)
        r.modem.set_by_name(mode)
        # NAVTEX once left fldigi transmitting and deaf to mode changes, so
        # never record until the modem really is the one asked for.
        if not self.wait_for(mode, lambda: r.modem.get_name() == mode, 3):
            raise Stuck("modem is %s, not %s" % (r.modem.get_name(), mode))
        time.sleep(1.0)
        if mode not in BANDWIDTH:
            raise KeyError("no bandwidth for %s; add it to BANDWIDTH" % mode)
        bw = float(BANDWIDTH[mode])
        lo = max(300.0, bw / 2 + 150)
        hi = min(3500.0, 3750.0 - bw / 2)
        carrier = int(rng.uniform(lo, hi)) if hi > lo else 1500
        r.modem.set_carrier(carrier)
        carrier = TX_FIXED_HZ.get(mode, r.modem.get_carrier())
        seconds = SLOW_SECONDS.get(mode.upper(), self.args.seconds)
        r.text.clear_tx()
        r.text.add_tx(TEXT * 100)  # enough that no mode runs dry and idles
        self.srv.start_recording()
        r.main.tx()
        time.sleep(seconds + 1.0)
        r.main.abort()
        r.main.rx()
        raw = self.srv.stop_recording()
        if not self.wait_for("RX after %s" % mode, lambda: r.main.get_trx_state() == "RX", 5):
            self.stuck = True
        samples = trim(decimate(raw))
        if len(samples) < OUT_RATE:
            self.log("%s: only %d samples, skipped" % (mode, len(samples)))
            return None
        safe = "".join(c if c.isalnum() or c in "-_" else "_" for c in mode)
        name = "%s.wav" % safe
        write_wav(os.path.join(self.args.out, name), samples)
        return {"file": name, "mode": mode, "carrier_hz": carrier, "bandwidth_hz": bw,
            "seconds": round(len(samples) / OUT_RATE, 2), "rate": OUT_RATE,
            "snr_db": None, "channel": "clean", "source": "fldigi %s" % self.version}


class Stuck(Exception):
    pass


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--fldigi", required=True, help="path to the FLTK fldigi binary")
    ap.add_argument("--out", required=True)
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--seconds", type=float, default=20)
    ap.add_argument("--base-port", type=int, default=50700)
    ap.add_argument("--modes", help="comma-separated list (default: all)")
    ap.add_argument("--list", action="store_true", help="print mode names and exit")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    args.out = os.path.expanduser(args.out)
    os.makedirs(args.out, exist_ok=True)

    first = Worker(0, args)
    workers = [first]
    try:
        first.start()
        names = list(first.rpc.modem.get_names())
        if args.list:
            print("\n".join(names))
            return
        modes = args.modes.split(",") if args.modes else [n for n in names if n.upper() not in SKIP]
        for i in range(1, max(1, args.jobs)):
            w = Worker(i, args)
            workers.append(w)
            w.start()
        queue = list(modes)
        qlock = threading.Lock()
        mlock = threading.Lock()
        manifest = open(os.path.join(args.out, "manifest.jsonl"), "a")

        def run(w):
            rng = random.Random(args.seed * 1000 + w.index)
            while True:
                with qlock:
                    if not queue:
                        return
                    mode = queue.pop(0)
                entry = None
                for attempt in (1, 2):
                    try:
                        entry = w.clip(mode, rng)
                        break
                    except Stuck as e:
                        w.log("%s: %s" % (mode, e))
                        w.restart()
                    except (OSError, xmlrpc.client.Error) as e:
                        w.log("%s failed: %s" % (mode, e))
                        break
                if w.stuck:
                    w.log("%s did not return to RX" % mode)
                    w.restart()
                if entry:
                    with mlock:
                        manifest.write(json.dumps(entry) + "\n")
                        manifest.flush()
                    w.log("%s: %.1f s at %d Hz" % (mode, entry["seconds"], entry["carrier_hz"]))

        threads = [threading.Thread(target=run, args=(w,)) for w in workers]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        manifest.close()
    finally:
        for w in workers:
            w.stop()


if __name__ == "__main__":
    main()
