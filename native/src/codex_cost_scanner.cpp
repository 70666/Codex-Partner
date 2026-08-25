#include "codex_cost_scanner.h"

#include "json.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace codex_partner {
namespace {

constexpr std::uint64_t kLongContextThreshold = 272'000;
constexpr std::size_t kMaxJsonLineBytes = 2 * 1024 * 1024;
constexpr std::size_t kMaxCachedSpendFiles = 8'192;
constexpr std::size_t kMaxCachedSpendEvents = 250'000;
// Official GPT-5.6 model documentation specifies a 1.25x uncached-input rate
// for cache writes. Keep this distinct from discounted cache reads.
constexpr double kGpt56CacheWriteMultiplier = 1.25;

struct Rates {
    std::string_view model;
    double input;
    double cached;
    double output;
    double long_input = 0.0;
    double long_cached = 0.0;
    double long_output = 0.0;
};

constexpr std::array kRates{
    Rates{"gpt-5", 1.25e-6, 1.25e-7, 1.0e-5},
    Rates{"gpt-5-mini", 2.5e-7, 2.5e-8, 2.0e-6},
    Rates{"gpt-5-nano", 5.0e-8, 5.0e-9, 4.0e-7},
    Rates{"gpt-5-pro", 1.5e-5, 1.5e-5, 1.2e-4},
    Rates{"gpt-5.1", 1.25e-6, 1.25e-7, 1.0e-5},
    Rates{"gpt-5.1-codex-max", 1.25e-6, 1.25e-7, 1.0e-5},
    Rates{"gpt-5.1-codex-mini", 2.5e-7, 2.5e-8, 2.0e-6},
    Rates{"gpt-5.2", 1.75e-6, 1.75e-7, 1.4e-5},
    Rates{"gpt-5.2-pro", 2.1e-5, 2.1e-5, 1.68e-4},
    Rates{"gpt-5.3", 1.75e-6, 1.75e-7, 1.4e-5},
    Rates{"gpt-5.4", 2.5e-6, 2.5e-7, 1.5e-5, 5.0e-6, 5.0e-7, 2.25e-5},
    Rates{"gpt-5.4-mini", 7.5e-7, 7.5e-8, 4.5e-6},
    Rates{"gpt-5.4-nano", 2.0e-7, 2.0e-8, 1.25e-6},
    Rates{"gpt-5.4-pro", 3.0e-5, 3.0e-5, 1.8e-4, 6.0e-5, 6.0e-5, 2.7e-4},
    Rates{"gpt-5.5", 5.0e-6, 5.0e-7, 3.0e-5, 1.0e-5, 1.0e-6, 4.5e-5},
    Rates{"gpt-5.5-pro", 3.0e-5, 3.0e-5, 1.8e-4},
    Rates{"gpt-5.6-sol", 4.0e-6, 4.0e-7, 2.0e-5, 8.0e-6, 8.0e-7, 3.0e-5},
    Rates{"gpt-5.6-terra", 2.0e-6, 2.0e-7, 1.2e-5, 4.0e-6, 4.0e-7, 1.8e-5},
    Rates{"gpt-5.6-luna", 2.0e-7, 2.0e-8, 1.2e-6, 4.0e-7, 4.0e-8, 1.8e-6},
};

struct TokenTotals {
    std::uint64_t input = 0;
    std::uint64_t cached = 0;
    std::uint64_t cache_write = 0;
    std::uint64_t output = 0;
};

struct ParserState {
    std::string current_model;
    std::string current_project = "Unknown project";
    TokenTotals previous;
    TokenTotals watermark;
    bool interleaved = false;
};

struct DerivedSpendEvent {
    std::chrono::system_clock::time_point timestamp;
    std::optional<double> cost;
    TokenTotals tokens;
    std::string model;
    std::string project;
    std::string unpriced_model;
};

struct ParsedSpendFile {
    std::vector<DerivedSpendEvent> events;
    ParserState parser_state;
};

struct FileFingerprint {
    std::uintmax_t size = 0;
    std::filesystem::file_time_type write_time{};
};

struct FileCandidate {
    std::filesystem::path path;
    std::optional<FileFingerprint> fingerprint;
};

struct TailCheckpoint {
    std::uint64_t hash_a = 0;
    std::uint64_t hash_b = 0;
    std::size_t length = 0;
    bool line_boundary = false;
};

struct CachedSpendFile {
    FileFingerprint fingerprint;
    std::shared_ptr<const ParsedSpendFile> parsed;
    std::optional<TailCheckpoint> tail_checkpoint;
    std::uint64_t last_used = 0;
};

std::mutex spend_cache_mutex;
// Process-local only: retain derived timestamps/token counts/prices, never raw
// JSON lines, prompts, responses, credentials, or account identifiers.
std::unordered_map<std::wstring, CachedSpendFile> spend_file_cache;
std::uint64_t spend_cache_clock = 0;

std::wstring CacheKey(const std::filesystem::path& path) {
    std::wstring key = path.lexically_normal().wstring();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return key;
}

std::optional<FileFingerprint> ReadFingerprint(const std::filesystem::path& path) {
    std::error_code size_error;
    const std::uintmax_t size = std::filesystem::file_size(path, size_error);
    std::error_code time_error;
    const auto write_time = std::filesystem::last_write_time(path, time_error);
    if (size_error || time_error) return std::nullopt;
    return FileFingerprint{size, write_time};
}

bool SameFingerprint(const FileFingerprint& left, const FileFingerprint& right) {
    return left.size == right.size && left.write_time == right.write_time;
}

std::optional<TailCheckpoint> ReadTailCheckpoint(
    const std::filesystem::path& path, std::uintmax_t end_offset) {
    if (end_offset == 0) return TailCheckpoint{};
    constexpr std::uintmax_t kCheckpointBytes = 256;
    const std::uintmax_t length_value = std::min(end_offset, kCheckpointBytes);
    if (end_offset > static_cast<std::uintmax_t>(std::numeric_limits<std::streamoff>::max())) return std::nullopt;
    const auto length = static_cast<std::size_t>(length_value);
    const auto start = static_cast<std::streamoff>(end_offset - length_value);
    std::ifstream stream(path, std::ios::binary);
    if (!stream || !stream.seekg(start)) return std::nullopt;
    std::string checkpoint(length, '\0');
    stream.read(checkpoint.data(), static_cast<std::streamsize>(length));
    if (stream.gcount() != static_cast<std::streamsize>(length)) return std::nullopt;
    std::uint64_t hash_a = 14'695'981'039'346'656'037ULL;
    std::uint64_t hash_b = 0x9E3779B97F4A7C15ULL;
    for (const unsigned char byte : checkpoint) {
        hash_a = (hash_a ^ byte) * 1'099'511'628'211ULL;
        hash_b ^= static_cast<std::uint64_t>(byte) + 0x9E3779B97F4A7C15ULL +
            (hash_b << 6U) + (hash_b >> 2U);
    }
    return TailCheckpoint{hash_a, hash_b, length, !checkpoint.empty() && checkpoint.back() == '\n'};
}

bool SameCheckpoint(const TailCheckpoint& left, const TailCheckpoint& right) {
    return left.hash_a == right.hash_a && left.hash_b == right.hash_b &&
        left.length == right.length && left.line_boundary == right.line_boundary;
}

std::shared_ptr<const ParsedSpendFile> FindCachedFile(
    const std::filesystem::path& path, const FileFingerprint& fingerprint) {
    std::scoped_lock lock(spend_cache_mutex);
    const auto found = spend_file_cache.find(CacheKey(path));
    if (found == spend_file_cache.end() || !SameFingerprint(found->second.fingerprint, fingerprint)) return {};
    found->second.last_used = ++spend_cache_clock;
    return found->second.parsed;
}

std::optional<CachedSpendFile> FindResumeBase(
    const std::filesystem::path& path, const FileFingerprint& fingerprint) {
    std::scoped_lock lock(spend_cache_mutex);
    const auto found = spend_file_cache.find(CacheKey(path));
    if (found == spend_file_cache.end() || found->second.fingerprint.size == 0 ||
        fingerprint.size <= found->second.fingerprint.size || !found->second.tail_checkpoint ||
        !found->second.tail_checkpoint->line_boundary) return std::nullopt;
    return found->second;
}

bool ResumeBoundaryMatches(const std::filesystem::path& path, const CachedSpendFile& base) {
    const auto checkpoint = ReadTailCheckpoint(path, base.fingerprint.size);
    return checkpoint && base.tail_checkpoint && SameCheckpoint(*checkpoint, *base.tail_checkpoint);
}

void StoreCachedFile(const std::filesystem::path& path, const FileFingerprint& fingerprint,
    std::shared_ptr<const ParsedSpendFile> parsed) {
    const auto checkpoint = ReadTailCheckpoint(path, fingerprint.size);
    std::scoped_lock lock(spend_cache_mutex);
    spend_file_cache[CacheKey(path)] = CachedSpendFile{
        fingerprint, std::move(parsed), checkpoint, ++spend_cache_clock};
}

void TrimSpendCache() {
    std::scoped_lock lock(spend_cache_mutex);
    std::size_t cached_events = 0;
    for (const auto& [key, value] : spend_file_cache) {
        (void)key;
        cached_events += value.parsed->events.size();
    }
    if (spend_file_cache.size() <= kMaxCachedSpendFiles && cached_events <= kMaxCachedSpendEvents) return;
    std::vector<std::pair<std::uint64_t, std::wstring>> oldest;
    oldest.reserve(spend_file_cache.size());
    for (const auto& [key, value] : spend_file_cache) oldest.emplace_back(value.last_used, key);
    std::sort(oldest.begin(), oldest.end());
    for (const auto& [last_used, key] : oldest) {
        (void)last_used;
        if (spend_file_cache.size() <= kMaxCachedSpendFiles && cached_events <= kMaxCachedSpendEvents) break;
        const auto found = spend_file_cache.find(key);
        if (found == spend_file_cache.end()) continue;
        cached_events -= found->second.parsed->events.size();
        spend_file_cache.erase(found);
    }
}

std::string Lower(std::string_view input) {
    std::string value(input);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool HasDateSuffix(std::string_view value) {
    if (value.size() < 11 || value[value.size() - 11] != '-') return false;
    const std::string_view suffix = value.substr(value.size() - 10);
    for (std::size_t index = 0; index < suffix.size(); ++index) {
        if (index == 4 || index == 7) {
            if (suffix[index] != '-') return false;
        } else if (!std::isdigit(static_cast<unsigned char>(suffix[index]))) {
            return false;
        }
    }
    return true;
}

std::string NormalizeModel(std::string_view raw) {
    std::string model = Lower(raw);
    while (!model.empty() && std::isspace(static_cast<unsigned char>(model.front()))) model.erase(model.begin());
    while (!model.empty() && std::isspace(static_cast<unsigned char>(model.back()))) model.pop_back();
    if (model.starts_with("openai/")) model.erase(0, 7);
    else if (model.find('/') != std::string::npos) return {};
    if (HasDateSuffix(model)) model.resize(model.size() - 11);
    if (model == "gpt-5.6" || model.starts_with("gpt-5.6-codex")) return "gpt-5.6-sol";
    if (model == "gpt-5-codex") return "gpt-5";
    if (model == "gpt-5.1-codex") return "gpt-5.1";
    if (model == "gpt-5.2-codex") return "gpt-5.2";
    if (model == "gpt-5.3-codex") return "gpt-5.3";
    if (model == "gpt-5.4-codex") return "gpt-5.4";
    if (model == "gpt-5.4-mini-codex") return "gpt-5.4-mini";
    if (model == "gpt-5.4-nano-codex") return "gpt-5.4-nano";
    return model;
}

bool CountsTowardCodexSubscription(std::string_view raw) {
    std::string model = Lower(raw);
    while (!model.empty() && std::isspace(static_cast<unsigned char>(model.front()))) model.erase(model.begin());
    const auto slash = model.find('/');
    if (slash != std::string::npos) return model.substr(0, slash) == "openai";
    constexpr std::array third_party_prefixes{
        std::string_view{"anthropic-"}, std::string_view{"claude-"}, std::string_view{"deepseek-"},
        std::string_view{"gemini-"}, std::string_view{"google-"}, std::string_view{"grok-"},
        std::string_view{"mistral-"}, std::string_view{"qwen-"},
    };
    return std::none_of(third_party_prefixes.begin(), third_party_prefixes.end(),
        [&](std::string_view prefix) { return model.starts_with(prefix); });
}

std::optional<std::chrono::system_clock::time_point> ParseTimestamp(std::string_view timestamp) {
    if (timestamp.size() < 19 || timestamp[4] != '-' || timestamp[7] != '-' ||
        (timestamp[10] != 'T' && timestamp[10] != ' ') || timestamp[13] != ':' || timestamp[16] != ':') return std::nullopt;
    const auto digit = [&](std::size_t index) -> int {
        const unsigned char ch = static_cast<unsigned char>(timestamp[index]);
        return std::isdigit(ch) ? static_cast<int>(ch - '0') : -100;
    };
    const int year = digit(0) * 1000 + digit(1) * 100 + digit(2) * 10 + digit(3);
    const int month = digit(5) * 10 + digit(6);
    const int day = digit(8) * 10 + digit(9);
    const int hour = digit(11) * 10 + digit(12);
    const int minute = digit(14) * 10 + digit(15);
    const int second = digit(17) * 10 + digit(18);
    const std::chrono::year_month_day value{std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)}, std::chrono::day{static_cast<unsigned>(day)}};
    if (!value.ok() || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) return std::nullopt;
    auto result = std::chrono::sys_days{value} + std::chrono::hours{hour} + std::chrono::minutes{minute} + std::chrono::seconds{second};
    std::size_t position = 19;
    if (position < timestamp.size() && timestamp[position] == '.') {
        ++position;
        const std::size_t fraction_start = position;
        while (position < timestamp.size() && std::isdigit(static_cast<unsigned char>(timestamp[position]))) ++position;
        if (position == fraction_start) return std::nullopt;
    }
    if (position == timestamp.size()) return result;
    if (timestamp[position] == 'Z' || timestamp[position] == 'z') {
        return position + 1 == timestamp.size() ? std::optional{result} : std::nullopt;
    }
    if ((timestamp[position] != '+' && timestamp[position] != '-') || position + 6 != timestamp.size() || timestamp[position + 3] != ':') return std::nullopt;
    const int offset_hour = digit(position + 1) * 10 + digit(position + 2);
    const int offset_minute = digit(position + 4) * 10 + digit(position + 5);
    if (offset_hour < 0 || offset_hour > 23 || offset_minute < 0 || offset_minute > 59) return std::nullopt;
    const int offset_sign = timestamp[position] == '+' ? 1 : -1;
    result -= offset_sign * (std::chrono::hours{offset_hour} + std::chrono::minutes{offset_minute});
    return result;
}

std::uint64_t JsonCount(const JsonValue* value) {
    const double number = value ? value->as_number().value_or(0.0) : 0.0;
    if (number <= 0.0) return 0;
    return static_cast<std::uint64_t>(std::min(number, static_cast<double>(std::numeric_limits<std::uint64_t>::max())));
}

TokenTotals ReadTotals(const JsonValue* value) {
    if (!value) return {};
    TokenTotals result;
    result.input = JsonCount(value->find("input_tokens"));
    result.cached = JsonCount(value->find("cached_input_tokens"));
    if (result.cached == 0) result.cached = JsonCount(value->find("cache_read_input_tokens"));
    result.cache_write = JsonCount(value->find("cache_write_input_tokens"));
    result.output = JsonCount(value->find("output_tokens"));
    result.cached = std::min(result.cached, result.input);
    result.cache_write = std::min(result.cache_write, result.input - result.cached);
    return result;
}

const JsonValue* TokenPayload(const JsonValue& object) {
    const JsonValue* payload = object.find("payload");
    if (!payload) payload = object.find("event_msg");
    if (!payload) return nullptr;
    const JsonValue* type = payload->find("type");
    return type && type->as_string().value_or("") == "token_count" ? payload : nullptr;
}

std::string StringField(const JsonValue* object, std::string_view key) {
    if (!object) return {};
    const JsonValue* value = object->find(key);
    return value ? std::string(value->as_string().value_or("")) : std::string{};
}

std::string ModelFrom(const JsonValue* object) {
    std::string model = StringField(object, "model");
    if (model.empty()) model = StringField(object, "model_name");
    if (model.empty() && object) {
        if (const JsonValue* info = object->find("info")) {
            model = StringField(info, "model");
            if (model.empty()) model = StringField(info, "model_name");
        }
    }
    return model;
}

std::string ProjectLeaf(std::string_view raw) {
    while (!raw.empty() && (raw.back() == '/' || raw.back() == '\\')) raw.remove_suffix(1);
    if (raw.empty()) return "Unknown project";
    const std::size_t separator = raw.find_last_of("/\\");
    std::string leaf(raw.substr(separator == std::string_view::npos ? 0 : separator + 1));
    if (leaf.empty() || leaf == "." || leaf == "..") return "Unknown project";
    constexpr std::size_t kMaximumProjectBytes = 80;
    if (leaf.size() > kMaximumProjectBytes) leaf.resize(kMaximumProjectBytes);
    return leaf;
}

std::string ProjectFrom(const JsonValue* object) {
    if (!object) return {};
    std::string project = StringField(object, "cwd");
    if (project.empty()) project = StringField(object, "working_directory");
    return project.empty() ? std::string{} : ProjectLeaf(project);
}

class FileAccumulator {
public:
    explicit FileAccumulator(ParsedSpendFile& parsed) : parsed_(parsed) {}

    void Process(std::string_view line) {
        if (line.size() > kMaxJsonLineBytes || (line.find("token_count") == std::string_view::npos &&
            line.find("turn_context") == std::string_view::npos && line.find("session_meta") == std::string_view::npos)) return;
        const auto parsed = ParseJson(line);
        if (!parsed.ok()) return;
        const std::string type = StringField(&parsed.value, "type");
        if (type == "turn_context" || type == "session_meta") {
            const JsonValue* context = parsed.value.find("payload");
            std::string project = ProjectFrom(context);
            if (project.empty()) project = ProjectFrom(&parsed.value);
            if (!project.empty()) parsed_.parser_state.current_project = std::move(project);
            if (type == "session_meta") return;
            std::string model = ModelFrom(context);
            if (model.empty()) model = ModelFrom(&parsed.value);
            parsed_.parser_state.current_model = std::move(model);
            return;
        }
        const JsonValue* payload = TokenPayload(parsed.value);
        if (!payload) return;
        const auto timestamp = ParseTimestamp(StringField(&parsed.value, "timestamp"));
        if (!timestamp) return;

        const JsonValue* info = payload->find("info");
        TokenTotals delta;
        if (const JsonValue* last = info ? info->find("last_token_usage") : nullptr) {
            delta = ReadTotals(last);
        } else if (const JsonValue* total = info ? info->find("total_token_usage") : nullptr) {
            delta = DeltaFromCumulative(ReadTotals(total));
        } else {
            delta = ReadTotals(payload);
        }
        if (delta.input == 0 && delta.cached == 0 && delta.output == 0) return;

        std::string event_model = ModelFrom(info);
        if (event_model.empty()) event_model = ModelFrom(payload);
        const std::string& raw_model = event_model.empty() ? parsed_.parser_state.current_model : event_model;
        if (!CountsTowardCodexSubscription(raw_model)) return;
        const auto usage_day = std::chrono::floor<std::chrono::days>(*timestamp);
        const auto cost = EstimateCodexCostUsd(
            raw_model, delta.input, delta.cached, delta.cache_write, delta.output, usage_day);
        std::string normalized_model = NormalizeModel(raw_model);
        if (normalized_model.empty()) normalized_model = "unattributed";
        std::string unpriced_model;
        if (!cost) {
            unpriced_model = normalized_model;
        }
        parsed_.events.push_back(DerivedSpendEvent{*timestamp, cost, delta, std::move(normalized_model),
            parsed_.parser_state.current_project, std::move(unpriced_model)});
    }

private:
    TokenTotals DeltaFromCumulative(const TokenTotals& total) {
        ParserState& state = parsed_.parser_state;
        if (total.input < state.watermark.input || total.cached < state.watermark.cached ||
            total.cache_write < state.watermark.cache_write || total.output < state.watermark.output) state.interleaved = true;
        const auto component = [&](std::uint64_t current, std::uint64_t previous, std::uint64_t watermark) {
            if (state.interleaved) return current >= watermark ? current - std::max(previous, watermark) : 0ULL;
            return current >= previous ? current - previous : 0ULL;
        };
        TokenTotals delta{
            component(total.input, state.previous.input, state.watermark.input),
            component(total.cached, state.previous.cached, state.watermark.cached),
            component(total.cache_write, state.previous.cache_write, state.watermark.cache_write),
            component(total.output, state.previous.output, state.watermark.output),
        };
        state.previous = total;
        state.watermark.input = std::max(state.watermark.input, total.input);
        state.watermark.cached = std::max(state.watermark.cached, total.cached);
        state.watermark.cache_write = std::max(state.watermark.cache_write, total.cache_write);
        state.watermark.output = std::max(state.watermark.output, total.output);
        delta.cached = std::min(delta.cached, delta.input);
        delta.cache_write = std::min(delta.cache_write, delta.input - delta.cached);
        return delta;
    }

    ParsedSpendFile& parsed_;
};

SpendSummary SummarizeFile(const ParsedSpendFile& parsed, std::chrono::system_clock::time_point now,
    std::stop_token stop) {
    SpendSummary summary;
    summary.files_scanned = 1;
    bool saw_priced = false;
    double one = 0.0;
    double seven = 0.0;
    double thirty = 0.0;
    for (const DerivedSpendEvent& event : parsed.events) {
        if (stop.stop_requested()) {
            summary.partial = true;
            summary.one_day_partial = true;
            summary.seven_day_partial = true;
            summary.thirty_day_partial = true;
            break;
        }
        const auto age = now - event.timestamp;
        if (age < std::chrono::system_clock::duration::zero() || age >= std::chrono::hours{24 * 30}) continue;
        const auto day = std::chrono::floor<std::chrono::days>(event.timestamp);
        auto daily = std::find_if(summary.daily_model_usage.begin(), summary.daily_model_usage.end(),
            [day](const DailyModelUsage& value) { return value.day == day; });
        if (daily == summary.daily_model_usage.end()) {
            summary.daily_model_usage.push_back(DailyModelUsage{day, {}});
            daily = std::prev(summary.daily_model_usage.end());
        }
        auto model = std::find_if(daily->models.begin(), daily->models.end(),
            [&](const ModelUsageAmount& value) { return value.model == event.model; });
        if (model == daily->models.end()) {
            daily->models.push_back(ModelUsageAmount{event.model});
            model = std::prev(daily->models.end());
        }
        ++model->usage_count;
        model->partial = model->partial || !event.cost;
        if (event.cost) model->cost_usd += *event.cost;

        auto project = std::find_if(summary.top_projects.begin(), summary.top_projects.end(),
            [&](const ProjectUsageAmount& value) { return value.project == event.project; });
        if (project == summary.top_projects.end()) {
            summary.top_projects.push_back(ProjectUsageAmount{event.project});
            project = std::prev(summary.top_projects.end());
        }
        ++project->usage_count;
        project->partial = project->partial || !event.cost;
        if (event.cost) project->cost_usd += *event.cost;

        if (!event.cost) {
            summary.partial = true;
            summary.thirty_day_partial = true;
            if (age < std::chrono::hours{24 * 7}) summary.seven_day_partial = true;
            if (age < std::chrono::hours{24}) summary.one_day_partial = true;
            ++summary.unpriced_events;
            summary.unpriced_input_tokens += event.tokens.input;
            summary.unpriced_cached_input_tokens += event.tokens.cached;
            summary.unpriced_cache_write_input_tokens += event.tokens.cache_write;
            summary.unpriced_output_tokens += event.tokens.output;
            if (std::find(summary.unpriced_models.begin(), summary.unpriced_models.end(), event.unpriced_model) == summary.unpriced_models.end()) {
                summary.unpriced_models.push_back(event.unpriced_model);
            }
            continue;
        }
        ++summary.priced_events;
        summary.priced_input_tokens += event.tokens.input;
        summary.priced_cached_input_tokens += event.tokens.cached;
        summary.priced_cache_write_input_tokens += event.tokens.cache_write;
        summary.priced_output_tokens += event.tokens.output;
        saw_priced = true;
        thirty += *event.cost;
        if (age < std::chrono::hours{24 * 7}) seven += *event.cost;
        if (age < std::chrono::hours{24}) one += *event.cost;
    }
    if (saw_priced) {
        summary.one_day_usd = one;
        summary.seven_day_usd = seven;
        summary.thirty_day_usd = thirty;
    }
    return summary;
}

void MergeOptional(std::optional<double>& target, const std::optional<double>& source) {
    if (!source) return;
    if (!target) target = 0.0;
    *target += *source;
}

void MergeSummary(SpendSummary& target, const SpendSummary& source) {
    MergeOptional(target.one_day_usd, source.one_day_usd);
    MergeOptional(target.seven_day_usd, source.seven_day_usd);
    MergeOptional(target.thirty_day_usd, source.thirty_day_usd);
    target.files_scanned += source.files_scanned;
    target.priced_events += source.priced_events;
    target.unpriced_events += source.unpriced_events;
    target.priced_input_tokens += source.priced_input_tokens;
    target.priced_cached_input_tokens += source.priced_cached_input_tokens;
    target.priced_cache_write_input_tokens += source.priced_cache_write_input_tokens;
    target.priced_output_tokens += source.priced_output_tokens;
    target.unpriced_input_tokens += source.unpriced_input_tokens;
    target.unpriced_cached_input_tokens += source.unpriced_cached_input_tokens;
    target.unpriced_cache_write_input_tokens += source.unpriced_cache_write_input_tokens;
    target.unpriced_output_tokens += source.unpriced_output_tokens;
    target.one_day_partial = target.one_day_partial || source.one_day_partial;
    target.seven_day_partial = target.seven_day_partial || source.seven_day_partial;
    target.thirty_day_partial = target.thirty_day_partial || source.thirty_day_partial;
    for (const auto& model : source.unpriced_models) {
        if (std::find(target.unpriced_models.begin(), target.unpriced_models.end(), model) == target.unpriced_models.end()) {
            target.unpriced_models.push_back(model);
        }
    }
    for (const auto& source_day : source.daily_model_usage) {
        auto target_day = std::find_if(target.daily_model_usage.begin(), target.daily_model_usage.end(),
            [&](const DailyModelUsage& value) { return value.day == source_day.day; });
        if (target_day == target.daily_model_usage.end()) {
            target.daily_model_usage.push_back(source_day);
            continue;
        }
        for (const auto& source_model : source_day.models) {
            auto target_model = std::find_if(target_day->models.begin(), target_day->models.end(),
                [&](const ModelUsageAmount& value) { return value.model == source_model.model; });
            if (target_model == target_day->models.end()) target_day->models.push_back(source_model);
            else {
                target_model->usage_count += source_model.usage_count;
                target_model->cost_usd += source_model.cost_usd;
                target_model->partial = target_model->partial || source_model.partial;
            }
        }
    }
    for (const auto& source_project : source.top_projects) {
        auto target_project = std::find_if(target.top_projects.begin(), target.top_projects.end(),
            [&](const ProjectUsageAmount& value) { return value.project == source_project.project; });
        if (target_project == target.top_projects.end()) target.top_projects.push_back(source_project);
        else {
            target_project->usage_count += source_project.usage_count;
            target_project->cost_usd += source_project.cost_usd;
            target_project->partial = target_project->partial || source_project.partial;
        }
    }
    target.partial = target.partial || source.partial;
}

std::shared_ptr<const ParsedSpendFile> ParseFile(
    const std::filesystem::path& path, std::stop_token stop, SpendSummary& failure,
    std::shared_ptr<const ParsedSpendFile> base = {}, std::uintmax_t start_offset = 0) {
    const auto mark_failure = [&failure] {
        failure.partial = true;
        failure.one_day_partial = true;
        failure.seven_day_partial = true;
        failure.thirty_day_partial = true;
    };
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        mark_failure();
        return {};
    }
    if (start_offset > static_cast<std::uintmax_t>(std::numeric_limits<std::streamoff>::max()) ||
        (start_offset > 0 && !stream.seekg(static_cast<std::streamoff>(start_offset)))) {
        mark_failure();
        return {};
    }
    auto parsed = base ? std::make_shared<ParsedSpendFile>(*base) : std::make_shared<ParsedSpendFile>();
    FileAccumulator accumulator(*parsed);
    std::string line;
    while (!stop.stop_requested() && std::getline(stream, line)) accumulator.Process(line);
    if (stop.stop_requested() || stream.bad()) {
        mark_failure();
        return {};
    }
    return parsed;
}

}  // namespace

std::optional<double> EstimateCodexCostUsd(std::string_view raw_model, std::uint64_t input_tokens, std::uint64_t cached_input_tokens, std::uint64_t output_tokens, std::chrono::sys_days usage_day) noexcept {
    return EstimateCodexCostUsd(raw_model, input_tokens, cached_input_tokens, 0, output_tokens, usage_day);
}

std::optional<double> EstimateCodexCostUsd(std::string_view raw_model, std::uint64_t input_tokens,
    std::uint64_t cached_input_tokens, std::uint64_t cache_write_input_tokens,
    std::uint64_t output_tokens, std::chrono::sys_days usage_day) noexcept {
    std::string model = NormalizeModel(raw_model);
    double fast_multiplier = 1.0;
    if (model.ends_with("-fast")) {
        model.resize(model.size() - 5);
        fast_multiplier = model == "gpt-5.5" ? 2.5 : 2.0;
    } else if (model.ends_with("-priority")) {
        model.resize(model.size() - 9);
        fast_multiplier = model == "gpt-5.5" ? 2.5 : 2.0;
    }
    const auto found = std::find_if(kRates.begin(), kRates.end(), [&](const Rates& rates) { return rates.model == model; });
    if (found == kRates.end()) return std::nullopt;
    Rates rates = *found;
    const auto cutoff = std::chrono::sys_days{std::chrono::year{2026} / 7 / 30};
    if (usage_day < cutoff && model == "gpt-5.6-sol") {
        rates = input_tokens > kLongContextThreshold ? Rates{model, 1.0e-5, 1.0e-6, 4.5e-5} : Rates{model, 5.0e-6, 5.0e-7, 3.0e-5};
    } else if (usage_day < cutoff && model == "gpt-5.6-terra") {
        rates = input_tokens > kLongContextThreshold ? Rates{model, 5.0e-6, 5.0e-7, 2.25e-5} : Rates{model, 2.5e-6, 2.5e-7, 1.5e-5};
    } else if (usage_day < cutoff && model == "gpt-5.6-luna") {
        rates = input_tokens > kLongContextThreshold ? Rates{model, 2.0e-6, 2.0e-7, 9.0e-6} : Rates{model, 1.0e-6, 1.0e-7, 6.0e-6};
    } else if (input_tokens > kLongContextThreshold && rates.long_input > 0.0) {
        rates.input = rates.long_input;
        rates.cached = rates.long_cached;
        rates.output = rates.long_output;
    }
    const std::uint64_t cached = std::min(cached_input_tokens, input_tokens);
    const std::uint64_t cache_write = std::min(cache_write_input_tokens, input_tokens - cached);
    const std::uint64_t non_cached = input_tokens - cached - cache_write;
    const double cache_write_rate = model.starts_with("gpt-5.6") ?
        rates.input * kGpt56CacheWriteMultiplier : rates.input;
    return (static_cast<double>(non_cached) * rates.input +
        static_cast<double>(cached) * rates.cached +
        static_cast<double>(cache_write) * cache_write_rate +
        static_cast<double>(output_tokens) * rates.output) * fast_multiplier;
}

SpendSummary ScanCodexSpend(const std::filesystem::path& codex_home, std::chrono::system_clock::time_point now,
    std::stop_token stop, SpendScanDiagnostics* diagnostics) {
    SpendSummary summary;
    const auto mark_incomplete = [&summary] {
        summary.partial = true;
        summary.one_day_partial = true;
        summary.seven_day_partial = true;
        summary.thirty_day_partial = true;
    };
    std::vector<FileCandidate> files;
    const std::array roots{codex_home / L"sessions", codex_home / L"archived_sessions"};
    for (const auto& root : roots) {
        if (stop.stop_requested()) {
            mark_incomplete();
            break;
        }
        std::error_code error;
        if (!std::filesystem::exists(root, error)) continue;
        std::filesystem::recursive_directory_iterator iterator(root, std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end) {
            if (stop.stop_requested()) {
                mark_incomplete();
                break;
            }
            const auto path = iterator->path();
            const bool regular = iterator->is_regular_file(error);
            iterator.increment(error);
            if (!regular || path.extension() != L".jsonl") continue;
            const auto fingerprint = ReadFingerprint(path);
            std::error_code fallback_time_error;
            const auto fallback_write_time = fingerprint ? std::filesystem::file_time_type{} :
                std::filesystem::last_write_time(path, fallback_time_error);
            if (fingerprint || !fallback_time_error) {
                const auto write_time = fingerprint ? fingerprint->write_time : fallback_write_time;
                const auto system_write_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    write_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
                if (system_write_time < now - std::chrono::hours{24 * 31}) continue;
            }
            files.push_back(FileCandidate{path, fingerprint});
        }
        if (error) mark_incomplete();
    }

    if (diagnostics) diagnostics->candidate_files = files.size();
    if (files.empty()) return summary;
    const unsigned hardware_threads = std::max(1U, std::thread::hardware_concurrency());
    const std::size_t worker_count = std::min<std::size_t>({files.size(), hardware_threads, 4U});
    std::atomic_size_t next_file{0};
    std::atomic_size_t parsed_files{0};
    std::atomic_size_t reused_files{0};
    std::atomic_size_t resumed_files{0};
    std::vector<SpendSummary> worker_summaries(worker_count);
    std::vector<std::jthread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&, worker] {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            while (true) {
                const std::size_t index = next_file.fetch_add(1, std::memory_order_relaxed);
                if (index >= files.size()) break;
                if (stop.stop_requested()) break;
                const FileCandidate& candidate = files[index];
                std::optional<FileFingerprint> fingerprint = candidate.fingerprint;
                if (!fingerprint) fingerprint = ReadFingerprint(candidate.path);
                std::shared_ptr<const ParsedSpendFile> parsed;
                if (fingerprint) parsed = FindCachedFile(candidate.path, *fingerprint);
                if (parsed) {
                    const auto current = ReadFingerprint(candidate.path);
                    if (!current || !SameFingerprint(*fingerprint, *current)) {
                        parsed.reset();
                        fingerprint = current;
                    }
                }
                if (parsed) {
                    reused_files.fetch_add(1, std::memory_order_relaxed);
                } else {
                    SpendSummary failure;
                    std::optional<CachedSpendFile> resume_base;
                    if (fingerprint) resume_base = FindResumeBase(candidate.path, *fingerprint);
                    if (resume_base && ResumeBoundaryMatches(candidate.path, *resume_base)) {
                        parsed = ParseFile(candidate.path, stop, failure, resume_base->parsed,
                            resume_base->fingerprint.size);
                        if (parsed) resumed_files.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        parsed = ParseFile(candidate.path, stop, failure);
                        if (parsed) parsed_files.fetch_add(1, std::memory_order_relaxed);
                    }
                    if (!parsed) {
                        MergeSummary(worker_summaries[worker], failure);
                        continue;
                    }
                    if (fingerprint) {
                        const auto after = ReadFingerprint(candidate.path);
                        if (after && SameFingerprint(*fingerprint, *after)) {
                            StoreCachedFile(candidate.path, *after, parsed);
                        }
                    }
                }
                MergeSummary(worker_summaries[worker], SummarizeFile(*parsed, now, stop));
            }
        });
    }
    for (auto& worker : workers) worker.join();
    for (const auto& worker_summary : worker_summaries) MergeSummary(summary, worker_summary);
    std::sort(summary.daily_model_usage.begin(), summary.daily_model_usage.end(),
        [](const DailyModelUsage& left, const DailyModelUsage& right) { return left.day < right.day; });
    const double project_total = std::accumulate(summary.top_projects.begin(), summary.top_projects.end(), 0.0,
        [](double total, const ProjectUsageAmount& project) { return total + project.cost_usd; });
    for (auto& project : summary.top_projects) {
        project.share_percent = project_total > 0.0 ? project.cost_usd * 100.0 / project_total : 0.0;
    }
    std::sort(summary.top_projects.begin(), summary.top_projects.end(),
        [](const ProjectUsageAmount& left, const ProjectUsageAmount& right) {
            if (left.cost_usd != right.cost_usd) return left.cost_usd > right.cost_usd;
            return left.usage_count > right.usage_count;
        });
    if (summary.top_projects.size() > 5) summary.top_projects.resize(5);
    if (stop.stop_requested()) mark_incomplete();
    if (diagnostics) {
        diagnostics->parsed_files = parsed_files.load(std::memory_order_relaxed);
        diagnostics->reused_files = reused_files.load(std::memory_order_relaxed);
        diagnostics->resumed_files = resumed_files.load(std::memory_order_relaxed);
    }
    TrimSpendCache();
    return summary;
}

}  // namespace codex_partner
