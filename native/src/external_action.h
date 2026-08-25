#pragma once

#include <cstdint>

namespace codex_partner {

enum class ExternalAction {
    None,
    CodexLogin,
    CodexFolder,
    ProjectSite,
    IssuePage,
    ReleasePage,
    NativeUpdateDownload,
};

enum class ExternalActionOutcome { Idle, Opened, Failed };

struct ExternalActionFeedback {
    ExternalAction action = ExternalAction::None;
    ExternalActionOutcome outcome = ExternalActionOutcome::Idle;
    bool diagnostics_copy_attempted = false;
    bool diagnostics_copied = false;
};

[[nodiscard]] constexpr const wchar_t* IssueReportUrl() noexcept {
    return L"https://github.com/70666/Codex-Partner/issues/new?template=bug_report.yml";
}

[[nodiscard]] constexpr bool ExternalActionFullySucceeded(ExternalActionFeedback feedback) noexcept {
    return feedback.outcome == ExternalActionOutcome::Opened &&
        (!feedback.diagnostics_copy_attempted || feedback.diagnostics_copied);
}

[[nodiscard]] constexpr bool ExternalActionPartiallySucceeded(ExternalActionFeedback feedback) noexcept {
    if (!feedback.diagnostics_copy_attempted) return false;
    const bool opened = feedback.outcome == ExternalActionOutcome::Opened;
    return opened != feedback.diagnostics_copied;
}

[[nodiscard]] constexpr bool ShellLaunchSucceeded(std::intptr_t result) noexcept {
    return result > 32;
}

[[nodiscard]] inline const wchar_t* ExternalActionStatus(
    ExternalActionFeedback feedback, bool chinese) noexcept {
    if (feedback.outcome == ExternalActionOutcome::Idle || feedback.action == ExternalAction::None) return L"";
    const bool opened = feedback.outcome == ExternalActionOutcome::Opened;
    switch (feedback.action) {
    case ExternalAction::CodexLogin:
        return opened ? (chinese ? L"已打开 Codex 登录终端" : L"Codex login terminal opened") :
            (chinese ? L"无法打开登录终端 · 请手动运行 codex login" : L"Couldn't open login terminal · run codex login manually");
    case ExternalAction::CodexFolder:
        return opened ? (chinese ? L"已打开 Codex 配置目录" : L"Codex configuration opened") :
            (chinese ? L"无法打开 Codex 配置目录" : L"Couldn't open Codex configuration");
    case ExternalAction::ProjectSite:
        return opened ? (chinese ? L"已打开项目主页" : L"Project page opened") :
            (chinese ? L"无法打开项目主页" : L"Couldn't open project page");
    case ExternalAction::IssuePage:
        if (opened && feedback.diagnostics_copied) {
            return chinese ? L"问题页面已打开 · 诊断已复制" : L"Issue opened · diagnostics copied";
        }
        if (opened) return chinese ? L"问题页面已打开 · 复制失败" : L"Issue opened · copy failed";
        if (feedback.diagnostics_copied) {
            return chinese ? L"诊断已复制 · 无法打开问题页面" : L"Diagnostics copied · couldn't open issue";
        }
        return chinese ? L"无法打开问题页面或复制诊断" : L"Couldn't open issue or copy diagnostics";
    case ExternalAction::ReleasePage:
        return opened ? (chinese ? L"已打开版本页面" : L"Release page opened") :
            (chinese ? L"无法打开版本页面" : L"Couldn't open release page");
    case ExternalAction::NativeUpdateDownload:
        return opened ? (chinese ? L"已打开 Native 更新下载" : L"Native update download opened") :
            (chinese ? L"无法打开 Native 更新下载" : L"Couldn't open Native update download");
    case ExternalAction::None: return L"";
    }
    return L"";
}

[[nodiscard]] inline const wchar_t* ExternalActionDescription(
    ExternalActionFeedback feedback, bool chinese) noexcept {
    if (feedback.action == ExternalAction::IssuePage && feedback.diagnostics_copy_attempted) {
        const bool opened = feedback.outcome == ExternalActionOutcome::Opened;
        if (opened && feedback.diagnostics_copied) {
            return chinese ? L"诊断摘要已在剪贴板中，请粘贴到新打开的问题表单。" :
                L"The diagnostic summary is on the clipboard; paste it into the opened issue form.";
        }
        if (opened) {
            return chinese ? L"问题表单已打开，但剪贴板正忙；可稍后单独使用“复制诊断摘要”。" :
                L"The issue form is open, but the clipboard was busy; use Copy diagnostics and try again.";
        }
        if (feedback.diagnostics_copied) {
            return chinese ? L"诊断摘要已在剪贴板中；请手动打开项目的 GitHub Issues 页面后粘贴。" :
                L"The diagnostic summary is on the clipboard; open the project Issues page manually and paste it.";
        }
        return chinese ? L"Windows 未能打开浏览器，剪贴板也正忙；请稍后重试。" :
            L"Windows did not open the browser and the clipboard was busy; try again.";
    }
    if (feedback.outcome == ExternalActionOutcome::Failed) {
        return chinese ? L"Windows 未能启动目标。请重试，或使用默认浏览器、文件资源管理器或终端手动打开。" :
            L"Windows did not launch the target. Try again, or open it manually in your default browser, File Explorer, or terminal.";
    }
    if (feedback.outcome == ExternalActionOutcome::Opened) {
        return chinese ? L"Windows 已接受打开请求。" : L"Windows accepted the open request.";
    }
    return L"";
}

}  // namespace codex_partner
