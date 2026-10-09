# TCIdigi
![Example Image](TCIdigi_alpha.png)
TCIdigi is a Qt 6 interface on fldigi’s modems. Radio control and modem audio use TCI, the protocol used by ZeusSDR and ExpertSDR3. This build does not include hamlib, rigCAT, flrig, or the sound-card backends.

It is based on fldigi 4.1.23, commit 61b97f41. The license is GPL-3.0-or-later. See `COPYING`. The About box says “Based on fldigi.”

This repository is TCIdigi. It is not the upstream fldigi project.

## Build

The engine is built with the fldigi autotools files. The Qt shell is built with CMake after that, because it links the engine objects and the link line from `src/Makefile`.

On macOS, Homebrew packages that satisfy a build here are autoconf, automake, pkg-config, fltk, libsamplerate, libsndfile, libpng, jpeg-turbo, gettext, and Qt 6. `glibtoolize` comes from the libtool formula. The shell has been built with the macOS deployment target set to 14.4.

```sh
autoreconf -fi
./configure
make -C src
cmake -S src/qtui -B build-qt \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.4
cmake --build build-qt --target fldigi-qt
```

The program is `build-qt/TCIdigi.app`. Installing it with `cmake --install build-qt --prefix /Applications` replaces `/Applications/TCIdigi.app`.

A launch with no `--config-dir` uses `~/.tcidigi`. TCIdigi does not read `~/.fldigi`.

`src/dialogs/confdialog.cxx` is already generated. Do not run `fluid` on `confdialog.fl` unless you mean to regenerate that file.

Linux can use the same steps when Qt 6 and FLTK are installed. A Windows build from this tree has not been made.

Generated configure output, object files, and `build-qt/` are gitignored.
