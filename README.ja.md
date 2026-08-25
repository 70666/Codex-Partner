<div align="center">

[简体中文](README.md) · [English](README.en.md) · 日本語 · [한국어](README.ko.md)

# Codex Partner

**Codex の利用枠・リセット時刻・ローカル利用コストを、Windows のタスクトレイに。**

軽量でプライバシーを重視した、ネイティブ Windows アプリです。

[![Release](https://img.shields.io/github/v/release/70666/Codex-Partner?display_name=tag&style=flat-square&color=4da3ff)](https://github.com/70666/Codex-Partner/releases/latest)
[![C++ checks](https://img.shields.io/github/actions/workflow/status/70666/Codex-Partner/pr-check.yml?branch=main&label=C%2B%2B%20checks&style=flat-square)](https://github.com/70666/Codex-Partner/actions)
[![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078d4?style=flat-square&logo=windows11&logoColor=white)](#動作環境)
[![License](https://img.shields.io/github/license/70666/Codex-Partner?style=flat-square)](LICENSE)

[最新版をダウンロード](https://github.com/70666/Codex-Partner/releases/latest) · [更新履歴](CHANGELOG.md) · [不具合を報告](https://github.com/70666/Codex-Partner/issues)

<sub>Native C++20 · Win32 · WebView なし · Node.js なし · 常駐 sidecar なし</sub>

</div>

---

Codex を使っている最中に、「あとどれくらい使えるのか」「いつリセットされるのか」を確認するためだけにブラウザを開く必要はありません。

Codex Partner は通知領域に常駐し、現在の利用枠、週間利用枠、リセット時刻、利用ペース、ローカル履歴から算出した API 相当コストを見やすくまとめます。詳しく見たいときだけパネルを開き、普段は静かにバックグラウンドで動作します。

## 主な機能

- **利用枠をひと目で確認** — 現在のサイクルと週間利用枠、残量、リセット時刻を表示します。
- **ローカル費用の推定** — 直近 1 日・7 日・30 日の API 相当額を集計。価格が不明な利用は無料扱いせず、未計上として明示します。
- **モデル・プロジェクト分析** — モデル別の推移と、推定額が大きい上位 5 プロジェクト、その構成比を確認できます。
- **利用量の通知** — 指定したしきい値に達すると Windows 通知でお知らせ。一時停止も可能です。
- **フローティングバー** — 作業を中断せずに利用状況を確認できるコンパクト表示です。
- **タスクトレイ中心の操作** — ダブルクリックで開き、右クリックから更新・設定・フローティングバーを操作できます。
- **見た目と操作を調整** — テーマ、言語、更新間隔、グローバルショートカット、プライバシー表示を設定できます。
- **古いデータを正直に表示** — 更新に失敗しても前回値は残しますが、リアルタイム情報のようには見せません。

## ネイティブ C++ にこだわる理由

Codex Partner は Web ページを包んだだけのアプリではありません。C++20 と Win32、WinHTTP、GDI+、Windows Shell API で実装し、Release ビルドでは Microsoft C/C++ ランタイムを静的リンクしています。

WebView2、Node.js、Rust、Tauri、別体の常駐プロセスには依存しません。起動が速く、常駐時の負荷を抑えながら、タスクトレイ、通知、DPI、アクセシビリティへ直接統合できます。

## インストール

[最新の Release](https://github.com/70666/Codex-Partner/releases/latest) から `Codex-Partner-0.0.0-native-windows-x64.exe` をダウンロードしてください。

公開直後のアプリは SmartScreen の評価がまだ十分でないため、Windows が確認画面を表示する場合があります。各 Release には検証用の SHA-256 ファイルも同梱しています。

### 動作環境

- Windows 10 / 11（x64）
- この PC で Codex にログインし、利用履歴が保存されていること

## プライバシーと費用表示

認証情報は利用状況を取得するリクエストにだけ使い、設定ファイルやログには保存しません。ローカルのセッションログから集計するのは token 数とプロジェクト活動のみで、プロンプトや回答本文をアプリのキャッシュへコピーすることはありません。

画面に表示する USD 金額は、公開 API 価格に基づく**API 相当額の推定**です。ChatGPT Pro / Plus の請求額ではなく、OpenAI から実際に請求される金額を示すものでもありません。価格や履歴が不足している場合は、無理に正確そうな数字を作らず、カバー率と最低額として表示します。

## ソースからビルド

Visual Studio 2022 以降で「C++ によるデスクトップ開発」と Windows SDK をインストールしてください。

```powershell
git clone https://github.com/70666/Codex-Partner.git
cd Codex-Partner
.\native\build.ps1 -Configuration Release
```

実行ファイルは `native\build\x64\Release\CodexPartner.exe` に生成されます。テストと再現可能なパッケージ作成は `.\scripts\local-check.ps1` で実行できます。

## 開発状況

現在のバージョンは **0.0.0** です。基本機能は利用できますが、まだ積極的に磨き込んでいる段階です。分かりにくい表示や反応のない操作、使いづらい流れを見つけたら、ぜひ [Issue](https://github.com/70666/Codex-Partner/issues) で教えてください。

Codex の利用状況ページを開く回数が少しでも減ったなら、⭐ を付けてもらえるとうれしいです。

