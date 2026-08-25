#include "usage_summary.h"

#include "usage_freshness.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace codex_partner {
namespace {

const wchar_t* T(bool chinese, const wchar_t* english, const wchar_t* simplified_chinese) noexcept {
    return chinese ? simplified_chinese : english;
}

std::wstring Remaining(const std::optional<RateWindow>& window, bool chinese,
    std::chrono::system_clock::time_point now) {
    if (!window || !std::isfinite(window->used_percent)) return T(chinese, L"Unavailable", L"不可用");
    const int remaining = static_cast<int>(std::lround(
        std::clamp(100.0 - window->used_percent, 0.0, 100.0)));
    std::wstring result = std::to_wstring(remaining) + T(chinese, L"% remaining", L"% 剩余");
    if (!window->resets_at) return result + T(chinese, L" · reset unavailable", L" · 重置时间不可用");

    const auto until_reset = std::chrono::duration_cast<std::chrono::minutes>(*window->resets_at - now);
    if (until_reset <= std::chrono::minutes::zero()) {
        return result + T(chinese, L" · resetting soon", L" · 即将重置");
    }
    const auto total_minutes = until_reset.count();
    const auto days = total_minutes / (24 * 60);
    const auto hours = (total_minutes % (24 * 60)) / 60;
    const auto minutes = total_minutes % 60;
    std::wstring duration;
    if (days > 0) {
        duration = std::to_wstring(days) + T(chinese, L"d", L" 天");
        if (hours > 0) duration += L" " + std::to_wstring(hours) + T(chinese, L"h", L" 小时");
    } else if (hours > 0) {
        duration = std::to_wstring(hours) + T(chinese, L"h", L" 小时");
        if (minutes > 0) duration += L" " + std::to_wstring(minutes) + T(chinese, L"m", L" 分钟");
    } else {
        duration = std::to_wstring(std::max<std::int64_t>(1, minutes)) + T(chinese, L"m", L" 分钟");
    }
    return result + T(chinese, L" · resets in ", L" · ") + duration +
        T(chinese, L"", L"后重置");
}

std::wstring Money(const std::optional<double>& value, bool partial) {
    if (!value || !std::isfinite(*value)) return L"—";
    std::wostringstream output;
    if (partial) output << L"≥ ";
    output << L'$' << std::fixed << std::setprecision(*value < 0.01 ? 3 : 2)
           << std::max(0.0, *value);
    return output.str();
}

std::wstring SnapshotStatus(const UsageSnapshot& snapshot, bool chinese) {
    if (snapshot.loading) return T(chinese, L"Refreshing", L"正在刷新");
    if (NeedsProviderSetup(snapshot)) return T(chinese, L"Codex sign-in required", L"需要登录 Codex");
    if (snapshot.stale) return T(chinese, L"Last known data", L"上次可用数据");
    if (!snapshot.error.empty()) return T(chinese, L"Usage unavailable", L"使用情况不可用");
    return T(chinese, L"Live", L"实时数据");
}

std::wstring SafeLabel(std::wstring_view value) {
    std::wstring result;
    result.reserve(std::min<std::size_t>(value.size(), 64));
    for (const wchar_t character : value) {
        if (result.size() >= 64) break;
        if (character < 0x20 || character == 0x7f) {
            if (!result.empty() && result.back() != L' ') result.push_back(L' ');
        } else {
            result.push_back(character);
        }
    }
    while (!result.empty() && result.back() == L' ') result.pop_back();
    return result;
}

std::wstring WideAscii(std::string_view value) {
    return std::wstring(value.begin(), value.end());
}

}  // namespace

std::wstring FormatSpendPaceInsight(const SpendSummary& spend, bool chinese, bool compact) {
    const auto insight = DeriveSpendPaceInsight(spend);
    if (!insight) return {};

    const std::wstring qualifier = insight->partial ?
        T(chinese, L"Known-priced pace", L"已知计价速度") :
        T(chinese, L"24h pace", L"近 24 小时速度");
    if (insight->level == SpendPaceLevel::NewActivity) {
        if (compact) {
            return qualifier + T(chinese,
                L" · activity concentrated in the last 24h",
                L" · 活动集中在近 24 小时");
        }
        return qualifier + T(chinese,
            L": activity is concentrated in the last 24 hours; the prior six-day daily average was below $0.01.",
            L"：活动集中在近 24 小时；此前 6 天的日均已知价值低于 $0.01。");
    }

    std::wostringstream multiple;
    if (*insight->multiple >= 99.95) multiple << L"99+";
    else multiple << std::fixed << std::setprecision(1) << *insight->multiple;
    const std::wstring baseline = Money(std::optional<double>{insight->prior_six_day_daily_average_usd}, false);
    if (compact) {
        return qualifier + L" · " + multiple.str() + T(chinese,
            L"× prior 6-day avg · ", L"× 此前 6 天日均 · ") + baseline +
            T(chinese, L"/day", L"/天");
    }
    return qualifier + T(chinese, L": the last 24 hours are ", L"：近 24 小时是此前 6 天日均的 ") +
        multiple.str() + T(chinese, L"× the prior six-day daily average (", L"×（日均 ") + baseline +
        T(chinese, L"/day).", L"/天）。");
}

std::wstring BuildUsageShareSummary(const UsageSnapshot& snapshot, bool chinese, bool hide_identity,
    std::chrono::system_clock::time_point now) {
    std::wostringstream output;
    output << T(chinese, L"Codex Partner usage snapshot", L"Codex Partner 使用摘要") << L"\r\n";
    const std::wstring safe_plan = SafeLabel(snapshot.plan);
    if (hide_identity) {
        output << T(chinese, L"Identity: Hidden by privacy setting", L"身份信息：已按隐私设置隐藏") << L"\r\n";
    } else if (!safe_plan.empty()) {
        output << T(chinese, L"Plan: ", L"套餐：") << safe_plan << L"\r\n";
    }
    output << T(chinese, L"Status: ", L"状态：") << SnapshotStatus(snapshot, chinese) << L"\r\n";
    output << T(chinese, L"Freshness: ", L"数据新鲜度：")
           << FormatUsageFreshness(snapshot.updated_at, chinese, now) << L"\r\n";
    output << T(chinese, L"Current session: ", L"当前周期：")
           << Remaining(snapshot.session, chinese, now) << L"\r\n";
    output << T(chinese, L"Weekly capacity: ", L"每周额度：")
           << Remaining(snapshot.weekly, chinese, now) << L"\r\n";
    if (snapshot.credits && std::isfinite(*snapshot.credits)) {
        output << T(chinese, L"Credits: ", L"点数：") << std::fixed << std::setprecision(2)
               << std::max(0.0, *snapshot.credits) << L"\r\n";
    }

    output << L"\r\n" << T(chinese,
        L"Local API-equivalent estimate (not a ChatGPT subscription invoice)",
        L"本地 API 等价费用估算（不是 ChatGPT 订阅账单）") << L"\r\n";
    const SpendSummary spend = snapshot.spend.value_or(SpendSummary{});
    output << T(chinese, L"1 day: ", L"近 1 天：") << Money(spend.one_day_usd, spend.one_day_partial)
           << T(chinese, L" · 7 days: ", L" · 近 7 天：") << Money(spend.seven_day_usd, spend.seven_day_partial)
           << T(chinese, L" · 30 days: ", L" · 近 30 天：") << Money(spend.thirty_day_usd, spend.thirty_day_partial)
           << L"\r\n";
    if (const std::wstring pace = FormatSpendPaceInsight(spend, chinese); !pace.empty()) {
        output << pace << L"\r\n";
    }
    if (spend.unpriced_events > 0 || spend.partial) {
        output << T(chinese, L"Coverage: ", L"计价覆盖：");
        if (const auto coverage = SpendPricingCoveragePercent(spend)) {
            output << *coverage << T(chinese, L"% of detected events priced", L"% 的检测事件已有公开价格");
        } else {
            output << T(chinese, L"unavailable", L"不可用");
        }
        if (const auto token_coverage = SpendTokenCoveragePercent(spend)) {
            output << T(chinese, L"; token coverage: ", L"；token 覆盖：")
                   << *token_coverage << L'%';
        }
        output << T(chinese, L"; known-priced lower bound; unpriced events: ",
            L"；金额为已知计价下限；未计价事件：") << spend.unpriced_events;
        const std::string models = SummarizeUnpricedModels(spend);
        if (!models.empty()) {
            output << T(chinese, L"; missing public price: ", L"；缺少公开价格：")
                   << WideAscii(models);
        }
        output << L"\r\n";
    } else {
        output << T(chinese, L"Coverage: 100% of detected events with a verified public price",
            L"计价覆盖：100% 的检测事件都有可验证公开价格") << L"\r\n";
    }
    if (spend.priced_cache_write_input_tokens > 0) {
        output << T(chinese, L"Cache writes priced: ", L"已计价缓存写入：")
               << spend.priced_cache_write_input_tokens
               << T(chinese, L" tokens (GPT-5.6 uses 1.25x input rate where applicable)",
                    L" token（适用的 GPT-5.6 请求按输入单价 1.25 倍计算）")
               << L"\r\n";
    }
    output << T(chinese, L"Credentials and account identifiers are not included.", L"摘要不包含凭据或账户标识信息。");
    return output.str();
}

}  // namespace codex_partner
