# SigID: native mode signal parameters

Surveyed from this tree on 2026-10-24 (Grok, read-only, spot-checked by Claude). Line numbers refer to commit a88444ef.


Measured from this tree. `get_bandwidth()` is `modem::bandwidth` after each modem’s constructor/`restart()`. `get_freq()` is `modem::frequency` (`src/include/modem.h:144`). Waterfall click-to-tune calls `set_freq()` which clamps so that **`frequency ± bandwidth/2` stays inside the waterfall** (`src/trx/modem.cxx:319–324`) — i.e. the UI treats `get_freq()` as the **center of `get_bandwidth()`**.

Baud values are `samplerate / symbol_length` (or `Bandwidth / Tones` for Olivia/Contestia). Occupied-BW numbers are **exactly** `get_bandwidth()`, which is a waterfall-box width, not a −3 dB lab measurement.

---

## Carrier-frequency convention (for 1500 Hz centering)

| Convention | Modes | What `get_freq()` is | 1500 Hz click |
|---|---|---|---|
| **Center of occupied box** | PSK/QPSK/8PSK/OFDM/PSKR, MFSK, Olivia/Contestia, THOR, DominoEX, MT63, RTTY, NAVTEX/SITOR-B, WEFAX, Throb/ThrobX, FSQ (RX), IFKP, SCAMP FSK, FSK-Hell | Geometric center of `get_bandwidth()` | Puts the **middle** of the stack at 1500 |
| **The RF tone itself** | CW, Feld Hell / Slow Hell / Hell X5 / Hell X9, SCAMP OOK | The keyed carrier | Puts the **tone** at 1500 |
| **TX forced to 1500** | FSQ TX (`modem.cxx:349,357`) | RX `get_freq()` is still the 33-tone center; TX always 1500 | RX can be moved; TX does not follow |
| **Optional lock to 1500** | IFKP (`ifkp.cxx:256–259`), MT63 (`mt63.cxx:329–336`) | Center, unless `ifkp_freqlock` / `mt63_centered` / `mt63_at500` | IFKP lock: always 1500. MT63 `mt63_at500`: center = `500 + bw/2` (lowest carrier at 500) |

Olivia/Contestia nuance: start/stop tones are at `get_freq() ± occupied_bw/2` (`olivia.cxx:105–119`). The Jalocha modulator’s first-carrier bin is `freq − Bandwidth·(1 − 0.5/Tones)/2` (`olivia.cxx:78–84,336`). That is **about ¼-tone off** a perfect first-to-last midpoint. Click-to-tune still uses `get_freq()` as the box center.

THOR/DominoEX: tones are at `(i+0.5)·spacing + freq − bandwidth/2` (`thor.cxx:1210`, `dominoex.cxx:659`). Box width is `N·spacing` with N=18, so first-to-last is 17·spacing; the extra ½-tone on each side is padding. The **actual tone-stack center equals `get_freq()`**.

MFSK: lowest tone = `get_freq() − bandwidth/2`, highest = `get_freq() + bandwidth/2` (`mfsk.cxx:945–951`). Exact.

PSK multi-carrier: `frequencies[0] = freq + (1−N)·inter_carrier/2` (`psk.cxx:2011`). Exact center of the N carriers.

---

## Family formulas (cite these; rows below are instances)

### PSK / QPSK / 8PSK / PSKR / OFDM — `src/psk/psk.cxx:362–1063`

- Default `samplerate = 8000`; 8PSK and OFDM use `16000`.
- Symbol rate `sc_bw = samplerate / symbollen` (`:1060`).
- Carrier spacing `inter_carrier = separation * sc_bw` (`:1062`). Default `separation = 1.4` (`:374`). OFDM sets `separation = 2.0`.
- `get_bandwidth() = sc_bw * (1 + separation * (numcarriers−1))` (`:1063`).
- BPSK: 1 bit/sym, no FEC (except PSK63F / PSKR).
- QPSK: 2 bit/sym, conv. K=5, polys `0x17/0x19` (`:66–68,981–983`), `CAP_REV`.
- PSKR / PSK63F: BPSK + NASA K=7 (`0x6d/0x4f`) + MFSK varicode; PSKR also 2×2×`idepth` interleave; PSK63F skips the interleaver (`:450–455,2336–2340`).
- 8PSK: 3 bit/sym, Gray-mapped. FEC-off modes: `_disablefec`. FEC-on: K=13 except 125F/250F (K=16) and 1000F/1200F (K=7 + puncture 2/3). Vestigial flag true; **pilot only if `progdefaults.pskpilot`** (default **false**, `configuration.h:376`). Pilot is one baud **below** the lowest carrier (`psk.cxx:2292–2294`) and is **not** included in `get_bandwidth()`.
- Idle/preamble: BPSK/QPSK send continuous `tx_symbol(0)` phase reversals (`:2613–2617`) → two lines at `fc ± baud/2` (PSK31 “railroad tracks”). PSKR flushes with `1,0` bit pairs (`:2563–2566`). 8PSK FEC-off: symbol 0; 8PSK FEC-on: encoded double-zeros (single centered tone) (`:2584–2608`).
- DCD on BPSK: shreg `0xAAAAAAAA` (`:1618–1623`). QPSK does **not** use DCD (`:1637`).

### MFSK — `src/mfsk/mfsk.cxx:177–336`

- `tonespacing = samplerate / symlen` = baud (`:297`). Orthogonal.
- `get_bandwidth() = (numtones − 1) * tonespacing` (`:336`).
- Gray-coded single-tone MFSK, NASA K=7, convolutional + block interleave (`depth`), MFSK varicode.
- Preamble typically 107 symbols; 64/128 use 180/214; 64L/128L use 2500/5000 plus huge `depth` (400/800).
- Image (FM inside the tone box) on MFSK16/32/64/128 (`CAP_IMG`). 64L/128L have no image.
- Idle: NUL + 32 zero bits (`:985–993`).

### Olivia / Contestia — `src/olivia/olivia.cxx:318–379`, `src/contestia/contestia.cxx:268–325`, modulator `src/include/jalocha/pj_mfsk.h:1693–1758`

- Nominal BW = `125 << bw_index` (`olivia.cxx:325`).
- Tones = `2 * (1 << tones_index)` (`:327`) → 2,4,8,16,32,64.
- Baud = `SampleRate / SymbolSepar` = **nominal_BW / Tones** (`pj_mfsk.h:1757–1758`; `SymbolSepar = SymbolLen/2`).
- Tone spacing = baud = nominal_BW / Tones.
- `get_bandwidth() = Bandwidth − Bandwidth/Tones` = **(N−1)×spacing** (`olivia.cxx:379`, `contestia.cxx:325`).
- Same Jalocha MFSK engine. Difference is FEC only (`pj_mfsk.h:1113–1124,1185`):
  - **Olivia:** 7-bit chars, Walsh block 64, scramble `ScramblingCodeOlivia`, rotate 13.
  - **Contestia:** 6-bit chars (forced upper case), Walsh block 32, scramble `ScramblingCodeContestia`, rotate 5.
- Optional start/stop two-tones at the box edges (`olivia.cxx:105`, default on).
- Generic `MODE_OLIVIA` / `MODE_CONTESTIA` take `progdefaults.oliviatones/bw` (default 8/500) (`configuration.h:772–803`).

### THOR — `src/thor/thor.cxx:218–354`, `src/include/thor.h:59`

- 18 tones, incremental IFK: `tone = (prev + 2 + sym) % 18` (`thor.cxx:1239`).
- `tonespacing = samplerate * doublespaced / symlen` (`:352`).
- Baud = `samplerate / symlen`.
- `get_bandwidth() = 18 * tonespacing` (`:354`).
- Always FEC (NASA K=7, or K=15 on some high-speed modes) + interleave + dual varicode (primary/secondary text). Idle = NUL on primary; gaps send secondary text (`:1271–1280,1349`).
- Image on several rates (`CAP_IMG`).

### DominoEX — `src/dominoex/dominoex.cxx:218–283`, `src/include/dominoex.h:46`

- Same 18-tone IFK and BW formula as THOR (`NUMTONES=18`, `bandwidth = NUMTONES * tonespacing`).
- FEC is **optional** (`progdefaults.DOMINOEX_FEC`); RsID has separate FEC / non-FEC codes that both map to the same `trx_mode`.
- No image. Secondary text like THOR.

### MT63 — `src/mt63/mt63.cxx:347–375`, `src/mt63/mt63base.cxx:87–122,743–778`, `src/mt63/symbol.dat:24–26`

- 64 DBPSK carriers, Walsh-Hadamard, 8000 Hz in.
- `SymbolSepar=200` samples at the **decimated** rate: 5 / 10 / 20 baud for 500 / 1000 / 2000 Hz.
- Carrier spacing = 500/64 = 7.8125 Hz, scaled with BW: 7.8125 / 15.625 / 31.25 Hz.
- S = interleave 32, L = 64.
- `get_freq()` is center. Optional 2-tone start at `freq − bw/2` and `freq + 31·bw/64` (`mt63.cxx:75–76`).

### Hell — `src/feld/feld.cxx:146–234`, `src/include/feld.h:42`

- AM/OOK modes: single carrier at `get_txfreq()` (`feld.cxx:579,605`). `get_bandwidth() = 14 × column_rate` (pixel rate).
- FSK modes: mark/space = `get_freq() ± bandwidth/2` (`:586–587`).

### RTTY — `src/rtty/rtty.cxx:95–258`

- 2-tone FSK. `SHIFT[] = {23,85,160,170,182,200,240,350,425,850}`, `BAUD[] = {45,45.45,50,56,75,100,110,150,200,300}`.
- Default: baud index 0 = **45 baud**, shift index 3 = **170 Hz** (`configuration.h:420–448`).
- `get_bandwidth() = shift` (`rtty.cxx:258`). Mark/space at `freq ± shift/2` (`:681–684`).
- Default 5-bit Baudot, 1.5 stop bits. Unshift-on-space configurable. Idle = repeating RY / mark hold depending on TX state.

### CW — `src/cw/cw.cxx:308–439`

- OOK Morse. `get_freq()` is the tone. Filter BW default 150 Hz, or `2 × WPM` if matched filter (`:362–364,437–439`). Not a fixed RF occupied width.

### Throb / ThrobX — `src/throb/throb.cxx:132–243,941–944`

- Two simultaneous tones from a 9-tone (Throb) or 11-tone (ThrobX) set, offsets relative to `get_freq()` (`:609–610`).
- `get_bandwidth() = freqs[n−1] − freqs[0]` (`:243`).
- Raised-cosine pulse. ThrobX idle/space symbols alternate (`:96–114`).

### FSQ — `src/fsq/fsq.cxx:169–308,1368–1379`, `src/include/fsq.h:41–45`

- 33-tone IFK, `tone = (prev + sym + 1) % 33` (`:1371`).
- `samplerate = 12000`, FFT 4096, `spacing = 3` bins → **8.7890625 Hz**.
- `get_bandwidth() = 33 * 3 * 12000 / 4096 = 290.039 Hz` (`:261`).
- Baud from `progdefaults.fsqbaud`: 1.5 / 2 / 3 / 4.5 / 6 → symlen 8192 / 6144 / 4096 / 3072 / 2048.
- Idle: symbols 28 then 30 (`:1375–1378`). BOT/EOT strings. No RsID.
- TX audio always 1500 Hz.

### IFKP — `src/ifkp/ifkp.cxx:58,114–117,256–272,711–720`, `src/include/ifkp.h:37–48`

- 33-tone IFK, `tone = (prev + sym + 1) % 33`.
- `samplerate = 16000`, `symlen = 4096` → base baud **3.90625**; ×0.5 / ×1 / ×2 from `ifkp_baud`.
- Spacing = `3 * 16000 / 4096 = 11.71875 Hz`.
- `get_bandwidth() = 33 * 11.71875 = 386.71875 Hz`.
- Idle: symbol 0. Image/avatar. RsID 1171.

### SCAMP — `src/scamp/scamp.cxx:113–180`, Golay in `src/scamp/scamp_protocol.cxx`

- FSK: two correlators at `freq ± Δ` (`:294–318`). OOK: one correlator at `freq` (`:319–329`).
- 6-bit alphabet + Golay. **No RsID.**
- Analog baud from `circbuffer_samples` at 8000 Hz (see table). FAST mode: `shift_freq` implies **166.67 Hz** tone spacing while `shift` used by the SNR meter is 83.33 — waterfall box uses `mode_bandwidth`.

### WEFAX — `src/wefax/wefax.cxx:267–269,2274–2284`

- Analog FM fax. Default shift 800 Hz (`fm_deviation = shift/2`). DWD often 850. `get_bandwidth() = 2 * deviation`.
- `get_freq()` is the FM center (APT/black/white at `center ± deviation`). IOC 576 or 288; LPM typically 120 / 60. **No RsID** (rsid FAX maps to `NUM_MODES`).

### NAVTEX / SITOR-B — `src/navtex/navtex.cxx:860,958,988–989,1945`

- 100 baud FSK, `deviation_f = 85` → 170 Hz shift. `get_bandwidth() = 170`. Center = `get_freq()`.
- CCIR 476 / SITOR-B FEC (4B/7B, phasing idle). NAVTEX vs SITOR-B is protocol framing, not RF. **No RsID.**

### RsID itself — `src/rsid/rsid.cxx:990–997`, `src/include/rsid.h:53–65`, codes in `src/rsid/rsid_defs.cxx`

- 15 sequential MFSK symbols, spacing `11025/1024 = 10.7666 Hz`, symbol time 1024/11025 s, baud = 10.7666.
- TX starts at `get_txfreq() − 7·10.7666` (`rsid.cxx:990`) so the 15 tones are **centered on `get_freq()`**, width ≈ 151 Hz.
- Second table after ESCAPE code 6.

---

## Tables

`RsID` is the code in `rsid_defs.cxx` that maps to this `trx_mode`. `—` means no code in this tree. Second-set codes are shown as `2:n`.

### CW

| enum | display | family | tones | spacing | baud | `get_bandwidth()` | RsID | distinctive | `get_freq()` |
|---|---|---|---|---|---|---|---|---|---|
| `MODE_CW` | CW | OOK Morse | 1 | — | WPM/1.2 (typ. 20 baud @ 24 WPM) | 150 Hz default, or `2×WPM` if matched filter (`cw.cxx:326,362–364`) | 26 | On/off keyed carrier; no idle tone | **the tone** |

### Contestia (`contestia.cxx:268–325,355–456`)

Walsh-32, 6-bit uppercase, rotate 5. Same RF as the matching Olivia.

| enum | display | tones | spacing = baud | baud | occ. BW `(N−1)·sp` | RsID |
|---|---|---|---|---|---|---|
| `MODE_CONTESTIA` | Contestia | user (default 8) | BW/N (default 62.5) | default 62.5 | default 437.5 | — (uses the selected variant’s code if any) |
| `MODE_CONTESTIA_4_125` | Cont4-125 | 4 | 31.25 | 31.25 | 93.75 | 204 |
| `MODE_CONTESTIA_4_250` | Cont4-250 | 4 | 62.5 | 62.5 | 187.5 | 55 |
| `MODE_CONTESTIA_4_500` | Cont4-500 | 4 | 125 | 125 | 375 | 54 |
| `MODE_CONTESTIA_4_1000` | Cont4-1K | 4 | 250 | 250 | 750 | 255 |
| `MODE_CONTESTIA_4_2000` | Cont4-2K | 4 | 500 | 500 | 1500 | 254 |
| `MODE_CONTESTIA_8_125` | Cont8-125 | 8 | 15.625 | 15.625 | 109.375 | 169 |
| `MODE_CONTESTIA_8_250` | Cont8-250 | 8 | 31.25 | 31.25 | 218.75 | 49 |
| `MODE_CONTESTIA_8_500` | Cont8-500 | 8 | 62.5 | 62.5 | 437.5 | 52 |
| `MODE_CONTESTIA_8_1000` | Cont8-1K | 8 | 125 | 125 | 875 | 117 |
| `MODE_CONTESTIA_8_2000` | Cont8-2K | 8 | 250 | 250 | 1750 | 247 |
| `MODE_CONTESTIA_16_250` | Cont16-250 | 16 | 15.625 | 15.625 | 234.375 | 275 |
| `MODE_CONTESTIA_16_500` | Cont16-500 | 16 | 31.25 | 31.25 | 468.75 | 50 |
| `MODE_CONTESTIA_16_1000` | Cont16-1K | 16 | 62.5 | 62.5 | 937.5 | 53 |
| `MODE_CONTESTIA_16_2000` | Cont16-2K | 16 | 125 | 125 | 1875 | 259 |
| `MODE_CONTESTIA_32_1000` | Cont32-1K | 32 | 31.25 | 31.25 | 968.75 | 51 |
| `MODE_CONTESTIA_32_2000` | Cont32-2K | 32 | 62.5 | 62.5 | 1937.5 | 201 |
| `MODE_CONTESTIA_64_500` | Cont64-500 | 64 | 7.8125 | 7.8125 | 492.1875 | 194 |
| `MODE_CONTESTIA_64_1000` | Cont64-1K | 64 | 15.625 | 15.625 | 984.375 | 193 |
| `MODE_CONTESTIA_64_2000` | Cont64-2K | 64 | 31.25 | 31.25 | 1968.75 | 191 |

`get_freq()` = **center**. Distinctive vs Olivia: half-length Walsh block, A–Z/0–9 alphabet, faster print; optional edge start-tones identical.

### DominoEX (`dominoex.cxx:224–283`) — 18-tone IFK, `get_freq()` = center

| enum | display | spacing Hz | baud | occ. BW = 18·sp | RsID | notes |
|---|---|---|---|---|---|---|
| `MODE_DOMINOEXMICRO` | DominoEX Micro | 2.0 | 2.0 | 36 | 2:691 | 8000 Hz, symlen 4000, ds=1 |
| `MODE_DOMINOEX4` | DominoEX 4 | 7.8125 | 3.90625 | 140.625 | 84 / 92 FEC | 8000, 2048, ds=2 |
| `MODE_DOMINOEX5` | DominoEX 5 | 10.7666 | 5.3833 | 193.80 | 85 / 93 FEC | 11025, 2048, ds=2 |
| `MODE_DOMINOEX8` | DominoEX 8 | 15.625 | 7.8125 | 281.25 | 86 / 97 FEC | 8000, 1024, ds=2 |
| `MODE_DOMINOEX11` | DominoEX 11 | 10.7666 | 10.7666 | 193.80 | 87 / 98 FEC | 11025, 1024, ds=1 |
| `MODE_DOMINOEX16` | DominoEX 16 | 15.625 | 15.625 | 281.25 | 88 / 99 FEC | 8000, 512, ds=1 |
| `MODE_DOMINOEX22` | DominoEX 22 | 21.5332 | 21.5332 | 387.60 | 90 / 101 FEC | 11025, 512, ds=1 |
| `MODE_DOMINOEX44` | DominoEX 44 | 86.1328 | 43.0664 | 1550.39 | 2:662 | 11025, 256, **ds=2** |
| `MODE_DOMINOEX88` | DominoEX 88 | 86.1328 | 86.1328 | 1550.39 | 2:681 | 11025, 128, ds=1 |

One tone at a time; IFK (tone depends on previous). Optional K=7 FEC. Secondary-text idle. EX5 vs EX11 (and EX16 vs EX8) share BW; EX11/16 are single-spaced (baud = spacing), EX5/8 are double-spaced (baud = spacing/2).

### Hell (`feld.cxx:151–234`)

| enum | display | family | tones | spacing | “baud” / pixel | occ. BW | RsID | distinctive | `get_freq()` |
|---|---|---|---|---|---|---|---|---|---|
| `MODE_FELDHELL` | Feld Hell | OOK Hell | 1 | — | 17.5 col/s × 14 px = 245 px/s | **245** | 104 | Raster OOK; 14-row font | **the tone** |
| `MODE_SLOWHELL` | Slow Hell | OOK Hell | 1 | — | 2.1875 col/s → 30.625 px/s | **30.625** | — | Same as Feld, ⅛ speed | **the tone** |
| `MODE_HELLX5` | Feld Hell X5 | OOK Hell | 1 | — | 87.5 col/s → 1225 px/s | **1225** | — | Feld ×5 | **the tone** |
| `MODE_HELLX9` | Feld Hell X9 | OOK Hell | 1 | — | 157.5 col/s → 2205 px/s | **2205** | — | Feld ×9 | **the tone** |
| `MODE_FSKH245` | FSK Hell-245 | 2-FSK Hell | 2 | 122.5 | 17.5 col/s | **122.5** | 108 | Continuous FSK; no AM | **center** |
| `MODE_FSKH105` | FSK Hell-105 | 2-FSK Hell | 2 | 55 | 17.5 col/s | **55** | 107 | Narrow FSK Hell | **center** |
| `MODE_HELL80` | Hell 80 | 2-FSK Hell | 2 | 300 | 35 col/s | **300** | 106 | Wider/faster FSK Hell | **center** |

### MFSK (`mfsk.cxx:177–336`) — Gray MFSK, K=7, interleave, `get_freq()` = center

| enum | display | tones | spacing = baud | baud | occ. BW `(N−1)·sp` | RsID | notes |
|---|---|---|---|---|---|---|---|
| `MODE_MFSK4` | MFSK-4 | 32 | 3.90625 | 3.90625 | 121.09 | — | 8000/2048, depth 5, preamble 107 |
| `MODE_MFSK8` | MFSK-8 | 32 | 7.8125 | 7.8125 | 242.19 | 60 | 8000/1024, depth 5 |
| `MODE_MFSK16` | MFSK-16 | 16 | 15.625 | 15.625 | 234.375 | 57 | 8000/512, depth 10, **image** |
| `MODE_MFSK32` | MFSK-32 | 16 | 31.25 | 31.25 | 468.75 | 147 | 8000/256, **image** |
| `MODE_MFSK11` | MFSK-11 | 16 | 10.7666 | 10.7666 | 161.50 | 148 | 11025/1024 |
| `MODE_MFSK22` | MFSK-22 | 16 | 21.5332 | 21.5332 | 323.00 | 152 | 11025/512 |
| `MODE_MFSK31` | MFSK-31 | 8 | 31.25 | 31.25 | 218.75 | 2:687 | 8000/256, 8 tones |
| `MODE_MFSK64` | MFSK-64 | 16 | 62.5 | 62.5 | 937.5 | 2:620 | preamble 180, **image** |
| `MODE_MFSK128` | MFSK-128 | 16 | 125 | 125 | 1875 | 2:625 | preamble 214, **image** |
| `MODE_MFSK64L` | MFSK-64L | 16 | 62.5 | 62.5 | 937.5 | 2:1026 | depth **400**, preamble 2500, no image |
| `MODE_MFSK128L` | MFSK-128L | 16 | 125 | 125 | 1875 | 2:1029 | depth **800**, preamble 5000, no image |

### WEFAX / NAVTEX / SITOR-B

| enum | display | family | tones | spacing | baud | occ. BW | RsID | distinctive | `get_freq()` |
|---|---|---|---|---|---|---|---|---|---|
| `MODE_WEFAX_576` | WEFAX-IOC576 | FM fax | analog FM | shift 800 (cfg) | LPM (typ. 120) | **800** (or cfg shift) | — | IOC 576; APT + phasing black/white | **FM center** |
| `MODE_WEFAX_288` | WEFAX-IOC288 | FM fax | analog FM | same | LPM (typ. 60) | **800** | — | IOC 288 | **FM center** |
| `MODE_NAVTEX` | NAVTEX | 2-FSK SITOR | 2 | 170 | 100 | **170** | — | CCIR 476, ZCZC/NNNN, phasing idle | **center** |
| `MODE_SITORB` | SITORB | 2-FSK SITOR | 2 | 170 | 100 | **170** | — | Same RF as NAVTEX; no NAVTEX framing | **center** |

### MT63 (`mt63.cxx:347–375`, `symbol.dat:24–26`) — 64 carriers, Walsh, `get_freq()` = center

| enum | display | carriers | spacing Hz | baud | occ. BW | RsID | interleave |
|---|---|---|---|---|---|---|---|
| `MODE_MT63_500S` | MT63-500S | 64 | 7.8125 | 5 | **500** | 10 | 32 |
| `MODE_MT63_500L` | MT63-500L | 64 | 7.8125 | 5 | **500** | 9 | 64 |
| `MODE_MT63_1000S` | MT63-1000S | 64 | 15.625 | 10 | **1000** | 13 | 32 |
| `MODE_MT63_1000L` | MT63-1000L | 64 | 15.625 | 10 | **1000** | 12 | 64 |
| `MODE_MT63_2000S` | MT63-2000S | 64 | 31.25 | 20 | **2000** | 17 | 32 |
| `MODE_MT63_2000L` | MT63-2000L | 64 | 31.25 | 20 | **2000** | 15 | 64 |

Constant-amplitude 64-carrier brick; S vs L is time-only (interleave). Optional start tones.

### BPSK (`psk.cxx:384–413`) — 1 carrier, sep unused, `get_freq()` = the carrier = center of `sc_bw`

| enum | display | baud = occ. BW | RsID | notes |
|---|---|---|---|---|
| `MODE_PSK31` | BPSK-31 | **31.25** | 1 | Varicode; idle 2-tone at ±15.625 |
| `MODE_PSK63` | BPSK-63 | **62.5** | 2 | |
| `MODE_PSK63F` | BPSK-63F | **62.5** | 22 | +K=7 FEC, MFSK varicode, **no** interleave |
| `MODE_PSK125` | BPSK-125 | **125** | 4 | |
| `MODE_PSK250` | BPSK-250 | **250** | 126 | |
| `MODE_PSK500` | BPSK-500 | **500** | 173 | |
| `MODE_PSK1000` | BPSK-1000 | **1000** | 2:570 | |

### Multi-carrier BPSK (no FEC) — `separation = 1.4`, `get_freq()` = center of N carriers

`bw = baud × (1 + 1.4×(N−1))`

| enum | display | N | baud | spacing 1.4·baud | occ. BW | RsID |
|---|---|---|---|---|---|---|
| `MODE_12X_PSK125` | 12xPSK125 | 12 | 125 | 175 | **2050** | 2:519 |
| `MODE_6X_PSK250` | 6xPSK250 | 6 | 250 | 350 | **2000** | 2:541 |
| `MODE_2X_PSK500` | 2xPSK500 | 2 | 500 | 700 | **1200** | 2:566 |
| `MODE_4X_PSK500` | 4xPSK500 | 4 | 500 | 700 | **2600** | 2:569 |
| `MODE_2X_PSK800` | 2xPSK800 | 2 | 800 | 1120 | **1920** | 2:610 |
| `MODE_2X_PSK1000` | 2xPSK1000 | 2 | 1000 | 1400 | **2400** | 2:587 |

### QPSK (`psk.cxx:415–448`) — same baud/BW as matching BPSK, K=5 FEC, `CAP_REV`

| enum | display | baud = occ. BW | RsID |
|---|---|---|---|
| `MODE_QPSK31` | QPSK-31 | 31.25 | 110 |
| `MODE_QPSK63` | QPSK-63 | 62.5 | 3 |
| `MODE_QPSK125` | QPSK-125 | 125 | 5 |
| `MODE_QPSK250` | QPSK-250 | 250 | 127 |
| `MODE_QPSK500` | QPSK-500 | 500 | **—** |

### 8PSK (`psk.cxx:533–657`) — `samplerate=16000`, 1 carrier, occ. BW = baud; vestigial **optional** (`pskpilot`, default off)

| enum | display | baud = occ. BW | RsID | FEC |
|---|---|---|---|---|
| `MODE_8PSK125` | 8PSK-125 | 125 | 2:1066 | none |
| `MODE_8PSK125FL` | 8PSK-125FL | 125 | 2:1239 | ½ K=13, idepth 384 |
| `MODE_8PSK125F` | 8PSK-125F | 125 | 2:1037 | ½ K=16, idepth 384 |
| `MODE_8PSK250` | 8PSK-250 | 250 | 2:1071 | none |
| `MODE_8PSK250FL` | 8PSK-250FL | 250 | 2:2052 | ½ K=13, idepth 512 |
| `MODE_8PSK250F` | 8PSK-250F | 250 | 2:1038 | ½ K=16, idepth 512 |
| `MODE_8PSK500` | 8PSK-500 | 500 | 2:1076 | none |
| `MODE_8PSK500F` | 8PSK-500F | 500 | 2:1043 | ⅔ K=13 punctured, idepth 640 |
| `MODE_8PSK1000` | 8PSK-1000 | 1000 | 2:1047 | none |
| `MODE_8PSK1000F` | 8PSK-1000F | 1000 | 2:1078 | ⅔ K=7 punctured, idepth 512 |
| `MODE_8PSK1200F` | 8PSK-1200F | **16000/13 ≈ 1230.77** | 2:1058 | ⅔ K=7 punctured, symbollen 13 |

### OFDM (`psk.cxx:457–530`) — `samplerate=16000`, `separation=2.0`, `get_freq()` = center of N data carriers

| enum | display | N | baud | spacing | occ. BW | RsID | constellation / FEC |
|---|---|---|---|---|---|---|---|
| `MODE_OFDM_500F` | OFDM-500F | 4 | 62.5 | 125 | **437.5** | 2:2053 | xPSK (2 b/sym), K=15, idepth 2000, vestigial flag, NUL×5+LF header (`:2574–2581`) |
| `MODE_OFDM_750F` | OFDM-750F | 3 | 125 | 250 | **625** | 2:2094 | 8PSK, K=13, idepth 3600, vestigial flag |
| `MODE_OFDM_3500` | OFDM-3500 | 7 | 250 | 500 | **3250** | **—** | 8PSK, **no FEC**, vestigial false |

Commented-out in this tree: `MODE_OFDM_2000F`, `MODE_OFDM_2000` (`globals.h:166–167`).

### Olivia (`olivia.cxx:318–379,410–512`) — Walsh-64, 7-bit, rotate 13. `get_freq()` = center

| enum | display | tones | spacing = baud | baud | occ. BW | RsID |
|---|---|---|---|---|---|---|
| `MODE_OLIVIA` | OLIVIA | user (default 8) | default 62.5 | 62.5 | 437.5 | — (8/500 is code 72 if that selection is used) |
| `MODE_OLIVIA_4_125` | OL 4-125 | 4 | 31.25 | 31.25 | 93.75 | 203 |
| `MODE_OLIVIA_4_250` | OL 4-250 | 4 | 62.5 | 62.5 | 187.5 | 75 |
| `MODE_OLIVIA_4_500` | OL 4-500 | 4 | 125 | 125 | 375 | 74 |
| `MODE_OLIVIA_4_1000` | OL 4-1K | 4 | 250 | 250 | 750 | 229 |
| `MODE_OLIVIA_4_2000` | OL 4-2K | 4 | 500 | 500 | 1500 | 238 |
| `MODE_OLIVIA_8_125` | OL 8-125 | 8 | 15.625 | 15.625 | 109.375 | 163 |
| `MODE_OLIVIA_8_250` | OL 8-250 | 8 | 31.25 | 31.25 | 218.75 | 69 |
| `MODE_OLIVIA_8_500` | OL 8-500 | 8 | 62.5 | 62.5 | 437.5 | 72 |
| `MODE_OLIVIA_8_1000` | OL 8-1K | 8 | 125 | 125 | 875 | 116 |
| `MODE_OLIVIA_8_2000` | OL 8-2K | 8 | 250 | 250 | 1750 | 214 |
| `MODE_OLIVIA_16_500` | OL 16-500 | 16 | 31.25 | 31.25 | 468.75 | 70 |
| `MODE_OLIVIA_16_1000` | OL 16-1K | 16 | 62.5 | 62.5 | 937.5 | 73 |
| `MODE_OLIVIA_16_2000` | OL 16-2K | 16 | 125 | 125 | 1875 | 234 |
| `MODE_OLIVIA_32_1000` | OL 32-1K | 32 | 31.25 | 31.25 | 968.75 | 71 |
| `MODE_OLIVIA_32_2000` | OL 32-2K | 32 | 62.5 | 62.5 | 1937.5 | 221 |
| `MODE_OLIVIA_64_500` | OL 64-500 | 64 | 7.8125 | 7.8125 | 492.1875 | **—** |
| `MODE_OLIVIA_64_1000` | OL 64-1K | 64 | 15.625 | 15.625 | 984.375 | **—** |
| `MODE_OLIVIA_64_2000` | OL 64-2K | 64 | 31.25 | 31.25 | 1968.75 | 211 |

### RTTY (`rtty.cxx:95–258`)

| enum | display | family | tones | spacing | baud | occ. BW | RsID | distinctive | `get_freq()` |
|---|---|---|---|---|---|---|---|---|---|
| `MODE_RTTY` | RTTY | 2-FSK | 2 | cfg (default **170**) | cfg (default **45**) | **= shift** | 39 (45), 40 (50), 41 (75), 37 (ASCII7), 38 (ASCII8) | Baudot/ASCII, 1.5 stop default; mark/space at ±shift/2 | **center** |

### SCAMP (`scamp.cxx:119–180`) — Golay FEC, no RsID. FSK: `get_freq()` = center. OOK: `get_freq()` = the tone.

| enum | display | family | tones | tone spacing | baud (from 8000/`circbuffer_samples`) | occ. BW | notes |
|---|---|---|---|---|---|---|---|
| `MODE_SCAMPFSK` | SCAMPFSK | 2-FSK | 2 | 66.67 | 8000/240 = **33.33** | **133.33** | `shift=66.67`; tones at ±33.33 |
| `MODE_SCAMPOOK` | SCAMPOOK | OOK | 1 | — | 8000/256 = **31.25** | **62.5** | |
| `MODE_SCFSKFST` | SCFSKFST | 2-FSK | 2 | 166.67 (from `shift_freq`) | 8000/96 = **83.33** | **333.33** | `shift` field is 83.33 (SNR meter only) |
| `MODE_SCFSKSLW` | SCFSKSLW | 2-FSK | 2 | 41.67 | 8000/576 = **13.89** | **69.44** | |
| `MODE_SCOOKSLW` | SCOOKSLW | OOK | 1 | — | **13.89** | **27.78** | `channel_bandwidth` still 41.67 (filter) |
| `MODE_SCFSKVSL` | SCFSKVSL | 2-FSK | 2 | 20.83 (`0.25×41.67` NCO) | analog window 1152 samples → **6.94**; protocol `FSK_VSLW` shares Slow’s 144/4 under `SCAMP_VERY_SLOW_MODES` — **unsure which is on-air bit rate** | **34.72** | `sample_count_check` doubled |

### THOR (`thor.cxx:228–354`) — 18-tone IFK+FEC, `get_freq()` = center

| enum | display | spacing Hz | baud | occ. BW = 18·sp | RsID | notes |
|---|---|---|---|---|---|---|
| `MODE_THORMICRO` | THOR Micro | 2.0 | 2.0 | 36 | 2:693 | 8000/4000, ds=1, idepth 4 |
| `MODE_THOR4` | THOR 4 | 7.8125 | 3.90625 | 140.625 | 136 | 8000/2048, ds=2 |
| `MODE_THOR5` | THOR 5 | 10.7666 | 5.3833 | 193.80 | 139 | 11025/2048, ds=2 |
| `MODE_THOR8` | THOR 8 | 15.625 | 7.8125 | 281.25 | 137 | 8000/1024, ds=2 |
| `MODE_THOR11` | THOR 11 | 10.7666 | 10.7666 | 193.80 | 143 | 11025/1024, ds=1, image |
| `MODE_THOR16` | THOR 16 | 15.625 | 15.625 | 281.25 | 138 | 8000/512, ds=1, image |
| `MODE_THOR22` | THOR 22 | 21.5332 | 21.5332 | 387.60 | 145 | 11025/512, ds=1, image |
| `MODE_THOR25` | THOR 25 | 25 | 25 | 450 | 2:713 | 8000/320, idepth 25, image |
| `MODE_THOR32` | THOR 32 | 31.25 | 31.25 | 562.5 | 2:2119 | 8000/256, image |
| `MODE_THOR44` | THOR 44 | 43.0664 | 43.0664 | 775.20 | 2:2156 | 11025/256, **ds=1**, image |
| `MODE_THOR56` | THOR 56 | 55.1724 | 55.1724 | 993.10 | 2:2157 | 16000/290, image |
| `MODE_THOR25x4` | THOR 25 x4 | 100 | 25 | 1800 | 2:639 | ds=4, idepth 50 |
| `MODE_THOR50x1` | THOR 50 x1 | 50 | 50 | 900 | 2:649 | 8000/160, ds=1, idepth 50 |
| `MODE_THOR50x2` | THOR 50 x2 | 100 | 50 | 1800 | 2:653 | ds=2, idepth 50 |
| `MODE_THOR100` | THOR 100 | 100 | 100 | 1800 | 2:658 | 8000/80, idepth 50 |

Always FEC + secondary text. Same RF as matching DominoEX except EX44 (ds=2, 1550 Hz) vs THOR44 (ds=1, 775 Hz).

### Throb / ThrobX (`throb.cxx:141–243,941–944`) — 2-of-N simultaneous tones, `get_freq()` = center (offset 0)

| enum | display | N tones | spacing | baud | occ. BW | RsID | tone offsets Hz |
|---|---|---|---|---|---|---|---|
| `MODE_THROB1` | Throb 1 | 9 | 8 | 8000/8192 = **0.9766** | **64** | 43 | ±32,24,16,8,0 |
| `MODE_THROB2` | Throb 2 | 9 | 8 | 8000/4096 = **1.9531** | **64** | 44 | same |
| `MODE_THROB4` | Throb 4 | 9 | 16 | 8000/2048 = **3.90625** | **128** | 45 | ±64…0 |
| `MODE_THROBX1` | ThrobX 1 | 11 | 7.8125 | **0.9766** | **78.125** | 46 | ±39.0625…0 |
| `MODE_THROBX2` | ThrobX 2 | 11 | 7.8125 | **1.9531** | **78.125** | 47 | same |
| `MODE_THROBX4` | ThrobX 4 | 11 | 15.625 | **3.90625** | **156.25** | 146 | ±78.125…0 |

Two raised-cosine tones at once. ThrobX idle/space swap (`idlesym`/`spacesym`).

### PSKR single-carrier (`psk.cxx:660–687`) — BPSK + K=7 + 2×2×idepth interleave + MFSK varicode

| enum | display | baud = occ. BW | RsID | idepth |
|---|---|---|---|---|
| `MODE_PSK125R` | PSK-125R | 125 | 183 | 40 |
| `MODE_PSK250R` | PSK-250R | 250 | 186 | 80 |
| `MODE_PSK500R` | PSK-500R | 500 | 187 | 160 |
| `MODE_PSK1000R` | PSK-1000R | 1000 | 2:580 | 160 |

### Multi-carrier PSKR — `separation = 1.4`, `get_freq()` = center

| enum | display | N | baud | occ. BW | RsID |
|---|---|---|---|---|---|
| `MODE_4X_PSK63R` | 4xPSK63R | 4 | 62.5 | **325** | 2:450 |
| `MODE_5X_PSK63R` | 5xPSK63R | 5 | 62.5 | **412.5** | 2:457 |
| `MODE_10X_PSK63R` | 10xPSK63R | 10 | 62.5 | **850** | 2:458 |
| `MODE_20X_PSK63R` | 20xPSK63R | 20 | 62.5 | **1725** | 2:460 |
| `MODE_32X_PSK63R` | 32xPSK63R | 32 | 62.5 | **2775** | 2:462 |
| `MODE_4X_PSK125R` | 4xPSK125R | 4 | 125 | **650** | 2:467 |
| `MODE_5X_PSK125R` | 5xPSK125R | 5 | 125 | **825** | 2:497 |
| `MODE_10X_PSK125R` | 10xPSK125R | 10 | 125 | **1700** | 2:513 |
| `MODE_12X_PSK125R` | 12xPSK125R | 12 | 125 | **2050** | 2:522 |
| `MODE_16X_PSK125R` | 16xPSK125R | 16 | 125 | **2750** | 2:527 |
| `MODE_2X_PSK250R` | 2xPSK250R | 2 | 250 | **600** | 2:529 |
| `MODE_3X_PSK250R` | 3xPSK250R | 3 | 250 | **950** | 2:533 |
| `MODE_5X_PSK250R` | 5xPSK250R | 5 | 250 | **1650** | 2:539 |
| `MODE_6X_PSK250R` | 6xPSK250R | 6 | 250 | **2000** | 2:545 |
| `MODE_7X_PSK250R` | 7xPSK250R | 7 | 250 | **2350** | 2:551 |
| `MODE_2X_PSK500R` | 2xPSK500R | 2 | 500 | **1200** | 2:553 |
| `MODE_3X_PSK500R` | 3xPSK500R | 3 | 500 | **1900** | 2:558 |
| `MODE_4X_PSK500R` | 4xPSK500R | 4 | 500 | **2600** | 2:564 |
| `MODE_2X_PSK800R` | 2xPSK800R | 2 | 800 | **1920** | 2:604 |
| `MODE_2X_PSK1000R` | 2xPSK1000R | 2 | 1000 | **2400** | 2:595 |

### FSQ / IFKP

| enum | display | family | tones | spacing | baud | occ. BW | RsID | distinctive | `get_freq()` |
|---|---|---|---|---|---|---|---|---|---|
| `MODE_FSQ` | FSQ | 33-tone IFK | 33 used (144 bins searched) | 8.789 | 1.5/2/3/4.5/6 (cfg) | **290.04** | **—** | Idle 28+30; directed calls; TX **locked 1500** | **center** (RX) |
| `MODE_IFKP` | IFKP | 33-tone IFK | 33 used (151 bins) | 11.719 | 1.953 / 3.906 / 7.812 (cfg 0.5/1/2) | **386.72** | 2:1171 | Idle symbol 0; optional 1500 lock; image | **center** |

---

## Not real on-air signals (listed separately)

| enum | display | `get_bandwidth()` | notes |
|---|---|---|---|
| `MODE_NULL` | NULL | 1 (`nullmodem.cxx:35,69`) | Dummy; also used for `MODE_EOT` |
| `MODE_SSB` | SSB | 4 (`ssb.cxx:32,56`) | No demodulator; `CAP_TX` cleared |
| `MODE_WWV` | WWV | 200 (`wwv.cxx:71`) | 1000 Hz tick scope for sound-card clock; not a traffic mode |
| `MODE_ANALYSIS` | Freq Analysis | 4 (`analysis.h:42`) | Frequency counter |
| `MODE_FMT` | Frequency Measurement Test | 20 (`fmt.cxx:809`) | FMT contest helper |
| `MODE_EOT` | EOT | (null modem) | Dummy to send RsID-EOT code 263 |

There is **no** `MODE_FFTSCAN` in `globals.h`. Packet (`MODE_PACKET`) is commented out of the enum (`globals.h:230`). OFDM 2000/2000F are commented out.

---

## Groups that look the same on a spectrum, and what separates them

| Group | Why they collide | Time-domain / decoder feature |
|---|---|---|
| **BPSK31 vs QPSK31** (and 63/125/250/500) | Identical baud and `get_bandwidth()`; same 2-line idle if both send `tx_symbol(0)` | QPSK is 4-phase with K=5 FEC (`psk.cxx:415,981`) and `CAP_REV`. BPSK DCD pattern `0xAAAAAAAA`; QPSK **disables DCD** (`:1637`). Constellation / Viterbi metric, not FFT bins. QPSK500 has **no RsID**. |
| **BPSK vs matching 8PSK** (125/250/500/1000) | Same baud, 1 carrier | 8PSK runs at 16 kHz internally but RF baud matches. 8-ary constellation; FEC-off 8PSK idle is constant symbol 0; FEC-on 8PSK preamble is a **single centered tone** (`:2600–2606`). RsID distinguishes when present. |
| **BPSK vs PSKR** at 125/250/500/1000 | Same baud/BW | PSKR: K=7 + interleave + MFSK varicode; preamble is `1,0` pairs not 2-tone (`:2558–2571`). DCD `0x0A0A0A0A` family (`:1627–1632`). |
| **PSK63 vs PSK63F** | Same 62.5 Hz | PSK63F is PSKR-style FEC without interleaver (`:450–455`). RsID 2 vs 22. |
| **NxPSK vs NxPSKR** (e.g. 12×PSK125 vs 12×PSK125R) | Identical N, baud, 1.4 spacing, BW | Only FEC/interleave/varicode and RsID (X vs RX codes). |
| **Olivia vs Contestia, same tones/BW** | Identical MFSK comb, baud, occupied BW, start-tones | Walsh block 64 / 7-bit / rotate 13 vs block 32 / 6-bit / rotate 5 (`pj_mfsk.h:1113–1185`). Contestia prints ~2× faster and only uppercase. **RsID is the practical discriminator** (separate code lists). |
| **Olivia/Contestia 8/500 vs generic MODE_OLIVIA/CONTESTIA** | Generic defaults to 8/500 | Same RF until the user changes tones/BW. |
| **THOR vs DominoEX** at Micro/4/5/8/11/16/22 | Same 18 tones, spacing, baud, BW | THOR always FEC + longer interleave + secondary text always; DominoEX FEC is a switch and has **two RsIDs**. THOR11/16/22 can send images. IFK increment is the same `+2`. A quiet idle THOR still hops on secondary characters; DominoEX without FEC is raw varicode. **RsID** is reliable. |
| **THOR5 vs THOR11** (and EX5 vs EX11; THOR8 vs THOR16; EX8 vs EX16) | Same occupied BW (double- vs single-spaced) | Baud differs 2:1 (ds=2 vs ds=1). Tone dwell is twice as long on the slower mode — visible on a spectrogram as slower hops, not as extra tones. |
| **THOR25x4 / THOR50x2 / THOR100** | All 1800 Hz, 18 tones at 100 Hz | Baud 25 / 50 / 100. Hop rate on the waterfall. RsID 639 / 653 / 658. |
| **THOR50x1 vs 2× something at ~900 Hz** | 900 Hz box | 18 IFK tones at 50 Hz vs PSK/MFSK structure. |
| **DominoEX44 vs EX88** | Both 1550 Hz, 86.13 Hz spacing | Baud 43 vs 86 (ds=2 vs 1). |
| **MFSK16 vs Contestia 16/250 vs Olivia? ** | MFSK16 BW 234.4 vs Contestia16/250 234.4 | MFSK16 is 16 tones at 15.625 **with Gray+K=7+interleave**, one tone; Olivia has no 16/250. Hop alphabet/FEC differ; MFSK16 often carries a picture after a low-tone prologue (`mfsk.cxx:1037–1047`). |
| **MFSK8 (32 tones, 242 Hz) vs MFSK16 (16 tones, 234 Hz)** | Similar width | 32 vs 16 bins; MFSK8 slower (7.8 baud). |
| **MFSK64 vs 64L; MFSK128 vs 128L** | Identical tones/baud/BW | L modes: huge interleave (depth 400/800) and long preamble; no image. Need many seconds of decode or RsID (1026/1029 vs 620/625). |
| **MT63 S vs L** at a given BW | Identical 64-carrier brick | Interleave 32 vs 64 only. RsID differs (ST vs LG). Latency ~ twice on L. |
| **MT63-500 vs Olivia/Contestia 64/500** | ~500 Hz, many tones | MT63: 64 **constant** DBPSK carriers (no hopping). Olivia/Contestia: **one of 64** MFSK tones at a time. Spectrogram: solid bar vs a single hopping line. |
| **FSQ vs IFKP** | Both 33-tone IFK | Spacing 8.79 vs 11.72 Hz, BW 290 vs 387, sample rates 12 k vs 16 k. FSQ idle 28+30; IFKP idle 0. FSQ TX pinned at 1500. IFKP has RsID; FSQ does not. |
| **FSQ vs DominoEX Micro / THOR Micro** | Slow IFK, ~2 baud | 33 vs 18 tones; 290 vs 36 Hz. Easy on BW. |
| **NAVTEX vs SITOR-B vs RTTY 100/170** | All 170 Hz / 100 baud FSK | NAVTEX/SITOR: 4B/7B CCIR 476, phasing idle (repeating alpha/rep). RTTY 100 is Baudot/ASCII with stop bits. NAVTEX has `ZCZC`/`NNNN`. Same RF until the bitstream. |
| **RTTY 45/170 vs FSKH105** | Both ~50–170 Hz 2-tone | Hell is a **raster** (column-synchronous AM-on-FSK), not 5-bit letters. Hell105 shift is 55 Hz not 170. |
| **Feld Hell vs CW** | Both OOK at a single frequency | Hell is a 14-row column raster at 17.5 col/s (fuzzy print); CW is Morse timing. |
| **SCAMP FSK vs RTTY-like 2-tone** | Two lines | SCAMP 33.3 baud / 66.7 shift is nonstandard vs amateur RTTY tables; Golay frames, not Baudot. |
| **WEFAX vs SSTV / wide FM** | ~800 Hz FM | APT 300/675 Hz bursts, then phasing (black/white lines) then image. No RsID. |
| **8PSK125 vs 8PSK125F vs 8PSK125FL** | Same 125 Hz | FEC on/off and K=13 vs K=16; preamble (2-tone vs centered tone). RsID differs. |
| **OFDM-500F vs 4×PSK63R** | 4 carriers, few hundred Hz | OFDM: 62.5 baud, **sep 2.0**, 437.5 Hz, xPSK+K=15. 4×PSK63R: 62.5 baud, **sep 1.4**, 325 Hz, BPSK+K=7. Carrier spacing 125 vs 87.5 Hz. |

---

## Modes in this tree with **no** RsID

`MODE_CONTESTIA` (generic), `MODE_OLIVIA` (generic), `MODE_OLIVIA_64_500`, `MODE_OLIVIA_64_1000`, `MODE_QPSK500`, `MODE_MFSK4`, `MODE_SLOWHELL`, `MODE_HELLX5`, `MODE_HELLX9`, `MODE_WEFAX_576`, `MODE_WEFAX_288`, `MODE_NAVTEX`, `MODE_SITORB`, all six `MODE_SCAMP*`, `MODE_FSQ`, `MODE_OFDM_3500`, plus the non-signal modes.

DominoEX FEC and non-FEC share one `trx_mode` but have **two** RsID codes each (listed above).

---

## Practical note for an auto-ID feature

RsID (`rsid_defs.cxx` + `rsid.cxx`) already identifies most traffic modes and reports a frequency that is the **same center** `get_freq()` uses, so a hit can be passed straight to `set_freq()` / `init_modem`. Gaps are FSQ, SCAMP, WEFAX, NAVTEX/SITOR, QPSK500, Olivia 64/500 and 64/1000, MFSK4, Slow/X5/X9 Hell, and OFDM-3500.

For those, spectrum shape gets you the family (1-tone OOK, 2-tone FSK, N-line PSK, hopping MFSK, solid MT63 brick, 33-tone IFK). Same-BW twins need a short time-domain test: hop rate (single vs double spaced THOR/Domino), Walsh-block length (Olivia vs Contestia), constellation (BPSK vs QPSK vs 8PSK), or preamble/idle (PSK 2-tone vs PSKR 1/0 vs 8PSK-FEC center tone vs FSQ 28/30).
