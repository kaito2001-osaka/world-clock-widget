// Minimal dependency-free JSON parser/serializer.
// Sufficient for WorldClockGadget's config.json / state.json schema.
// Not a general-purpose library: supports objects, arrays, strings,
// numbers, bool and null, with UTF-8 passthrough for strings.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <sstream>
#include <cstdint>
#include <stdexcept>

namespace json {

class Value;
using Array  = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>; // keep insertion order

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
public:
    Type type = Type::Null;
    bool        b = false;
    double      num = 0.0;
    std::string str;
    Array       arr;
    Object      obj;

    Value() = default;
    Value(bool v) : type(Type::Bool), b(v) {}
    Value(double v) : type(Type::Number), num(v) {}
    Value(int v) : type(Type::Number), num(v) {}
    Value(const char* v) : type(Type::String), str(v) {}
    Value(std::string v) : type(Type::String), str(std::move(v)) {}

    bool isObject() const { return type == Type::Object; }
    bool isArray()  const { return type == Type::Array; }

    // Object lookup helpers (return defaults when missing / wrong type).
    const Value* find(const std::string& key) const {
        if (type != Type::Object) return nullptr;
        for (auto& kv : obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    std::string getString(const std::string& key, const std::string& def) const {
        auto* v = find(key);
        return (v && v->type == Type::String) ? v->str : def;
    }
    double getNumber(const std::string& key, double def) const {
        auto* v = find(key);
        return (v && v->type == Type::Number) ? v->num : def;
    }
    int getInt(const std::string& key, int def) const {
        // Casting a double that does not fit in int (or NaN) is undefined
        // behaviour. These bounds are exactly the values that truncate toward
        // zero into int's range; NaN fails both comparisons.
        const double d = getNumber(key, def);
        if (!(d > -2147483649.0 && d < 2147483648.0)) return def;
        return static_cast<int>(d);
    }
    bool getBool(const std::string& key, bool def) const {
        auto* v = find(key);
        return (v && v->type == Type::Bool) ? v->b : def;
    }

    void set(const std::string& key, Value v) {
        type = Type::Object;
        for (auto& kv : obj) if (kv.first == key) { kv.second = std::move(v); return; }
        obj.emplace_back(key, std::move(v));
    }
};

// ---------- Parser ----------
class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}

    Value parse() {
        skipWs();
        Value v = parseValue();
        skipWs();
        // "{...}xyz" or two writes run together is a corrupt file, not a
        // config that happens to have a tail.
        if (i_ != s_.size()) fail("trailing characters");
        return v;
    }

private:
    // config.json nests three deep (root -> cities -> city). The limit only
    // has to stop a run of '[' from overflowing the stack, which no catch
    // block can recover from.
    static constexpr int kMaxDepth = 64;

    const std::string& s_;
    size_t i_ = 0;
    int depth_ = 0;

    [[noreturn]] void fail(const char* msg) { throw std::runtime_error(msg); }

    char peek() { return i_ < s_.size() ? s_[i_] : '\0'; }
    char get()  { return i_ < s_.size() ? s_[i_++] : '\0'; }

    void skipWs() {
        while (i_ < s_.size()) {
            char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i_;
            else break;
        }
    }

    Value parseValue() {
        skipWs();
        char c = peek();
        switch (c) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': { Value v; v.type = Type::String; v.str = parseString(); return v; }
            case 't': case 'f': return parseBool();
            case 'n': expect("null"); return Value();
            default:  return parseNumber();
        }
    }

    void expect(const char* lit) {
        for (const char* p = lit; *p; ++p) if (get() != *p) fail("invalid literal");
    }

    Value parseBool() {
        if (peek() == 't') { expect("true");  return Value(true); }
        expect("false"); return Value(false);
    }

    Value parseNumber() {
        size_t start = i_;
        if (peek() == '-') get();
        while (isdigit((unsigned char)peek())) get();
        if (peek() == '.') { get(); while (isdigit((unsigned char)peek())) get(); }
        if (peek() == 'e' || peek() == 'E') {
            get();
            if (peek() == '+' || peek() == '-') get();
            while (isdigit((unsigned char)peek())) get();
        }
        std::string n = s_.substr(start, i_ - start);
        if (n.empty()) fail("invalid number");
        Value v; v.type = Type::Number; v.num = std::stod(n);
        return v;
    }

    std::string parseString() {
        if (get() != '"') fail("expected string");
        std::string out;
        while (true) {
            char c = get();
            if (c == '\0') fail("unterminated string");
            if (c == '"') break;
            if (c == '\\') {
                char e = get();
                switch (e) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'u': {
                        unsigned cp = parseHex4();
                        // Characters outside the BMP arrive as a surrogate
                        // pair (an emoji is "🗼"), which the settings
                        // app produces for every non-ASCII character. Join the
                        // pair into one code point; appendUtf8 turns anything
                        // still unpaired into U+FFFD.
                        if (cp >= 0xD800 && cp <= 0xDBFF &&
                            peek() == '\\' && i_ + 1 < s_.size() && s_[i_ + 1] == 'u') {
                            const size_t save = i_;
                            get(); get();                       // consume the "\u"
                            unsigned lo = parseHex4();
                            if (lo >= 0xDC00 && lo <= 0xDFFF)
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            else
                                i_ = save;  // not a low surrogate: re-read it as its own escape
                        }
                        appendUtf8(out, cp);
                        break;
                    }
                    default: fail("bad escape");
                }
            } else {
                out += c;
            }
        }
        return out;
    }

    unsigned parseHex4() {
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = get();
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (c - '0');
            else if (c >= 'a' && c <= 'f') v |= (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (c - 'A' + 10);
            else fail("bad \\u");
        }
        return v;
    }

    // Always emits well-formed UTF-8. A lone surrogate or an out-of-range code
    // point has no valid encoding, so it becomes U+FFFD rather than the
    // invalid three-byte sequence that would otherwise reach MultiByteToWideChar.
    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }

    Value parseArray() {
        Value v; v.type = Type::Array;
        get(); // [
        if (++depth_ > kMaxDepth) fail("nesting too deep");
        skipWs();
        if (peek() == ']') { get(); --depth_; return v; }
        while (true) {
            v.arr.push_back(parseValue());
            skipWs();
            char c = get();
            if (c == ',') { skipWs(); continue; }
            if (c == ']') break;
            fail("expected , or ]");
        }
        --depth_;
        return v;
    }

    Value parseObject() {
        Value v; v.type = Type::Object;
        get(); // {
        if (++depth_ > kMaxDepth) fail("nesting too deep");
        skipWs();
        if (peek() == '}') { get(); --depth_; return v; }
        while (true) {
            skipWs();
            std::string key = parseString();
            skipWs();
            if (get() != ':') fail("expected :");
            Value val = parseValue();
            v.obj.emplace_back(std::move(key), std::move(val));
            skipWs();
            char c = get();
            if (c == ',') continue;
            if (c == '}') break;
            fail("expected , or }");
        }
        --depth_;
        return v;
    }
};

inline Value parse(const std::string& s) { return Parser(s).parse(); }

// ---------- Serializer ----------
inline void escape(std::ostream& o, const std::string& s) {
    o << '"';
    for (char c : s) {
        switch (c) {
            case '"':  o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\n': o << "\\n";  break;
            case '\r': o << "\\r";  break;
            case '\t': o << "\\t";  break;
            case '\b': o << "\\b";  break;
            case '\f': o << "\\f";  break;
            default:
                // JSON forbids raw control characters, and System.Text.Json
                // rejects the whole file over one. Compare as unsigned so
                // UTF-8 lead and continuation bytes pass through.
                if ((unsigned char)c < 0x20) {
                    static const char* kHex = "0123456789ABCDEF";
                    o << "\\u00" << kHex[(unsigned char)c >> 4] << kHex[c & 0xF];
                } else {
                    o << c;
                }
                break;
        }
    }
    o << '"';
}

inline void write(std::ostream& o, const Value& v, int indent = 0) {
    std::string pad(indent * 2, ' ');
    std::string pad2((indent + 1) * 2, ' ');
    switch (v.type) {
        case Type::Null:   o << "null"; break;
        case Type::Bool:   o << (v.b ? "true" : "false"); break;
        case Type::Number: {
            // Integer-valued numbers print without decimals.
            if (v.num == (long long)v.num) o << (long long)v.num;
            else o << v.num;
            break;
        }
        case Type::String: escape(o, v.str); break;
        case Type::Array:
            if (v.arr.empty()) { o << "[]"; break; }
            o << "[\n";
            for (size_t k = 0; k < v.arr.size(); ++k) {
                o << pad2; write(o, v.arr[k], indent + 1);
                if (k + 1 < v.arr.size()) o << ",";
                o << "\n";
            }
            o << pad << "]";
            break;
        case Type::Object:
            if (v.obj.empty()) { o << "{}"; break; }
            o << "{\n";
            for (size_t k = 0; k < v.obj.size(); ++k) {
                o << pad2; escape(o, v.obj[k].first); o << ": ";
                write(o, v.obj[k].second, indent + 1);
                if (k + 1 < v.obj.size()) o << ",";
                o << "\n";
            }
            o << pad << "}";
            break;
    }
}

inline std::string dump(const Value& v) {
    std::ostringstream o;
    write(o, v, 0);
    o << "\n";
    return o.str();
}

} // namespace json
