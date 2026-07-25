# MdsScope

MdsScope is a C++/Qt desktop application for browsing WebScope-style shot
configuration files (`*.toml, *.webscp`) and plotting MDSplus signal data through MDSIP.

It is intended to be a transparent, open-source alternative to older Java 8 based
WebScope-style clients, which can be difficult to maintain on newer Java
runtimes. The application is currently EAST-specific: the bundled signal
configuration, MDSIP defaults, login flow, and HTTP metadata API target EAST.

## Features

- Load TOML and WebScope-style `.webscp` environment files.
- Plot multiple synchronized MDSplus signal panels.
- Three sampling quality modes: **thin** (saved EAST preview when available), **medium** (0.1 ms server-side average), and **full** (complete data).
- Apply single-shot or batch-shot expressions globally.
- Use EAST HTTP metadata for latest shot and top-bar shot summary.
- Run on Linux, macOS, and Windows.

### Sampling Quality Modes

MdsScope offers three data sampling modes to balance speed and precision:

- **Thin**: The fastest choice for routine browsing and rapid shot switching.
  It uses the prepared EAST `_s` signal when available and falls back to the
  Medium strategy otherwise.

- **Medium**: A balance between speed and signal detail, suitable when peaks,
  pulses, and trigger shapes matter. It requests an approximately `0.1 ms`
  Average STC result from the EAST server. If the original signal is less dense
  than `0.1 ms`, its native resolution is retained instead of being
  interpolated.

- **Full**: Reads all raw data with no sampling. It has the highest transfer
  and memory cost and is intended for detailed analysis requiring every
  original sample.

The current mode can be set globally with the top toolbar dropdown, per panel
from the panel context menu, or per signal in the source setup dialog. The
effective mode is the higher of the global mode and a signal's saved TOML mode.

The global dropdown starts with the saved startup default. To change that
default, first select the desired Rate, then right-click the Rate control and
choose **Set Default**. Applying the startup default while opening a
configuration does not modify the file. Later user-initiated global, panel, or
source Rate changes are written to the current TOML file. Legacy `.webscp`
files do not store Rate settings.

## Requirements

- CMake 3.16 or newer
- A C++23 compiler
- Qt 6.4 or newer with Core, Widgets, Network, and Concurrent
- Qt 6 DBus on Linux

### Dependency Installation

Ubuntu:

```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev qt6-base-dev-tools
```

macOS:

```bash
xcode-select --install
brew install cmake qt
```

Windows: install Visual Studio 2022 with the C++ desktop workload, CMake, and a
matching Qt 6 MSVC build.

## Build and Install

Prebuilt DEB, RPM, Linux portable ZIP, Apple Silicon macOS, and Windows
packages are published from Git tags.

### Prebuilt Linux Packages

Install the DEB matching the Ubuntu release and architecture:

```bash
sudo apt install ./mdsscope-ubuntu24.04.amd64.deb
```

Install the RPM matching Fedora 44, Enterprise Linux 9, or Enterprise Linux 10:

```bash
sudo dnf install ./mdsscope-fedora44.x86_64.rpm
sudo dnf install ./mdsscope-el10.x86_64.rpm
```

The EL9 package uses Qt 6 from EPEL 9. On Rocky Linux 9 or AlmaLinux 9, enable
CRB and EPEL before installing it:

```bash
sudo dnf install dnf-plugins-core
sudo dnf config-manager --set-enabled crb
sudo dnf install epel-release
sudo dnf install ./mdsscope-el9.x86_64.rpm
```

The portable Linux ZIP includes Qt and the other non-system runtime libraries.
It does not need to be installed:

```bash
unzip mdsscope-linux-x86_64.zip
./MdsScope/MdsScope
```

### Prebuilt macOS Packages

The macOS packages target Apple Silicon (`arm64`). They are ad-hoc signed for
Apple Silicon but are not signed with an Apple Developer ID and are not
notarized. Gatekeeper may therefore block the first launch.

For the DMG, open it, drag `MdsScope.app` into `Applications`, then Control-click
the installed app and select **Open**. If macOS still blocks it, use **Open
Anyway** under **System Settings → Privacy & Security**. As a per-app Terminal
fallback:

```bash
xattr -dr com.apple.quarantine /Applications/MdsScope.app
open /Applications/MdsScope.app
```

For the ZIP binary package, extract and launch it directly:

```bash
ditto -x -k mdsscope-macos-arm64.zip .
xattr -dr com.apple.quarantine ./MdsScope.app
open ./MdsScope.app
```

Only remove the quarantine attribute from packages downloaded from the official
MdsScope GitHub Releases page. Do not disable Gatekeeper globally.

### Prebuilt Windows Package

The Windows ZIP is portable and includes the required Qt DLLs. Extract it and
run `MdsScope.exe`; no installer or setup wizard is required.

### Build from Source

To build from source:

Linux:

```bash
cmake -S . -B build
cmake --build build -j
./build/MdsScope
```

macOS:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build -j
open ./build/MdsScope.app
```

Windows:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\MdsScope.exe
```

Install from a source build when needed:

Linux:

```bash
cmake --install build
```

For non-root Linux installs, the default command-line prefix is `~/.local`.

macOS:

```bash
# GUI app bundle
cmake --install build --prefix "$HOME/Applications" --component app

# Command-line tools and shared resources
cmake --install build --prefix "$HOME/.local" --component tools
```

## Configuration

On first run, MdsScope creates a user configuration directory and copies bundled
templates if it is empty:

```text
Linux:   ~/.config/mdsscope/
macOS:   ~/Library/Application Support/mdsscope/
Windows: %LOCALAPPDATA%\MdsScope\
```

`auth.cache` does not store credentials in plaintext. Its contents are
obfuscated and bound to the current machine/user to reduce accidental
disclosure. It is not designed to protect credentials from software running
with the same user privileges.

MdsScope uses TOML as its native configuration format and also supports legacy
WebScope-compatible `.webscp` files. Saving from the GUI writes same-named TOML
and WebSCP files so a layout can still be opened by the old client. WebSCP only
contains settings supported by the legacy format; MdsScope extensions such as
per-source Rate remain in TOML. When both `init.toml` and `init.webscp` exist,
MdsScope prefers the TOML file.

Example TOML:

```toml
version = 1

[[panels]]
column = 1
row = 1
title = "Plasma current"
x_label = "s"
y_label = "kA"

[[panels.signals]]
tree = "pcs_east"
server = "202.127.204.12"
y = "\\pcrl01"
read_mode = "full"
```

Panels use the shot shown in the MdsScope UI. A signal-level `shot` value is
only needed for explicit per-signal shot overrides, such as shot comparison.

Shot fields support single shots, semicolon lists, ranges, and mixed
expressions:

```text
143850
143850;143851;143853
143850-143858
143850-143858;143865
```

Useful signal options include `shot`, `hidden = true`, and
`read_mode = "medium"` or `read_mode = "full"` for per-signal data mode
overrides. Omitting `read_mode` means Thin at the source level; a higher saved
startup/global Rate can still raise the effective read mode without rewriting
the TOML file.

Convert legacy `.webscp` files with `transfer`:

```bash
./build/transfer resources/environment/init.webscp
./build/transfer --recursive resources/environment
```

On Windows source builds, use the executable under `build\Release`:

```powershell
.\build\Release\transfer.exe ".\resources\environment\init.webscp"
.\build\Release\transfer.exe --recursive --out-dir ".\converted" ".\resources\environment"
```

### EAST HTTP Metadata API

The EAST HTTP metadata endpoint is stored in `resources/APIurl`. It is used for
latest shot lookup and the top-bar summary. Plot data continues to come from
EAST MDSIP.

### SSH Remote Access

Use the SSH button next to Login to tunnel MDSIP data and EAST metadata when
they are only reachable from an internal network. Linux, macOS, and Windows are
supported when a system OpenSSH client is available. The SSH server may run on
any platform, but it must allow TCP forwarding and reach the required internal
services.

Authentication supports a password, a selected identity file, or the default
OpenSSH configuration and `ssh-agent` when both are left empty. Saved SSH
settings are stored in the machine-bound encrypted user cache.

SSH access is slower than a direct internal-network connection because of
unavoidable network, encryption, and compression overhead. Users are responsible
for using only trusted, authorized SSH hosts and for complying with applicable
network and security policies.

The browser button next to SSH lets users save named HTTP or HTTPS addresses,
edit them, and open them in the system default browser. No target addresses are included by default.
When an SSH data tunnel is already connected, saved web addresses use the same
SSH forwarding path; otherwise they open normally. URLs with explicit ports are
supported.

### Data Export

Data export supports text, CSV, TSV, and JSON formats. By default, exported
files are written under:

```text
Linux:   ~/Downloads/mdsscope/output/
macOS:   ~/Downloads/mdsscope/output/
Windows: %USERPROFILE%\Downloads\mdsscope\output\  (usual location)
```

## Benchmark Mode

MdsScope includes a simple benchmark mode for MDS data loading:

```bash
./build/MdsScope --benchmark resources/environment/your_config.webscp --summary
```

## Repository Hygiene

Do not commit:

- access tokens
- local auth caches
- build artifacts

The EAST API URL is intentionally stored in `resources/APIurl`; do not commit
personal tokens or cache files.

## Disclaimer

This software is provided for research, engineering, and data visualization
workflows. It is provided "as is", without warranty of any kind, express or
implied, including but not limited to warranties of correctness, reliability,
fitness for a particular purpose, or non-infringement.

The authors and contributors are not responsible for incorrect analysis,
operational decisions, data loss, service disruption, unauthorized access, or
any other direct or indirect consequences arising from use of this software, to
the fullest extent permitted by applicable law.

SSH forwarding and web bookmarks are general-purpose, user-configured features.
The project provides no target web addresses, credentials, or access
authorization, and does not operate destination systems or relay user traffic.
Users are solely responsible for accessing only authorized systems and
complying with applicable laws and institutional policies.

## References

The original EAST web-based MDSplus/WebScope workflow is described in:

Yang, F.; Xiao, B. J. A web based MDSplus data analysis and visualization
system for EAST. Fusion Engineering and Design 2012, 87(12), 2161-2165.

https://doi.org/10.1016/j.fusengdes.2012.09.015

## License

Copyright (C) 2026 Weikang Wang.

MdsScope is licensed under
[GPL-3.0-or-later](https://www.gnu.org/licenses/gpl-3.0.html). See `COPYING`
for the full GNU General Public License version 3 text.
