# OpenTermix

*Read this in other languages: [Русский](README.ru.md).*

OpenTermix is an open-source, lightweight MobaXterm-like terminal for Linux.
It focuses on speed, a minimal ergonomic UI, and native integration with your
existing SSH setup.

Built with **Qt 6** (C++ in a deliberately simple, C-like style),
[QTermWidget](https://github.com/lxqt/qtermwidget) for the embedded terminal,
and [libssh](https://www.libssh.org/) for the SFTP browser.

![OpenTermix](resources/opentermix.png)

## Features (MVP - SSH core)

- Tabbed embedded terminal (local shell and SSH), running your system shell.
- Split view: 1 / 2 / 4 terminals per tab.
- Multi-execution: broadcast keystrokes to every open terminal at once.
- Detach a tab into its own window.
- Reads and writes `~/.ssh/config`:
  - session tree grouped into folders (stored as per-host `# OpenTermix-Group:`
    comments, so grouping never reorders the Host blocks in your config),
  - graphical add/edit dialog with required fields marked and validated,
  - a timestamped backup is made before the config is rewritten.
- Built-in SFTP file browser (drag files in to upload, download selection).
- Light/dark themes, configurable terminal font, persisted window layout.

Deferred for later phases: VNC, embedded X11 server, RDP/Telnet/Serial/Mosh,
SSH tunnel manager, dev tools (editor, keygen, diff, macros), password manager,
cross-platform packaging. X11 forwarding still works through `ssh -X`.

## Build (Linux)

### Dependencies

- CMake >= 3.18, a C++17 compiler
- Qt 6 (Widgets)
- `qtermwidget6` (development package)
- `libssh` (development package)

On Debian (trixie/sid) / recent Ubuntu:

```bash
sudo apt install cmake g++ pkg-config qt6-base-dev libqtermwidget-dev libutf8proc-dev libssh-dev
```

Notes:
- The Qt6 qtermwidget dev package is `libqtermwidget-dev` (it provides the
  `qtermwidget6` pkg-config module). On some distributions it is named
  `libqtermwidget6-dev`.
- `libutf8proc-dev` is required because `qtermwidget6.pc` lists it as a
  dependency.
- `pkg-config` is usually already present on a full desktop install, but a
  minimal system (e.g. a fresh `debootstrap` chroot) may not have it -
  `CMakeLists.txt` requires it (`find_package(PkgConfig REQUIRED)`).

### Compile

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/opentermix
```

### Build a .deb package

The `debian/` directory holds a standard `debhelper` packaging setup. Install
the packaging toolchain in addition to the build dependencies above, then
build from the repository root:

```bash
sudo apt install debhelper cmake g++ pkg-config qt6-base-dev qt6-tools-dev qt6-l10n-tools \
                 libqtermwidget-dev libutf8proc-dev libssh-dev
dpkg-buildpackage -us -uc -b
```

This writes `opentermix_<version>_amd64.deb` (plus `.buildinfo`/`.changes`) to
the parent directory. Install it with:

```bash
sudo dpkg -i ../opentermix_*_amd64.deb
```

This works as-is only when run directly *on* a supported target (see below) -
see "Runtime dependencies" for why a `.deb` built on one distro/release
generally won't install or run on another.

#### Supported targets

OpenTermix needs a Qt6 build of `qtermwidget` (`libqtermwidget-dev` on
Debian, `libqtermwidget6-2-dev` on Ubuntu - `debian/control` declares both
as alternatives). That package doesn't exist yet on every deb-based release:

- **Debian**: trixie (13) and newer. Older releases only have a Qt5 build.
- **Ubuntu**: 26.04 "Resolute" and newer (25.10 "Questing" also has it).
  **22.04 and 24.04 LTS are not supported** - Ubuntu freezes each release's
  package set from a Debian snapshot months before release, and Debian's
  `qtermwidget` didn't get its Qt6 port until December 2024, well after both
  of those LTS freezes. Ubuntu doesn't backport new library versions into an
  already-released LTS, so this isn't expected to change for them.
- Derivatives (Mint, Pop!_OS, Zorin, ...) follow whichever Ubuntu/Debian base
  they track.

#### Cross-building for another target

To build a `.deb` for a target other than the machine you're on, use
`scripts/build-deb.sh`: it bootstraps a throwaway `debootstrap` chroot for
that exact distro/release, builds inside it, and copies the resulting `.deb`
back out - so the binary is always linked against that target's real Qt6/
qtermwidget, not whatever happens to be installed locally.

```bash
sudo apt install debootstrap
bash scripts/build-deb.sh debian trixie
bash scripts/build-deb.sh ubuntu resolute
```

It needs `sudo` interactively (for `debootstrap`/`mount`/`chroot`), so run it
in a real terminal rather than through an AI assistant's non-interactive
shell.

### Runtime dependencies

Installing the `.deb` pulls in:

- `libc6 (>= 2.34)`, `libgcc-s1 (>= 3.0)`, `libstdc++6 (>= 5)` - standard C/C++ runtimes
- `libqt6core6t64 (>= 6.8.2)`, `libqt6gui6 (>= 6.1.2)`, `libqt6widgets6 (>= 6.3.0)` - Qt 6 runtime
- `libqtermwidget6-2 (>= 2.1.0)` - the embedded terminal widget
- `libssh-4 (>= 0.8.0)` - SSH/SFTP support
- `qt6-svg-plugins` - the SVG icon engine plugin used to render the toolbar
  icons (loaded at runtime via `QIcon`, so `dpkg-shlibdeps` can't auto-detect
  it - declared explicitly in `debian/control` instead)

These minimum versions are computed by `dpkg-shlibdeps` from whatever Qt6/qtermwidget
the package was built against, so they track the build machine, not the target.
If you build on Debian sid/unstable, the resulting `.deb` will demand very recent
library versions and can fail to install on older releases (e.g. Debian stable)
with an "unmet dependencies" error.

**Don't fix this by hand-editing the version numbers in `debian/control`.**
Qt embeds a version-tagged symbol (`qt_version_tag`) in every binary tied to the
exact Qt release it was compiled against; a binary built against Qt 6.10 cannot
load against an older `libqt6core6t64`, no matter what the package metadata
claims. Lowering the declared `Depends` only makes `dpkg` accept the install -
the app then crashes on launch with something like:

```
libQt6Core.so.6: version `Qt_6.10' not found
```

To target an older distribution, build in a matching environment instead (a
`debootstrap` chroot or container for that release, or the target machine
itself) so the binary is actually linked against the older Qt6/qtermwidget.

## Layout

```
src/
  app/        MainWindow: docks, menus, theme; SettingsWidget
  terminal/   TerminalWidget, TerminalGroup, TerminalArea (VS Code-like groups), MultiExec
  sessions/   Session, SshConfigParser, SessionTreeModel, SessionPanel, editor
  sftp/       SftpClient (libssh worker thread), SftpBrowserWidget
resources/    QSS themes + .qrc
```

## Localisation

The UI is translatable via Qt Linguist. Translations live in `translations/`
and are compiled to `.qm` and embedded under the `:/i18n` resource prefix; the
matching translation for the system locale is loaded at startup.

Shipped locales: English (source), Russian (`translations/opentermix_ru.ts`).

To add a new locale (for example French, `fr`):

1. Add the file to `OPENTERMIX_TS_FILES` in `CMakeLists.txt`:
   `translations/opentermix_fr.ts`.
2. Generate/refresh the translation catalogue from the sources:
   ```bash
   /usr/lib/qt6/bin/lupdate -recursive src -ts translations/opentermix_fr.ts
   ```
   (or, after configuring, `cmake --build build --target update_translations`).
3. Translate the strings (edit the `.ts` in Qt Linguist or by hand).
4. Rebuild - the `.qm` is compiled and embedded automatically.

## Notes

- SFTP authentication in this MVP uses your SSH agent or on-disk keys only.
  Password auth is planned together with the password manager.
- An unknown host key is stored automatically; a *changed* key aborts the
  connection.

## License

OpenTermix is licensed under the GNU General Public License v3.0 or later
(GPL-3.0-or-later). See [LICENSE](LICENSE) for the full text.
