#include "json.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

namespace codex_partner {

JsonValue::JsonValue(bool value) : type_(Type::Boolean), boolean_(value) {}
JsonValue::JsonValue(double value) : type_(Type::Number), number_(value) {}
JsonValue::JsonValue(std::string value) : type_(Type::String), string_(std::move(value)) {}
JsonValue::JsonValue(Object value) : type_(Type::Object), object_(std::move(value)) {}
JsonValue::JsonValue(Array value) : type_(Type::Array), array_(std::move(value)) {}

const JsonValue* JsonValue::find(std::string_view key) const noexcept {
    if (type_ != Type::Object) return nullptr;
    const auto it = object_.find(std::string(key));
    return it == object_.end() ? nullptr : &it->second;
}

const JsonValue* JsonValue::at(std::size_t index) const noexcept {
    return type_ == Type::Array && index < array_.size() ? &array_[index] : nullptr;
}

std::optional<std::string_view> JsonValue::as_string() const noexcept {
    return type_ == Type::String ? std::optional<std::string_view>(string_) : std::nullopt;
}

std::optional<double> JsonValue::as_number() const noexcept {
    return type_ == Type::Number ? std::optional<double>(number_) : std::nullopt;
}

std::optional<bool> JsonValue::as_bool() const noexcept {
    return type_ == Type::Boolean ? std::optional<bool>(boolean_) : std::nullopt;
}

std::size_t JsonValue::size() const noexcept {
    if (type_ == Type::Array) return array_.size();
    if (type_ == Type::Object) return object_.size();
    return 0;
}

void JsonValue::secure_clear() noexcept {
    if (!string_.empty()) {
        volatile char* bytes = string_.data();
        for (std::size_t index = 0; index < string_.size(); ++index) bytes[index] = 0;
        string_.clear();
    }
    for (auto& item : object_) item.second.secure_clear();
    for (auto& value : array_) value.secure_clear();
    object_.clear();
    array_.clear();
    boolean_ = false;
    number_ = 0.0;
    type_ = Type::Null;
}

namespace {

void AppendUtf8(std::string& output, unsigned codepoint) {
    if (codepoint <= 0x7F) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

class Parser {
public:
    explicit Parser(std::string_view input) : input_(input) {}

    JsonParseResult Run() {
        SkipSpace();
        JsonValue value = ParseValue();
        SkipSpace();
        if (error_.empty() && position_ != input_.size()) Fail("Unexpected trailing data");
        return {std::move(value), std::move(error_)};
    }

private:
    JsonValue ParseValue() {
        if (position_ >= input_.size()) return Fail("Unexpected end of JSON");
        switch (input_[position_]) {
        case 'n': return ParseLiteral("null", JsonValue{});
        case 't': return ParseLiteral("true", JsonValue(true));
        case 'f': return ParseLiteral("false", JsonValue(false));
        case '"': return JsonValue(ParseString());
        case '{': return ParseObject();
        case '[': return ParseArray();
        default: return ParseNumber();
        }
    }

    JsonValue ParseLiteral(std::string_view literal, JsonValue value) {
        if (input_.substr(position_, literal.size()) != literal) return Fail("Invalid JSON literal");
        position_ += literal.size();
        return value;
    }

    JsonValue ParseObject() {
        ++position_;
        JsonValue::Object object;
        SkipSpace();
        if (Consume('}')) return JsonValue(std::move(object));
        while (error_.empty()) {
            if (position_ >= input_.size() || input_[position_] != '"') return Fail("Object key must be a string");
            std::string key = ParseString();
            SkipSpace();
            if (!Consume(':')) return Fail("Expected ':' after object key");
            SkipSpace();
            object.insert_or_assign(std::move(key), ParseValue());
            SkipSpace();
            if (Consume('}')) break;
            if (!Consume(',')) return Fail("Expected ',' between object members");
            SkipSpace();
        }
        return JsonValue(std::move(object));
    }

    JsonValue ParseArray() {
        ++position_;
        JsonValue::Array array;
        SkipSpace();
        if (Consume(']')) return JsonValue(std::move(array));
        while (error_.empty()) {
            array.push_back(ParseValue());
            SkipSpace();
            if (Consume(']')) break;
            if (!Consume(',')) return Fail("Expected ',' between array values");
            SkipSpace();
        }
        return JsonValue(std::move(array));
    }

    std::string ParseString() {
        std::string output;
        ++position_;
        while (position_ < input_.size()) {
            const char ch = input_[position_++];
            if (ch == '"') return output;
            if (ch != '\\') {
                if (static_cast<unsigned char>(ch) < 0x20) {
                    Fail("Control character in JSON string");
                    return {};
                }
                output.push_back(ch);
                continue;
            }
            if (position_ >= input_.size()) break;
            const char escaped = input_[position_++];
            switch (escaped) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u': {
                if (position_ + 4 > input_.size()) return FailString("Incomplete Unicode escape");
                unsigned codepoint = 0;
                for (int i = 0; i < 4; ++i) {
                    const char digit = input_[position_++];
                    codepoint <<= 4;
                    if (digit >= '0' && digit <= '9') codepoint += static_cast<unsigned>(digit - '0');
                    else if (digit >= 'a' && digit <= 'f') codepoint += static_cast<unsigned>(digit - 'a' + 10);
                    else if (digit >= 'A' && digit <= 'F') codepoint += static_cast<unsigned>(digit - 'A' + 10);
                    else return FailString("Invalid Unicode escape");
                }
                AppendUtf8(output, codepoint);
                break;
            }
            default: return FailString("Invalid JSON string escape");
            }
        }
        return FailString("Unterminated JSON string");
    }

    JsonValue ParseNumber() {
        const std::size_t start = position_;
        if (position_ < input_.size() && input_[position_] == '-') ++position_;
        while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
        }
        if (start == position_) return Fail("Expected a JSON value");
        const std::string number(input_.substr(start, position_ - start));
        char* end = nullptr;
        const double value = std::strtod(number.c_str(), &end);
        if (end == number.c_str() || *end != '\0' || !std::isfinite(value)) return Fail("Invalid JSON number");
        return JsonValue(value);
    }

    bool Consume(char expected) {
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void SkipSpace() {
        while (position_ < input_.size()) {
            const char ch = input_[position_];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
            ++position_;
        }
    }

    JsonValue Fail(std::string message) {
        if (error_.empty()) error_ = std::move(message) + " at byte " + std::to_string(position_);
        return {};
    }

    std::string FailString(std::string message) {
        Fail(std::move(message));
        return {};
    }

    std::string_view input_;
    std::size_t position_ = 0;
    std::string error_;
};

}  // namespace

JsonParseResult ParseJson(std::string_view source) {
    return Parser(source).Run();
}

}  // namespace codex_partner
