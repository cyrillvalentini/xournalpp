#include "Bson.h"

#include <bit>      // for endian
#include <cmath>    // for trunc, isfinite
#include <cstring>  // for memcpy

namespace xoj::sba::bson {

static_assert(std::endian::native == std::endian::little, "The BSON code assumes a little endian machine");

namespace {
enum Type : uint8_t {
    DOUBLE = 0x01,
    STRING = 0x02,
    DOCUMENT = 0x03,
    ARRAY = 0x04,
    BINARY = 0x05,
    UNDEFINED = 0x06,
    BOOL = 0x08,
    NULL_VALUE = 0x0A,
    INT32 = 0x10,
    INT64 = 0x12,
};

/// Maximum nesting depth, so that a malicious file cannot overflow the stack
constexpr int MAX_DEPTH = 64;

class Reader {
public:
    explicit Reader(std::string_view data): data(data) {}

    Document readDocument(int depth) {
        if (depth > MAX_DEPTH) {
            throw ParseError("BSON: nesting too deep");
        }
        const size_t start = pos;
        const auto length = readInt32();
        if (length < 5 || start + static_cast<size_t>(length) > data.size()) {
            throw ParseError("BSON: invalid document length");
        }
        const size_t end = start + static_cast<size_t>(length) - 1;  // position of the trailing 0x00

        Document doc;
        while (pos < end) {
            const auto type = readByte();
            std::string key = readCString();
            doc.push_back({std::move(key), readValue(type, depth)});
        }
        if (pos != end || readByte() != 0) {
            throw ParseError("BSON: document not terminated");
        }
        return doc;
    }

private:
    Value readValue(uint8_t type, int depth) {
        switch (type) {
            case DOUBLE: {
                double d{};
                auto raw = readBytes(8);
                std::memcpy(&d, raw.data(), 8);
                return d;
            }
            case STRING: {
                const auto length = readInt32();
                if (length < 1) {
                    throw ParseError("BSON: invalid string length");
                }
                auto raw = readBytes(static_cast<size_t>(length));
                if (raw.back() != '\0') {
                    throw ParseError("BSON: string not terminated");
                }
                return std::string(raw.substr(0, raw.size() - 1));
            }
            case DOCUMENT:
                return readDocument(depth + 1);
            case ARRAY: {
                // An array is a document with the keys "0", "1", ...
                Document d = readDocument(depth + 1);
                Array a;
                a.reserve(d.size());
                for (auto& m: d) {
                    a.push_back(std::move(m.value));
                }
                return a;
            }
            case BINARY: {
                const auto length = readInt32();
                if (length < 0) {
                    throw ParseError("BSON: invalid binary length");
                }
                readByte();  // subtype
                return Binary{std::string(readBytes(static_cast<size_t>(length)))};
            }
            case UNDEFINED:
            case NULL_VALUE:
                return nullptr;
            case BOOL:
                return readByte() != 0;
            case INT32:
                return readInt32();
            case INT64: {
                int64_t i{};
                auto raw = readBytes(8);
                std::memcpy(&i, raw.data(), 8);
                return i;
            }
            default:
                throw ParseError("BSON: unsupported element type " + std::to_string(type));
        }
    }

    std::string_view readBytes(size_t n) {
        if (n > data.size() - pos) {
            throw ParseError("BSON: unexpected end of data");
        }
        auto res = data.substr(pos, n);
        pos += n;
        return res;
    }

    uint8_t readByte() { return static_cast<uint8_t>(readBytes(1)[0]); }

    int32_t readInt32() {
        int32_t i{};
        auto raw = readBytes(4);
        std::memcpy(&i, raw.data(), 4);
        return i;
    }

    std::string readCString() {
        const auto end = data.find('\0', pos);
        if (end == std::string_view::npos) {
            throw ParseError("BSON: key not terminated");
        }
        std::string res(data.substr(pos, end - pos));
        pos = end + 1;
        return res;
    }

    std::string_view data;
    size_t pos = 0;
};

class Writer {
public:
    void writeDocument(const Document& doc) {
        const size_t start = out.size();
        out.append(4, '\0');  // length placeholder
        for (const auto& m: doc) {
            writeElement(m.key, m.value);
        }
        out.push_back('\0');
        patchLength(start);
    }

    std::string out;

private:
    void writeArray(const Array& a) {
        const size_t start = out.size();
        out.append(4, '\0');
        for (size_t i = 0; i < a.size(); i++) {
            writeElement(std::to_string(i), a[i]);
        }
        out.push_back('\0');
        patchLength(start);
    }

    void writeElement(const std::string& key, const Value& value) {
        const size_t typePos = out.size();
        out.push_back('\0');  // type placeholder
        out.append(key);
        out.push_back('\0');

        uint8_t type = NULL_VALUE;
        std::visit(
                [&](const auto& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        type = NULL_VALUE;
                    } else if constexpr (std::is_same_v<T, bool>) {
                        type = BOOL;
                        out.push_back(v ? '\1' : '\0');
                    } else if constexpr (std::is_same_v<T, int32_t>) {
                        type = INT32;
                        writeRaw(v);
                    } else if constexpr (std::is_same_v<T, int64_t>) {
                        type = INT64;
                        writeRaw(v);
                    } else if constexpr (std::is_same_v<T, double>) {
                        type = DOUBLE;
                        writeRaw(v);
                    } else if constexpr (std::is_same_v<T, std::string>) {
                        type = STRING;
                        writeRaw(static_cast<int32_t>(v.size() + 1));
                        out.append(v);
                        out.push_back('\0');
                    } else if constexpr (std::is_same_v<T, Binary>) {
                        type = BINARY;
                        writeRaw(static_cast<int32_t>(v.data.size()));
                        out.push_back('\0');  // generic subtype
                        out.append(v.data);
                    } else if constexpr (std::is_same_v<T, Array>) {
                        type = ARRAY;
                        writeArray(v);
                    } else if constexpr (std::is_same_v<T, Document>) {
                        type = DOCUMENT;
                        writeDocument(v);
                    }
                },
                value.v);
        out[typePos] = static_cast<char>(type);
    }

    template <typename T>
    void writeRaw(T v) {
        char buf[sizeof(T)];
        std::memcpy(buf, &v, sizeof(T));
        out.append(buf, sizeof(T));
    }

    void patchLength(size_t start) {
        const auto length = static_cast<int32_t>(out.size() - start);
        std::memcpy(out.data() + start, &length, 4);
    }
};
}  // namespace

std::optional<double> Value::number() const {
    if (auto* d = std::get_if<double>(&v)) {
        return *d;
    }
    if (auto* i = std::get_if<int32_t>(&v)) {
        return static_cast<double>(*i);
    }
    if (auto* i = std::get_if<int64_t>(&v)) {
        return static_cast<double>(*i);
    }
    return std::nullopt;
}

std::optional<int64_t> Value::integer() const {
    if (auto* i = std::get_if<int32_t>(&v)) {
        return *i;
    }
    if (auto* i = std::get_if<int64_t>(&v)) {
        return *i;
    }
    if (auto* d = std::get_if<double>(&v); d && std::isfinite(*d) && std::trunc(*d) == *d) {
        return static_cast<int64_t>(*d);
    }
    return std::nullopt;
}

std::optional<bool> Value::boolean() const {
    if (auto* b = std::get_if<bool>(&v)) {
        return *b;
    }
    return std::nullopt;
}

const Value* find(const Document& doc, std::string_view key) {
    for (const auto& m: doc) {
        if (m.key == key) {
            return &m.value;
        }
    }
    return nullptr;
}

double getNumber(const Document& doc, std::string_view key, double fallback) {
    const Value* v = find(doc, key);
    auto n = v ? v->number() : std::nullopt;
    return n && std::isfinite(*n) ? *n : fallback;
}

int64_t getInteger(const Document& doc, std::string_view key, int64_t fallback) {
    const Value* v = find(doc, key);
    auto n = v ? v->integer() : std::nullopt;
    return n.value_or(fallback);
}

bool getBool(const Document& doc, std::string_view key, bool fallback) {
    const Value* v = find(doc, key);
    auto b = v ? v->boolean() : std::nullopt;
    return b.value_or(fallback);
}

const std::string* getString(const Document& doc, std::string_view key) {
    const Value* v = find(doc, key);
    return v ? v->string() : nullptr;
}

const Array* getArray(const Document& doc, std::string_view key) {
    const Value* v = find(doc, key);
    return v ? v->array() : nullptr;
}

const Document* getDocument(const Document& doc, std::string_view key) {
    const Value* v = find(doc, key);
    return v ? v->document() : nullptr;
}

Document parse(std::string_view data) {
    Reader r(data);
    return r.readDocument(0);
}

std::string serialize(const Document& doc) {
    Writer w;
    w.writeDocument(doc);
    return std::move(w.out);
}

}  // namespace xoj::sba::bson
