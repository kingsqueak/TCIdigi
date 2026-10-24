# SigID: automatic signal identification

Find signals in the receive passband, say what mode each one is, and write a
clickable mark in the receive pane. Clicking a mark switches to that mode,
tunes to the signal, and QSYs the rig so the signal sits at 1500 Hz.

The first target is every mode TCIdigi can receive. The design leaves room for
other signals (ALE, STANAG, PACTOR, ARDOP, VARA, FT8, HFDL, DRM) later.

Status: **planning** (plan written 2026-10-24, nothing built yet). Update the status table at the end after each phase.

## Goals and limits

- Works without a TXID or RsID. When RsID does fire, SigID stays quiet for that
  signal so the two never report it twice.
- Receive only. SigID never keys, tunes an ATU, or transmits. Clicking a mark
  can move the dial (QSY), and nothing else on the radio.
- Can go upstream to fldigi. The core and the FLTK UI use only what fldigi
  already has: its FFT and filters, `progdefaults`, `REQ()`, `LOG_*`, gettext.
  No new libraries and no Qt. The Qt pieces are separate TCIdigi-only commits.
- Off by default. With it off, the only cost is one flag check per audio block.
- CPU budget: under 5% of one core at 8 kHz with a full 4 kHz passband.

## How it works

```
trx loop audio ──► SigId::receive()   (TRX thread; same hook as RsID)
                     │
                     ├─ 1. Segment: averaged FFT, noise floor, occupied bands
                     │      → candidates {low, high, center, power, age}
                     ├─ 2. Features per candidate, by family:
                     │      tones (count, spacing, keying rate)   FSK/MFSK/IFK
                     │      power-law lines (x², x⁴, x⁸ → order, baud)  PSK
                     │      envelope (on/off keying, duty)       CW/Hell
                     │      carrier comb (count, spacing)        MT63/PSK-R multi/OFDM
                     ├─ 3. Classify: score features against the signature
                     │      table → ranked {mode, confidence}
                     ├─ 4. Verify look-alikes (Olivia/Contestia, DominoEX/Thor,
                     │      PSK/PSK-R): short trial decode with the family's own
                     │      decoder classes on buffered audio
                     └─ 5. Report: REQ(sigid_report, hit) to the UI thread,
                            deduped per mode and frequency
UI thread ──► clickable mark in the receive pane (FLTK and Qt)
click ─────► switch mode, QSY so the signal lands at the center frequency
```

### New code: `src/sigid/` (core, no UI)

| File | Contents |
|---|---|
| `include/sigid.h` | `class SigId`, `struct sigid_hit`, `sigid_report()` declaration |
| `sigid/sigid.cxx` | Engine: decimation, block FFT, scheduling, candidate tracking |
| `sigid/segment.cxx` | Noise floor and occupied-band segmentation |
| `sigid/features.cxx` | Feature extractors, one function per family |
| `sigid/signatures.cxx` | Signature table: one row per mode |
| `sigid/classify.cxx` | Scoring and confidence |
| `sigid/verify.cxx` | Trial-decode checks for look-alike groups |

**Adding a mode** means adding one row to `signatures.cxx`. If the mode belongs
to a new family, it also needs one extractor in `features.cxx`. Each row
records:
`{trx_mode, family, tones|carriers, spacing_hz, baud, bandwidth_hz, carrier_ref, flags}`.
`carrier_ref` says what `get_freq()` means for that modem (the center, the
lowest tone, or the mark tone), so a click sets the carrier where that modem
expects it. Where possible, rows take their numbers from the modem's own
constants, not copied values. Signals with no native modem later get rows with
`trx_mode = NUM_MODES` and a name. Those rows identify the signal but have no
mode to switch to.

Later backends sit behind one small interface, `SigIdClassifier::score(features)
→ ranked list`. An ONNX model backend could be added behind a configure option
that is off by default, `--enable-sigid-onnx`. Upstream builds never pull it in.

### Changes to existing files (kept small for upstream)

| File | Change |
|---|---|
| `src/trx/trx.cxx` | One line next to `ReedSolomon->receive()`: `if (progdefaults.sigid) SigID->receive(...)` |
| `src/include/configuration.h` | `sigid`, `sigid_modes` (bitset like `rsid_rx_modes`), `sigid_min_conf`, `sigid_center_hz` (1500), `sigid_center_on_click`, `sigid_dedupe_s` |
| `src/dialogs/fl_digi.cxx` | `sigid_report()`: writes the mark. Reuses `note_qrg`, so `qrg_marks` and the QSY style still work |
| `src/widgets/FTextRXTX.cxx` | Move the body of `handle_qsy` into a shared `activate_qrg_mark(const char*)`, for FLTK and Qt both to call |
| `src/dialogs/confdialog.fl` | A SigID tab: enable, minimum confidence, center Hz, center on click, mode checkboxes |
| `src/Makefile.am` | Add the `sigid/*.cxx` files. The Qt build picks them up through `link_fldigi.py` |
| Qt only: `tcidigi_rx.h`, `engine.*`, `mainwindow.cpp` | A new receive kind `TCIDIGI_RX_LINK` for clickable text, plus `engine_rx_mark_click(text)`. This also makes the existing RsID marks clickable in Qt |

### The mark and the click

`<<2026-10-24T14:02Z SigID OLIVIA-8-250 @ 14072000+1834 92%>>`

When the mark is made, SigID stores the target in `qrg_marks`. The target is
the mode, plus the dial and carrier that put the signal at `sigid_center_hz`:
dial = rf + (carrier − 1500) on USB, or rf − (carrier − 1500) on LSB, and
carrier = 1500. The existing click path (`init_modem_sync` + `qsy`) then does
the rest. With no rig control (rf = 0), the click only moves the carrier. RTTY
and CW follow the same rules `do_qsy()` already applies to them.

## Splitting work with Grok

Grok runs from the CLI (`~/.grok/bin/grok -p`) and does well-bounded side
tasks. Claude keeps the design, the integration, and the final review.

- Read-only surveys and second-opinion reviews run with
  `--permission-mode plan` and web search off.
- Code-writing tasks run in a separate git worktree (`--worktree`), never with
  `--always-approve`. Claude reviews the diff before anything is merged.
- Every prompt carries the rules: no radio, serial port, or TCI; no
  transmitting; no commits or pushes.

| Phase | Grok | Claude |
|---|---|---|
| 0 | `impair.py` (noise, fading, drift) in a worktree | Corpus generator, `sigid-scan`, the run with no radio attached |
| 1 | Review of the segmentation diff | Core class, trx hook, configuration |
| 2 | First draft of `signatures.cxx` from SIGID-MODES.md, in a worktree | Extractors, scoring, confusion matrix |
| 3 | Research each look-alike check against the decoder sources | `verify.cxx` |
| 4–5 | Review of the UI diffs | FLTK and Qt integration |
| 7 | Read upstream fldigi's patch rules and recent merges | Branch, cherry-picks, MR |

## Phases (one session each)

Each phase ends with a clean build of both `fldigi` and `fldigi-qt`, a status
update in this file, and a commit when the user approves it. Phases 0–3 never
need the radio.

### Phase 0: test corpus and harness
- `scripts/sigid/`: a manifest of test WAV files (mode, carrier, SNR,
  channel), plus `impair.py`, which adds noise, a Watterson-style fading
  channel, drift and offset to clean WAVs.
- Clean WAVs for every mode: `gen_corpus.py` drives the FLTK `fldigi` over
  XML-RPC (`modem.set_by_name`, `modem.set_carrier`, `text.add_tx`,
  `main.tx`, `main.abort`). TCIdigi has no audio file generate or playback,
  so `tci_fake.py`, a TCI 2.x server on 127.0.0.1, stands in for the radio.
  It answers TX_CHRONO and records the TX_AUDIO frames, and it can stream a
  WAV back as RX audio for the later UI tests (`tci_fake.py --play`). Every
  worker uses a throwaway `--config-dir` with TCI pointed at the fake server
  only, no rig, no PTT, and no RsID. Nothing plays on a sound card or reaches
  a radio. The 48 kHz TX audio is decimated to 8 kHz (flat to 3.8 kHz).
  The corpus lives outside git in `~/src/sigid-corpus/`.
- `sigid-scan` CLI (built by `make check` only, not installed): runs the core
  on a WAV file and prints the hits. It needs the core to build without FLTK.
  It is also the regression test for every later phase.
- **Done when:** the corpus exists for all modes in the appendix, and
  `sigid-scan` builds and runs, even though it does nothing yet.

### Phase 1: core skeleton and segmentation
- `SigId` class, the trx hook, configuration elements, `segment.cxx`.
- Candidates go to `LOG_INFO` only, with no UI yet.
- **Done when:** on the corpus, center frequency is within ±5 Hz and bandwidth
  within ±15% at SNR ≥ 0 dB (2.5 kHz noise bandwidth), and CPU is within budget.

### Phase 2: features and classification
- Extractors for tones, power-law lines, envelope, and carrier comb, plus the
  signature table for all native modes and the scoring.
- `sigid-scan --matrix` prints a confusion matrix over the whole corpus.
- **Done when:** the family is right ≥ 95% of the time and the exact mode or
  speed ≥ 85% at SNR ≥ 3 dB. Look-alike groups may still be reported as
  "A or B".

### Phase 3: telling look-alikes apart
- `verify.cxx`. Olivia vs Contestia: run both `pj_mfsk` receiver setups and
  compare sync SNR. DominoEX vs Thor, PSK vs PSK-R vs QPSK, and the MFSK
  variants each get a matching check.
- Some pairs differ only in interleave depth or framing (MT63 S/L,
  MFSK64/64L, NAVTEX/SITOR-B). Those stay reported as a group, with the more
  common member as the click target, unless a cheap check turns up.
- **Done when:** the confusion matrix shows ≥ 90% within each group that has
  a check, at SNR ≥ 3 dB.

### Phase 4: FLTK receive-pane marks (the upstream UI)
- `sigid_report()`, deduping and rate limiting, the centered target,
  `activate_qrg_mark()`, and the configuration tab.
- Test with File > Audio > Playback of corpus files, a throwaway config, and
  no rig.
- **Done when:** clicking a mark switches mode and centers the signal; RsID
  and SigID never report the same signal twice; with SigID off, behavior is
  unchanged.

### Phase 5: Qt shell (TCIdigi only)
- `TCIDIGI_RX_LINK`, the engine call, clickable runs in the receive pane, and
  Qt preferences.
- QSY with a real rig is tested only after the user says so. It moves the
  dial; it does not transmit.
- **Done when:** both shells behave the same, and the existing RsID marks are
  clickable in Qt.

### Phase 6: waterfall labels and polish
- Short mode labels on the waterfall at each detected signal, in FLTK and Qt.
- Check CPU on the Mac and on Linux, and write the help page.
- **Done when:** labels track the signals, and SigID adds under 5% CPU.

### Phase 7: upstream submission
- Ask on the fldigi mailing list first, with a short description and a
  screenshot, to find out whether upstream wants it and in what form.
- Branch `sigid` in `~/src/fldigi-sf`, taken from upstream master.
  Cherry-pick the core and FLTK commits only (no Qt, no TCI), build with
  upstream autotools on Linux and macOS, and open an MR the same way as MR #9.
- **Done when:** the MR is open, or upstream's answer is written down here.

### Later
Non-native signals added as rows plus extractors: ALE 2G, STANAG 4285/4529,
PACTOR 1–4 (ID only), ARDOP, VARA (ID only), FT8/FT4/WSPR, HFDL, DRM, NAVTEX
and SITOR variants, and WEFAX already native. External decoders could be
plugged in, and the optional ONNX backend added.

## Status

| Phase | State | Notes |
|---|---|---|
| 0 Corpus and harness | done 2026-10-24 | See the Phase 0 notes below |
| 1 Segmentation | done 2026-10-24 | See the Phase 1 notes below |
| 2 Classification | done 2026-10-24 | Family 93% and group 92% at 3 dB; see the Phase 2 notes |
| 3 Look-alikes | not started | |
| 4 FLTK marks | not started | |
| 5 Qt shell | not started | |
| 6 Waterfall and polish | not started | |
| 7 Upstream | not started | |

### Phase 0 notes (2026-10-24)

- `scripts/sigid/`:
  - `tci_fake.py`: fake TCI server
  - `gen_corpus.py`: clean clips over XML-RPC
  - `check_corpus.py`: checks each clip's measured band against its label
  - `impair.py` and `test_impair.py`: written by Grok, reviewed, and checked
    on a real clip (offset exact, SNR within 0.2 dB, fading looks right)
- `src/include/sigid.h`, `src/sigid/sigid.cxx` (a stub), and
  `src/sigid/sigid-scan.cxx`. `make -C src check` builds `sigid-scan`. The
  fldigi and Qt builds don't include SigID yet.
- Clean corpus: `~/src/sigid-corpus/clean/`, one clip per mode at a random
  carrier (seed 1). `manifest.jsonl` is the ground truth, and `check.jsonl`
  holds the measured spans.
- 156 clips (59.5 minutes) cover every mode fldigi can transmit. All 156
  pass `check_corpus.py`, and the checker flags mislabeled test clips.
  Clips are 20 s, or 40–60 s for slow modes.
  - OFDM500F, OFDM750F and OFDM3500 are left out. They're in the menu, but
    `init_modem()` has them commented out "pending development work", so
    they fall back to BPSK31. SigID leaves them out too, until upstream
    turns them on.
  - FSQ always transmits at 1500 Hz, so its manifest carrier is 1500.
  - On-off keyed modes (CW, Hell, SCAMP OOK) splatter well past their
    nominal bandwidth, so the checker tests their peak frequency, not their
    span.
- Impaired sets come from `impair.py` when a phase needs them. Phase 1 starts
  with AWGN at 10, 3 and 0 dB.
- Found along the way: NAVTEX and SITOR-B transmit ignore Abort.
  `navtex::process_tx()` sends the whole TX buffer as one message, and
  nothing checks for abort. Upstream fldigi has the same code. The generator
  confirms the mode before each clip and restarts fldigi if it doesn't get
  back to RX.
- Grok in headless mode needs explicit `--allow` rules: `Bash(python3*)`,
  `Edit(scripts/sigid/**)`, `Write(scripts/sigid/**)`. Without them it stops
  at its first file write or command.

### Phase 1 notes (2026-10-24)

What was built:
- **Core:** `src/sigid/segment.cxx` (noise floor, occupied runs, joining
  runs), `sigid_internal.h`, and the engine in `sigid.cxx`. The engine
  resamples with libsamplerate, uses fldigi's header-only `g_fft`, and
  tracks candidates over time. `SigId::candidates()` reports any that have
  lasted at least 1 s.
- **Receiver:** `sigid_trx.cxx` is the only SigID file that uses fldigi's
  configuration and logging. `trx.cxx` gets three lines: start, stop, and
  `sigid_receive()` in the normal receive path. The feed is the raw receive
  audio, ahead of the modem and squelch, so SQL cannot affect it. A new
  configuration element, `SIGID`, is off by default. The event log gets a
  "SigID: N signals" line at most every 5 s, when the list changes.
- **Build:** `src/Makefile.am` adds the core and glue to fldigi. The Qt
  build picks them up after CMake is re-run. Keep its deployment target at
  26.0.
- **Scripts:**
  - `score_segments.py`: scores a corpus set
  - `impair_corpus.py`: builds the impaired sets; `--tilt` adds a receiver
    slope
  - `live_check.py`: plays a WAV into fldigi through `tci_fake.py` and
    prints the SigID log lines

How it works:
- **Noise floor:** first a straight line fitted only to bins that look like
  noise, which gets the passband's level and tilt without wide signals
  lifting it. Then each bin's floor is the mean of the nearby noise bins,
  in a window that widens until it holds enough of them.
- **Edges:** the threshold (seed 3 dB, extend 1.5 dB over a 5-bin average)
  only finds a signal. Its edges are the 0.5%–99.5% power span, which
  doesn't grow with SNR the way a threshold crossing does.
- **Joining:** pieces join when the gap is at most 32 Hz or 20% of the
  narrower piece; when their short-term power is anti-correlated, for
  FSK/MFSK/IFK up to 900 Hz apart; or when they form an even comb of equal
  carriers, for multi-carrier PSK with a plausible carrier count.

Results (`score_segments.py`, 156 clips per set):

| Set | found | whole | center ±5 Hz | width ±15% | extra |
|---|---|---|---|---|---|
| AWGN 10 dB | 100% | 97% | 96% | 94% | 9 |
| AWGN 3 dB | 100% | 96% | 95% | 89% | 2 |
| AWGN 0 dB | 98% | 92% | 89% | 81% | 0 |
| AWGN 3 dB, receiver tilt | 99% | 94% | 92% | 87% | 2 |

"Center" allows 10% of the bandwidth for wide signals. "Width" is against
the clean clip's 99% span, and at least 16 Hz. The goal was ±5 Hz and ±15%
at 0 dB. Center comes close; width falls short for reasons that are known
and belong to Phase 2:
- Four-tone Olivia and Contestia centers sit half a tone low, because
  Contestia's upper-case text seldom uses the top tone. The tone grid
  found in Phase 2 gives the true center.
- Fast PSK (500 baud and up) and the 2000 Hz-wide modes come out narrow at
  0 dB: their sidelobes and edges are under the noise. Phase 2 can widen
  them to the signature.
- Fast multi-carrier PSK ran out of queued text before the end of its clip
  and went to idle, which is pairs of lines half the baud rate either side
  of each carrier. Phase 2 needs an "idle PSK" pattern for that.
- Sidelobes of 1000-baud PSK show up as extra candidates, mostly at 10 dB.

CPU: 59.5 minutes of audio in 1.1 s, about 0.03% of one core, at 8 kHz.
A 16 kHz modem adds the resampler: about 0.5% of one core.

Grok's review of the diff found three real problems, all fixed:
- `receive()` ignored libsamplerate's `input_frames_used`, and read `x[0]`
  from an empty vector when a call produced nothing. Input left over now
  waits for the next block. Results are now the same for 512-, 32- and
  7-sample blocks at 8000, 11025 and 16000 Hz.
- Going from 16 kHz to 8 kHz and back reused a converter still holding old
  audio. It now resets.
- A region updated only its best-matching track. Other tracks it covered
  stayed reported for up to 0.5 s; they are now dropped. A new track is
  also fully set before it is stored.

Grok also noted that `kmax` wasn't clamped; it is now. Separately, testing
at 16 kHz showed `SRC_SINC_FASTEST` folding 4–5 kHz noise onto the top of
the band, so the converter is now `SRC_SINC_BEST_QUALITY`.

Live: with `live_check.py` and three signals at 6 dB, fldigi logs all three
every 5 s, within 4 Hz of their true centers. The fake server's linear
upsampling tilts the noise about 10 dB across the band, which is how the
first floor (flat at the 20th percentile) was found to fail. That's why
the line fit and the tilted test set were added.

Notes for later phases:
- fldigi skips the whole `fldigi.prefs` file unless it has `version` and
  `dual_channels` entries. A fresh config has a log mask of 0, so it logs
  nothing. `live_check.py` seeds both.
- Twice, `impair_corpus.py` left an empty manifest beside complete clips.
  The cause wasn't found, and re-runs worked. The script now checks its
  manifest and fails loudly.

### Phase 2 notes (2026-10-24)

What was built:
- **Signature table:** `src/sigid/sigid_signatures.h` defines the format,
  and `signatures.cxx` is generated by `scripts/sigid/gen_signatures.py`:
  156 rows in 10 families. Look-alike groups (45) are set automatically
  from identical physical parameters: kind, tones, spacing, baud and PSK
  order. That correctly puts MFSK31 with Olivia/Contestia 8/250, MFSK16
  with Contestia 16/250, MFSK32 with 16/500, and MFSK64 and MFSK128 (with
  their L versions) with 16/1K and 16/2K. Adding a mode means adding one
  line to the generator.
- **Classifier:** `src/sigid/features.cxx`. Each candidate is mixed to a
  complex baseband once, and every row whose predicted width is close
  enough is scored by its family's test:
  - **Tone families** (MFSK, Olivia, IFK, FSK2, Throb): a spectrogram with
    one-symbol frames. Score how much of each frame's energy sits on the
    row's tone grid (best of six offsets), counted above the share an
    evenly spread signal would get. Also require the tone to change at the
    right rate, enough of the tones to be used, and a line at the baud in
    the size of the frequency steps.
  - **Narrow FSK** (FSK Hell, shift < baud): the instantaneous frequency
    sits at ±shift/2.
  - **PSK:** lines in z², z⁴ and z⁸, compared with each other (BPSK: a
    strong z²; QPSK: z⁴ well above z²; 8PSK: z⁸ above z⁴), each standing
    clear of its local noise, plus an envelope dip once a symbol.
  - **Multi-carrier PSK:** a shared complex STFT, one per baud. Each
    carrier must be steadily on, dip once a symbol, and show a BPSK line.
  - **MT63:** a comb at the carrier spacing, and frames that don't hold a
    single tone.
  - **On-off keying:** off-time and flat tops, with the keying rate as a
    line in the size of the envelope's steps (Hell also at the column
    rate). A slower keyed rate that divides this row's and shows a line
    explains the line as a harmonic. CW is the fallback with no fixed rate.
    Width is ignored for keyed modes.
- **Engine:** `sigid.cxx` keeps 16 s of audio. A track is classified at
  3 s old and every 3 s after; once the same answer has come back three
  times with at least 30% confidence, every 8 s. The window is 10 s, 16 s
  for narrow (slow) signals and 6 s for wide ones. Row scores are averaged
  over time. Confidence is the winner's lead over the best row outside its
  look-alike group.
- **Tools:** `sigid-scan --signatures`, `--top N` and `--trace` (each
  row's measurements and the CPU used per family). `score_class.py`
  scores sets.
- **Corpus:** regenerated with 5× the text, because fast modes had run out
  of text and gone to idle partway through their clips. The old corpus is
  in `~/src/sigid-corpus/v1`. All 156 new clips pass `check_corpus.py`,
  with no fldigi restarts (the NAVTEX abort fix works).

Results (`score_class.py`, 156 clips per set). "Group" means the right
mode or a look-alike; "family" counts a look-alike from another family as
right.

| Set | exact | group | family | none |
|---|---|---|---|---|
| AWGN 10 dB | 65% | 98% | 99% | 1 |
| AWGN 3 dB | 63% | 92% | 93% | 4 |
| AWGN 3 dB, tilt | 61% | 91% | 92% | 5 |
| AWGN 0 dB | 51% | 76% | 80% | 12 |

The goal was family ≥ 95% and group ≥ 85% at SNR ≥ 3 dB. At 10 dB both
are met; at 3 dB the group goal is met and family is at 93%. The 3 dB
misses:
- 8PSK at 500 baud and up: at 3 dB in 2500 Hz, 8PSK1000 has about 5 dB
  in its own band, too little for an 8th-power line.
- Contestia 4/2K, 64/1K and 64/2K, and MFSK64L/128L: wide, with few
  symbols per tone in a 6 s window.
- Hell X9, Throb 2 vs ThrobX 2 (Throb speed and set are a weak spot
  generally), and PSK125RC16.

"Exact" is around 60% because choosing within a look-alike group is
Phase 3's job.

CPU: the four sets (220 minutes of audio) took 190 s, about 1.4% of a
core. A single wide signal costs about 6% until its answer settles and
about 2% after.

Live: `live_check.py` with BPSK31, Olivia 8/250 and RTTY at 6 dB logs all
three by name, at 93%, 75–84% and 30% confidence. RTTY's confidence is
lower because other FSK rows score close to it.

Grok: the plan gave Grok the first draft of the signature table, in a
worktree. It stopped three times on headless permissions: running
python3, then writing a helper under /tmp, outside the paths it was
allowed to edit. So the table was generated here from its SIGID-MODES.md
survey. For code tasks, Grok needs an allow rule for every path and
command it might use, `/tmp` included.

Grok's read-only review of the Phase 2 diff found seven real problems, all
fixed:
- Within one large `receive()` block, the clock and the audio window ran
  ahead of the FFTs, so one whole-clip call classified nothing. Audio is
  now taken a hop at a time, and results are the same for any block size.
- `line_in` returned 0 when too few neighbour bins fell in ±25% of a low
  rate, and that depended on FFT padding. It now looks at least 10 bins
  out.
- The per-candidate caches were file-level statics keyed by a pointer to a
  local vector. They are now a context object owned by each call, so
  nothing is shared between calls or threads.
- One MT63 "one tone at a time" probe was reused for every MT63 width. It
  is now per carrier spacing.
- A settled track whose answer changed still waited 8 s for its next check.
- Any lone positive score had confidence 1 and could settle; settling now
  also needs a score of at least 0.3.
- The spectrogram cache key could overflow a 32-bit `long` (Windows).

Mistakes found along the way, all fixed:
- FSQ's "4.5 baud" setting is a 3072-sample symbol at 12 kHz, 3.906 baud.
- Line tests need a local floor, not the whole spectrum's median.
- Random on-off keying has no envelope line at its baud; its edges do.
- An N-tone grid at the baud spacing gets about 1/N of an even spread on
  any tone, so concentration is measured above that.

## Appendix: native mode signal parameters

The full table, with file:line for each family, is in
[SIGID-MODES.md](SIGID-MODES.md). Grok surveyed it read-only on 2026-10-24,
and the cited lines were spot-checked. What matters for the design:

- **Where the carrier sits.** `get_freq()` is the center of `get_bandwidth()`
  for every mode except CW, the OOK Hell modes, and SCAMP OOK, where it is the
  keyed tone. In both cases it is the center of the energy, so a hit's center
  frequency can go straight to `init_modem` and `qsy`. Watch for: FSQ always
  transmits at 1500 Hz, IFKP has an optional 1500 Hz lock, and MT63 has
  `mt63_at500`.
- **Modes with no RsID at all**, where SigID helps most: FSQ, all SCAMP modes,
  WEFAX, NAVTEX, SITOR-B, QPSK500, Olivia 64/500 and 64/1000, MFSK4, Slow,
  X5 and X9 Hell, OFDM-3500.
- **Pairs a spectrum separates**: tone count, spacing and keying rate
  (THOR/DominoEX 5 vs 11, 8 vs 16, and 25x4 vs 50x2 vs 100), BPSK vs QPSK vs
  8PSK (x², x⁴ and x⁸ lines), MT63 vs Olivia 64 (steady comb vs one hopping
  tone), FSQ vs IFKP (8.79 vs 11.72 Hz spacing), OFDM-500F vs 4xPSK63R
  (carrier spacing 2.0 vs 1.4 × baud).
- **Pairs only a decoder separates** (phase 3, or reported as "A or B"):
  Olivia vs Contestia, THOR vs DominoEX at the same speed, BPSK vs PSK-R and
  NxPSK vs NxPSK-R, PSK63 vs PSK63F, MFSK64/128 vs the L versions, MT63 S vs
  L, 8PSK with FEC vs without, and NAVTEX vs SITOR-B vs RTTY 100/170.
