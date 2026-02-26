# P!NG Installer (macOS)

Build an installer package that prompts for admin password and installs P!NG to `/Library/Audio/Plug-Ins/`.

## Build the installer

From the project root:

```bash
cmake --build build --target installer
```

Or build everything, then run the script directly:

```bash
cmake --build build
./Installer/build_installer.sh
```

The installer is created at `Installer/output/P!NG-Audio-Plug-In-1.0.0.pkg`.

## Distribution

Share the `.pkg` file. Recipients can:
1. Double-click the `.pkg`
2. Follow the installer (macOS will prompt for admin password)
3. P!NG AU and VST3 will be installed to `/Library/Audio/Plug-Ins/`
4. Rescan plugins in their DAW
