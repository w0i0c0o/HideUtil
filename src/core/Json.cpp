#include "core/Json.h"

#include <windows.h>

#include <cmath>
#include <iomanip>
#include <sstream>

namespace hideutil {
namespace core {

namespace {

bool IsWhitespace(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r';
}

void AppendUtf8(std::wstring& out, unsigned int codePoint) {
    if (codePoint <= 0xFFFF) {
        out.push_back(static_cast<wchar_t>(codePoint));
    } else {
        codePoint -= 0x10000;
        out.push_back(static_cast<wchar_t>(0xD800 + (codePoint >> 10)));
        out.push_back(static_cast<wchar_t>(0xDC00 + (codePoint & 0x3FF)));
    }
}

std::wstring FormatNumber(double v) {
    if (std::isfinite(v)) {
        const double rounded = std::floor(v);
        if (rounded == v && std::fabs(v) < 1e15) {
            return std::to_wstring(static_cast<long long>(v));
        }
    }
    std::wostringstream oss;
    oss << std::setprecision(17) << v;
    return oss.str();
}

void EscapeInto(const std::wstring& s, std::wstring& out) {
    out.push_back(L'"');
    for (wchar_t c : s) {
        switch (c) {
            case L'"': out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\b': out += L"\\b"; break;
            case L'\f': out += L"\\f"; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default:
                if (c < 0x20) {
                    wchar_t buf[8];
                    swprintf_s(buf, L"\\u%04x", static_cast<unsigned int>(c));
                    out += buf;
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    out.push_back(L'"');
}

// ---------------------------------------------------------------------------
// Recursive-descent parser.
// ---------------------------------------------------------------------------

class Parser {
public:
    Parser(const std::wstring& text, std::wstring& error)
        : text_(text), error_(error) {}

    bool ParseDocument(JsonValue& out) {
        SkipWhitespace();
        if (!ParseValue(out)) {
            return false;
        }
        SkipWhitespace();
        if (pos_ != text_.size()) {
            return Fail(L"trailing characters after JSON value");
        }
        return true;
    }

private:
    bool Fail(const wchar_t* message) {
        std::wostringstream oss;
        oss << message << L" at offset " << pos_;
        error_ = oss.str();
        return false;
    }

    void SkipWhitespace() {
        while (pos_ < text_.size() && IsWhitespace(text_[pos_])) {
            ++pos_;
        }
    }

    bool ParseValue(JsonValue& out) {
        if (pos_ >= text_.size()) {
            return Fail(L"unexpected end of input");
        }
        const wchar_t c = text_[pos_];
        switch (c) {
            case L'{': return ParseObject(out);
            case L'[': return ParseArray(out);
            case L'"': {
                std::wstring s;
                if (!ParseString(s)) return false;
                out = JsonValue::MakeString(s);
                return true;
            }
            case L't': return ParseLiteral(L"true", JsonValue::MakeBool(true), out);
            case L'f': return ParseLiteral(L"false", JsonValue::MakeBool(false), out);
            case L'n': return ParseLiteral(L"null", JsonValue::MakeNull(), out);
            default: return ParseNumber(out);
        }
    }

    bool ParseLiteral(const wchar_t* literal, JsonValue value, JsonValue& out) {
        const std::size_t len = wcslen(literal);
        if (text_.compare(pos_, len, literal) != 0) {
            return Fail(L"invalid literal");
        }
        pos_ += len;
        out = std::move(value);
        return true;
    }

    bool ParseNumber(JsonValue& out) {
        const std::size_t start = pos_;
        if (pos_ < text_.size() && (text_[pos_] == L'-' || text_[pos_] == L'+')) {
            ++pos_;
        }
        bool sawDigit = false;
        while (pos_ < text_.size() && iswdigit(text_[pos_])) {
            ++pos_;
            sawDigit = true;
        }
        if (pos_ < text_.size() && text_[pos_] == L'.') {
            ++pos_;
            while (pos_ < text_.size() && iswdigit(text_[pos_])) {
                ++pos_;
                sawDigit = true;
            }
        }
        if (!sawDigit) {
            return Fail(L"invalid number");
        }
        if (pos_ < text_.size() && (text_[pos_] == L'e' || text_[pos_] == L'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == L'-' || text_[pos_] == L'+')) {
                ++pos_;
            }
            while (pos_ < text_.size() && iswdigit(text_[pos_])) {
                ++pos_;
            }
        }
        const std::wstring token = text_.substr(start, pos_ - start);
        out = JsonValue::MakeNumber(_wtof(token.c_str()));
        return true;
    }

    bool ParseHex4(unsigned int& value) {
        if (pos_ + 4 > text_.size()) {
            return Fail(L"incomplete \\u escape");
        }
        unsigned int v = 0;
        for (int i = 0; i < 4; ++i) {
            const wchar_t c = text_[pos_++];
            v <<= 4;
            if (c >= L'0' && c <= L'9') {
                v += static_cast<unsigned int>(c - L'0');
            } else if (c >= L'a' && c <= L'f') {
                v += static_cast<unsigned int>(c - L'a' + 10);
            } else if (c >= L'A' && c <= L'F') {
                v += static_cast<unsigned int>(c - L'A' + 10);
            } else {
                return Fail(L"invalid hex digit in \\u escape");
            }
        }
        value = v;
        return true;
    }

    bool ParseString(std::wstring& out) {
        if (pos_ >= text_.size() || text_[pos_] != L'"') {
            return Fail(L"expected string");
        }
        ++pos_;
        while (true) {
            if (pos_ >= text_.size()) {
                return Fail(L"unterminated string");
            }
            const wchar_t c = text_[pos_++];
            if (c == L'"') {
                return true;
            }
            if (c == L'\\') {
                if (pos_ >= text_.size()) {
                    return Fail(L"unterminated escape");
                }
                const wchar_t e = text_[pos_++];
                switch (e) {
                    case L'"': out.push_back(L'"'); break;
                    case L'\\': out.push_back(L'\\'); break;
                    case L'/': out.push_back(L'/'); break;
                    case L'b': out.push_back(L'\b'); break;
                    case L'f': out.push_back(L'\f'); break;
                    case L'n': out.push_back(L'\n'); break;
                    case L'r': out.push_back(L'\r'); break;
                    case L't': out.push_back(L'\t'); break;
                    case L'u': {
                        unsigned int cp = 0;
                        if (!ParseHex4(cp)) return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < text_.size() &&
                            text_[pos_] == L'\\' && text_[pos_ + 1] == L'u') {
                            pos_ += 2;
                            unsigned int low = 0;
                            if (!ParseHex4(low)) return false;
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            } else {
                                AppendUtf8(out, cp);
                                cp = low;
                            }
                        }
                        AppendUtf8(out, cp);
                        break;
                    }
                    default:
                        return Fail(L"invalid escape sequence");
                }
            } else {
                out.push_back(c);
            }
        }
    }

    bool ParseArray(JsonValue& out) {
        ++pos_;  // consume '['
        out = JsonValue::MakeArray();
        SkipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == L']') {
            ++pos_;
            return true;
        }
        while (true) {
            JsonValue item;
            SkipWhitespace();
            if (!ParseValue(item)) {
                return false;
            }
            out.Push(std::move(item));
            SkipWhitespace();
            if (pos_ >= text_.size()) {
                return Fail(L"unterminated array");
            }
            const wchar_t c = text_[pos_];
            if (c == L',') {
                ++pos_;
                continue;
            }
            if (c == L']') {
                ++pos_;
                return true;
            }
            return Fail(L"expected ',' or ']' in array");
        }
    }

    bool ParseObject(JsonValue& out) {
        ++pos_;  // consume '{'
        out = JsonValue::MakeObject();
        SkipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == L'}') {
            ++pos_;
            return true;
        }
        while (true) {
            SkipWhitespace();
            std::wstring key;
            if (!ParseString(key)) {
                return false;
            }
            SkipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != L':') {
                return Fail(L"expected ':' in object");
            }
            ++pos_;
            SkipWhitespace();
            JsonValue value;
            if (!ParseValue(value)) {
                return false;
            }
            out.Set(key, std::move(value));
            SkipWhitespace();
            if (pos_ >= text_.size()) {
                return Fail(L"unterminated object");
            }
            const wchar_t c = text_[pos_];
            if (c == L',') {
                ++pos_;
                continue;
            }
            if (c == L'}') {
                ++pos_;
                return true;
            }
            return Fail(L"expected ',' or '}' in object");
        }
    }

    const std::wstring& text_;
    std::wstring& error_;
    std::size_t pos_ = 0;
};

void DumpInto(const JsonValue& value, int indent, int depth, std::wstring& out) {
    const bool pretty = indent >= 0;
    const auto newlineIndent = [&](int level) {
        if (!pretty) return;
        out.push_back(L'\n');
        out.append(static_cast<std::size_t>(indent) * level, L' ');
    };

    switch (value.type()) {
        case JsonValue::Type::Null: out += L"null"; break;
        case JsonValue::Type::Bool: out += value.AsBool() ? L"true" : L"false"; break;
        case JsonValue::Type::Number: out += FormatNumber(value.AsNumber()); break;
        case JsonValue::Type::String: EscapeInto(value.AsString(), out); break;
        case JsonValue::Type::Array: {
            const auto& arr = value.AsArray();
            if (arr.empty()) {
                out += L"[]";
                break;
            }
            out.push_back(L'[');
            for (std::size_t i = 0; i < arr.size(); ++i) {
                if (i > 0) out.push_back(L',');
                newlineIndent(depth + 1);
                DumpInto(arr[i], indent, depth + 1, out);
            }
            newlineIndent(depth);
            out.push_back(L']');
            break;
        }
        case JsonValue::Type::Object: {
            const auto& obj = value.AsObject();
            if (obj.empty()) {
                out += L"{}";
                break;
            }
            out.push_back(L'{');
            for (std::size_t i = 0; i < obj.size(); ++i) {
                if (i > 0) out.push_back(L',');
                newlineIndent(depth + 1);
                EscapeInto(obj[i].first, out);
                out.push_back(L':');
                if (pretty) out.push_back(L' ');
                DumpInto(obj[i].second, indent, depth + 1, out);
            }
            newlineIndent(depth);
            out.push_back(L'}');
            break;
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// JsonValue.
// ---------------------------------------------------------------------------

JsonValue::JsonValue() : type_(Type::Null) {}

JsonValue::JsonValue(const JsonValue& other) : type_(other.type_) {
    bool_ = other.bool_;
    number_ = other.number_;
    string_ = other.string_;
    if (other.array_) array_ = std::make_unique<Array>(*other.array_);
    if (other.object_) object_ = std::make_unique<Object>(*other.object_);
}

JsonValue::JsonValue(JsonValue&& other) noexcept
    : type_(other.type_),
      bool_(other.bool_),
      number_(other.number_),
      string_(std::move(other.string_)),
      array_(std::move(other.array_)),
      object_(std::move(other.object_)) {
    other.type_ = Type::Null;
}

JsonValue& JsonValue::operator=(const JsonValue& other) {
    if (this != &other) {
        JsonValue copy(other);
        *this = std::move(copy);
    }
    return *this;
}

JsonValue& JsonValue::operator=(JsonValue&& other) noexcept {
    if (this != &other) {
        type_ = other.type_;
        bool_ = other.bool_;
        number_ = other.number_;
        string_ = std::move(other.string_);
        array_ = std::move(other.array_);
        object_ = std::move(other.object_);
        other.type_ = Type::Null;
    }
    return *this;
}

JsonValue::~JsonValue() = default;

JsonValue JsonValue::MakeNull() { return JsonValue(); }

JsonValue JsonValue::MakeBool(bool value) {
    JsonValue v;
    v.type_ = Type::Bool;
    v.bool_ = value;
    return v;
}

JsonValue JsonValue::MakeNumber(double value) {
    JsonValue v;
    v.type_ = Type::Number;
    v.number_ = value;
    return v;
}

JsonValue JsonValue::MakeString(const std::wstring& value) {
    JsonValue v;
    v.type_ = Type::String;
    v.string_ = value;
    return v;
}

JsonValue JsonValue::MakeArray() {
    JsonValue v;
    v.type_ = Type::Array;
    v.array_ = std::make_unique<Array>();
    return v;
}

JsonValue JsonValue::MakeObject() {
    JsonValue v;
    v.type_ = Type::Object;
    v.object_ = std::make_unique<Object>();
    return v;
}

bool JsonValue::AsBool() const { return type_ == Type::Bool ? bool_ : false; }
double JsonValue::AsNumber() const { return type_ == Type::Number ? number_ : 0.0; }
long long JsonValue::AsInt() const {
    return type_ == Type::Number ? static_cast<long long>(number_) : 0LL;
}
const std::wstring& JsonValue::AsString() const {
    static const std::wstring kEmpty;
    return type_ == Type::String ? string_ : kEmpty;
}
const JsonValue::Array& JsonValue::AsArray() const {
    static const Array kEmpty;
    return (type_ == Type::Array && array_) ? *array_ : kEmpty;
}
JsonValue::Object& JsonValue::AsObjectMut() {
    if (type_ != Type::Object || !object_) {
        type_ = Type::Object;
        object_ = std::make_unique<Object>();
    }
    return *object_;
}
const JsonValue::Object& JsonValue::AsObject() const {
    static const Object kEmpty;
    return (type_ == Type::Object && object_) ? *object_ : kEmpty;
}

bool JsonValue::Has(const std::wstring& key) const { return Find(key) != nullptr; }

const JsonValue* JsonValue::Find(const std::wstring& key) const {
    if (type_ != Type::Object || !object_) return nullptr;
    for (const auto& kv : *object_) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

JsonValue* JsonValue::Find(const std::wstring& key) {
    if (type_ != Type::Object || !object_) return nullptr;
    for (auto& kv : *object_) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

void JsonValue::Set(const std::wstring& key, JsonValue value) {
    Object& obj = AsObjectMut();
    for (auto& kv : obj) {
        if (kv.first == key) {
            kv.second = std::move(value);
            return;
        }
    }
    obj.emplace_back(key, std::move(value));
}

void JsonValue::Push(JsonValue value) {
    if (type_ != Type::Array || !array_) {
        type_ = Type::Array;
        array_ = std::make_unique<Array>();
    }
    array_->push_back(std::move(value));
}

std::size_t JsonValue::Size() const {
    if (type_ == Type::Array && array_) return array_->size();
    if (type_ == Type::Object && object_) return object_->size();
    return 0;
}

const JsonValue& JsonValue::operator[](std::size_t index) const {
    static const JsonValue kNull;
    if (type_ == Type::Array && array_ && index < array_->size()) {
        return (*array_)[index];
    }
    return kNull;
}

std::wstring JsonValue::Dump(int indent) const {
    std::wstring out;
    DumpInto(*this, indent, 0, out);
    return out;
}

bool JsonValue::Parse(const std::wstring& text, JsonValue& out, std::wstring& error) {
    std::wstring normalized = text;
    // Strip a leading UTF-8 BOM that survived the decoding step.
    if (!normalized.empty() && normalized.front() == 0xFEFF) {
        normalized.erase(normalized.begin());
    }
    error.clear();
    Parser parser(normalized, error);
    return parser.ParseDocument(out);
}

// ---------------------------------------------------------------------------
// UTF-8 file helpers.
// ---------------------------------------------------------------------------

bool ReadTextFileUtf8(const std::wstring& path, std::wstring& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) {
        CloseHandle(file);
        return false;
    }
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD totalRead = 0;
    if (!bytes.empty()) {
        if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &totalRead, nullptr)) {
            CloseHandle(file);
            return false;
        }
    }
    CloseHandle(file);
    bytes.resize(totalRead);

    if (bytes.empty()) {
        out.clear();
        return true;
    }
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, bytes.data(),
                                      static_cast<int>(bytes.size()), nullptr, 0);
    if (wideLen <= 0) {
        return false;
    }
    out.assign(static_cast<std::size_t>(wideLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()),
                        out.data(), wideLen);
    return true;
}

bool WriteTextFileUtf8(const std::wstring& path, const std::wstring& text) {
    if (text.empty()) {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        CloseHandle(file);
        return true;
    }
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) {
        return false;
    }
    std::string bytes(static_cast<std::size_t>(utf8Len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        bytes.data(), utf8Len, nullptr, nullptr);

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == bytes.size();
}

}  // namespace core
}  // namespace hideutil
