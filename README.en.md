<div align="center">

[简体中文](README.md) · English · [日本語](README.ja.md) · [한국어](README.ko.md)

# Codex Partner

**Codex limits, reset times, and local spend insights—right in your Windows tray.**

A lightweight, privacy-minded, truly native companion for Codex.

[![Release](https://img.shields.io/github/v/release/70666/Codex-Partner?display_name=tag&style=flat-square&color=4da3ff)](https://github.com/70666/Codex-Partner/releases/latest)
[![C++ checks](https://img.shields.io/github/actions/workflow/status/70666/Codex-Partner/pr-check.yml?branch=main&label=C%2B%2B%20checks&style=flat-square)](https://github.com/70666/Codex-Partner/actions)
[![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078d4?style=flat-square&logo=windows11&logoColor=white)](#requirements)
[![License](https://img.shields.io/github/license/70666/Codex-Partner?style=flat-square)](LICENSE)

[Download](https://github.com/70666/Codex-Partner/releases/latest) · [Changelog](CHANGELOG.md) · [Report an issue](https://github.com/70666/Codex-Partner/issues)

<sub>Native C++20 · Win32 · No WebView · No Node.js · No resident sidecar</sub>

</div>

---

Codex is great at getting work done. Keeping track of its limits should not require another browser tab.

Codex Partner lives quietly in the notification area and turns the information you care about into a glanceable status: current and weekly limits, reset times, usage pace, and an API-equivalent estimate derived from local activity. Open the panel when you need detail; leave it in the background when you do not.

## What it does

- **Limits at a glance** — current-cycle and weekly usage, remaining capacity, and reset times.
- **Local spend estimates** — API-equivalent totals for the last 1, 7, and 30 days, with unpriced activity called out instead of treated as free.
- **Model and project insights** — model trends plus the five projects with the highest estimated spend and their shares.
- **Useful alerts** — native Windows notifications at a threshold you choose, with snooze controls.
- **Floating usage bar** — a compact always-available view for focused work.
- **Tray-first workflow** — double-click to open; right-click to refresh, toggle the floating bar, or open settings.
- **English and Chinese UI** — theme, language, refresh cadence, global shortcut, and privacy controls.
- **Honest stale states** — the last successful result stays visible after a failed refresh, but is clearly marked as cached.

## Native by design

Codex Partner is not a wrapped web page. The desktop app is written in C++20 using Win32, WinHTTP, GDI+, and Windows Shell APIs. Release builds statically link the Microsoft C/C++ runtime.

There is no WebView2, Node.js, Rust, Tauri, or separate background sidecar. The result is a fast-starting app with a small resident footprint and direct integration with Windows tray, notifications, DPI scaling, and accessibility.

## Install

Download `Codex-Partner-0.0.0-native-windows-x64.exe` from the [latest release](https://github.com/70666/Codex-Partner/releases/latest).

Codex Partner is still new, so Windows SmartScreen may ask for confirmation before the app has built enough reputation. Every release includes SHA-256 files for independent verification.

### Requirements

- Windows 10 or Windows 11, x64
- A local Codex sign-in and usage history

## Privacy and pricing clarity

Credentials are read only for provider requests. They are never written to the settings file or logs. Local session logs are scanned for aggregate token and project activity; prompts and responses are not copied into the application cache. Identity and project names can also be hidden before sharing a screenshot or summary.

Displayed USD values are **API-equivalent estimates**, not a ChatGPT Pro or Plus invoice and not a prediction of what OpenAI will charge you. When pricing or activity data is incomplete, Codex Partner shows coverage and a lower bound instead of manufacturing a precise-looking number.

## Build from source

Install Visual Studio 2022 or newer with the Desktop development with C++ workload and a Windows SDK.

```powershell
git clone https://github.com/70666/Codex-Partner.git
cd Codex-Partner
.\native\build.ps1 -Configuration Release
```

The binary is written to `native\build\x64\Release\CodexPartner.exe`. Run `.\scripts\local-check.ps1` for the full local test and deterministic packaging pass.

## Project status

The current release is **0.0.0**. The core workflow is usable, but the product is still being shaped quickly. If a number is confusing, a click feels unresponsive, or a workflow gets in your way, please [open an issue](https://github.com/70666/Codex-Partner/issues).

If Codex Partner saves you a few trips to the usage page, consider leaving a ⭐. It helps other Windows Codex users find the project.

