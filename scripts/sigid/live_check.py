#!/usr/bin/env python3
# live_check.py -- play a WAV into fldigi through the fake TCI server
# Copyright (C) 2026
# This file is part of fldigi. GPL-3.0-or-later.
"""Run fldigi on a WAV as receive audio and print what SigID logs.

fldigi starts with a throwaway configuration: TCI points at tci_fake.py on
127.0.0.1 (no radio, no sound card), SigID is on, squelch is off, and the
event log records everything. The WAV loops as receive audio for
--seconds, then fldigi is closed and the SigID lines from its log are
printed. The configuration is deleted unless --keep is given.

    live_check.py --fldigi src/fldigi clip.wav [--seconds 25] [--keep]
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
import xmlrpc.client

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tci_fake import FakeTci, read_wav_mono, resample_linear  # noqa: E402
from gen_corpus import DEF_XML  # noqa: E402

# fldigi ignores fldigi.prefs unless it has version and dual_channels.
PREFS = """; FLTK preferences file format 1.0
; vendor: w1hkj.org
; application: fldigi

[.]

version:4.2.13
dual_channels:YES
debug_mask:65535
debug_level:3
sqlonoff:0
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("wav")
    ap.add_argument("--fldigi", required=True)
    ap.add_argument("--seconds", type=float, default=25)
    ap.add_argument("--port", type=int, default=50950)
    ap.add_argument("--keep", action="store_true", help="keep the configuration folder")
    args = ap.parse_args()

    rate, mono = read_wav_mono(args.wav)
    srv = FakeTci(port=args.port, rx_source=resample_linear(mono, rate, 48000), loop=True)
    srv.start()
    cfg = tempfile.mkdtemp(prefix="sigid-live-")
    with open(os.path.join(cfg, "fldigi_def.xml"), "w") as f:
        f.write(DEF_XML.format(port=args.port).replace(
            "</FLDIGI_DEFS>", "<SIGID>1</SIGID>\n</FLDIGI_DEFS>"))
    with open(os.path.join(cfg, "fldigi.prefs"), "w") as f:
        f.write(PREFS)
    proc = subprocess.Popen([args.fldigi, "--config-dir", cfg,
                             "--xmlrpc-server-port", str(args.port + 1),
                             "--arq-server-port", str(args.port + 2)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    rpc = xmlrpc.client.ServerProxy("http://127.0.0.1:%d/" % (args.port + 1))
    try:
        for _ in range(60):
            try:
                rpc.fldigi.version()
                break
            except (OSError, xmlrpc.client.Error):
                time.sleep(0.5)
        if not srv.connected.wait(20):
            sys.exit("fldigi did not connect to the fake TCI server")
        rpc.main.set_squelch(False)
        time.sleep(args.seconds)
    finally:
        try:
            rpc.fldigi.terminate(0)
        except (OSError, xmlrpc.client.Error):
            pass
        try:
            proc.wait(10)
        except subprocess.TimeoutExpired:
            proc.kill()
        srv.stop()
    log = os.path.join(cfg, "debug", "status_log.txt")
    lines = open(log, errors="replace").read().splitlines() if os.path.exists(log) else []
    hits = [line.strip() for line in lines if line.strip().startswith("SigID:")]
    print("\n".join(hits) if hits else "no SigID lines in %s" % log)
    if args.keep:
        print("configuration kept in %s" % cfg)
    else:
        shutil.rmtree(cfg, ignore_errors=True)


if __name__ == "__main__":
    main()
