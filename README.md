# Arcanum Community Edition

> [!IMPORTANT]
> This is a beta, technology preview, or whatever other label you have in mind for a pre-release software.

## Status

The game is ready, but I haven't tested everything - just the speedrun - and that was about three months ago, so things might have changed since then.

Regarding the source code, about half of the modules (not half of the code) are in good shape. All APIs have meaningful names, along with brief documentation, annotations, and explanations (see `skill.c` and `quest.c` as examples). The other half may have cryptic names, little to no symbols , and no documentation at all (`anim.c` is extremely large and hard to understand).

## Installation

You must own the game to play. Purchase your copy on [GOG](https://www.gog.com/game/arcanum_of_steamworks_and_magick_obscura) or [Steam](https://store.steampowered.com/app/500810).

<details>
    <summary>Minimum installation</summary>

    ```
    .
    ├── arcanum1.dat
    ├── arcanum2.dat
    ├── arcanum3.dat
    ├── arcanum4.dat
    ├── modules
    │   ├── Arcanum
    │   │   ├── movies
    │   │   │   ├── 00069.bik
    │   │   │   ├── 01138.bik
    │   │   │   ├── 02112.bik
    │   │   │   ├── 50000.bik
    │   │   │   ├── 51169.bik
    │   │   │   ├── 91568.bik
    │   │   │   ├── A0021.bik
    │   │   │   ├── G0021.bik
    │   │   │   └── movies.mes
    │   │   └── sound
    │   │       └── music
    │   │           ├── Arcanum.mp3
    │   │           ├── Caladon.mp3
    │   │           ├── Caladon_Catacombs.mp3
    │   │           ├── Cities.mp3
    │   │           ├── Combat 1.mp3
    │   │           ├── Combat 2.mp3
    │   │           ├── Combat 3.mp3
    │   │           ├── Combat 4.mp3
    │   │           ├── Combat 5.mp3
    │   │           ├── Combat 6.mp3
    │   │           ├── CombatMusic.mp3
    │   │           ├── Dungeons.mp3
    │   │           ├── DwarvenMusic.mp3
    │   │           ├── Interlude.mp3
    │   │           ├── Isle_of_Despair.mp3
    │   │           ├── Kerghan.mp3
    │   │           ├── Mines.mp3
    │   │           ├── Qintara.mp3
    │   │           ├── Tarant.mp3
    │   │           ├── Tarant_Sewers.mp3
    │   │           ├── Towns.mp3
    │   │           ├── Tulla.mp3
    │   │           ├── Vendegoth.mp3
    │   │           ├── Villages.mp3
    │   │           ├── Void.mp3
    │   │           └── Wilderness.mp3
    │   ├── Arcanum.PATCH0
    │   ├── Arcanum.dat
    │   └── Vormantown.dat
    └── tig.dat
    ```
</details>

### Windows

Download and copy `arcanum-ce.exe` to your `Arcanum` folder. It serves as a drop-in replacement for `arcanum.exe`.

### Linux

- Use the Windows installation as a base - it contains the data assets needed to play. Copy the `Arcanum` folder somewhere, for example `/home/john/Desktop/Arcanum`.

- Alternatively, you can extract the required files from the GOG installer:

```console
$ sudo apt install innoextract
$ innoextract ~/Downloads/setup_arcanum.exe -I app
$ mv app Arcanum
```

- Download and copy `arcanum-ce` to this folder.

- Run `./arcanum-ce`.

### macOS

> [!NOTE]
> macOS 10.13 (High Sierra) or higher is required. Runs natively on Intel-based Macs and Apple Silicon.

- Use the Windows installation as a base - it contains the data assets needed to play. Copy the `Arcanum` folder somewhere, for example `/Applications/Arcanum`.

- Alternatively, if you have Homebrew installed, you can extract the required files from the GOG installer:

```console
$ brew install innoextract
$ innoextract ~/Downloads/setup_arcanum.exe -I app
$ mv app /Applications/Arcanum
```

- Download and copy `Arcanum Community Edition.app` to this folder.

- Run `Arcanum Community Edition.app`.

### Android & iOS

These ports are not currently intended for players. Touch controls are not yet implemented, and window management is subpar. No further instructions will be provided until these issues are resolved (but you can easily figure it out, it's not rocket science).

## Building from source

Check [`ci-build.yml`](.github/workflows/ci-build.yml) for details on how the project is compiled. The build has no external video dependencies — video playback uses a vendored MJPEG-in-AVI decoder in `first_party/bink_compat/` and works on every supported target out of the box.

## Video playback

The cross-platform video backend plays `.avi` (MJPEG / PCM) files produced from your original `.bik` cutscenes. The conversion is a one-time step per install. Custom `.mp4`/`.mov`/`.mkv`/`.webm` replacement videos dropped under `data/`, `art/`, or `modules/` are also auto-detected and converted alongside the cutscenes.

**In-game (recommended):** launch Arcanum CE with unconverted videos in your data tree and it prompts you on first launch. If `ffmpeg` is in your `PATH` the prompt offers a single **Convert** button that runs the conversion in the background with a live progress bar; otherwise it shows the platform-specific install instructions and a re-check button. Converted `.avi` files for the original cutscenes land in `<game>/data/videos/`; user replacement videos get their `.avi` written as a sibling of the source file so the existing override search still finds it.

**Command line (fallback):**

```
# Install ffmpeg first:
#   macOS:   brew install ffmpeg
#   Linux:   sudo apt install ffmpeg
#   Windows: https://ffmpeg.org/download.html
python3 scripts/convert_videos.py                  # auto-detects ~/Applications/Arcanum etc.
python3 scripts/convert_videos.py --game-dir /path/to/Arcanum
```

The script enumerates every `.bik` inside the game's DAT archives plus any loose video files under the install tree, and writes `.avi` outputs to `<game>/data/videos/`. Re-runs skip files whose `.avi` is already up to date.

**Converting your own replacement videos**

To convert an arbitrary file by hand the encoder settings are:

```
ffmpeg -i INPUT.mp4 -c:v mjpeg -q:v 4 -pix_fmt yuvj420p -c:a pcm_s16le -f avi OUTPUT.avi
```

Drop the result anywhere the game's override search probes: `<game>/art/ui/mainmenu_bg.avi` for a custom main-menu background, `<game>/data/videos/<basename>.avi` to replace a specific cutscene, etc. Append `_native` to the basename (e.g. `mainmenu_bg_native.avi`) to keep the original pixel size instead of scaling to fit.

**Per-platform notes**

- **Windows x86 with the original `binkw32.dll`** is unchanged — the native path keeps playing `.bik` files directly. The conversion step is only needed on Windows x64 / Linux / macOS / iOS / Android.
- **iOS / Android:** subprocess spawning isn't viable from the application sandbox, so the in-game prompt asks you to run the conversion on a desktop first and copy the resulting `.avi` files to the device alongside the rest of your owned game data (via Files on iOS, Android File Transfer on Android).

## Configuration

Several configuration options are available as command-line switches (admittedly not very user-friendly):

- `-4637`: Enable cheat level 3

- `-window`: Run in windowed mode (default is fullscreen)

- `-geometry=1280x720`: Set window size (default is 800x600)

Most options live in `arcanum.cfg` in the game directory.

On first launch, if `HighRes/config.ini` from the Unofficial Arcanum Patch is present, its keys (`Width`, `Height`, `Windowed`, `ShowFPS`, `ScrollFPS`, `ScrollDist`, `Logos`, `Intro` are migrated -- edit `arcanum.cfg` from then on:

- `resolution width`, `resolution height`: rendered resolution (default 800x600)
- `windowed`: `0` for fullscreen (default), `1` for windowed
- `show fps`: `1` to overlay the frame rate
- `scroll fps`, `scroll dist`: edge-scroll cadence and distance
- `logos`, `intro`: `0` to skip the startup logos / intro cinematic

- `aspect snap`: `1` (default) rewrites `resolution width` / `height` at startup to the nearest screen-aspect-matching pair so the rendered area fills the display instead of letterboxing; `0` uses the configured dimensions exactly. Setting to 800x600 (original resolution) will not be affected).

- `macos ignore notch` (macOS only): `1` to draw under the camera notch via a borderless full-display window; `0` (default) keeps the standard SDL fullscreen which respects the  safe area.


## Contributing

Play the game and file bugs if any (there are likely many). Attach a save game for investigation. Suggestions for quality of life improvements are also welcome. The major objective for 25H2 is to clarify remaining functions.

## License

The source code is this repository is available under the [Sustainable Use License](LICENSE.md).
