#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace codex_partner {

class JsonValue {
public:
    enum class Type { Null, Boolean, Number, String, Object, Array };
    using Object = std::unordered_map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;

    JsonValue() = default;
    explicit JsonValue(bool value);
    explicit JsonValue(double value);
    explicit JsonValue(std::string value);
    explicit JsonValue(Object value);
    explicit JsonValue(Array value);

    [[nodiscard]] Type type() const noexcept { return type_; }
    [[nodiscard]] const JsonValue* find(std::string_view key) const noexcept;
    [[nodiscard]] const JsonValue* at(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<std::string_view> as_string() const noexcept;
    [[nodiscard]] std::optional<double> as_number() const noexcept;
    [[nodiscard]] std::optional<bool> as_bool() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    void secure_clear() noexcept;

private:
    Type type_ = Type::Null;
    bool boolean_ = false;
    double number_ = 0.0;
    std::string string_;
    Object object_;
    Array array_;
};

struct JsonParseResult {
    JsonValue value;
    std::string error;
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

[[nodiscard]] JsonParseResult ParseJson(std::string_view source);

}  // namespace codex_partner
