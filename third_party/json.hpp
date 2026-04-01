/**
 * Minimal JSON builder/parser for the Robot Simulator project.
 * Supports: string, int, double, bool, null, nested objects, arrays.
 * Interface is intentionally similar to nlohmann/json for easy migration.
 */
#pragma once
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <algorithm>

namespace nlohmann {

class json {
public:
    using object_t = std::map<std::string, json>;
    using array_t  = std::vector<json>;
    using value_t  = std::variant<
        std::monostate,   // null
        bool,
        int64_t,
        double,
        std::string,
        array_t,
        object_t
    >;

    json() : value_(std::monostate{}) {}
    json(std::nullptr_t) : value_(std::monostate{}) {}
    json(bool v) : value_(v) {}
    json(int v)      : value_(static_cast<int64_t>(v)) {}
    json(int64_t v)  : value_(v) {}
    json(double v) : value_(v) {}
    json(const char* v) : value_(std::string(v)) {}
    json(const std::string& v) : value_(v) {}
    json(std::string&& v) : value_(std::move(v)) {}
    json(const array_t& v) : value_(v) {}
    json(const object_t& v) : value_(v) {}

    // initializer_list for object {"key", val, "key2", val2, ...}
    // We use a helper pair approach instead
    static json object() { json j; j.value_ = object_t{}; return j; }
    static json array()  { json j; j.value_ = array_t{};  return j; }

    // Array push
    void push_back(const json& v) {
        if (std::holds_alternative<std::monostate>(value_))
            value_ = array_t{};
        std::get<array_t>(value_).push_back(v);
    }

    // Object access
    json& operator[](const std::string& key) {
        if (std::holds_alternative<std::monostate>(value_))
            value_ = object_t{};
        return std::get<object_t>(value_)[key];
    }
    const json& operator[](const std::string& key) const {
        return std::get<object_t>(value_).at(key);
    }
    json& operator[](size_t idx) {
        return std::get<array_t>(value_)[idx];
    }
    const json& operator[](size_t idx) const {
        return std::get<array_t>(value_)[idx];
    }

    bool contains(const std::string& key) const {
        if (!std::holds_alternative<object_t>(value_)) return false;
        return std::get<object_t>(value_).count(key) > 0;
    }

    size_t size() const {
        if (std::holds_alternative<array_t>(value_))
            return std::get<array_t>(value_).size();
        if (std::holds_alternative<object_t>(value_))
            return std::get<object_t>(value_).size();
        return 0;
    }

    // Type checks
    bool is_null()   const { return std::holds_alternative<std::monostate>(value_); }
    bool is_bool()   const { return std::holds_alternative<bool>(value_); }
    bool is_number() const { return std::holds_alternative<int64_t>(value_) || std::holds_alternative<double>(value_); }
    bool is_string() const { return std::holds_alternative<std::string>(value_); }
    bool is_array()  const { return std::holds_alternative<array_t>(value_); }
    bool is_object() const { return std::holds_alternative<object_t>(value_); }

    // Value getters
    template<typename T> T get() const;

    std::string dump(int indent = -1) const {
        std::ostringstream os;
        dump_to(os, indent, 0);
        return os.str();
    }

    // Simple parser
    static json parse(const std::string& s) {
        size_t pos = 0;
        return parse_value(s, pos);
    }

    // Iterator support for arrays/objects
    auto begin() {
        if (is_array()) return std::get<array_t>(value_).begin();
        throw std::runtime_error("not an array");
    }
    auto end() {
        if (is_array()) return std::get<array_t>(value_).end();
        throw std::runtime_error("not an array");
    }

private:
    value_t value_;

    // ---- serialization ----
    void dump_to(std::ostringstream& os, int indent, int depth) const {
        std::string nl   = (indent >= 0) ? "\n" : "";
        std::string pad  = (indent >= 0) ? std::string((depth+1)*indent, ' ') : "";
        std::string cpad = (indent >= 0) ? std::string(depth*indent, ' ') : "";
        std::string sep  = (indent >= 0) ? " " : "";

        if (std::holds_alternative<std::monostate>(value_)) {
            os << "null";
        } else if (std::holds_alternative<bool>(value_)) {
            os << (std::get<bool>(value_) ? "true" : "false");
        } else if (std::holds_alternative<int64_t>(value_)) {
            os << std::get<int64_t>(value_);
        } else if (std::holds_alternative<double>(value_)) {
            os << std::fixed << std::setprecision(6) << std::get<double>(value_);
        } else if (std::holds_alternative<std::string>(value_)) {
            os << '"';
            for (char c : std::get<std::string>(value_)) {
                if (c == '"')  os << "\\\"";
                else if (c == '\\') os << "\\\\";
                else if (c == '\n') os << "\\n";
                else if (c == '\r') os << "\\r";
                else if (c == '\t') os << "\\t";
                else os << c;
            }
            os << '"';
        } else if (std::holds_alternative<array_t>(value_)) {
            const auto& arr = std::get<array_t>(value_);
            os << '[';
            for (size_t i = 0; i < arr.size(); ++i) {
                if (indent >= 0) os << nl << pad;
                arr[i].dump_to(os, indent, depth+1);
                if (i+1 < arr.size()) os << ',';
            }
            if (indent >= 0 && !arr.empty()) os << nl << cpad;
            os << ']';
        } else if (std::holds_alternative<object_t>(value_)) {
            const auto& obj = std::get<object_t>(value_);
            os << '{';
            size_t i = 0;
            for (const auto& [k, v] : obj) {
                if (indent >= 0) os << nl << pad;
                os << '"' << k << '"' << ':' << sep;
                v.dump_to(os, indent, depth+1);
                if (i+1 < obj.size()) os << ',';
                ++i;
            }
            if (indent >= 0 && !obj.empty()) os << nl << cpad;
            os << '}';
        }
    }

    // ---- parser helpers ----
    static void skip_ws(const std::string& s, size_t& pos) {
        while (pos < s.size() && std::isspace((unsigned char)s[pos])) ++pos;
    }

    static json parse_value(const std::string& s, size_t& pos) {
        skip_ws(s, pos);
        if (pos >= s.size()) throw std::runtime_error("unexpected end");
        char c = s[pos];
        if (c == '"')  return parse_string(s, pos);
        if (c == '{')  return parse_object(s, pos);
        if (c == '[')  return parse_array(s, pos);
        if (c == 't')  { pos += 4; return json(true); }
        if (c == 'f')  { pos += 5; return json(false); }
        if (c == 'n')  { pos += 4; return json(nullptr); }
        return parse_number(s, pos);
    }

    static std::string parse_raw_string(const std::string& s, size_t& pos) {
        ++pos; // skip opening "
        std::string result;
        while (pos < s.size() && s[pos] != '"') {
            if (s[pos] == '\\') {
                ++pos;
                switch (s[pos]) {
                    case '"':  result += '"'; break;
                    case '\\': result += '\\'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    default:   result += s[pos]; break;
                }
            } else {
                result += s[pos];
            }
            ++pos;
        }
        ++pos; // skip closing "
        return result;
    }

    static json parse_string(const std::string& s, size_t& pos) {
        return json(parse_raw_string(s, pos));
    }

    static json parse_object(const std::string& s, size_t& pos) {
        ++pos; // skip {
        json obj = json::object();
        skip_ws(s, pos);
        while (pos < s.size() && s[pos] != '}') {
            skip_ws(s, pos);
            std::string key = parse_raw_string(s, pos);
            skip_ws(s, pos);
            ++pos; // skip :
            json val = parse_value(s, pos);
            obj[key] = val;
            skip_ws(s, pos);
            if (pos < s.size() && s[pos] == ',') ++pos;
            skip_ws(s, pos);
        }
        ++pos; // skip }
        return obj;
    }

    static json parse_array(const std::string& s, size_t& pos) {
        ++pos; // skip [
        json arr = json::array();
        skip_ws(s, pos);
        while (pos < s.size() && s[pos] != ']') {
            arr.push_back(parse_value(s, pos));
            skip_ws(s, pos);
            if (pos < s.size() && s[pos] == ',') ++pos;
            skip_ws(s, pos);
        }
        ++pos; // skip ]
        return arr;
    }

    static json parse_number(const std::string& s, size_t& pos) {
        size_t start = pos;
        bool is_float = false;
        if (pos < s.size() && s[pos] == '-') ++pos;
        while (pos < s.size() && std::isdigit((unsigned char)s[pos])) ++pos;
        if (pos < s.size() && s[pos] == '.') { is_float = true; ++pos; }
        while (pos < s.size() && std::isdigit((unsigned char)s[pos])) ++pos;
        if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
            is_float = true; ++pos;
            if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) ++pos;
            while (pos < s.size() && std::isdigit((unsigned char)s[pos])) ++pos;
        }
        std::string num = s.substr(start, pos - start);
        if (is_float) return json(std::stod(num));
        return json(static_cast<int64_t>(std::stoll(num)));
    }
};

// Template specializations for get<>
template<> inline bool        json::get<bool>()        const { return std::get<bool>(value_); }
template<> inline int         json::get<int>()         const { return static_cast<int>(std::get<int64_t>(value_)); }
template<> inline int64_t     json::get<int64_t>()     const { return std::get<int64_t>(value_); }
template<> inline double      json::get<double>()      const {
    if (std::holds_alternative<int64_t>(value_)) return static_cast<double>(std::get<int64_t>(value_));
    return std::get<double>(value_);
}
template<> inline std::string json::get<std::string>() const { return std::get<std::string>(value_); }

} // namespace nlohmann
