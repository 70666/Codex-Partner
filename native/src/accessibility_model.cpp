#include "accessibility_model.h"
#include "notification_snooze.h"

#include "usage_freshness.h"
#include "usage_summary.h"
#include "version.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace codex_partner::accessibility {
namespace {

const wchar_t* T(bool chinese, const wchar_t* english, const wchar_t* simplified_chinese) noexcept {
    return chinese ? simplified_chinese : english;
}

std::wstring Percent(double value) {
    return std::to_wstring(static_cast<int>(std::round(std::clamp(value, 0.0, 100.0)))) + L"%";
}

std::wstring Money(const std::optional<double>& value, bool partial, bool chinese) {
    if (!value) return T(chinese, L"unavailable", L"不可用");
    std::wostringstream output;
    if (partial) output << L"≥ ";
    output << L'$' << std::fixed << std::setprecision(2) << std::max(0.0, *value);
    return output.str();
}

std::wstring WideAscii(std::string_view value) {
    return std::wstring(value.begin(), value.end());
}

std::wstring WideUtf8(std::string_view value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return WideAscii(value);
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), length);
    return result;
}

unsigned InteractiveState(bool focused, bool pressed, bool checked = false, bool selected = false) noexcept {
    unsigned state = StateFocusable;
    if (focused) state |= StateFocused;
    if (pressed) state |= StatePressed;
    if (checked) state |= StateChecked;
    if (selected) state |= StateSelected;
    return state;
}

Element ActionElement(long child_id, Role role, Bounds bounds, std::wstring name, std::wstring description,
    std::wstring default_action, std::wstring shortcut, int command, bool focused, bool pressed,
    bool checked = false, bool selected = false, std::wstring value = {}) {
    return Element{child_id, role, bounds, std::move(name), std::move(value), std::move(description),
        std::move(default_action), std::move(shortcut), command,
        InteractiveState(focused, pressed, checked, selected)};
}

Element StaticElement(long child_id, Bounds bounds, std::wstring name, std::wstring value, std::wstring description = {}) {
    return Element{child_id, Role::StaticText, bounds, std::move(name), std::move(value),
        std::move(description), {}, {}, 0, StateReadOnly};
}

std::wstring WindowValue(const std::optional<RateWindow>& window, bool chinese) {
    if (!window) return T(chinese, L"Unavailable", L"不可用");
    std::wstring value = Percent(window->used_percent) + T(chinese, L" used", L" 已使用");
    value += L", " + Percent(100.0 - window->used_percent) + T(chinese, L" remaining", L" 剩余");
    if (window->window_minutes > 0) value += L", " + std::to_wstring(window->window_minutes) + T(chinese, L" minute window", L" 分钟窗口");
    return value;
}

std::wstring SpendValue(const SpendSummary& spend, bool chinese) {
    std::wstring value = T(chinese, L"1 day ", L"近 1 天 ") + Money(spend.one_day_usd, spend.one_day_partial, chinese);
    value += T(chinese, L", 7 days ", L"，近 7 天 ") + Money(spend.seven_day_usd, spend.seven_day_partial, chinese);
    value += T(chinese, L", 30 days ", L"，近 30 天 ") + Money(spend.thirty_day_usd, spend.thirty_day_partial, chinese);
    if (const auto coverage = SpendPricingCoveragePercent(spend)) {
        value += T(chinese, L", event pricing coverage ", L"，事件计价覆盖 ") +
            std::to_wstring(*coverage) + L"%";
    }
    if (const auto coverage = SpendTokenCoveragePercent(spend)) {
        value += T(chinese, L", token pricing coverage ", L"，token 计价覆盖 ") +
            std::to_wstring(*coverage) + L"%";
    }
    if (spend.priced_cache_write_input_tokens > 0) {
        value += T(chinese, L", priced cache-write tokens ", L"，已计价缓存写入 token ") +
            std::to_wstring(spend.priced_cache_write_input_tokens);
    }
    if (spend.unpriced_events > 0 || spend.partial) {
        value += T(chinese, L", known-priced lower bound", L"，金额为已知计价下限");
        value += T(chinese, L", unpriced events ", L"，未计价事件 ") + std::to_wstring(spend.unpriced_events);
        const std::string models = SummarizeUnpricedModels(spend);
        if (!models.empty()) {
            value += T(chinese, L", missing public price ", L"，缺少公开价格 ") + WideAscii(models);
        }
    }
    if (const std::wstring pace = FormatSpendPaceInsight(spend, chinese); !pace.empty()) {
        value += T(chinese, L". ", L"。") + pace;
    }
    return value;
}

std::wstring PaceDescription(const UsageSnapshot& snapshot, bool chinese) {
    const auto pace = MostUrgentPaceForecast(snapshot);
    if (!pace) return {};
    const bool weekly = pace->window_minutes >= 6 * 24 * 60 || pace->window_title == L"Weekly";
    const std::wstring window = weekly ? T(chinese, L"weekly capacity", L"每周额度") :
        T(chinese, L"current session", L"当前周期");
    const auto minutes = std::max<std::int64_t>(1, pace->until_exhaustion.count());
    std::wstring remaining;
    if (minutes < 60) remaining = std::to_wstring(minutes) + T(chinese, L" minutes", L" 分钟");
    else if (minutes < 48 * 60) remaining = std::to_wstring(std::max<std::int64_t>(1, minutes / 60)) + T(chinese, L" hours", L" 小时");
    else remaining = std::to_wstring(std::max<std::int64_t>(1, minutes / (24 * 60))) + T(chinese, L" days", L" 天");
    return chinese ? L"按当前速度，" + window + L"可能在约 " + remaining + L"后、重置前用尽" :
        L"At the current pace, " + window + L" may run out in about " + remaining + L", before reset";
}

void AddSettingsTabs(std::vector<Element>& elements, ui::SettingsTab active, bool chinese,
    ui::SettingsAction focused, ui::SettingsAction pressed) {
    struct TabEntry { long id; int y; ui::SettingsTab tab; ui::SettingsAction action; const wchar_t* en; const wchar_t* zh; };
    constexpr std::array entries{
        TabEntry{1, 90, ui::SettingsTab::General, ui::SettingsAction::SelectGeneral, L"General", L"通用"},
        TabEntry{2, 138, ui::SettingsTab::Providers, ui::SettingsAction::SelectProviders, L"Providers", L"提供商"},
        TabEntry{3, 184, ui::SettingsTab::Notifications, ui::SettingsAction::SelectNotifications, L"Notifications", L"通知"},
        TabEntry{4, 230, ui::SettingsTab::FloatBar, ui::SettingsAction::SelectFloatBar, L"Floating bar", L"浮动用量条"},
        TabEntry{5, 276, ui::SettingsTab::UsageSpend, ui::SettingsAction::SelectUsageSpend, L"Usage and spend", L"使用与费用"},
        TabEntry{6, 322, ui::SettingsTab::About, ui::SettingsAction::SelectAbout, L"About", L"关于"},
    };
    for (const auto& entry : entries) {
        elements.push_back(ActionElement(entry.id, Role::PageTab, {28, entry.y, 156, 48}, T(chinese, entry.en, entry.zh),
            T(chinese, L"Open this settings page", L"打开此设置页面"), T(chinese, L"Select", L"选择"), {},
            static_cast<int>(entry.action), focused == entry.action, pressed == entry.action, false, active == entry.tab));
    }
}

}  // namespace

long PopupChildId(ui::PopupAction action) noexcept {
    switch (action) {
    case ui::PopupAction::CopySummary: return 4;
    case ui::PopupAction::Refresh: return 1;
    case ui::PopupAction::Settings: return 2;
    case ui::PopupAction::Primary: return 3;
    case ui::PopupAction::None: return 0;
    }
    return 0;
}

long FloatBarChildId(ui::FloatBarAction action) noexcept {
    switch (action) {
    case ui::FloatBarAction::OpenPopup: return 1;
    case ui::FloatBarAction::Hide: return 2;
    case ui::FloatBarAction::None: return 0;
    }
    return 0;
}

long SettingsChildId(ui::SettingsTab tab, ui::SettingsAction action) noexcept {
    switch (action) {
    case ui::SettingsAction::SelectGeneral: return 1;
    case ui::SettingsAction::SelectProviders: return 2;
    case ui::SettingsAction::SelectNotifications: return 3;
    case ui::SettingsAction::SelectFloatBar: return 4;
    case ui::SettingsAction::SelectUsageSpend: return 5;
    case ui::SettingsAction::SelectAbout: return 6;
    case ui::SettingsAction::CycleLanguage: return 10;
    case ui::SettingsAction::CycleTheme: return 11;
    case ui::SettingsAction::ChooseGlobalShortcut: return 16;
    case ui::SettingsAction::CycleRefresh: return 12;
    case ui::SettingsAction::ToggleStartAtLogin: return 13;
    case ui::SettingsAction::ToggleStartMinimized: return 14;
    case ui::SettingsAction::TogglePrivacy: return 15;
    case ui::SettingsAction::ToggleFloatBar: return 35;
    case ui::SettingsAction::ResetFloatBarPosition: return 36;
    case ui::SettingsAction::ToggleUsageNotifications: return 30;
    case ui::SettingsAction::ChooseUsageWarning: return 31;
    case ui::SettingsAction::ChooseNotificationSnooze: return 33;
    case ui::SettingsAction::TestNotification: return 32;
    case ui::SettingsAction::LaunchCodexLogin: return 21;
    case ui::SettingsAction::OpenCodexFolder: return tab == ui::SettingsTab::UsageSpend ? 0 : 22;
    case ui::SettingsAction::CheckForUpdates: return 53;
    case ui::SettingsAction::ReportIssue: return 54;
    case ui::SettingsAction::OpenProjectSite: return 51;
    case ui::SettingsAction::CopyDiagnostics: return 52;
    case ui::SettingsAction::None: return 0;
    }
    return 0;
}

std::vector<Element> BuildPopupElements(const UsageSnapshot& snapshot, bool chinese, bool identity_hidden, RefreshPhase refresh_phase,
    ui::CopySummaryState copy_state, ExternalActionFeedback external_feedback, ui::PopupAction focused,
    ui::PopupAction pressed, bool refresh_queued) {
    const bool refreshing = RefreshIsActive(refresh_phase);
    const bool scanning_spend = refresh_phase == RefreshPhase::ScanningSpend;
    std::vector<Element> elements;
    elements.reserve(9);
    const bool copied = copy_state == ui::CopySummaryState::Copied;
    const bool failed = copy_state == ui::CopySummaryState::Failed;
    elements.push_back(ActionElement(4, Role::PushButton, {276, 12, 42, 40},
        copied ? T(chinese, L"Usage summary copied", L"使用摘要已复制") :
        failed ? T(chinese, L"Couldn't copy usage summary", L"使用摘要复制失败") :
            T(chinese, L"Copy usage summary", L"复制使用摘要"),
        T(chinese,
            L"Copy limits and API-equivalent estimates without credentials or account identifiers; respects Hide identity",
            L"复制额度与 API 等价费用估算，不包含凭据或账户标识，并遵循隐藏身份设置"),
        T(chinese, L"Copy", L"复制"), L"Ctrl+C", static_cast<int>(ui::PopupAction::CopySummary),
        focused == ui::PopupAction::CopySummary, pressed == ui::PopupAction::CopySummary,
        false, false, copied ? T(chinese, L"Copied; ready to paste", L"已复制，可直接粘贴") :
            failed ? T(chinese, L"Copy failed; clipboard may be busy", L"复制失败；剪贴板可能正被占用") : L""));
    elements.push_back(ActionElement(1, Role::PushButton, {318, 12, 42, 40},
        refresh_queued ? T(chinese, L"One more refresh queued", L"已排队再刷新一次") :
        refreshing ? T(chinese, L"Queue one more refresh", L"完成后再刷新一次") :
            T(chinese, L"Refresh usage", L"刷新使用情况"),
        refresh_queued ? T(chinese, L"The current cycle will finish before one trailing refresh", L"当前周期完成后会执行一次尾随刷新") :
            T(chinese, L"Fetch Codex limits and local spend now", L"立即读取 Codex 额度和本地费用"),
        T(chinese, L"Press", L"按下"), L"F5", static_cast<int>(ui::PopupAction::Refresh),
        focused == ui::PopupAction::Refresh, pressed == ui::PopupAction::Refresh));
    elements.push_back(ActionElement(2, Role::PushButton, {358, 12, 42, 40},
        T(chinese, L"Open settings", L"打开设置"), T(chinese, L"Open Codex Partner preferences", L"打开 Codex Partner 偏好设置"),
        T(chinese, L"Press", L"按下"), {}, static_cast<int>(ui::PopupAction::Settings),
        focused == ui::PopupAction::Settings, pressed == ui::PopupAction::Settings));
    const bool needs_setup = NeedsProviderSetup(snapshot);
    const UsagePrimaryTarget primary_target = ResolveUsagePrimaryTarget(snapshot);
    static_cast<void>(external_feedback);
    const bool retry = primary_target == UsagePrimaryTarget::RefreshUsage;
    elements.push_back(ActionElement(3, Role::PushButton,
        {16, 526, 368, 54},
        primary_target == UsagePrimaryTarget::ProviderSetup ?
            T(chinese, L"Set up Codex in Providers", L"前往提供商连接 Codex") :
        retry ? (refresh_queued ? T(chinese, L"Codex refresh queued", L"Codex 刷新已排队") :
            refreshing ? T(chinese, L"Refreshing Codex usage", L"正在重新连接 Codex") :
            T(chinese, L"Retry Codex refresh", L"重试 Codex 刷新")) :
            T(chinese, L"View detailed analytics", L"查看详细分析"),
        primary_target == UsagePrimaryTarget::ProviderSetup ?
            T(chinese, L"Open provider settings and finish the guided Codex sign-in", L"打开提供商设置并完成 Codex 登录引导") :
        retry ? T(chinese, L"Retry the read-only Codex connection without leaving this panel", L"不离开此面板，重试只读 Codex 连接") :
            T(chinese, L"Open model trends and project spend inside Codex Partner", L"在 Codex Partner 内查看模型趋势与项目费用"),
        retry ? T(chinese, L"Retry", L"重试") : T(chinese, L"Open", L"打开"), {},
        static_cast<int>(ui::PopupAction::Primary),
        focused == ui::PopupAction::Primary, pressed == ui::PopupAction::Primary));
    if (retry && refreshing) elements.back().state |= StateUnavailable;

    std::wstring status;
    if (refresh_queued) status = T(chinese, L"One more refresh is queued after the current cycle", L"当前周期完成后已排队再刷新一次");
    else if (refresh_phase == RefreshPhase::FetchingUsage) status = T(chinese,
        L"Refreshing Codex limits and local spend", L"正在刷新 Codex 额度与本地费用");
    else if (scanning_spend) status = T(chinese,
        L"Codex limits ready; local spend scan continues", L"Codex 额度已就绪；本地费用扫描仍在继续");
    else if (needs_setup) status = T(chinese, L"Codex sign-in required. Open Providers to connect without pasting an API key", L"需要登录 Codex。打开提供商即可连接，无需粘贴 API Key");
    else if (snapshot.stale) status = T(chinese, L"Showing last known usage", L"正在显示上次可用数据");
    else if (!snapshot.error.empty()) status = T(chinese, L"Usage unavailable", L"使用情况不可用");
    else status = T(chinese, L"Codex usage ready", L"Codex 使用情况已就绪");
    const std::wstring pace = PaceDescription(snapshot, chinese);
    if (!pace.empty()) status += L". " + pace;
    status += T(chinese, L". ", L"。") + FormatUsageFreshness(snapshot.updated_at, chinese);
    if (identity_hidden) status += T(chinese, L". Identity hidden by privacy setting", L"。身份信息已按隐私设置隐藏");
    else if (!snapshot.plan.empty()) status += L", " + snapshot.plan;
    elements.push_back(StaticElement(10, {16, 66, 368, 106}, T(chinese, L"Codex status", L"Codex 状态"), status));
    elements.push_back(StaticElement(11, {16, 188, 368, 126}, T(chinese, L"Current session", L"当前周期"), WindowValue(snapshot.session, chinese)));
    elements.push_back(StaticElement(12, {16, 314, 368, 126}, T(chinese, L"Weekly capacity", L"每周额度"), WindowValue(snapshot.weekly, chinese)));
    const SpendSummary empty;
    elements.push_back(StaticElement(13, {16, 442, 368, 72}, T(chinese, L"Local API-equivalent spend estimate", L"本地 API 等价费用估算"),
        SpendValue(snapshot.spend.value_or(empty), chinese), scanning_spend ?
            T(chinese, L"Local logs are still being scanned; the latest estimate remains visible", L"本地日志仍在扫描；最近一次估算继续显示") :
            T(chinese, L"This is an estimate, not a subscription invoice", L"这是估算值，不是订阅账单")));
    return elements;
}

std::vector<Element> BuildFloatBarElements(const UsageSnapshot& snapshot, bool chinese, bool identity_hidden, RefreshPhase refresh_phase,
    ui::FloatBarAction focused, ui::FloatBarAction pressed) {
    std::wstring value;
    if (refresh_phase == RefreshPhase::FetchingUsage) value = T(chinese, L"Refreshing Codex limits", L"正在刷新 Codex 额度");
    else if (refresh_phase == RefreshPhase::ScanningSpend) value = T(chinese,
        L"Codex limits ready; local spend scan continues", L"Codex 额度已就绪；本地费用扫描仍在继续");
    else if (snapshot.stale) value = T(chinese, L"Showing last known usage", L"正在显示上次可用数据");
    else if (!snapshot.error.empty()) value = T(chinese, L"Usage unavailable", L"使用情况不可用");
    else value = T(chinese, L"Codex usage ready", L"Codex 使用情况已就绪");
    value += T(chinese, L". Session: ", L"。当前周期：") + WindowValue(snapshot.session, chinese);
    value += T(chinese, L". Weekly: ", L"。每周额度：") + WindowValue(snapshot.weekly, chinese);
    const std::wstring pace = PaceDescription(snapshot, chinese);
    if (!pace.empty()) value += L". " + pace;
    value += T(chinese, L". ", L"。") + FormatUsageFreshness(snapshot.updated_at, chinese);
    if (identity_hidden) value += T(chinese, L". Identity hidden by privacy setting", L"。身份信息已按隐私设置隐藏");

    std::vector<Element> elements;
    elements.reserve(2);
    elements.push_back(ActionElement(1, Role::PushButton, {0, 0, 382, 76},
        T(chinese, L"Open Codex Partner usage", L"打开 Codex Partner 使用面板"),
        T(chinese, L"Drag the background to reposition the floating bar", L"拖动背景可移动浮动用量条"),
        T(chinese, L"Open", L"打开"), L"Enter", static_cast<int>(ui::FloatBarAction::OpenPopup),
        focused == ui::FloatBarAction::OpenPopup, pressed == ui::FloatBarAction::OpenPopup,
        false, false, std::move(value)));
    elements.push_back(ActionElement(2, Role::PushButton, {382, 0, 38, 76},
        T(chinese, L"Hide floating bar", L"隐藏浮动用量条"),
        T(chinese, L"You can show it again from Settings", L"可以随时在设置中重新显示"),
        T(chinese, L"Hide", L"隐藏"), L"Esc", static_cast<int>(ui::FloatBarAction::Hide),
        focused == ui::FloatBarAction::Hide, pressed == ui::FloatBarAction::Hide));
    return elements;
}

std::vector<Element> BuildSettingsElements(const AppSettings& settings, const UsageSnapshot& snapshot,
    const UpdateCheckState& update, SettingsPersistenceState persistence, ExternalActionFeedback external_feedback,
    RefreshPhase refresh_phase, ui::SettingsTab tab, bool chinese,
    ui::SettingsAction focused, ui::SettingsAction pressed, GlobalShortcutStatus global_shortcut_status) {
    std::vector<Element> elements;
    elements.reserve(14);
    AddSettingsTabs(elements, tab, chinese, focused, pressed);
    const auto is_focused = [&](ui::SettingsAction action) { return focused == action; };
    const auto is_pressed = [&](ui::SettingsAction action) { return pressed == action; };

    if (tab == ui::SettingsTab::General) {
        const std::wstring language = settings.language == LanguageMode::SimplifiedChinese ? L"简体中文" :
            settings.language == LanguageMode::English ? L"English" : T(chinese, L"System language", L"跟随系统");
        const std::wstring theme = settings.theme == ThemeMode::Light ? T(chinese, L"Light", L"浅色") :
            settings.theme == ThemeMode::Dark ? T(chinese, L"Dark", L"深色") : T(chinese, L"System theme", L"跟随系统");
        elements.push_back(ActionElement(10, Role::ComboBox, {208, 104, 472, 64}, T(chinese, L"Language", L"语言"),
            T(chinese, L"Choose the display language", L"选择界面显示语言"), T(chinese, L"Choose", L"选择"), {},
            static_cast<int>(ui::SettingsAction::CycleLanguage), is_focused(ui::SettingsAction::CycleLanguage), is_pressed(ui::SettingsAction::CycleLanguage), false, false, language));
        elements.push_back(ActionElement(11, Role::ComboBox, {208, 172, 472, 64}, T(chinese, L"Theme", L"主题"),
            T(chinese, L"Choose the application theme", L"选择应用主题"), T(chinese, L"Choose", L"选择"), {},
            static_cast<int>(ui::SettingsAction::CycleTheme), is_focused(ui::SettingsAction::CycleTheme), is_pressed(ui::SettingsAction::CycleTheme), false, false, theme));
        const std::wstring shortcut_description = global_shortcut_status == GlobalShortcutStatus::CandidateUnavailable ?
            T(chinese, L"The attempted shortcut is used by another app; the current shortcut was kept", L"尝试的快捷键已被其他应用占用；已保留当前快捷键") :
            global_shortcut_status == GlobalShortcutStatus::Unavailable ?
                T(chinese, L"The saved shortcut could not be registered; choose a different combination", L"无法注册已保存的快捷键；请选择其他组合") :
            settings.global_shortcut == GlobalShortcut::Disabled ?
                T(chinese, L"System-wide quick peek is turned off", L"全局快速查看已关闭") :
                T(chinese, L"Open or hide Codex Partner from any application", L"在任意应用中打开或隐藏 Codex Partner");
        const std::wstring shortcut_value = settings.global_shortcut == GlobalShortcut::Disabled && chinese ?
            L"关闭" : GlobalShortcutLabel(settings.global_shortcut);
        elements.push_back(ActionElement(16, Role::ComboBox, {208, 240, 472, 64},
            T(chinese, L"Quick peek shortcut", L"快速查看快捷键"), shortcut_description,
            T(chinese, L"Choose", L"选择"), {}, static_cast<int>(ui::SettingsAction::ChooseGlobalShortcut),
            is_focused(ui::SettingsAction::ChooseGlobalShortcut), is_pressed(ui::SettingsAction::ChooseGlobalShortcut),
            false, false, shortcut_value));
        elements.push_back(ActionElement(12, Role::ComboBox, {208, 308, 472, 64}, T(chinese, L"Refresh interval", L"刷新间隔"),
            T(chinese, L"Choose how often usage refreshes", L"选择使用情况刷新频率"), T(chinese, L"Choose", L"选择"), {},
            static_cast<int>(ui::SettingsAction::CycleRefresh), is_focused(ui::SettingsAction::CycleRefresh), is_pressed(ui::SettingsAction::CycleRefresh), false, false,
            std::to_wstring(settings.refresh_minutes) + T(chinese, L" minutes", L" 分钟")));
        elements.push_back(ActionElement(13, Role::CheckButton, {208, 376, 472, 64}, T(chinese, L"Start at login", L"登录时启动"),
            T(chinese, L"Launch with the current Windows account", L"随当前 Windows 用户登录启动"), T(chinese, L"Toggle", L"切换"), {},
            static_cast<int>(ui::SettingsAction::ToggleStartAtLogin), is_focused(ui::SettingsAction::ToggleStartAtLogin), is_pressed(ui::SettingsAction::ToggleStartAtLogin), settings.start_at_login));
        elements.push_back(ActionElement(14, Role::CheckButton, {208, 444, 472, 64}, T(chinese, L"Start minimized", L"启动时最小化"),
            T(chinese, L"Keep startup quietly in the notification area", L"启动后安静地留在通知区域"), T(chinese, L"Toggle", L"切换"), {},
            static_cast<int>(ui::SettingsAction::ToggleStartMinimized), is_focused(ui::SettingsAction::ToggleStartMinimized), is_pressed(ui::SettingsAction::ToggleStartMinimized), settings.start_minimized));
        elements.push_back(ActionElement(15, Role::CheckButton, {208, 512, 472, 64}, T(chinese, L"Hide identity", L"隐藏身份信息"),
            T(chinese, L"Hide plan details in the popup, floating bar, and copied usage summaries", L"在弹窗、浮动条和复制使用摘要中隐藏套餐信息"), T(chinese, L"Toggle", L"切换"), {},
            static_cast<int>(ui::SettingsAction::TogglePrivacy), is_focused(ui::SettingsAction::TogglePrivacy), is_pressed(ui::SettingsAction::TogglePrivacy), settings.hide_identity));
    } else if (tab == ui::SettingsTab::Providers) {
        const bool needs_setup = NeedsProviderSetup(snapshot);
        const std::wstring connection = needs_setup ? T(chinese, L"Sign-in required", L"需要登录") :
            snapshot.connection == ProviderConnectionState::CredentialsDetected ?
                (snapshot.error.empty() ? T(chinese, L"Credentials detected and connected", L"已检测凭据并连接") :
                    T(chinese, L"Credentials detected; refresh needs attention", L"已检测凭据；刷新需要处理")) :
                T(chinese, L"Checking local credentials", L"正在检查本地凭据");
        elements.push_back(StaticElement(20, {214, 104, 454, 160}, T(chinese, L"Codex provider", L"Codex 提供商"),
            connection + (snapshot.plan.empty() ? L"" : L", " + snapshot.plan),
            T(chinese, L"Reads CODEX_HOME\\auth.json or %USERPROFILE%\\.codex\\auth.json without copying credentials", L"只读检测 CODEX_HOME\\auth.json 或 %USERPROFILE%\\.codex\\auth.json，不复制凭据")));
        elements.push_back(ActionElement(21, Role::PushButton, {214, 478, 221, 60},
            needs_setup ? T(chinese, L"Open Codex login", L"打开 Codex 登录") : T(chinese, L"Re-authenticate Codex", L"重新登录 Codex"),
            T(chinese, L"Open a terminal with the official codex login command", L"打开终端并运行官方 codex login 命令"),
            T(chinese, L"Open", L"打开"), {}, static_cast<int>(ui::SettingsAction::LaunchCodexLogin),
            is_focused(ui::SettingsAction::LaunchCodexLogin), is_pressed(ui::SettingsAction::LaunchCodexLogin)));
        elements.push_back(ActionElement(22, Role::PushButton, {448, 478, 221, 60}, T(chinese, L"Open Codex configuration folder", L"打开 Codex 配置文件夹"), {},
            T(chinese, L"Open", L"打开"), {}, static_cast<int>(ui::SettingsAction::OpenCodexFolder),
            is_focused(ui::SettingsAction::OpenCodexFolder), is_pressed(ui::SettingsAction::OpenCodexFolder)));
    } else if (tab == ui::SettingsTab::Notifications) {
        elements.push_back(ActionElement(30, Role::CheckButton, {208, 96, 472, 80}, T(chinese, L"Usage alerts", L"额度提醒"),
            T(chinese, L"Notify only when usage crosses a threshold", L"仅在使用率跨过阈值时提醒"), T(chinese, L"Toggle", L"切换"), {},
            static_cast<int>(ui::SettingsAction::ToggleUsageNotifications), is_focused(ui::SettingsAction::ToggleUsageNotifications), is_pressed(ui::SettingsAction::ToggleUsageNotifications), settings.usage_notifications));
        elements.push_back(ActionElement(31, Role::ComboBox, {208, 176, 472, 72}, T(chinese, L"Warning threshold", L"预警阈值"),
            T(chinese, L"Choose when warning alerts begin", L"选择预警提醒开始阈值"), T(chinese, L"Choose", L"选择"), {},
            static_cast<int>(ui::SettingsAction::ChooseUsageWarning), is_focused(ui::SettingsAction::ChooseUsageWarning), is_pressed(ui::SettingsAction::ChooseUsageWarning), false, false,
            std::to_wstring(settings.usage_warning_percent) + L"%"));
        Element snooze_element = ActionElement(33, Role::ComboBox, {208, 248, 472, 72}, T(chinese, L"Pause alerts", L"暂停提醒"),
            T(chinese, L"Pause for 1, 4, or 24 hours, then resume automatically", L"暂停 1、4 或 24 小时，之后自动恢复"),
            T(chinese, L"Choose", L"选择"), {}, static_cast<int>(ui::SettingsAction::ChooseNotificationSnooze),
            is_focused(ui::SettingsAction::ChooseNotificationSnooze),
            is_pressed(ui::SettingsAction::ChooseNotificationSnooze), false, false,
            settings.usage_notifications ? FormatNotificationSnooze(settings.notification_snoozed_until, chinese) :
                T(chinese, L"Alerts are off", L"提醒已关闭"));
        if (!settings.usage_notifications) snooze_element.state |= StateUnavailable;
        elements.push_back(std::move(snooze_element));
        elements.push_back(ActionElement(32, Role::PushButton, {208, 458, 472, 72}, T(chinese, L"Send a test notification", L"发送测试通知"), {},
            T(chinese, L"Press", L"按下"), {}, static_cast<int>(ui::SettingsAction::TestNotification),
            is_focused(ui::SettingsAction::TestNotification), is_pressed(ui::SettingsAction::TestNotification)));
    } else if (tab == ui::SettingsTab::FloatBar) {
        elements.push_back(ActionElement(35, Role::CheckButton, {208, 96, 472, 80},
            T(chinese, L"Show floating bar", L"显示浮动用量条"),
            T(chinese, L"Keep session and weekly capacity visible above other windows", L"在其他窗口上方持续显示当前周期和每周额度"),
            T(chinese, L"Toggle", L"切换"), {}, static_cast<int>(ui::SettingsAction::ToggleFloatBar),
            is_focused(ui::SettingsAction::ToggleFloatBar), is_pressed(ui::SettingsAction::ToggleFloatBar), settings.show_float_bar));
        elements.push_back(ActionElement(36, Role::PushButton, {208, 394, 472, 62},
            T(chinese, L"Reset floating bar position", L"重置浮动用量条位置"),
            T(chinese, L"Move it to the top right of the current monitor", L"将它移到当前显示器右上角"),
            T(chinese, L"Reset", L"重置"), {}, static_cast<int>(ui::SettingsAction::ResetFloatBarPosition),
            is_focused(ui::SettingsAction::ResetFloatBarPosition), is_pressed(ui::SettingsAction::ResetFloatBarPosition)));
    } else if (tab == ui::SettingsTab::UsageSpend) {
        const SpendSummary empty;
        const SpendSummary& spend = snapshot.spend.value_or(empty);
        const std::wstring spend_description = refresh_phase == RefreshPhase::FetchingUsage ?
            T(chinese, L"Refreshing Codex limits and local spend; current values remain visible", L"正在刷新 Codex 额度与本地费用；当前数值继续显示") :
            refresh_phase == RefreshPhase::ScanningSpend ?
                T(chinese, L"Codex limits are ready; local logs are still being scanned and the current estimate remains visible", L"Codex 额度已就绪；本地日志仍在扫描，当前估算继续显示") :
                T(chinese,
                    L"Known API-equivalent value, not a subscription bill. Reported GPT-5.6 cache writes use the official 1.25 times input rate; unknown models remain outside the known floor",
                    L"已知 API 等价价值，不是订阅账单。日志报告的 GPT-5.6 缓存写入按官方输入价格的 1.25 倍计价；未知模型仍不计入已知下限");
        elements.push_back(StaticElement(40, {214, 98, 454, 150}, T(chinese, L"Usage and spend estimate", L"使用与费用估算"),
            SpendValue(spend, chinese), spend_description));
        std::size_t model_uses = 0;
        double model_cost = 0.0;
        for (const auto& day : spend.daily_model_usage) {
            for (const auto& model : day.models) {
                model_uses += model.usage_count;
                model_cost += model.cost_usd;
            }
        }
        std::wstring chart_value = std::to_wstring(model_uses) +
            T(chinese, L" attributed log events across ", L" 个归因日志事件，覆盖 ") +
            std::to_wstring(spend.daily_model_usage.size()) + T(chinese, L" active days, ", L" 个活跃日，") +
            Money(model_cost, spend.partial, chinese);
        elements.push_back(StaticElement(41, {214, 258, 454, 216},
            T(chinese, L"30-day Codex log attribution chart", L"30 天 Codex 日志归因曲线"), std::move(chart_value),
            T(chinese, L"Model names come from local Codex logs and can include automatic routing and background tasks; move the pointer across the chart to inspect each day's attributed events and estimated amount",
                L"模型名称来自本地 Codex 日志，可能包含自动路由和后台任务；在曲线上移动鼠标可查看每天的归因事件和估算金额")));
        std::wstring projects_value;
        for (std::size_t index = 0; index < spend.top_projects.size(); ++index) {
            const auto& project = spend.top_projects[index];
            if (!projects_value.empty()) projects_value += L"; ";
            projects_value += settings.hide_identity ?
                T(chinese, L"Private project ", L"隐私项目 ") + std::to_wstring(index + 1) :
                WideUtf8(project.project);
            projects_value += L", " + Money(project.cost_usd, project.partial, chinese) + L", " +
                Percent(project.share_percent);
        }
        if (projects_value.empty()) projects_value = T(chinese, L"No project activity yet", L"暂无项目活动");
        elements.push_back(StaticElement(42, {214, 482, 454, 174},
            T(chinese, L"Top five projects by estimated value", L"估算金额最高的五个项目"),
            std::move(projects_value),
            T(chinese, L"Project names are reduced to their final folder name and never include full paths",
                L"项目名只保留最后一级文件夹名称，不包含完整路径")));
    } else {
        elements.push_back(StaticElement(50, {214, 104, 454, 188}, T(chinese, L"About Codex Partner", L"关于 Codex Partner"),
            L"Codex Partner " CODEX_PARTNER_VERSION_WIDE, T(chinese, L"Native C++ application for Windows", L"Windows 原生 C++ 应用")));
        std::wstring update_name;
        std::wstring update_value;
        std::wstring update_description;
        std::wstring update_action;
        switch (update.status) {
        case UpdateCheckStatus::Idle:
            update_name = T(chinese, L"Check for updates", L"检查更新");
            update_value = T(chinese, L"Not checked", L"尚未检查");
            update_description = T(chinese,
                L"Read the latest stable tag from the public GitHub Releases API; no download starts",
                L"从公开 GitHub Releases API 读取最新稳定标签；不会开始下载");
            update_action = T(chinese, L"Check", L"检查");
            break;
        case UpdateCheckStatus::Checking:
            update_name = T(chinese, L"Checking for updates", L"正在检查更新");
            update_value = T(chinese, L"Contacting the public GitHub Releases API", L"正在访问公开 GitHub Releases API");
            update_description = T(chinese, L"No download or installation is performed", L"不会下载或安装任何内容");
            update_action = T(chinese, L"Checking", L"检查中");
            break;
        case UpdateCheckStatus::UpToDate:
            update_name = T(chinese, L"Codex Partner is up to date", L"Codex Partner 已是最新版");
            update_value = update.latest_version;
            update_description = T(chinese, L"Check the latest stable release again", L"再次检查最新稳定版本");
            update_action = T(chinese, L"Check again", L"再次检查");
            break;
        case UpdateCheckStatus::Available:
            update_name = update.latest_version + T(chinese, L" is available", L" 可以更新");
            if (ResolveUpdateNavigation(update).kind == UpdateNavigationKind::NativeDownload) {
                update_value = T(chinese, L"One-click Native EXE is ready on GitHub", L"GitHub 已提供一键 Native EXE");
                update_description = T(chinese,
                    L"Opens the locally constructed canonical GitHub download for the exact Native version; Windows or the browser still asks how to save or run it",
                    L"打开本地构造的精确 Native 版本 GitHub 官方下载；Windows 或浏览器仍会询问如何保存或运行");
                update_action = T(chinese, L"Download Native EXE", L"下载 Native EXE");
            } else {
                update_value = T(chinese, L"Review the release in your browser before downloading", L"下载前先在浏览器中查看版本详情");
                update_description = T(chinese, L"Opens the canonical Codex Partner GitHub release page", L"打开 Codex Partner 的官方 GitHub 版本页面");
                update_action = T(chinese, L"View release", L"查看版本");
            }
            break;
        case UpdateCheckStatus::Failed:
            update_name = T(chinese, L"Could not check for updates", L"无法检查更新");
            update_value = T(chinese, L"No data was changed", L"没有更改任何数据");
            update_description = T(chinese, L"Try again when the network is available", L"请在网络可用时重试");
            update_action = T(chinese, L"Try again", L"重试");
            break;
        }
        Element update_element = ActionElement(53, Role::PushButton, {214, 310, 454, 146},
            std::move(update_name), std::move(update_description), std::move(update_action), {},
            static_cast<int>(ui::SettingsAction::CheckForUpdates),
            is_focused(ui::SettingsAction::CheckForUpdates), is_pressed(ui::SettingsAction::CheckForUpdates),
            false, false, std::move(update_value));
        if (update.status == UpdateCheckStatus::Checking) update_element.state |= StateUnavailable;
        elements.push_back(std::move(update_element));
        elements.push_back(ActionElement(54, Role::PushButton, {214, 448, 454, 58},
            T(chinese, L"Report a problem", L"报告问题"),
            T(chinese, L"Copy a credential-free diagnostic summary, then open the GitHub bug report form",
                L"复制不含凭据的诊断摘要，然后打开 GitHub 错误报告表单"),
            T(chinese, L"Report", L"报告"), {}, static_cast<int>(ui::SettingsAction::ReportIssue),
            is_focused(ui::SettingsAction::ReportIssue), is_pressed(ui::SettingsAction::ReportIssue)));
        elements.push_back(ActionElement(51, Role::Link, {214, 506, 221, 52}, T(chinese, L"View Codex Partner on GitHub", L"在 GitHub 查看 Codex Partner"), {},
            T(chinese, L"Open", L"打开"), {}, static_cast<int>(ui::SettingsAction::OpenProjectSite),
            is_focused(ui::SettingsAction::OpenProjectSite), is_pressed(ui::SettingsAction::OpenProjectSite)));
        elements.push_back(ActionElement(52, Role::PushButton, {448, 506, 221, 52}, T(chinese, L"Copy diagnostic summary", L"复制诊断摘要"),
            T(chinese, L"Copy a credential-free issue report summary", L"复制不含凭据的问题诊断摘要"), T(chinese, L"Press", L"按下"), {},
            static_cast<int>(ui::SettingsAction::CopyDiagnostics), is_focused(ui::SettingsAction::CopyDiagnostics), is_pressed(ui::SettingsAction::CopyDiagnostics)));
    }
    std::wstring persistence_value;
    std::wstring persistence_description;
    if (external_feedback.outcome != ExternalActionOutcome::Idle) {
        persistence_value = ExternalActionStatus(external_feedback, chinese);
        persistence_description = ExternalActionDescription(external_feedback, chinese);
    } else if (global_shortcut_status == GlobalShortcutStatus::CandidateUnavailable) {
        persistence_value = T(chinese, L"Shortcut unavailable; current shortcut kept", L"快捷键不可用；已保留当前快捷键");
        persistence_description = T(chinese,
            L"The attempted combination is registered by another application; choose a different shortcut",
            L"尝试的组合已被其他应用注册；请选择其他快捷键");
    } else if (global_shortcut_status == GlobalShortcutStatus::Unavailable) {
        persistence_value = T(chinese, L"Saved shortcut is unavailable", L"已保存的快捷键不可用");
        persistence_description = T(chinese,
            L"Another application may be using it; choose a different shortcut or turn quick peek off",
            L"可能已被其他应用占用；请选择其他快捷键或关闭快速查看");
    } else if (persistence == SettingsPersistenceState::Saved) {
        persistence_value = T(chinese, L"Settings saved", L"设置已保存");
        persistence_description = T(chinese, L"The latest preferences were committed to disk", L"最新偏好设置已写入磁盘");
    } else if (persistence == SettingsPersistenceState::Failed) {
        persistence_value = T(chinese, L"Could not save settings; previous values restored", L"无法保存设置，已恢复上次保存的值");
        persistence_description = T(chinese,
            L"Check Windows account permissions and Codex Partner application-data access, then try again",
            L"请检查 Windows 账户权限及 Codex Partner 应用数据访问权限后重试");
    } else {
        persistence_value = T(chinese, L"No pending settings changes", L"没有待保存的设置更改");
        persistence_description = T(chinese, L"Preferences save immediately after a change", L"偏好设置会在更改后立即保存");
    }
    elements.push_back(StaticElement(60, {408, 20, 260, 36}, T(chinese, L"Settings status", L"设置状态"),
        std::move(persistence_value), std::move(persistence_description)));
    return elements;
}

}  // namespace codex_partner::accessibility
