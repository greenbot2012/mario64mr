#!/bin/bash
# Mario 64 MR on the Steam Frame, installed from the Frame itself (desktop mode): makes the game from your own Super
# Mario 64 (USA) ROM on the Frame, installs the app, and adds "Mario 64 MR" to your Steam library.
#
# Started by "Install Mario 64 MR.desktop" (double-click it in desktop mode), or by hand:
#     bash install.sh [path/to/rom]
# Running it again updates the app and the game, and keeps your saves.
#
# Environment (for testing a fork): MARIO64MR_REPO (owner/repo of the sources, default lucas-martinic/mario64mr),
# MARIO64MR_REF (branch or tag, default main), MARIO64MR_APK_REPO (where the app's release is, default
# lucas-martinic/mario64mr), MARIO64MR_APK (a local Mario64MR-SteamFrame.apk to use instead of downloading it).
set -euo pipefail

REPO="${MARIO64MR_REPO:-lucas-martinic/mario64mr}"
REF="${MARIO64MR_REF:-main}"
APK_REPO="${MARIO64MR_APK_REPO:-lucas-martinic/mario64mr}"
PACKAGE="com.lucasmartinic.mario64mr"
DIR="${HOME}/.local/share/mario64mr"
LEPTON="${HOME}/.steam/steam/steamapps/common/Lepton/lepton"
LEPTON_APPID=3029110

mkdir -p "$DIR"
exec > >(tee "$DIR/install.log") 2>&1

say() { printf '\n\033[1m== %s\033[0m\n' "$*"; }
fail() {
    printf '\n\033[31mCould not install Mario 64 MR: %s\033[0m\n' "$*"
    printf 'The full log is in %s\n' "$DIR/install.log"
    command -v notify-send >/dev/null && notify-send -i dialog-error "Mario 64 MR" "Install failed: $*" || true
    exit 1
}
ask_file() {  # a file picker in desktop mode, else nothing
    if command -v kdialog >/dev/null && [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
        kdialog --title "$1" --getopenfilename "${HOME}/Downloads" "*.z64 *.v64 *.n64 *.zip *.Z64 *.V64 *.N64 *.ZIP" 2>/dev/null || true
    fi
}

say "Checking this Steam Frame"
[ "$(uname -m)" = aarch64 ] || fail "this installer is for the Steam Frame (ARM64 SteamOS). On a PC, see the README."
command -v steamos-add-to-steam >/dev/null || fail "this needs SteamOS (steamos-add-to-steam not found)"
missing=()
for tool in python3 git make patch cc clang ld.lld curl tar; do
    command -v "$tool" >/dev/null || missing+=("$tool")
done
[ ${#missing[@]} -eq 0 ] || fail "missing tools: ${missing[*]} (they come with SteamOS on the Frame; is it up to date?)"

if [ ! -x "$LEPTON" ]; then
    say "Installing Lepton (Valve's Android support) from Steam"
    echo "Steam will ask to install \"Lepton\". Accept, and this continues once it's installed."
    xdg-open "steam://install/${LEPTON_APPID}" >/dev/null 2>&1 || steam "steam://install/${LEPTON_APPID}" >/dev/null 2>&1 &
    for _ in $(seq 1 360); do
        [ -x "$LEPTON" ] && break
        sleep 5
    done
    [ -x "$LEPTON" ] || fail "Lepton isn't installed. Install it from Steam (search your Library's Tools for Lepton) and run this again."
fi
echo "Lepton: $("$LEPTON" version 2>/dev/null | head -1)"

say "Getting the sources ($REPO, $REF)"
rm -rf "$DIR/src.new"
mkdir -p "$DIR/src.new"
curl -fsSL --retry 3 "https://github.com/$REPO/archive/$REF.tar.gz" | tar -xz -C "$DIR/src.new" --strip-components=1 ||
    fail "couldn't download https://github.com/$REPO/archive/$REF.tar.gz"
rm -rf "$DIR/src"
mv "$DIR/src.new" "$DIR/src"
TOOL="$DIR/src/install_mario64mr.py"
[ -f "$TOOL" ] || fail "the sources have no install_mario64mr.py"

say "Finding your Super Mario 64 (USA) ROM"
# A ROM inside a .zip is taken out of it first; every candidate is checked by the tool (unmodified US release only)
rom_ok() { python3 "$TOOL" "$1" --check >/dev/null 2>&1; }
unzip_rom() {  # prints the path of the first N64 ROM inside a zip, extracted into $DIR/rom-from-zip
    python3 - "$1" "$DIR/rom-from-zip" <<'EOF'
import sys, zipfile, pathlib
src, out = sys.argv[1], pathlib.Path(sys.argv[2])
try:
    z = zipfile.ZipFile(src)
except zipfile.BadZipFile:
    sys.exit(1)
for info in z.infolist():
    if info.filename.lower().endswith((".z64", ".v64", ".n64")) and info.file_size == 8 * 1024 * 1024:
        out.mkdir(parents=True, exist_ok=True)
        target = out / pathlib.PurePath(info.filename).name
        target.write_bytes(z.read(info))
        print(target)
        break
EOF
}
try_rom() {  # prints a usable ROM path for a candidate file (a ROM, or a zip with one), or nothing
    case "${1,,}" in
        *.zip) local inside; inside="$(unzip_rom "$1" || true)"; [ -n "$inside" ] && rom_ok "$inside" && echo "$inside" ;;
        *) rom_ok "$1" && echo "$1" ;;
    esac
    return 0
}
ROM=""
if [ -n "${1:-}" ]; then
    ROM="$(try_rom "$1")"
    [ -n "$ROM" ] || { python3 "$TOOL" "$1" --check || true; fail "$1 isn't a Super Mario 64 (USA) ROM (see above)"; }
fi
if [ -z "$ROM" ]; then
    while IFS= read -r -d '' candidate; do
        ROM="$(try_rom "$candidate")"
        [ -n "$ROM" ] && break
    done < <(find "${HOME}/Downloads" "${HOME}/Desktop" "${HOME}/Documents" "${HOME}" -maxdepth 2 -type f \
                  \( -iname '*.z64' -o -iname '*.v64' -o -iname '*.n64' -o -iname '*.zip' \) -size -100M -print0 2>/dev/null)
fi
if [ -z "$ROM" ]; then
    echo "No Super Mario 64 (USA) ROM in Downloads, Desktop or Documents. Choose it..."
    picked="$(ask_file "Choose your Super Mario 64 (USA) ROM")"
    [ -n "$picked" ] || fail "no ROM. Put your Super Mario 64 (USA) ROM (.z64, .v64 or .n64) in Downloads and run this again."
    ROM="$(try_rom "$picked")"
    [ -n "$ROM" ] || { python3 "$TOOL" "$picked" --check || true; fail "$picked isn't a Super Mario 64 (USA) ROM (see above)"; }
fi
echo "ROM: $ROM"

say "Getting the app (Mario64MR-SteamFrame.apk)"
mkdir -p "$DIR/app"
if [ -n "${MARIO64MR_APK:-}" ]; then
    cp "$MARIO64MR_APK" "$DIR/app/Mario64MR-SteamFrame.apk.part"
else
    curl -fL --retry 3 --progress-bar -o "$DIR/app/Mario64MR-SteamFrame.apk.part" \
        "https://github.com/$APK_REPO/releases/latest/download/Mario64MR-SteamFrame.apk" ||
        fail "couldn't download the app from https://github.com/$APK_REPO/releases"
fi
# (Lepton reinstalls the app when its file changes, so only replace it when it did)
if ! cmp -s "$DIR/app/Mario64MR-SteamFrame.apk.part" "$DIR/app/Mario64MR-SteamFrame.apk" 2>/dev/null; then
    mv "$DIR/app/Mario64MR-SteamFrame.apk.part" "$DIR/app/Mario64MR-SteamFrame.apk"
else
    rm "$DIR/app/Mario64MR-SteamFrame.apk.part"
fi

say "Making the game from your ROM (the first time about 10 minutes and a 700 MB download)"
cd "$DIR/src"
python3 "$TOOL" "$ROM" --out "$DIR/game" --work "$DIR/work" || fail "making the game failed (see above)"
# The NDK download is only needed again for a new NDK version, and a ROM taken out of a zip isn't needed at all
rm -f "$DIR/work/"android-ndk-*.zip
rm -rf "$DIR/rom-from-zip"

say "Putting the game where the app finds it"
# Lepton gives each Steam launch its own Android storage: /sdcard is $COMPAT/external (kept across launches and app
# updates), so the game goes straight into the app's folder there, no adb needed
FILES="$DIR/compat/external/Android/data/$PACKAGE/files"
mkdir -p "$FILES/sm64/res" "$FILES/native"
cp "$DIR/game/baserom.us.z64" "$FILES/"
cp "$DIR/game/sm64/res/base.zip" "$FILES/sm64/res/"
cp "$DIR/game/native/"*.so "$FILES/native/"

say "Adding Mario 64 MR to your Steam library"
cat >"$DIR/launch.sh" <<EOF
#!/bin/bash
# Starts Mario 64 MR in Lepton the way Steam starts an Android game, with its own storage in $DIR/compat
export SteamAppId="\${SteamAppId:-0}"
[ "\$SteamAppId" != 0 ] || export SteamAppId=4064064064
export STEAM_COMPAT_DATA_PATH="$DIR/compat"
export STEAM_COMPAT_INSTALL_PATH="$DIR/app"
export STEAM_COMPAT_LIBRARY_PATHS="$DIR"
export STEAM_COMPAT_CLIENT_INSTALL_PATH="\${STEAM_COMPAT_CLIENT_INSTALL_PATH:-\$HOME/.steam/steam}"
# (Steam sets a Fossilize dump path for every launch; Lepton then needs the shader cache folder it sits in)
export STEAM_COMPAT_SHADER_PATH="$DIR/shadercache"
export STEAM_FOSSILIZE_DUMP_PATH="$DIR/shadercache/fozpipelinesv6/steamapp_pipeline_cache"
mkdir -p "\$STEAM_FOSSILIZE_DUMP_PATH"
exec "$LEPTON" waitforexitandrun -- "$DIR/app/Mario64MR-SteamFrame.apk"
EOF
chmod +x "$DIR/launch.sh"
cat >"$DIR/Mario 64 MR.desktop" <<EOF
[Desktop Entry]
Name=Mario 64 MR
Comment=Mixed reality Super Mario 64
Exec=$DIR/launch.sh
Icon=applications-games
Terminal=false
Type=Application
Categories=Game;
EOF
chmod +x "$DIR/Mario 64 MR.desktop"
in_steam() {  # is there a Steam shortcut that starts $DIR/launch.sh? (also after one was deleted: add it again)
    python3 - "$DIR/launch.sh" "${HOME}"/.steam/steam/userdata/*/config/shortcuts.vdf <<'EOF'
import re, sys
launcher = sys.argv[1].encode()
for path in sys.argv[2:]:
    try:
        data = open(path, "rb").read()
    except OSError:
        continue
    # binary VDF: each shortcut has an "Exe" string field, \x01Exe\x00<value>\x00
    if any(launcher in m.group(1) for m in re.finditer(rb"\x01Exe\x00([^\x00]*)\x00", data, re.I)):
        sys.exit(0)
sys.exit(1)
EOF
}
rm -f "$DIR/.added-to-steam"
if in_steam; then
    echo "Already in your Steam library."
else
    steamos-add-to-steam "$DIR/Mario 64 MR.desktop" || fail "couldn't add it to Steam. Is Steam running?"
fi

say "Done"
echo "Mario 64 MR is in your Steam library. Go back to the headset view (Return to Gaming Mode) and start it from"
echo "your Library. The first start takes a little longer while Lepton installs the app."
command -v notify-send >/dev/null && notify-send -i applications-games "Mario 64 MR" "Installed. Start it from your Steam library." || true
