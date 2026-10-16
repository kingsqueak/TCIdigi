# TCIdigi

TCIdigi is a Qt 6 interface on fldigi’s modems. Radio control and modem audio use TCI, the protocol used by ZeusSDR and ExpertSDR3. This build does not include hamlib, rigCAT, flrig, or the sound-card backends.

It is based on fldigi 4.2.13, commit 38708315. The license is GPL-3.0-or-later. See `COPYING`. The About box says “Based on fldigi.”

This repository is TCIdigi. It is not the upstream fldigi project.

## macOS download

The Mac DMG needs macOS 26.0 or newer on Apple silicon. DMGs 0.26 to 0.28 ran only on macOS 27; 0.29 is the first that runs on macOS 26.

Drag TCIdigi into Applications. The app is not notarized, so macOS can refuse to open it with “can’t be opened”. Clear the download quarantine once from Terminal, then open it as usual:

```sh
xattr -dr com.apple.quarantine /Applications/TCIdigi-<version>.app
```

## Lookup and logging

Call sign lookup uses QRZ (Log, QRZ account). The Log button sends the contact to the QRZ logbook when a QRZ logbook key is set, and to eQSL.cc when Log, eQSL account has “Send to eQSL when logging” turned on. The status box next to the reports shows the result from each one.

The eQSL account dialog takes the eQSL user ID, password, an optional QTH nickname (for accounts with more than one location), and a QSL message where `{CALL}`, `{NAME}` and `{MODE}` are filled in from the contact. It starts from fldigi’s eQSL settings when those are set. The upload is the one fldigi makes, sent over https.

## Build

One build from this tree produces two programs:

- **fldigi**, the FLTK program, built by the fldigi autotools files (`make -C src`). It is fldigi with the TCI and QRZ changes. `flarq` is built with it.
- **TCIdigi**, the Qt 6 program, built by CMake after that. It links the engine objects that `make -C src` compiled and uses the link line from `src/Makefile`, so fldigi has to be built first.

The two programs keep separate settings. fldigi uses `~/.fldigi` and TCIdigi uses `~/.tcidigi`.

On macOS, Homebrew packages that satisfy a build here are autoconf, automake, pkg-config, fltk, libsamplerate, libsndfile, libpng, jpeg-turbo, gettext, and Qt 6. `glibtoolize` comes from the libtool formula. The Qt program targets macOS 26.0 (the engine objects are compiled for 14.4).

```sh
autoreconf -fi
./configure
make -C src
cmake -S src/qtui -B build-qt \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0
cmake --build build-qt --target fldigi-qt
```

### Where the programs are after the build

| Program | Path after the build | How to run it |
|---|---|---|
| fldigi (FLTK) | `src/fldigi` | `./src/fldigi` |
| flarq | `src/flarq` | `./src/flarq` |
| TCIdigi (Qt), macOS | `build-qt/TCIdigi-<version>.app` | `open build-qt/TCIdigi-<version>.app`, or `./build-qt/TCIdigi` from a shell |
| TCIdigi (Qt), Linux | `build-qt/TCIdigi-<version>` | `./build-qt/TCIdigi-<version>` |

The version comes from `src/include/tcidigi_version.h`. On macOS, `build-qt/TCIdigi` is a small script that runs the current bundle, because a shell cannot run the `.app` directory itself.

Installing TCIdigi with `cmake --install build-qt --prefix /Applications` puts `/Applications/TCIdigi-<version>.app` there. Bundles from older versions are left in place. `make -C src install` installs fldigi and flarq under the configure prefix (`/usr/local/bin` by default).

A TCIdigi launch with no `--config-dir` uses `~/.tcidigi`. TCIdigi does not read `~/.fldigi`.

`src/dialogs/confdialog.cxx` is already generated. Do not run `fluid` on `confdialog.fl` unless you mean to regenerate that file.

Linux can use the same steps when Qt 6 and FLTK are installed. A Windows build from this tree has not been made.

Generated configure output, object files, and `build-qt/` are gitignored.

Before shipping a Mac DMG, run `scripts/mac-audit-bundle.py <App.app>`. It checks every library in the bundle against the macOS 26.5 SDK and fails if one imports a function macOS 26 does not have. Homebrew’s glib bottle uses `pipe2` from macOS 27, so the DMG carries a glib rebuilt from source with the macOS 26.5 SDK and `MACOSX_DEPLOYMENT_TARGET=26.0`.
