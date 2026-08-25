# Codex Partner

Codex Partner is an independent, native Windows companion for viewing Codex
limits, reset times, local activity, and API-equivalent usage estimates.

The repository contains one product and one implementation:

- C++20
- Win32, WinHTTP, GDI+, and Windows Shell APIs
- no Rust, Tauri, React, WebView2, Node.js, or sidecar process
- statically linked Microsoft C/C++ runtime for Release builds

## Build

Requirements: Windows 10/11, Visual Studio 2022 or newer, the Desktop
development with C++ workload, and a Windows SDK.

```powershell
.\native\build.ps1 -Configuration Release
```

Output:

```text
native\build\x64\Release\CodexPartner.exe
```

## Test and package

```powershell
.\scripts\local-check.ps1
```

The release package contains only the tested native executable, this README,
LICENSE, NOTICE, and build metadata. Generated assets are:

```text
Codex-Partner-<version>-native-windows-x64.exe
Codex-Partner-<version>-native-windows-x64.exe.sha256
Codex-Partner-<version>-windows-x64.zip
Codex-Partner-<version>-windows-x64.sha256
```

## Privacy and estimates

Codex credentials are read only for the provider request and are not written to
the settings file or logs. Local session logs are scanned for aggregate token
activity; prompts and responses are never copied into the application cache.

Displayed USD values are API-equivalent estimates, not a ChatGPT subscription
invoice. Unknown-price activity remains visibly excluded rather than being
silently treated as free.

## Repository

Project home: https://github.com/70666/Codex-Partner
