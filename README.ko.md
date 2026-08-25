<div align="center">

[简体中文](README.md) · [English](README.en.md) · [日本語](README.ja.md) · 한국어

# Codex Partner

**Codex 한도, 초기화 시각, 로컬 사용 비용을 Windows 트레이에서 한눈에.**

가볍고 개인정보를 존중하는 네이티브 Windows용 Codex 동반 앱입니다.

[![Release](https://img.shields.io/github/v/release/70666/Codex-Partner?display_name=tag&style=flat-square&color=4da3ff)](https://github.com/70666/Codex-Partner/releases/latest)
[![C++ checks](https://img.shields.io/github/actions/workflow/status/70666/Codex-Partner/pr-check.yml?branch=main&label=C%2B%2B%20checks&style=flat-square)](https://github.com/70666/Codex-Partner/actions)
[![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078d4?style=flat-square&logo=windows11&logoColor=white)](#시스템-요구-사항)
[![License](https://img.shields.io/github/license/70666/Codex-Partner?style=flat-square)](LICENSE)

[최신 버전 다운로드](https://github.com/70666/Codex-Partner/releases/latest) · [변경 내역](CHANGELOG.md) · [문제 제보](https://github.com/70666/Codex-Partner/issues)

<sub>Native C++20 · Win32 · WebView 없음 · Node.js 없음 · 상주 sidecar 없음</sub>

</div>

---

Codex를 쓰는 도중 남은 한도와 초기화 시각을 확인하려고 매번 브라우저를 열 필요는 없습니다.

Codex Partner는 알림 영역에 조용히 상주하면서 현재 주기와 주간 한도, 초기화 시각, 사용 속도, 로컬 활동을 바탕으로 계산한 API 환산 비용을 보기 좋게 정리합니다. 자세한 내용이 필요할 때만 패널을 열고, 평소에는 백그라운드에 두면 됩니다.

## 주요 기능

- **한눈에 보는 사용 한도** — 현재 주기 및 주간 사용량, 남은 비율, 초기화 시각을 보여 줍니다.
- **로컬 비용 추정** — 최근 1일, 7일, 30일의 API 환산 금액을 집계합니다. 가격을 알 수 없는 활동은 무료로 처리하지 않고 별도로 표시합니다.
- **모델 및 프로젝트 분석** — 모델별 사용 추이와 추정 금액이 높은 프로젝트 5개, 각 비중을 확인할 수 있습니다.
- **사용량 알림** — 원하는 기준에 도달하면 Windows 알림을 보내며, 일정 시간 알림을 미룰 수도 있습니다.
- **플로팅 사용량 바** — 작업 흐름을 끊지 않고 한도를 확인하는 작은 상시 표시창입니다.
- **트레이 중심 조작** — 두 번 클릭해 열고, 오른쪽 클릭 메뉴에서 새로 고침과 설정, 플로팅 바를 빠르게 조작합니다.
- **사용자 설정** — 테마, 언어, 새로 고침 간격, 전역 단축키, 개인정보 표시를 조정할 수 있습니다.
- **캐시 상태를 명확하게** — 갱신에 실패하면 마지막 데이터를 유지하되, 실시간 정보로 오해하지 않도록 분명히 표시합니다.

## 네이티브 C++로 만든 이유

Codex Partner는 웹 페이지를 감싼 앱이 아닙니다. C++20, Win32, WinHTTP, GDI+, Windows Shell API로 작성했으며 Release 빌드는 Microsoft C/C++ 런타임을 정적 링크합니다.

WebView2, Node.js, Rust, Tauri 또는 별도의 백그라운드 sidecar에 의존하지 않습니다. 빠르게 실행되고 상주 부담이 작으며, Windows 트레이와 알림, DPI, 접근성 기능에 직접 연결됩니다.

## 설치

[최신 Release](https://github.com/70666/Codex-Partner/releases/latest)에서 `Codex-Partner-0.0.0-native-windows-x64.exe`를 내려받으세요.

아직 공개된 지 오래되지 않은 앱이라 SmartScreen 평판이 충분히 쌓이기 전까지 Windows가 확인 메시지를 표시할 수 있습니다. 각 Release에는 파일 검증을 위한 SHA-256 값도 함께 제공합니다.

### 시스템 요구 사항

- Windows 10 또는 Windows 11(x64)
- 이 PC에서 Codex에 로그인되어 있고 로컬 사용 기록이 존재할 것

## 개인정보와 비용 표시 원칙

인증 정보는 사용량 요청에만 읽어 사용하며 설정 파일이나 로그에 기록하지 않습니다. 로컬 세션 로그에서는 token 수와 프로젝트 활동만 집계하고, 프롬프트와 답변 본문은 앱 캐시에 복사하지 않습니다. 화면을 공유할 때는 사용자 정보와 프로젝트 이름을 숨길 수도 있습니다.

화면의 USD 금액은 공개 API 가격을 적용한 **API 환산 추정치**입니다. ChatGPT Pro/Plus 청구서가 아니며 OpenAI가 실제로 청구할 금액을 뜻하지도 않습니다. 가격이나 활동 기록이 불완전하면 그럴듯한 숫자를 만들어 내지 않고, 반영 범위와 최소 금액을 함께 보여 줍니다.

## 소스에서 빌드

Visual Studio 2022 이상에서 'C++를 사용한 데스크톱 개발' 워크로드와 Windows SDK를 설치하세요.

```powershell
git clone https://github.com/70666/Codex-Partner.git
cd Codex-Partner
.\native\build.ps1 -Configuration Release
```

실행 파일은 `native\build\x64\Release\CodexPartner.exe`에 생성됩니다. 전체 테스트와 재현 가능한 패키징은 `.\scripts\local-check.ps1`로 실행할 수 있습니다.

## 프로젝트 상태

현재 버전은 **0.0.0**입니다. 핵심 흐름은 사용할 수 있지만 제품을 빠르게 다듬고 있는 단계입니다. 이해하기 어려운 숫자, 반응이 어색한 클릭, 불편한 흐름을 발견했다면 [Issue](https://github.com/70666/Codex-Partner/issues)를 남겨 주세요.

Codex 사용량 페이지를 여는 횟수를 조금이라도 줄여 줬다면 ⭐로 응원해 주세요.

