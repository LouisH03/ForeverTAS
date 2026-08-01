# Portable application bundles

ForeverTAS uses one CMake installation definition as the source of truth for
all release artifacts. Platform scripts build that install tree, deploy the
native Qt runtime, and wrap it in the expected portable format.

## Artifacts

| Platform | Artifact | User workflow |
| --- | --- | --- |
| Linux | `ForeverTAS-<version>-linux-<arch>.AppImage` | Mark executable and run |
| Windows | `ForeverTAS-<version>-windows-<arch>.zip` | Extract and run `ForeverTAS.exe` |

Each artifact is native to its operating system. The search and physics code is
compiled directly for the target platform; Wine or another compatibility layer
is not part of the release runtime.

macOS is not supported, and CMake rejects attempts to configure a macOS build.

## Canonical install tree

Configure and build normally, then stage the application with:

```sh
cmake --install build/release --prefix dist/ForeverTAS
```

The install rules own the executable or application bundle, application icon,
Linux desktop/AppStream metadata, licenses, and the QML deployment entry point.
Do not copy build-tree files manually into release artifacts.

The CTest target `forevertas-install-smoke` creates a clean staged install and
launches that installed application using the QML smoke mode.

## Application icon

`packaging/icons/dev.skycrafter.forevertas.svg` is the canonical application
icon used by the running Qt application and Linux desktops. Regenerate the
committed Linux PNG and Windows ICO assets after changing it:

```sh
./packaging/icons/generate-icons.sh
```

The generator requires Inkscape and ImageMagick. Run it with `--check` to verify
that all platform assets match the canonical SVG without modifying them.

## Linux AppImage

Run on an x86_64 or arm64 Linux build host:

```sh
./packaging/linux/build-appimage.sh
```

The script:

1. Configures a Release build with an install prefix of `/usr`.
2. Installs it into an isolated AppDir with `DESTDIR`.
3. Validates the desktop and AppStream metadata.
4. Uses linuxdeploy and its Qt plugin to collect Qt libraries, QML imports,
   plugins, and native dependencies.
5. Creates the AppImage and a SHA-256 file under `dist/`.

`LINUXDEPLOY` and `LINUXDEPLOY_PLUGIN_QT` can point at preinstalled tools. If
they are unset, the script downloads the official continuous AppImages into
`build/package-tools/`. `FOREVERTAS_BUILD_DIR`, `FOREVERTAS_DIST_DIR`, and
`FOREVERTAS_TOOLS_DIR` override the default working directories.

The script selects the Qt 6 `qmake` used by the build and exposes a filtered
plugin tree to linuxdeploy. This prevents unrelated distribution plugin packs
from adding optional codecs or unresolved third-party dependencies. Set `QMAKE`
to override Qt discovery.

linuxdeploy's optional stripping pass is disabled by default because some modern
distributions emit RELR ELF sections that its bundled binutils cannot parse. Set
`FOREVERTAS_ENABLE_STRIP=1` only when the selected linuxdeploy build and every
input library are known to support stripping safely.

Build public AppImages on the oldest controlled Linux base that the project
supports. The produced executable cannot be more compatible than the glibc and
other non-bundled system interfaces of its build environment. The release
workflow currently uses Ubuntu 22.04 with Qt 6.9.3.

## Windows portable ZIP

Run from PowerShell on a native Windows host with Qt and Ninja available:

```powershell
./packaging/windows/build-portable.ps1
```

CMake's Qt QML deployment script invokes the native Windows deployment tooling
during installation. CPack then creates a ZIP containing the executable,
compiler runtime, Qt DLLs, QML modules, plugins, icons, and licenses. No
installer or registry write is required to launch it.
Code signing is recommended for public downloads but remains separate from the
portable layout.

## GitHub Actions

`.github/workflows/package.yml` runs CPU verification on GitHub-hosted runners.
For trusted pushes, version tags, and manual dispatches, it builds the Linux
bundle on the `forevertas-linux` self-hosted runner inside an Ubuntu 22.04
container, then uploads the AppImage and its checksum. Pull requests never run
on the self-hosted machine. Windows packaging remains available through the
local PowerShell script but is not currently part of the Actions matrix.

## Settings and writable data

The executable bundle is portable in the no-installation sense. ForeverTAS
settings continue to use Qt's platform-native per-user settings location. They
are deliberately not stored inside the bundle because an AppImage is mounted
read-only. A future USB-style data mode should use an explicit writable data
directory outside the application artifact.

## Release verification

Before publishing each artifact:

1. Test it on a clean machine without a separate Qt installation.
2. Launch the QML and Quick 3D UI.
3. Detect or select a Packs directory and load a replay.
4. Start and stop a search.
5. Verify the sampled Best run and multi-car Race Viewer.
6. Check that loaded libraries resolve only from the bundle or documented
   operating-system libraries.
7. Review bundled third-party libraries and notices.
