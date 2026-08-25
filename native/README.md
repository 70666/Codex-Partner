# Native application

`native/` is the complete Codex Partner application.

```powershell
.\build.ps1
.\build.ps1 -TestsOnly
.\build\x64\Release\CodexPartner.Tests.exe
.\package.ps1 -SkipBuild
```

The application uses only Windows system APIs and the statically linked C/C++
runtime. `src/` contains product code, `tests/` contains deterministic tests,
and the three project files are named exclusively for Codex Partner.

Proof-mode environment variables use the `CODEX_PARTNER_` prefix. They are
automation-only and never persisted.
