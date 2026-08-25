# Security policy

Codex Partner handles local provider credentials and usage data, so security
reports are treated as product issues rather than ordinary support requests.

## Supported versions

Security fixes target the latest stable GitHub release and the current default
branch. Older releases may be asked to upgrade instead of receiving a separate
patch. The Native C++ edition is a focused preview, but credential exposure,
unsafe navigation, update-integrity, and local privilege-boundary reports are
still in scope.

## Report a vulnerability privately

Use GitHub's **Report a vulnerability** action in this repository's Security
tab when it is available. Do not publish exploit details, credentials, tokens,
cookies, private provider responses, or personal account data in a public
issue.

If private vulnerability reporting is unavailable, contact the repository
owner through their GitHub profile and ask for a private reporting channel.
You may open a public issue saying only that you need private security contact;
do not include the vulnerability itself.

Please include:

- affected edition and exact version;
- Windows version and installation type;
- a minimal reproduction with secrets replaced by obvious placeholders;
- expected and observed security boundaries;
- whether the issue is already being exploited or publicly known.

## In-scope examples

- credentials or raw provider data written to logs, diagnostics, or UI proof;
- bypasses of DPAPI/secure-file handling for app-managed secrets;
- untrusted release metadata causing arbitrary download, navigation, or code
  execution;
- unsafe loopback API exposure or cross-provider credential mixing;
- privilege escalation, unintended startup persistence, or unsafe file writes.

General provider outages, incorrect quota values without a security impact,
and antivirus false positives belong in the normal bug template.

## Disclosure

Please allow time to reproduce, fix, package, and publish before disclosure.
The project will credit reporters who want attribution and will avoid naming
reporters who prefer privacy.
