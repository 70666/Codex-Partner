<div align="center">

# Codex Partner

**把 Codex 的额度、重置时间和本地使用成本，安静地放进 Windows 托盘。**

一款真正原生、轻量、重视隐私的 Codex 桌面伴侣。

[![Release](https://img.shields.io/github/v/release/70666/Codex-Partner?display_name=tag&style=flat-square&color=4da3ff)](https://github.com/70666/Codex-Partner/releases/latest)
[![C++ checks](https://img.shields.io/github/actions/workflow/status/70666/Codex-Partner/pr-check.yml?branch=main&label=C%2B%2B%20checks&style=flat-square)](https://github.com/70666/Codex-Partner/actions)
[![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078d4?style=flat-square&logo=windows11&logoColor=white)](#系统要求)
[![License](https://img.shields.io/github/license/70666/Codex-Partner?style=flat-square)](LICENSE)

[下载最新版](https://github.com/70666/Codex-Partner/releases/latest) · [查看更新记录](CHANGELOG.md) · [反馈问题](https://github.com/70666/Codex-Partner/issues)

<sub>Native C++20 · Win32 · 无 WebView · 无 Node.js · 无常驻 sidecar</sub>

</div>

---

Codex 很好用，但“还剩多少额度、什么时候重置、最近到底用了多少”不该每次都打开网页才能知道。

Codex Partner 常驻在通知区域，把最重要的信息整理成一眼就能读懂的状态：当前周期、每周额度、重置时间、使用节奏，以及根据本地活动计算的 API 等价费用。需要细看时点开面板，不需要时它就安静待在后台。

## 它能做什么

- **额度一眼可见**：查看当前周期与每周额度、剩余比例和预计重置时间。
- **本地费用估算**：统计近 1 天、7 天和 30 天的 API 等价费用，并明确标注未计价活动，不把未知价格偷偷当成免费。
- **模型与项目分析**：按模型查看使用趋势，并列出估算金额最高的五个项目及占比。
- **用量提醒**：达到自定义阈值时发送 Windows 通知，也可以暂时静音。
- **浮动用量条**：在工作时用更小的常驻视图掌握额度，不必反复打开主面板。
- **托盘即入口**：双击图标打开面板，右键快速刷新、切换浮动条或进入设置。
- **中英文界面**：语言、主题、刷新频率、全局快捷键和隐私显示均可调整。
- **断网也不“失忆”**：刷新失败时保留上一次成功数据，同时清楚标记为缓存状态，避免把旧数据伪装成实时结果。

## 为什么是原生 C++

Codex Partner 不是网页套壳。桌面端使用 C++20、Win32、WinHTTP、GDI+ 与 Windows Shell API 编写，Release 构建静态链接 Microsoft C/C++ 运行库。

这意味着：

- 启动快，常驻开销小；
- 不依赖 WebView2、Node.js、Rust、Tauri 或额外后台进程；
- 与 Windows 托盘、通知、DPI 和辅助功能直接集成；
- 发布包中只有原生程序和必要文档，边界清晰、容易审查。

## 安装

前往 [Releases](https://github.com/70666/Codex-Partner/releases/latest)，下载：

```text
Codex-Partner-0.0.0-native-windows-x64.exe
```

这是早期版本，Windows SmartScreen 可能会因为程序尚未积累信誉而提示确认。每个 Release 同时提供 SHA-256 文件，便于核对下载内容。

### 系统要求

- Windows 10 或 Windows 11（x64）
- 已在本机登录并使用 Codex

## 隐私不是一句口号

Codex Partner 读取凭据只用于向提供方请求用量信息，不会把凭据写入设置文件或日志。

本地会话日志仅用于汇总 token 与项目活动；提示词和回复内容不会被复制进应用缓存。你也可以隐藏身份与项目名称，方便截图或分享状态。

费用数字是按照公开 API 价格计算的**等价估算**，不是 ChatGPT Pro/Plus 订阅账单，也不代表 OpenAI 会向你收取对应金额。遇到尚无价格的模型或不完整记录，界面会给出覆盖率与“至少”金额，而不是制造一个看似精确的答案。

## 从源码构建

需要 Visual Studio 2022 或更新版本，并安装“使用 C++ 的桌面开发”工作负载与 Windows SDK。

```powershell
git clone https://github.com/70666/Codex-Partner.git
cd Codex-Partner
.\native\build.ps1 -Configuration Release
```

生成文件：

```text
native\build\x64\Release\CodexPartner.exe
```

运行完整的本地检查与确定性打包：

```powershell
.\scripts\local-check.ps1
```

## 项目状态

当前版本为 **0.0.0**。核心链路已经可用，但产品仍处于快速打磨阶段。我们更看重真实体验：如果某个数字让人困惑、某次点击没有反馈，或者某段流程显得多余，都值得提出来。

欢迎通过 [Issues](https://github.com/70666/Codex-Partner/issues) 提交问题和建议。请勿在截图、日志或测试数据中包含凭据、会话内容及其他敏感信息。参与开发前可以先阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。

如果 Codex Partner 让你少打开几次用量网页，欢迎点一个 ⭐。它会帮助更多 Windows 用户发现这个项目。

---

<div align="center">

**Codex Partner is an independent native Windows companion for Codex usage, limits and local API-equivalent spend insights.**

Made for Windows, with C++.

</div>
