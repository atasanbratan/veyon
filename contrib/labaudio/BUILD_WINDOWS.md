# Building Veyon + LabAudio on Windows (native MSYS2, no admin rights)

Layout: `veyonlab/msys64` (portable MSYS2), `veyonlab/veyon-src` (this repo, branch `labaudio`), `veyonlab/build`.

1. Extract `msys2-x86_64-latest.tar.xz` to `veyonlab/msys64`, then in `msys64/usr/bin/bash.exe -l` (MSYSTEM=UCRT64):
   `pacman -Syu` (twice), then
   `pacman -S mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,qt6-base,qt6-multimedia,qt6-tools,qt6-translations,qt6-5compat,qt6-declarative,qt6-httpserver,qt6-websockets,qca-qt6,opus,lzo2,openssl,openldap,cyrus-sasl,libjpeg-turbo,libpng,zlib,nsis,pkgconf,libvncserver,gdb} git`
2. Interception library (Veyon's Windows input locking), from https://github.com/oblitum/Interception (LGPLv3 for
   non-commercial use): `gcc -O2 -shared -DINTERCEPTION_EXPORT -o interception.dll library/interception.c
   -Wl,--out-implib,libinterception.dll.a`, copy the DLL to `/ucrt64/bin`, the `.dll.a` to `/ucrt64/lib`,
   `interception.h` to `/ucrt64/include`.
3. `git submodule update --init --depth 1`
4. `cmake ../veyon-src -G Ninja -DCMAKE_BUILD_TYPE=Release -DWITH_WERROR=OFF -DWITH_TRANSLATIONS=OFF` in `veyonlab/build`, then `ninja`.
5. `bash veyon-src/contrib/labaudio/package-windows.sh` → `dist/veyon-<version>-labaudio-win64-setup.exe`.

Tests without Veyon: `cmake -S plugins/labaudio/selftest -B ../build-selftest -G Ninja && ninja -C ../build-selftest`, then
`labaudio_selftest.exe` (codec/jitter/mixer) and `labaudio_loopback.exe 6 speak|listen|talk` (real headset, local TCP).
