/*
 * Xournal++
 *
 * Minimal BSON reader and writer, used for Saber notes (.sbn2 inside .sba)
 *
 * Only the element types Saber writes are supported: double, string, document, array, binary, bool, null, int32 and
 * int64.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstdint>      // for int32_t, int64_t
#include <optional>     // for optional
#include <stdexcept>    // for runtime_error
#include <string>       // for string
#include <string_view>  // for string_view
#include <variant>      // for variant, monostate
#include <vector>       // for vector

namespace xoj::sba::bson {

struct Value;
struct Member;

using Array = std::vector<Value>;

/// A BSON document. The order of the members is kept.
using Document = std::vector<Member>;

struct Binary {
    std::string data;
};

struct ParseError: std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Value {
    std::variant<std::monostate, bool, int32_t, int64_t, double, std::string, Binary, Array, Document> v;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b): v(b) {}
    Value(int32_t i): v(i) {}
    Value(int64_t i): v(i) {}
    Value(double d): v(d) {}
    Value(const char* s): v(std::string(s)) {}
    Value(std::string s): v(std::move(s)) {}
    Value(Binary b): v(std::move(b)) {}
    Value(Array a): v(std::move(a)) {}
    Value(Document d): v(std::move(d)) {}

    bool isNull() const { return std::holds_alternative<std::monostate>(v); }

    /// Any numeric type as double
    std::optional<double> number() const;
    /// int32, int64 (or an integral double) as int64
    std::optional<int64_t> integer() const;
    std::optional<bool> boolean() const;

    const std::string* string() const { return std::get_if<std::string>(&v); }
    const Binary* binary() const { return std::get_if<Binary>(&v); }
    const Array* array() const { return std::get_if<Array>(&v); }
    const Document* document() const { return std::get_if<Document>(&v); }
};

struct Member {
    std::string key;
    Value value;
};

/// Returns the member with the given key, or nullptr
const Value* find(const Document& doc, std::string_view key);

double getNumber(const Document& doc, std::string_view key, double fallback);
int64_t getInteger(const Document& doc, std::string_view key, int64_t fallback);
bool getBool(const Document& doc, std::string_view key, bool fallback);
const std::string* getString(const Document& doc, std::string_view key);
const Array* getArray(const Document& doc, std::string_view key);
const Document* getDocument(const Document& doc, std::string_view key);

/// Parses a BSON document. Throws ParseError on malformed input.
Document parse(std::string_view data);

/// Serializes a document to BSON
std::string serialize(const Document& doc);

}  // namespace xoj::sba::bson
