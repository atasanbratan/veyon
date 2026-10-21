#!/usr/bin/env bash
# Builds the Windows installer for Veyon + LabAudio from a native MSYS2 UCRT64 build.
# Run inside MSYS2 (MSYSTEM=UCRT64):  tools/package-windows.sh
# Output: dist/veyon-<version>-labaudio-win64-setup.exe
set -euo pipefail

ROOT="${VEYONLAB_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
SRC="$ROOT/veyon-src"
BUILD="$ROOT/build"
PREFIX=/ucrt64
QTPLUGINS="$PREFIX/share/qt6/plugins"
VERSION=$(sed -n 's/^!define VERSION "\(.*\)"/\1/p' "$BUILD/nsis/veyon.nsi")
STAGE="$ROOT/dist/stage-$VERSION"

echo "== staging Veyon $VERSION in $STAGE"
rm -rf "$STAGE"
mkdir -p "$STAGE"/{plugins,translations,crypto,imageformats,platforms,styles,tls,interception}

cp "$SRC"/3rdparty/interception/* "$STAGE/interception/"
cp "$SRC/3rdparty/ddengine/ddengine64.dll" "$STAGE/"
cp "$BUILD/core/veyon-core.dll" "$STAGE/"
find "$BUILD" -mindepth 2 -name 'veyon-*.exe' -not -path '*/CMakeFiles/*' -exec cp '{}' "$STAGE/" \;
find "$BUILD/plugins" -name '*.dll' -not -path '*/CMakeFiles/*' -exec cp '{}' "$STAGE/plugins/" \;
# Same layout as upstream: helper libraries live next to the executables.
shopt -s nullglob
for f in "$STAGE"/plugins/lib*.dll "$STAGE"/plugins/vnchooks.dll; do mv "$f" "$STAGE/"; done
shopt -u nullglob

echo "== translations"
for ts in "$SRC"/translations/veyon_*.ts; do
	lrelease -silent "$ts" -qm "$STAGE/translations/$(basename "${ts%.ts}").qm"
done

echo "== Qt plugins"
cp "$QTPLUGINS/platforms/qwindows.dll" "$STAGE/platforms/"
cp "$QTPLUGINS"/styles/*.dll "$STAGE/styles/"
cp "$QTPLUGINS/imageformats/qjpeg.dll" "$STAGE/imageformats/"
cp "$QTPLUGINS/tls/qopensslbackend.dll" "$STAGE/tls/"
cp "$QTPLUGINS/crypto/libqca-ossl.dll" "$STAGE/crypto/"

echo "== resolving DLL dependencies from $PREFIX/bin"
# Repeat until no new DLL is found: copy every imported DLL that exists in the MSYS2 prefix.
while :; do
	added=0
	while IFS= read -r dll; do
		if [ -f "$PREFIX/bin/$dll" ] && [ ! -f "$STAGE/$dll" ]; then
			cp "$PREFIX/bin/$dll" "$STAGE/"
			added=$((added + 1))
		fi
	done < <(find "$STAGE" -iname '*.dll' -o -iname '*.exe' | xargs -d '\n' objdump -p 2>/dev/null |
			 sed -n 's/^\s*DLL Name: //p' | sort -u)
	[ "$added" -eq 0 ] && break
	echo "   added $added"
done

cp "$SRC/COPYING" "$STAGE/COPYING"
cp "$SRC/COPYING" "$STAGE/LICENSE.TXT"
sed 's/$/\r/' "$SRC/README.md" > "$STAGE/README.TXT"
cp -r "$SRC/nsis" "$STAGE/"
sed -e 's/^!define INSTALLER_NAME "veyon-\${VERSION}-win64-setup.exe"/!define INSTALLER_NAME "veyon-${VERSION}-labaudio-win64-setup.exe"/' \
	"$BUILD/nsis/veyon.nsi" > "$STAGE/veyon.nsi"

# The LabAudio hub runs inside veyon-worker.exe and must accept students on every network profile
# (seen failing on a multi-homed teacher PC). Add the rule on install, remove it on uninstall.
cat > "$STAGE/fw-install.nsh" <<'NSH'
nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="Veyon Lab Audio"'
nsExec::ExecToLog 'netsh advfirewall firewall add rule name="Veyon Lab Audio" dir=in action=allow program="$INSTDIR\veyon-worker.exe" protocol=TCP localport=11500-11599 profile=any'
NSH
cat > "$STAGE/fw-uninstall.nsh" <<'NSH'
nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="Veyon Lab Audio"'
NSH
awk -v ins="$STAGE/fw-install.nsh" -v unins="$STAGE/fw-uninstall.nsh" '
	{ print }
	$0 == "File \"veyon-worker.exe\"" { while ((getline l < ins) > 0) print l; close(ins) }
	$0 == "Section Uninstall" { while ((getline l < unins) > 0) print l; close(unins) }
' "$STAGE/veyon.nsi" > "$STAGE/veyon.nsi.tmp" && mv "$STAGE/veyon.nsi.tmp" "$STAGE/veyon.nsi"
rm -f "$STAGE/fw-install.nsh" "$STAGE/fw-uninstall.nsh"
grep -q 'name="Veyon Lab Audio" dir=in' "$STAGE/veyon.nsi" || { echo "firewall rule not inserted"; exit 1; }

# The native Windows makensis only accepts backslashes in relative File/bitmap/icon paths.
sed -i -E '/^(File |!define MUI_[A-Z_]*(BITMAP|ICON) )/ s#/#\\#g' "$STAGE/veyon.nsi"

echo "== stripping"
find "$STAGE" \( -iname '*.dll' -o -iname '*.exe' \) -not -path '*/interception/*' -not -name 'ddengine64.dll' \
	-exec strip --strip-unneeded '{}' \; 2>/dev/null || true

echo "== building installer"
(cd "$STAGE" && makensis -V2 veyon.nsi)
mv "$STAGE"/veyon-*-labaudio-win64-setup.exe "$ROOT/dist/"
ls -la "$ROOT"/dist/*.exe
