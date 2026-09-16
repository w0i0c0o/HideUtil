// Minimal, dependency-free JSON parser / serializer.
//
// Rationale: the design document (section 9.1) asks to minimise third-party
// dependencies to keep the build offline-capable and the attack surface small.
// This implementation supports exactly the subset required by config.json /
// state.json: null / bool / number / string / array / object, with the usual
// escape sequences and \uXXXX (including surrogate pairs).
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace hideutil {
namespace core {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<JsonValue>;
    using Object = std::vector<std::pair<std::wstring, JsonValue>>;

    JsonValue();
    JsonValue(const JsonValue& other);
    JsonValue(JsonValue&& other) noexcept;
    JsonValue& operator=(const JsonValue& other);
    JsonValue& operator=(JsonValue&& other) noexcept;
    ~JsonValue();

    static JsonValue MakeNull();
    static JsonValue MakeBool(bool value);
    static JsonValue MakeNumber(double value);
    static JsonValue MakeString(const std::wstring& value);
    static JsonValue MakeArray();
    static JsonValue MakeObject();

    Type type() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }
    bool IsBool() const { return type_ == Type::Bool; }
    bool IsNumber() const { return type_ == Type::Number; }
    bool IsString() const { return type_ == Type::String; }
    bool IsArray() const { return type_ == Type::Array; }
    bool IsObject() const { return type_ == Type::Object; }

    bool AsBool() const;
    double AsNumber() const;
    long long AsInt() const;
    const std::wstring& AsString() const;
    const Array& AsArray() const;
    Object& AsObjectMut();
    const Object& AsObject() const;

    // Object helpers.
    bool Has(const std::wstring& key) const;
    const JsonValue* Find(const std::wstring& key) const;
    JsonValue* Find(const std::wstring& key);
    void Set(const std::wstring& key, JsonValue value);

    // Array helpers.
    void Push(JsonValue value);
    std::size_t Size() const;
    const JsonValue& operator[](std::size_t index) const;

    // Serialize with pretty printing (indent < 0 => compact).
    std::wstring Dump(int indent = 2) const;

    // Parse a wide (already decoded) JSON document.
    static bool Parse(const std::wstring& text, JsonValue& out, std::wstring& error);

private:
    Type type_;
    bool bool_ = false;
    double number_ = 0.0;
    std::wstring string_;
    std::unique_ptr<Array> array_;
    std::unique_ptr<Object> object_;
};

// UTF-8 text file helpers (no BOM is written; a BOM is tolerated on read).
bool ReadTextFileUtf8(const std::wstring& path, std::wstring& out);
bool WriteTextFileUtf8(const std::wstring& path, const std::wstring& text);

}  // namespace core
}  // namespace hideutil
