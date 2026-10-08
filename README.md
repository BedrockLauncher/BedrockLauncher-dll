# BedrockLauncher-dll

`BedrockLauncher-dll` is a lightweight Windows DLL designed to work alongside Bedrock Launcher and the Minecraft for Windows package.

The DLL provides a small compatibility layer for the game's launch flow, handling package detection, existing Minecraft instances, process launching, and window activation.

## Features

- Detects the current Minecraft package family.
- Checks for an already running Minecraft instance.
- Prevents duplicate launcher instances through a package-based mutex.
- Launches `Minecraft.Windows.exe` when required.
- Waits for Minecraft to initialize.
- Finds the Minecraft window and brings it to the foreground.
- Uses Windows package APIs instead of hardcoded installation paths.
- Designed to work with packaged Microsoft Store / Xbox versions of Minecraft for Windows.
- Exports the `GameLaunch` entry point expected by the launcher environment.

## How it works

When the DLL is loaded, it creates a background worker and retrieves the package family name of the current package.

It then checks the current Windows session for an existing `Minecraft.Windows.exe` process belonging to the same package family.

If Minecraft is already running, the DLL waits for the existing instance to initialize.

If no matching instance is found, it starts:

```text
Minecraft.Windows.exe
```

After launching or detecting Minecraft, the DLL searches for windows belonging to the same package and brings the Bedrock window to the foreground.

A package-based mutex is also used to avoid starting multiple instances of the launcher environment.

## Exported API

The DLL exports:

```cpp
VOID GameLaunch(VOID);
```

The exported function is kept as the entry point expected by the Game Launch Helper environment.

The actual launch handling is performed asynchronously when the DLL is attached to the process.

## Requirements

- Windows 10 or newer
- Minecraft for Windows
- A packaged/AppX environment
- MinGW or another Windows-compatible C/C++ toolchain
- Windows SDK headers and libraries

The implementation uses Windows APIs including:

```text
appmodel.h
winternl.h
wtsapi32.h
windows.h
```

## Building

The project can be built as a Windows DLL using MinGW or another compatible toolchain.

The resulting binary should be:

```text
BedrockLauncher-dll.dll
```

Make sure the required Windows libraries are linked, including the libraries providing the Windows Terminal Services APIs used by the DLL :)r.

## Compatibility

`BedrockLauncher-dll` is intended specifically for Windows and the Minecraft for Windows GDK!!

It is not intended to replace Minecraft's executable or modify the game itself. Its purpose is to provide the launcher-side process and package handling required to start and activate the correct Minecraft instance.

## License

See [LICENSE](LICENSE) for the license used by this project.