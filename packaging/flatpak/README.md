# Flatpak development package

This manifest builds the current recursive checkout in the Freedesktop 25.08 SDK.
It currently targets x86-64, matching the bundled Linux FFmpeg libraries.
Only Release is compiled. LTO is disabled for this initial packaging validation
to reduce compilation/link time; the regular Linux build retains its LTO settings.

On Linux with `flatpak` and `flatpak-builder` installed:

```sh
flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub org.freedesktop.Platform//25.08 org.freedesktop.Sdk//25.08
flatpak-builder --user --arch=x86_64 --force-clean --repo=build/flatpak-repo \
  build/flatpak-app packaging/flatpak/com.nxemu.NxEmu.yml
flatpak-builder --run build/flatpak-app packaging/flatpak/com.nxemu.NxEmu.yml \
  sh -s < packaging/flatpak/check-runtime.sh
flatpak build-bundle build/flatpak-repo build/NxEmu-x86_64.flatpak com.nxemu.NxEmu
flatpak install --user build/NxEmu-x86_64.flatpak
flatpak run com.nxemu.NxEmu
```

The runtime check validates resources, modules and dynamic-library resolution.
It does not test the UI, Vulkan rendering, controllers or game execution.
The application needs a Wayland session. File and folder access uses the desktop
portal; no blanket home-directory permission is granted. Device access is needed
for graphics, game controllers and direct USB adapters.

The manifest uses local directory sources so CI tests the actual checkout,
including uncommitted packaging changes. Before submitting to Flathub, replace
this source with a pinned upstream release and add release metadata and screenshots.
Flathub publication and acceptance are separate from building this development bundle.

The dedicated Flatpak workflow is intended for manual validation, independently
of the normal four-platform CI. No Debug build is added.
