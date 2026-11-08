#!/usr/bin/env python3
"""
Mario 64 MR, made from your own Super Mario 64 ROM (US) and put on your headset.

The app (Mario64MR.apk) carries none of Nintendo's game: no data and no code. This tool makes both on your own
computer, from your ROM and from the community's public sources, fetched at pinned revisions:

  the game data    extracted from your ROM with the sm64ex PC port's own tools
                     Mario64MRData/baserom.us.z64      the ROM, for the title screen's music and sounds
                     Mario64MRData/sm64/res/base.zip   sm64ex's resource pack: the textures and the sound banks
  the game code    compiled for the headset with Android's NDK
                     Mario64MRData/native/libsm64game.so   the whole game: sm64ex with Mario 64 MR's changes
                                                           (native/fullgame)
                     Mario64MRData/native/libgoddard.so    the title screen's face: the decomp's Goddard
                                                           (n64decomp/sm64, with native/goddard)
                     Mario64MRData/native/libsm64.so       the title screen's sound (libsm64)

usage:
    python3 install_mario64mr.py "Super Mario 64 (USA).z64"            # makes ./Mario64MRData
    python3 install_mario64mr.py "Super Mario 64 (USA).z64" --push     # ...and adb pushes it to the headset
    python3 install_mario64mr.py "Super Mario 64 (USA).z64" --push --label my-label.png
                                                                       # ...with your own cartridge label
    python3 install_mario64mr.py "Super Mario 64 (USA).z64" --check    # only checks the ROM

Needs python3, git, make, patch and a C compiler (macOS: Xcode command line tools; Linux: build-essential;
Windows: WSL). The Android NDK (r27c) is found where Android Studio or Homebrew put it, or downloaded (~700 MB) the
first time. The ROM may be .z64, .v64 or .n64; it must be the unmodified US release (SHA-1 9bef1128...), checked
before anything else: a different region, a ROM hack, a bad dump or a zipped ROM is turned down with the reason.
"""
import argparse
import glob
import hashlib
import os
import platform
import re
import shutil
import stat
import struct
import subprocess
import sys
import time
import urllib.request
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "native/fullgame"))
import patch_sm64ex  # noqa: E402

SM64EX = "https://github.com/sm64pc/sm64ex.git"
SM64EX_REVISION = patch_sm64ex.REVISION
DECOMP = "https://github.com/n64decomp/sm64.git"
DECOMP_REVISION = "9921382a68bb0c865e5e45eb594d9c64db59b1af"
LIBSM64 = "https://github.com/libsm64/libsm64.git"
LIBSM64_REVISION = "fd11813208272b4271d92bd92feb8f3fdbe61be5"
NDK_VERSION = "r27c"
US_SHA1 = "9bef1128717f958171a4afac3ed78ee2bb4e86ce"
PACKAGE = "com.lucasmartinic.mario64mr"
DEVICE_FOLDER = f"/sdcard/Android/data/{PACKAGE}/files"
PACK_BUILD = "build/pack"
GAME_BUILD = "build/us_android_60_ext"
LIBRARIES = ("libsm64game.so", "libgoddard.so", "libsm64.so")


def run(cmd, cwd=None, env=None):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], cwd=cwd, check=True, env=env)


ROM_SIZE = 8 * 1024 * 1024
ROM_ORDERS = {  # the first word of an N64 ROM, as each kind of dump stores it
    b"\x80\x37\x12\x40": ".z64 (big-endian)",
    b"\x37\x80\x40\x12": ".v64 (byte-swapped)",
    b"\x40\x12\x37\x80": ".n64 (little-endian)",
}
REGIONS = {"E": "USA", "J": "Japan", "P": "Europe"}
ARCHIVES = {b"PK\x03\x04": "a .zip", b"7z\xbc\xaf": "a .7z", b"Rar!": "a .rar", b"\x1f\x8b": "a .gz"}


class RomError(Exception):
    pass


def rom_problem(path, message, hint):
    return RomError(f"{path}: {message}\n  {hint}\n  Nothing was built or sent to the headset.")


def describe_rom(rom):
    """What an N64 ROM's header says it is (in .z64 order): its name, game code, region and revision."""
    name = rom[0x20:0x34].decode("ascii", "replace").strip(" \0")
    code = rom[0x3B:0x3F].decode("ascii", "replace")
    region = REGIONS.get(code[3:], f"region '{code[3:]}'")
    if code == "NSMJ" and rom[0x3F] == 3:
        region = "Japan, Shindou edition"
    return name, code, region, rom[0x3F]


def read_rom(path):
    """The ROM in .z64 byte order (big-endian), from any of the three dump orders, checked to be exactly the
    unmodified US release, with a clear reason when it isn't (before anything is downloaded, built or pushed)."""
    if not path.is_file():
        raise rom_problem(path, "no such file", "Give the path to your ROM file (in quotes if it has spaces).")
    data = path.read_bytes()
    head = data[:4]
    for magic, kind in ARCHIVES.items():
        if head.startswith(magic):
            raise rom_problem(path, f"this is {kind} archive, not a ROM",
                              "Extract it first, and give the .z64 / .v64 / .n64 file inside.")
    order = ROM_ORDERS.get(head)
    if order is None:
        raise rom_problem(path, "not a Nintendo 64 ROM (its header isn't one)",
                          "Use a Super Mario 64 (USA) dump: a .z64, .v64 or .n64 file of about 8 MB.")
    rom = bytearray(data)
    if order.startswith(".v64"):
        rom[0::2], rom[1::2] = rom[1::2], rom[0::2]
    elif order.startswith(".n64"):
        for i in range(0, len(rom) - 3, 4):
            rom[i:i + 4] = rom[i:i + 4][::-1]
    rom = bytes(rom)
    sha1 = hashlib.sha1(rom).hexdigest()
    if sha1 == US_SHA1:
        print(f"ROM: Super Mario 64 (USA), {order}, SHA-1 matches", flush=True)
        return rom
    name, code, region, revision = describe_rom(rom)
    expected = f"An unmodified Super Mario 64 (USA) dump has SHA-1 {US_SHA1} (as .z64)."
    if code[1:3] != "SM" or "MARIO" not in name.upper():
        raise rom_problem(path, f"this is an N64 ROM of \"{name}\" (game code {code}), not Super Mario 64", expected)
    if code != "NSME":
        raise rom_problem(path, f"this is Super Mario 64 ({region}): only the USA release is supported",
                          "The game is built from the decompilation's US version, which needs the US ROM. " + expected)
    if len(rom) != ROM_SIZE:
        raise rom_problem(path, f"Super Mario 64 (USA) by its header, but {len(rom) / 1048576:.0f} MB instead of 8 MB: "
                          "a ROM hack (they're often extended) or a bad dump", expected)
    raise rom_problem(path, f"Super Mario 64 (USA) by its header, but its contents differ (SHA-1 {sha1}): "
                      "a ROM hack, a patched or trainer ROM, or a bad dump", expected)


def fetch(url, revision, work, shallow=False):
    """A checkout of url at revision, in work (kept for the next run)."""
    if not (work / ".git").is_dir():
        work.mkdir(parents=True, exist_ok=True)
        run(["git", "init", "--quiet"], cwd=work)
        run(["git", "remote", "add", "origin", url], cwd=work)
    run(["git", "fetch", "--quiet"] + (["--depth", "1"] if shallow else []) + ["origin", revision], cwd=work)
    run(["git", "checkout", "--quiet", "--force", "--detach", revision], cwd=work)


# ------------------------------------------------------------------------------------------------ the game data

def prepare_pack(work, rom):
    # macOS ships GNU Make 3.81, which predates "!=" assignments
    makefile = subprocess.check_output(["git", "show", "HEAD:Makefile"], cwd=work, text=True)
    (work / "Makefile").write_text(re.sub(r"^(\w+) != (.*)$", r"\1 := $(shell \2)", makefile, flags=re.M))
    split = subprocess.check_output(["git", "show", "HEAD:Makefile.split"], cwd=work, text=True)
    (work / "Makefile.split").write_text(re.sub(r"^define (\w+) =[^\S\n]*$", r"define \1", split, flags=re.M))
    (work / "baserom.us.z64").write_bytes(rom)
    # Sequence 0 is data-only assembly upstream: take its bytes from the ROM instead of needing a MIPS assembler
    table = 0x7B0860
    offset, size = struct.unpack_from(">II", rom, table + 4)
    (work / "sound/sequences/00_sound_player.m64").write_bytes(rom[table + offset:table + offset + size])
    (work / "sound/sequences/00_sound_player.s").unlink(missing_ok=True)


def build_pack(work):
    run(["make", "-C", "tools", "-j1"], cwd=work)  # (its audiofile library is not parallel-safe)
    run([sys.executable, "extract_assets.py", "us"], cwd=work)
    # Only the resource pack's parts of the game build: the sound banks, and the skyboxes (and cake) cut into tiles
    targets = [f"{PACK_BUILD}/sound/sound_data.ctl", f"{PACK_BUILD}/sound/sequences.bin"]
    targets += [f"{PACK_BUILD}/bin/{p.stem}_skybox.c" for p in sorted((work / "textures/skyboxes").glob("*.png"))]
    targets.append(f"{PACK_BUILD}/levels/ending/cake.inc.c")  # (the ending's cake picture, cut the same way)
    # (the system's compiler and preprocessor: on macOS the Makefile would look for Homebrew's gcc)
    run(["make", "-j4", "VERSION=us", "EXTERNAL_DATA=1", "BUILD_DIR=" + PACK_BUILD, "NOEXTRACT=1", "HOST_OS=Linux",
         "OSX_BUILD=0", "CC=cc", "CXX=c++", "CPP=cc -E -P -x c"] + targets, cwd=work)


def pack(work, out):
    """base.zip as sm64ex's own "res" target makes it (Makefile, basepack.lst)."""
    files = [(work / PACK_BUILD / "sound" / n, "sound/" + n) for n in ("bank_sets", "sequences.bin", "sound_data.ctl", "sound_data.tbl")]
    tiles = work / PACK_BUILD / "textures/skybox_tiles"
    files += [(p, "gfx/" + p.relative_to(work / PACK_BUILD).as_posix()) for p in sorted(tiles.rglob("*.png"))]
    for top in ("actors", "levels", "textures"):
        files += [(p, "gfx/" + p.relative_to(work).as_posix()) for p in sorted((work / top).rglob("*.png"))]
    missing = [str(p) for p, _ in files if not p.is_file()]
    if missing:
        sys.exit("not made: " + ", ".join(missing[:5]))
    zip_path = out / "sm64/res/base.zip"
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for src, name in files:
            z.write(src, name)
    print(f"{zip_path}: {len(files)} files, {zip_path.stat().st_size / 1e6:.1f} MB")


# ------------------------------------------------------------------------------------------------ the game code

def host_tag():
    system = platform.system()
    if system == "Darwin":
        return "darwin-x86_64"  # (universal: Apple silicon too)
    if system == "Linux":
        return "linux-x86_64"
    sys.exit(f"{system} isn't supported: on Windows, run this in WSL")


def ndk_compiler(ndk):
    return Path(ndk) / "toolchains/llvm/prebuilt" / host_tag() / "bin/aarch64-linux-android29-clang"


def find_ndk(work, given):
    """An Android NDK (r27 or later works; r27c is what the app is built with), else one downloaded into work."""
    candidates = [given] if given else []
    candidates += [os.environ.get(v) for v in ("ANDROID_NDK_HOME", "ANDROID_NDK_ROOT", "ANDROID_NDK")]
    home = Path.home()
    for pattern in (home / "Library/Android/sdk/ndk/*", home / "Android/Sdk/ndk/*",
                    "/opt/homebrew/share/android-ndk", "/usr/local/share/android-ndk",
                    "/Applications/Unity/Hub/Editor/*/PlaybackEngines/AndroidPlayer/NDK", ndk_home(work) / "ndk",
                    work / "ndk"):
        candidates += sorted(glob.glob(str(pattern)), reverse=True)
    for c in candidates:
        if c and ndk_compiler(c).is_file():
            print(f"Android NDK: {c}")
            return Path(c)
    return download_ndk(work)


def on_windows_drive(path):
    """A path on a Windows drive under WSL (/mnt/c/...): slow, and Windows' path length limit applies."""
    return Path(path).resolve().as_posix().startswith("/mnt/") and "microsoft" in platform.uname().release.lower()


WINDOWS_PATH_LIMIT = 260  # (MAX_PATH, as C:\... counts it)


def ndk_home(work):
    """Where a downloaded NDK goes: with the work folder, unless that's on a Windows drive under WSL, where its long
    paths can go past Windows' limit and its 1 GB of small files take ages: then in the Linux home."""
    return Path.home() / ".cache/mario64mr" if on_windows_drive(work) else work


class Progress:
    """Prints how far along something long is, every 5%, so it never looks stuck."""

    def __init__(self, what):
        self.what, self.shown = what, -1

    def __call__(self, done, total):
        if total <= 0:
            return
        step = min(100, done * 100 // total) // 5 * 5
        if step != self.shown:
            self.shown = step
            print(f"  {self.what}: {step}%", flush=True)


def extract_ndk_zip(archive, unpacked):
    """Unzips the NDK with its executable bits and symlinks, showing progress, and stops with the reason (rather than
    hanging, or going on half unpacked) when a file can't be written: on a Windows drive, usually the path length."""
    with zipfile.ZipFile(archive) as z:
        entries = z.infolist()
        if on_windows_drive(unpacked):
            longest = max(entries, key=lambda e: len(e.filename))
            # (/mnt/c/... is C:\..., 4 characters fewer)
            length = len((unpacked / longest.filename).as_posix()) - 4
            if length >= WINDOWS_PATH_LIMIT:
                sys.exit(f"Can't unpack the Android NDK in {unpacked}: its longest path would be {length} characters "
                         f"there, past Windows' limit of {WINDOWS_PATH_LIMIT - 1}.\n"
                         f"  Run the tool from your WSL home instead (cd ~), or give a shorter --work folder.")
        total, done = sum(e.file_size for e in entries), 0
        progress = Progress("Unpacking the Android NDK")
        root = unpacked.resolve()
        for entry in entries:
            path = unpacked / entry.filename
            if root not in path.resolve().parents and path.resolve() != root:
                sys.exit(f"{archive}: unexpected path inside it: {entry.filename}")
            mode = entry.external_attr >> 16
            try:
                if entry.is_dir():
                    path.mkdir(parents=True, exist_ok=True)
                    continue
                path.parent.mkdir(parents=True, exist_ok=True)
                if stat.S_ISLNK(mode):
                    os.symlink(z.read(entry).decode(), path)
                else:
                    with z.open(entry) as source, open(path, "wb") as file:
                        shutil.copyfileobj(source, file, 1 << 20)
                    if mode & 0o777:
                        os.chmod(path, mode & 0o777)
            except OSError as error:
                hint = ("On a Windows drive this is usually Windows' path length limit: run the tool from your WSL home "
                        "(cd ~), or give a shorter --work folder." if on_windows_drive(unpacked) else
                        "Check there's enough free space (the NDK needs about 2 GB unpacked), or give another --work folder.")
                sys.exit(f"Unpacking the Android NDK failed at {path} ({len(path.as_posix())} characters):\n"
                         f"  {error}\n  {hint}")
            done += entry.file_size
            progress(done, total)


def download_ndk(work):
    system = platform.system()
    name = f"android-ndk-{NDK_VERSION}-" + ("darwin.dmg" if system == "Darwin" else "linux.zip")
    url = "https://dl.google.com/android/repository/" + name
    home = ndk_home(work)
    home.mkdir(parents=True, exist_ok=True)
    if home != work:
        print(f"The Android NDK goes in {home} (your work folder is on a Windows drive, where it wouldn't fit)")
    archive = home / name
    if not archive.is_file():
        print(f"Downloading the Android NDK ({url}, about 700 MB)...", flush=True)
        part = archive.with_suffix(".part")
        progress = Progress("Downloading the Android NDK")
        urllib.request.urlretrieve(url, part, lambda blocks, size, total: progress(blocks * size, total))
        part.rename(archive)
    target = home / "ndk"
    shutil.rmtree(target, ignore_errors=True)
    if system == "Darwin":
        mount = work / "ndk-mount"
        mount.mkdir(exist_ok=True)
        run(["hdiutil", "attach", "-nobrowse", "-readonly", "-mountpoint", mount, archive])
        try:
            apps = glob.glob(str(mount / "*.app/Contents/NDK"))
            if not apps:
                sys.exit(f"No NDK inside {archive}")
            run(["cp", "-R", apps[0], target])
        finally:
            run(["hdiutil", "detach", mount])
    else:
        unpacked = home / "ndk-unpacked"
        shutil.rmtree(unpacked, ignore_errors=True)
        extract_ndk_zip(archive, unpacked)
        (unpacked / f"android-ndk-{NDK_VERSION}").rename(target)
        shutil.rmtree(unpacked)
    if not ndk_compiler(target).is_file():
        sys.exit(f"The NDK in {target} has no {ndk_compiler(target).name}")
    return target


def build_game(work, rom, ndk, out):
    """libsm64game.so: sm64ex with the PC port's 60 fps interpolation, the Unity bridge and Mario 64 MR's changes,
    its data left out (it reads base.zip at runtime)."""
    patch_sm64ex.prepare(work, rom, sixty=True, external_data=True)
    # The attract demos' inputs are game data compiled in even with external data: blank them (never played, the
    # demo timer is off)
    for demo in (work / "assets/demos").glob("*.bin"):
        demo.write_bytes(bytes(4))
    cc = str(ndk_compiler(ndk))
    flags = "-Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types"
    run(["make", "-j" + str(os.cpu_count() or 4), "HOST_OS=Linux", "OSX_BUILD=0", "BUILD_DIR=" + GAME_BUILD,
         "CC=" + cc, "CXX=" + cc + "++", "LD=" + cc, "CPP=" + cc + " -E -P -x c",
         "RENDER_API=UNITY", "WINDOW_API=UNITY", "AUDIO_API=UNITY", "CONTROLLER_API=UNITY", "NOEXTRACT=1",
         "EXTERNAL_DATA=1", "EXT_OPTIONS_MENU=1", "TEXTURE_FIX=0", "BETTERCAMERA=0", "NODRAWINGDISTANCE=0",
         "LDFLAGS=-shared -Wl,--no-undefined -Wl,-soname,libsm64game.so -Wl,-z,max-page-size=16384 -lm",
         "OPT_FLAGS=-O2 -fPIC " + flags,
         f"CC_CHECK={cc} -fsyntax-only -fPIC -Isrc -Iinclude -I. -I{GAME_BUILD} -I{GAME_BUILD}/include -DVERSION_US "
         f"-D_LANGUAGE_C -DNON_MATCHING -DAVOID_UB -DF3DEX_GBI_2E -DRAPI_UNITY -DWAPI_UNITY -DAAPI_UNITY {flags}"],
        cwd=work)
    shutil.copy2(work / GAME_BUILD / "sm64.us.f3dex2e", out / "libsm64game.so")


def build_goddard(decomp, extracted, ndk, work, out):
    """libgoddard.so: the decomp's Goddard (the title screen's face) as a library, with Mario 64 MR's patches (two
    hands, holding, depth) and a small API (native/goddard/shim). Its textures are blanks, filled at runtime from
    base.zip."""
    kit = HERE / "native/goddard"
    src = work / "src"
    shutil.rmtree(src, ignore_errors=True)
    src.mkdir(parents=True)
    shutil.copytree(decomp / "src/goddard", src / "goddard")
    for p in sorted((kit / "patches").glob("*.patch")):
        with open(p) as f:
            print(f"+ patch {p.name}", flush=True)
            subprocess.run(["patch", "-s", "-d", str(src / "goddard"), "-p1"], stdin=f, check=True)
    with open(src / "goddard/renderer.c", "a") as f:
        f.write('#include "gd_texture_table.inc.c"\n')
    gen = work / "gen"
    shutil.rmtree(gen, ignore_errors=True)
    run([sys.executable, kit / "gen_textures.py", decomp, extracted, gen])
    sources = sorted((src / "goddard").glob("*.c")) + sorted((src / "goddard/dynlists").glob("*.c"))
    sources += [decomp / "lib/src" / n for n in ("guPerspectiveF.c", "guOrthoF.c", "guRotateF.c", "guTranslateF.c",
                                                 "guMtxF2L.c", "guScaleF.c", "guNormalize.c")]
    sources += [kit / "shim/n64_stubs.c", kit / "shim/gd_api.c"]
    defines = ["-DVERSION_US=1", "-DF3D_OLD=1", "-DNON_MATCHING=1", "-DAVOID_UB=1", "-D_LANGUAGE_C",
               "-DNO_SEGMENTED_MEMORY", "-DGBI_FLOATS"]
    includes = [f"-I{gen}", f"-I{src}", f"-I{decomp}/include", f"-I{decomp}/src", f"-I{decomp}", f"-I{kit}/shim"]
    run([ndk_compiler(ndk), "-std=gnu99", "-O2", "-fPIC", "-fno-strict-aliasing", "-fsigned-char", "-fvisibility=hidden",
         "-w", "-U_FORTIFY_SOURCE", "-D_FORTIFY_SOURCE=0"] + defines + includes +
        ["-shared", "-Wl,-soname,libgoddard.so", "-Wl,-z,max-page-size=16384", "-o", out / "libgoddard.so"]
        + sources + ["-lm"])


def build_libsm64(libsm64, ndk, out):
    """libsm64.so: libsm64 as it is upstream (the title screen plays the game's music and sounds through it, from
    your ROM)."""
    if not (libsm64 / "src/decomp/mario/geo.inc.c").is_file():
        run([sys.executable, "import-mario-geo.py"], cwd=libsm64)  # (Mario's model, from the decomp's sources)
    dirs = ["src", "src/decomp", "src/decomp/engine", "src/decomp/include/PR", "src/decomp/game", "src/decomp/pc",
            "src/decomp/pc/audio", "src/decomp/mario", "src/decomp/tools", "src/decomp/audio"]
    sources = sorted(str(p.relative_to(libsm64)) for d in dirs for p in (libsm64 / d).glob("*.c"))
    run([ndk_compiler(ndk), "-O2", "-fno-strict-aliasing", "-Wno-everything", "-fPIC", "-fvisibility=hidden",
         "-DSM64_LIB_EXPORT", "-DGBI_FLOATS", "-DVERSION_US", "-DNO_SEGMENTED_MEMORY", "-I", "src/decomp/include",
         "-shared", "-Wl,-soname,libsm64.so", "-Wl,-z,max-page-size=16384", "-o", out / "libsm64.so"]
        + sources + ["-lm"], cwd=libsm64)


# ------------------------------------------------------------------------------------------------ the headset

def add_label(picture, out):
    """Your own cartridge label (the app has a plain one): a PNG or JPEG, square, with the label in the middle as
    tall as the picture."""
    ext = picture.suffix.lower()
    if ext not in (".png", ".jpg", ".jpeg"):
        sys.exit(f"{picture}: the label must be a .png or .jpg picture")
    head = picture.read_bytes()[:4]
    if not (head.startswith(b"\x89PNG") or head.startswith(b"\xff\xd8")):
        sys.exit(f"{picture}: not a PNG or JPEG picture")
    for old in out.glob("label.*"):
        old.unlink()
    shutil.copy2(picture, out / ("label" + (".png" if ext == ".png" else ".jpg")))


def push(out):
    devices = subprocess.run(["adb", "devices"], capture_output=True, text=True, check=True).stdout
    if "\tdevice" not in devices:
        sys.exit("No headset on adb: connect it (developer mode, USB debugging) and run again with --push")
    # The app's own folder, made by the app (one made by adb may not be the app's): open it once if needed
    if subprocess.run(["adb", "shell", "ls", DEVICE_FOLDER], capture_output=True).returncode != 0:
        if PACKAGE not in subprocess.run(["adb", "shell", "pm", "list", "packages", PACKAGE], capture_output=True, text=True).stdout:
            sys.exit("Mario 64 MR is not installed: adb install -r -g Mario64MR.apk first")
        print("Opening Mario 64 MR once, so it makes its data folder...")
        run(["adb", "shell", "monkey", "-p", PACKAGE, "-c", "android.intent.category.LAUNCHER", "1"])
        for _ in range(30):
            if subprocess.run(["adb", "shell", "ls", DEVICE_FOLDER], capture_output=True).returncode == 0:
                break
            time.sleep(1)
        else:
            sys.exit("Mario 64 MR did not start: put the headset on (it must be awake), open it once, and run again")
        run(["adb", "shell", "am", "force-stop", PACKAGE])
    run(["adb", "shell", "mkdir", "-p", DEVICE_FOLDER + "/sm64/res", DEVICE_FOLDER + "/native"])
    run(["adb", "push", out / "baserom.us.z64", DEVICE_FOLDER + "/"])
    run(["adb", "push", out / "sm64/res/base.zip", DEVICE_FOLDER + "/sm64/res/"])
    run(["adb", "push"] + [out / "native" / n for n in LIBRARIES] + [DEVICE_FOLDER + "/native/"])
    labels = sorted(out.glob("label.*"))
    if labels:
        run(["adb", "shell", "rm", "-f", DEVICE_FOLDER + "/label.png", DEVICE_FOLDER + "/label.jpg"])
        run(["adb", "push", labels[0], DEVICE_FOLDER + "/"])
    run(["adb", "shell", "am", "force-stop", PACKAGE])
    print("On the headset. Start Mario 64 MR from the library (Unknown sources).")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rom", type=Path, help="your Super Mario 64 (US) ROM")
    parser.add_argument("--out", type=Path, default=Path("Mario64MRData"), help="where to put it all (./Mario64MRData)")
    parser.add_argument("--work", type=Path, default=Path("Mario64MRData-work"),
                        help="sources, NDK and builds, kept to make a second run quick (./Mario64MRData-work)")
    parser.add_argument("--ndk", type=Path, help="an Android NDK to use (else found, or downloaded)")
    parser.add_argument("--push", action="store_true", help="also adb push it to the headset")
    parser.add_argument("--label", type=Path, help="your own cartridge label: a square PNG or JPEG (see the README)")
    parser.add_argument("--check", action="store_true", help="only check the ROM, and stop")
    args = parser.parse_args()
    # The ROM first: the most likely thing to be wrong, and nothing should be fetched, built or pushed until it's right
    try:
        rom = read_rom(args.rom)
    except RomError as problem:
        sys.exit(str(problem))
    if args.check:
        return
    for tool in ("git", "make", "patch", "cc"):
        if shutil.which(tool) is None:
            sys.exit(f"{tool} not found (see the top of this script for what is needed)")
    work, out = args.work.resolve(), args.out.resolve()
    if on_windows_drive(work):
        print(f"Note: {work} is on a Windows drive. It works, but building there is much slower than in your WSL "
              "home: cd ~ and run the tool from there.", flush=True)
    sm64ex = work / "sm64ex"
    (out / "native").mkdir(parents=True, exist_ok=True)
    if args.label:
        add_label(args.label, out)

    print("== The game data, from your ROM", flush=True)
    fetch(SM64EX, SM64EX_REVISION, sm64ex, shallow=True)
    prepare_pack(sm64ex, rom)
    build_pack(sm64ex)
    (out / "baserom.us.z64").write_bytes(rom)
    pack(sm64ex, out)

    print("== The game code, for the headset", flush=True)
    ndk = find_ndk(work, args.ndk)
    build_game(sm64ex, rom, ndk, out / "native")
    decomp = work / "sm64-decomp"
    fetch(DECOMP, DECOMP_REVISION, decomp, shallow=True)
    build_goddard(decomp, sm64ex, ndk, work / "goddard", out / "native")
    libsm64 = work / "libsm64"
    fetch(LIBSM64, LIBSM64_REVISION, libsm64, shallow=True)
    build_libsm64(libsm64, ndk, out / "native")
    for name in LIBRARIES:
        print(f"{out / 'native' / name}: {(out / 'native' / name).stat().st_size / 1e6:.1f} MB")

    if args.push:
        push(out)
    else:
        print(f"Done: {out}. To the headset: python3 {Path(sys.argv[0]).name} <rom> --push  (or the README's adb push)")


if __name__ == "__main__":
    main()
