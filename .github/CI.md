# Continuous integration

GitHub Actions builds the native C++ solution, runs `CodexPartner.Tests.exe`,
and verifies the four-file release asset contract on Windows.

Run the same checks locally:

```powershell
.\scripts\local-check.ps1
```
