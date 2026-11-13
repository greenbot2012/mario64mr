# Mario 64 MR

<p align="center"><img src="media/mario-out-of-the-game.gif" width="400" alt="Reaching into the game window, picking Mario up and setting him down on a real table, where he runs around and jumps back in"></p>

Mixed reality Super Mario 64 for Meta Quest 3, 3S and 2 (and the
[Steam Frame](#steam-frame), and, experimental, [PICO](#pico-experimental)). A Super Mario 64 cartridge sits in your room. Stretch it
open with your hands into a window onto the game, then play the whole game with the Touch controller in your right
hand. Your left hand reaches in, picks Mario up, and carries him out onto your desk.

- **The cartridge**: a real object in your room. Pick it up, throw it, drop it on your table, and stretch it open
  into a window onto the game, up to four times its first size.
- **The title screen**: the original Goddard code, compiled from the decompilation. Stretch Mario's face with
  both hands and pull his nose out of the window.
- **The whole game**, at the PC port's 60 fps, smoothly interpolated to the headset's 72 to 120 Hz.
- **Mario out in your room**: pick him up out of the window and he runs on your furniture, stands on your palm,
  and jumps back in.
- **Head in the window**: put your head through it and you're inside the level, with the sky all around you. Look
  back and your room is still there, through a hole in the sky.
- **Diorama**: put the window down on a table and the whole level rises out of it, a little world you can walk
  around, move, turn and scale while you play.
- **Options** in the game's pause menu: refresh rate, resolution, anti-aliasing, foveation, texture filtering,
  sharpening, sound and HUD.

By [Lucas Martinic](https://lucasmartinic.com): [X @lucas_martinic](https://x.com/lucas_martinic) ·
[Instagram @lucasmartinic](https://www.instagram.com/lucasmartinic/) ·
[LinkedIn](https://www.linkedin.com/in/lucas-martinic)

> **Sharing a clip?** I'd love to see it! If you post videos or photos of Mario 64 MR, please tag me
> ([@lucas_martinic](https://x.com/lucas_martinic) on X, [@lucasmartinic](https://www.instagram.com/lucasmartinic/)
> on Instagram, [Lucas Martinic](https://www.linkedin.com/in/lucas-martinic) on LinkedIn). This took a lot of
> work to put together, and seeing Mario running around your rooms makes it all worth it. Thank you!

**The app contains none of Nintendo's game: no data and no code.** It needs your own copy of Super Mario 64 (US).
The tool in this repository makes the game from your ROM on your own computer, and puts it on your headset:

- the game's **data** (textures, sounds), extracted from your ROM with the [sm64ex](https://github.com/sm64pc/sm64ex)
  PC port's own tools;
- the game's **code**, compiled for the headset from the community's public sources: the
  [SM64 decompilation](https://github.com/n64decomp/sm64), sm64ex and [libsm64](https://github.com/libsm64/libsm64),
  with Mario 64 MR's changes (`native/`).

## What's here

| | What it is |
| --- | --- |
| [Releases](../../releases) › `Mario64MR.apk` | The app |
| [Releases](../../releases) › `Mario64MR-SteamFrame.apk` | The app for the [Steam Frame](#steam-frame) |
| [Releases](../../releases) › `Mario64MR-Pico.apk` | The app for PICO headsets ([experimental](#pico-experimental)) |
| `install_mario64mr.bat` | On Windows: double-click it, and it does everything below for you |
| `install_mario64mr.py` | Makes the game from your ROM and pushes it to the headset |
| `native/` | Mario 64 MR's changes to the game's sources, applied by the tool |

## You need

- A Meta Quest 3, 3S or 2 in developer mode (Meta Horizon app › Devices › Developer mode), connected to your
  computer over USB with `adb`. Allow USB debugging when the headset asks.
- Hand tracking on (Settings › Movement tracking › Hand and body tracking), and the right Touch controller.
- Your room set up (Settings › Physical space › Space setup), so the cartridge lands on your table and Mario can run
  on it. The app asks for permission to use it the first time.
- Your own **Super Mario 64 (USA)** ROM: `.z64`, `.v64` or `.n64`, SHA-1 `9bef1128717f958171a4afac3ed78ee2bb4e86ce`
- A computer with about 4 GB of free space:
  - **Windows 10 or 11**: nothing else. `install_mario64mr.bat` sets up everything it needs (see
    [On Windows](#on-windows)).
  - **Mac or Linux**: `python3`, `git`, `make`, `patch` and a C compiler.
    - Mac: `xcode-select --install`
    - Ubuntu: `sudo apt install build-essential git python3`
  - (WSL on Windows works too, like Linux. Put this repository in your WSL home (`cd ~`, then `git clone` it there)
    rather than on a Windows drive (`/mnt/c/...`): building there is much slower, and long paths can go past
    Windows' limit.)
- (Optionally) The Android NDK. The tool finds one installed by Android Studio, Homebrew (`brew install --cask android-ndk`) or
  Unity. Not neccessary because it automatically downloads r27c from Google the first time (about 700 MB, 2 GB unpacked).

## Install

### On Windows

1. Download `Mario64MR.apk` from [Releases](../../releases), and this repository (Code › Download ZIP). Unzip
   it, and leave the APK in your Downloads (or put it in the unzipped folder).

2. Double-click **`install_mario64mr.bat`**, and choose your ROM when it asks. If Windows says it protected your PC,
   click More info › Run anyway. If Microsoft Defender calls it a Trojan (`Trojan:Script/Sabsik.EN.A!ml`), that's a
   false alarm: see [Troubleshooting](#troubleshooting).

   - It checks your ROM first: with a wrong one, it says what's wrong and stops before downloading anything.
   - The first time, it sets up its own build tools (MSYS2, the Android NDK and adb) in
     `%LOCALAPPDATA%\Mario64MR`: no admin rights, nothing installed for the rest of Windows. Delete that folder to
     remove them.
   - Then it makes the game from your ROM. The first run takes 10 to 30 minutes, depending on your PC and internet;
     a second run takes a couple of minutes.
   - At the end it asks you to connect the headset with USB (developer mode on, allow USB debugging when it asks),
     then installs the app and sends the game to it.

3. Open Mario 64 MR on the headset: see step 5 under [On Mac or Linux](#on-mac-or-linux).

### On Mac or Linux

1. Download `Mario64MR.apk` from [Releases](../../releases), and this repository (Code › Download ZIP, or
   `git clone`).

2. Install the app:

   ```
   adb install -r -g Mario64MR.apk
   ```

3. Check your ROM first (it takes a second, and needs nothing else installed):

   ```
   python3 install_mario64mr.py "Super Mario 64 (USA).z64" --check
   ```

   It should say `ROM: Super Mario 64 (USA), ..., SHA-1 matches`. If not, it says what the file is instead (another
   region, a ROM hack, a bad dump, a zipped ROM...); see [Troubleshooting](#troubleshooting). The tool always checks
   it first anyway: with a wrong ROM, nothing is downloaded, built or sent to the headset.

4. Put the headset on and keep it awake. Then make the game and push it, all in one step:

   ```
   python3 install_mario64mr.py "Super Mario 64 (USA).z64" --push
   ```

   The first run takes 10 to 20 minutes. It downloads the sources, builds their tools, extracts the textures and
   sounds from your ROM, compiles the game for the headset, and pushes it all into the app's folder. Everything
   lands in `Mario64MRData/`; `Mario64MRData-work/` keeps the sources and builds, so a second run is quick. If the
   app has never been opened, the tool opens it once so it creates its folder, then closes it.

5. Open **Mario 64 MR** from your Library: it's under **Unknown Sources** (in the filter at the top right of the
   Library, or its own folder on newer versions), or search the Library for "Mario". If Unknown Sources is missing
   or empty, connect to Wi-Fi and restart the headset; or start it over USB:
   `adb shell monkey -p com.lucasmartinic.mario64mr -c android.intent.category.LAUNCHER 1`

If anything is missing, the app says so in front of you and the game won't start.

### Pushing by hand

To make it all without a headset connected, leave out `--push`. Later, open the app once, close it, and push:

```
D=/sdcard/Android/data/com.lucasmartinic.mario64mr/files
adb shell mkdir -p $D/sm64/res $D/native
adb push Mario64MRData/baserom.us.z64 $D/
adb push Mario64MRData/sm64/res/base.zip $D/sm64/res/
adb push Mario64MRData/native/libsm64game.so Mario64MRData/native/libgoddard.so Mario64MRData/native/libsm64.so $D/native/
```

### Your own cartridge label

The app's cartridge has a plain label: the real one is Nintendo's artwork, so it can't come with the app. You can
put your own on it, for example a photo or scan of your own cartridge's label:

- a **PNG or JPEG**, **square** (1024 × 1024 looks sharp);
- the label **in the middle**, as tall as the picture (a real label's shape fills about 87% of its width); what's
  left at the sides doesn't show.

Add it when you install:

```
python3 install_mario64mr.py "Super Mario 64 (USA).z64" --push --label my-label.png
```

or push it on its own at any time (`label.png`, or `label.jpg` for a JPEG), and restart the app:

```
adb push my-label.png /sdcard/Android/data/com.lucasmartinic.mario64mr/files/label.png
```

To go back to the plain label, delete it:
`adb shell rm /sdcard/Android/data/com.lucasmartinic.mario64mr/files/label.png`

## Updating

Download the new `Mario64MR.apk` from [Releases](../../releases) and install it over the old one, with the headset
connected over USB:

```
adb install -r -g Mario64MR.apk
```

(or drag it onto SideQuest). The game you made from your ROM stays on the headset, so you don't need to run the
installer again. On PICO it's `Mario64MR-Pico.apk`; on the Steam Frame, run the [installer on the Frame](#on-the-frame-itself-no-computer-needed)
again.

## Playing

The right Touch controller plays the game; your left hand is for Mario. Put the controller down and both hands are
tracked, for the two-handed moves. Pick up the left controller too and the game switches to a two-controller layout
(put it down and it switches back).

**The cartridge**
- It floats in front of you at the start. Pinch it to pick it up (or squeeze the controller's grip on it), and let
  go to drop it onto your table, or throw it.
- Pinch both ends with both hands and pull apart: it stretches into a window. Or, holding it with the controller,
  pull the trigger.
- Squeeze the window's frame with both hands to close it back into the cartridge (or, holding the frame with the
  controller, pull the trigger). Pinch the frame with one hand, or grip it, to move the window.
- Your left hand and the controller's grip stretch and squeeze it too, as two hands do.
- Holding the window with the controller, push the thumbstick up or down to make it bigger or smaller (up to four times its first size).

**The title screen**
- Pinch Mario's face to stretch it, and pull his nose out of the window. The stick and trigger move the hand cursor
  and grab, as on the N64.
- To start the game, click the thumbstick, pinch "PRESS START", or make the left hand's menu gesture (look at your
  left palm and pinch).

**The game: one controller** (right Touch)

| Controller | N64 |
| --- | --- |
| Thumbstick | Stick |
| Trigger | A (jump) |
| A | B |
| Grip | Z |
| B held + thumbstick | C buttons (camera; C-down past the farthest step zooms out further) |
| B tap (without the stick) | R |
| Thumbstick click (or the left hand menu gesture) | Start |

**The game: two controllers** (the left hand can't grab Mario while it holds a controller)

| Controller | N64 |
| --- | --- |
| Left thumbstick | Stick |
| Right thumbstick | C buttons (camera) |
| Right trigger (or A) | A (jump) |
| Right grip (or B) | B |
| Left trigger | Z |
| Left grip | R |
| Thumbstick click or the left menu button | Start |

An Xbox controller paired with the headset works too (A = A, X / B = B, triggers = Z and R, right stick = C).

**Your left hand in the game**
- Put your hand into the window and the game holds still while it's in there.
- Pinch Mario to pick him up. Carry him out of the window and let go: he lands in your room and runs on it.
  Carry him back in, or let him jump back through the window, and he's back in the game. Leaving the course brings
  him back too.
- Throw him by letting go mid-swing.
- Hold your palm flat under him and he stands on it. Tilt it and he falls off.

**Your head in the window**
- Lean into the window, through its opening, and you're inside the level: look all around, the sky is a dome
  around you. Trees and other flat sprites turn to face you.
- Look back the way you came and you see your room through the opening. Lean back out to leave.

**Diorama (tabletop)**
- Hold the open window by its **lower part** (with your hand, or the controller's grip) and bring it down near a
  table, desk or the floor: a glowing circle appears on the surface below.
- Let go over the circle: the window dives into it and the level rises out of it as a diorama, about 2 m across,
  sky and HUD gone. Play on as usual: the stick moves Mario the way you're looking.
- The circle stays as the diorama's base, the same size whatever the diorama's:
  - pinch it (or grip it with the controller) and move your hand to slide the diorama across the table;
  - pinch it with both hands (or a hand and the controller) to turn it and scale it, pulling apart or together;
  - holding it with the controller, the thumbstick up / down scales it and left / right turns it.
- The level follows Mario up and down, keeping him about 10 cm below your eyes (as they were when you put it down),
  so you always see the ground under him: when he climbs a mountain it sinks into
  the table (whatever falls below the table is cut away), and when he goes down it rises off it.
- To leave, lift the base up off the table: the level sinks back into it and the window is back in your hand.
  Leaving the course ends the diorama too.

**Options**

Pause the game (START), then press **R** (one controller: tap B; two controllers: the left grip) for the options.
Move with the stick, change with the stick left / right, A to pick, B to go back.

| Option | What it does |
| --- | --- |
| Graphics › Refresh rate | 72, 80, 90 or 120 Hz. Higher is smoother, and harder on the headset |
| Graphics › Resolution | The render resolution, 70% to 150%. Higher is sharper, and slower |
| Graphics › Anti-aliasing | Smooths jagged edges: off, MSAA 2x or 4x |
| Graphics › Foveation | Draws the edges of your view at a lower resolution, to save power: off to high |
| Graphics › Textures | The game's textures in the distance: as the N64 drew them (they shimmer far away), or smoothed with mipmaps and anisotropic filtering |
| Graphics › Sharpening | The headset's own sharpening of the picture |
| Graphics › Defaults | Puts all the graphics options back as they were |
| Sound | The music, sound effects and ambience volumes |
| HUD | The lives, coins and stars counters on or off |
| Bring Mario back | Puts Mario back where he last was in the game, if he gets stuck out in your room |

Changes apply at once, and are kept for next time.

**Recording**

Use the headset's own capture (Meta button › Camera › Record video): it records the passthrough and the game together.

## Quest 2

It runs at 72 Hz there by default (90 Hz on Quest 3 and 3S), with more foveation, and its passthrough is black and
white, so your room is grey around the window. If it stutters, lower the resolution or anti-aliasing in the options.

<a id="steam-frame-experimental"></a>

## Steam Frame

Valve's Steam Frame has its own app: **`Mario64MR-SteamFrame.apk`** in [Releases](../../releases). The game you
make from your ROM is the same for both headsets; only the app is different.

> **Tested on a Steam Frame 2026-11-10** with Lepton v3.0.5 (rootfs v3.0.3): the cartridge, the title screen,
> passthrough, the full game, carrying Mario in and out of the window and the diorama (on the floor) all work.
> If something's wrong on yours, please [open an issue](../../issues) with the `[Mario64MR]` lines from
> `adb logcat -s Unity`.

What's different from the Quest:

- **No hand tracking** on the Frame, so the **left controller is your left hand**: reach into the window and
  hold its grip to pick Mario up, carry him out and let go to put him down (or throw him). Mario's white gloves hold
  the controllers, but he can't stand on your palm. You play with both controllers: left stick moves, right stick is the camera
  (C buttons), right trigger or A jumps (A), right grip or B is B, left trigger is Z, left X or Y is R, and START is
  a thumbstick click or the left menu button. The right grip picks up the cartridge and the window; while you hold
  it, the right trigger opens or closes it, and the right stick resizes it.
- **No room scan**, so the cartridge lands on the floor, not on your table.
- **Grey passthrough**: the Frame's cameras are black and white.
- In the options, **refresh rate** and **sharpening** do nothing: the Frame runs at its own rate.

### On the Frame itself (no computer needed)

1. On the Frame, switch to desktop mode, and put your **Super Mario 64 (USA)** ROM in **Downloads** (a `.z64`,
   `.v64` or `.n64`, or a `.zip` with one inside).
2. Download [`Install-Mario64MR.desktop`](steam-frame/Install-Mario64MR.desktop) (Raw › save it) and
   double-click it (Execute, if it asks).
3. A terminal opens and does the rest: it makes the game from your ROM on the Frame (the first time about
   10 minutes and a 700 MB download), installs the app, and adds **Mario 64 MR** to your Steam library. If it
   can't find your ROM, it asks you to choose it.
4. Return to Gaming Mode and start Mario 64 MR from your Library.

Run it again to update. Everything goes in `~/.local/share/mario64mr` (delete it, and the Steam shortcut, to
remove Mario 64 MR); the log is `~/.local/share/mario64mr/install.log`.

### From a computer (Mac or Linux)

The installer on the Frame above is the easy way. From a computer, the app has to be installed through Lepton
itself, over SSH: installed with a plain `adb install`, it opens to nothing on the Frame.

1. On the Frame, turn on Developer Mode, then start **Lepton Development** from your Library (an Android home
   screen opens). Keep the Frame on the same Wi-Fi as your computer, and set up SSH to it (your computer's public
   key in the Frame's `~/.ssh/authorized_keys`).
2. Make the game and send it to the Frame along with the app:

   ```
   python3 install_mario64mr.py "Super Mario 64 (USA).z64" --push --frame --apk Mario64MR-SteamFrame.apk --lepton-install steamos@frame.lan
   ```

   Or with the Frame's IP address (Quick Settings › Wi-Fi): `--frame 192.168.1.23 --lepton-install steamos@192.168.1.23`.
   If `frame` isn't found, the installer also tries `frame.lan`.
3. Start Mario 64 MR from Lepton Development's home screen.

On **Windows**, use the installer on the Frame: `install_mario64mr.bat` doesn't install the Frame's app (installed
from Windows, without Lepton, it would open to nothing), and sends you there when it finds it.

> **If the app shows nothing when you open it on the Frame** (and `adb logcat -s Unity` has
> `xrCreateInstance: XR_ERROR_FILE_ACCESS_ERROR`), it was installed with a plain `adb install`, which leaves
> Lepton's OpenXR/Vulkan layer libraries (`libVkLayer_VALVE_fdm_injection.so`, `libVkLayer_fossilize.so`) as
> empty stubs: only Lepton's own `install_app` sets them up. Install it again through Lepton, in a terminal on the
> Frame (desktop mode, or over SSH):
> `~/.steam/steam/steamapps/common/Lepton/lepton install_app dev /path/to/Mario64MR-SteamFrame.apk`

To push the game by hand instead, `adb connect frame` first, then follow [Pushing by hand](#pushing-by-hand).

## PICO (experimental)

PICO headsets have their own app: **`Mario64MR-Pico.apk`** in [Releases](../../releases). The game you make from
your ROM is the same as for the Quest; only the app is different.

> **Experimental.** Tested on one PICO headset (PICO OS 6) with a room mesh: hand tracking, passthrough, the full
> game, Mario in and out of the window, and the diorama work. Not tested on a PICO 4 yet. If you try it, please
> [open an issue](../../issues) saying how it went, with the `[Mario64MR]` lines from `adb logcat -s Unity` if
> something's wrong.

What's different from the Quest:

- A PICO tracks **your hands or the controllers, not both at once**. With your hands, it's as on the Quest: pinch to
  open the cartridge and to take Mario out. With the controllers (or once you pick them up), it plays like the
  [Steam Frame](#steam-frame): the left controller is your left hand (hold its grip to pick Mario up), left X or Y
  is R, and Mario's gloves hold the controllers. An Xbox controller works too.
- **The room**: on headsets with a room mesh (like the PICO 4 Ultra) the cartridge lands on your table and Mario
  runs on it. On a PICO 4 it uses your Room Capture scan (floor, walls and furniture) if you made one; without
  either, just the floor.
- In the options, **sharpening** does nothing: PICO sets it when the app is built.

To install it, follow [Install](#install) with `Mario64MR-Pico.apk` in place of `Mario64MR.apk`:

1. Turn on developer mode (Settings › General › About: click Software Version until it says you're a developer,
   then Settings › Developer › USB Debugging) and connect the headset with USB.
2. **Windows**: put `Mario64MR-Pico.apk` in your Downloads and double-click `install_mario64mr.bat`.
   **Mac or Linux**: `python3 install_mario64mr.py "Super Mario 64 (USA).z64" --push --apk Mario64MR-Pico.apk`
3. Allow the room permission when it asks, and start Mario 64 MR from your Library (Unknown sources).

## Troubleshooting

- **Microsoft Defender says `Trojan:Script/Sabsik.EN.A!ml`** for `install_mario64mr.bat` (or the downloaded ZIP),
  and blocks or removes it: a false alarm. The `!ml` means it's a machine-learning guess, not a match with any known
  virus. The installer looks like one to it because it starts PowerShell and downloads programs to run, which is
  just what it does to set up its build tools. Don't take my word for it; both files are plain text you can read in
  Notepad:
  - `install_mario64mr.bat` is six lines: it starts `windows\install_mario64mr.ps1`, and nothing else.
  - `windows\install_mario64mr.ps1` downloads only from MSYS2's releases on GitHub (`github.com/msys2`) and from
    Google (`dl.google.com`: adb), and installs MSYS2's packages (make, git, Python...) from MSYS2's mirrors. Then
    `install_mario64mr.py` fetches the game's public sources from GitHub (`sm64pc/sm64ex`, `n64decomp/sm64`,
    `libsm64/libsm64`) and the Android NDK from Google.

  Once you've looked, get the file back: Windows Security › Virus & threat protection › Protection history, open
  the item, and Actions › Restore (or Allow). You can also report it to Microsoft as a false positive at
  [microsoft.com/wdsi/filesubmission](https://www.microsoft.com/en-us/wdsi/filesubmission), which helps it go away
  for everyone. Or skip the `.bat`: the [Mac or Linux](#on-mac-or-linux) steps work in WSL on Windows too.
- **"Mario 64 MR needs the game"**: the game isn't on the headset, or it's in the wrong place. Run the tool with
  `--push` again, and restart the app.
- **START does nothing**: the full game didn't start. That usually means something is missing; see above.
  `adb logcat -s Unity` shows lines starting with `[Mario64MR]`.
- **The ROM is turned down**: the tool only takes an unmodified dump of **Super Mario 64 (USA)**, and says why not:
  - *Japan / Europe / Shindou edition*: only the USA release is supported (the game is built from the
    decompilation's US version).
  - *contents differ* or *24 MB / 64 MB instead of 8 MB*: a ROM hack, a patched or trainer ROM, or a bad dump.
    Use a clean dump of your cartridge.
  - *a .zip / .7z archive*: extract it, and give the `.z64`, `.v64` or `.n64` file inside.
  - *not a Nintendo 64 ROM* or *an N64 ROM of ...*: the wrong file.

  `.z64`, `.v64` and `.n64` byte orders all work; the SHA-1 is checked as `.z64`.
- **Unpacking the Android NDK fails** (on Windows): the tool says why. It's almost always the path being too long
  for Windows: run the tool from your WSL home (`cd ~`) instead of a Windows drive (`/mnt/c/...`).
- **The tool fails building**: check that `make` and a C compiler work (`cc --version`). Then delete
  `Mario64MRData-work/` and run it again.
- **The cartridge falls through the table, or Mario has nothing to land on**: run Space Setup again, and allow the
  app to use your space.

## About the game

Super Mario 64, Mario and Nintendo 64 are trademarks of Nintendo. Mario 64 MR is a free, non-commercial fan project.
It is not affiliated with, endorsed or sponsored by Nintendo.

- **Nothing of Nintendo's is distributed here.** The app contains none of the game's code or data, and no Nintendo
  artwork. The game is built on your own computer from your own copy's data and the community's public sources: the
  decompilation of the game (n64decomp/sm64), its PC port (sm64pc/sm64ex) and libsm64.
- **No ROMs are provided, and none will be.** Use a ROM you dumped from a cartridge you own. Please don't ask for
  ROMs, or share them, in this repository's issues.
- It is free and always will be: it isn't sold, and there are no donations or ads.

The code written for Mario 64 MR in this repository is under the MIT license (`LICENSE`); the sources the tool
downloads keep their own terms.

If you hold rights in something here and want it changed or removed, open an issue or contact
[@lucas_martinic](https://x.com/lucas_martinic), and it will be dealt with promptly.
