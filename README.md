# NxEmu

NxEmu is a free, open-source Nintendo Switch emulator for Windows, Linux and macOS, written in C++.

## Installation

The latest stable releases are available [here](https://www.nxemu.com/downloads.html).

## Building

```bash
git clone --recursive https://github.com/N3xoX1/nxemu.git
```

Build the RmlUi frontend with CMake (Visual Studio 2022 build tools and the Vulkan SDK on Windows):

```powershell
./src/script/build_windows.ps1 Release
```

On Linux, use `sh src/script/build_linux.sh Release`; the desktop session must support Wayland.
On macOS with Xcode installed, use `sh src/script/build_macos.sh Release`.
The former frontend, its resource tools and hand-written Visual Studio projects have been removed.

## Contributing

Contributions are welcome via pull requests.

## Links

- [Website](https://www.nxemu.com)
- [About](https://www.nxemu.com/about.html)
- [Discord](https://discord.gg/hEa4hNyFWU)

## License

NxEmu is licensed under the [GNU General Public License v3.0](./license.txt).
