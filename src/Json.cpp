#include "Json.h"

#include <cstdio>
#include <cstdlib>

namespace
{
    const Json kNull;
    const std::string kEmpty;

    void AppendUtf8(std::string& out, unsigned code)
    {
        if (code < 0x80) out += (char)code;
        else if (code < 0x800) { out += (char)(0xC0 | (code >> 6)); out += (char)(0x80 | (code & 0x3F)); }
        else if (code < 0x10000)
        {
            out += (char)(0xE0 | (code >> 12));
            out += (char)(0x80 | ((code >> 6) & 0x3F));
            out += (char)(0x80 | (code & 0x3F));
        }
        else
        {
            out += (char)(0xF0 | (code >> 18));
            out += (char)(0x80 | ((code >> 12) & 0x3F));
            out += (char)(0x80 | ((code >> 6) & 0x3F));
            out += (char)(0x80 | (code & 0x3F));
        }
    }

    struct Parser
    {
        const std::string& s;
        size_t i = 0;
        bool ok = true;
        int depth = 0;

        void Space()
        {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
        }

        bool Literal(const char* word)
        {
            size_t n = 0;
            while (word[n]) ++n;
            if (s.compare(i, n, word) != 0) return false;
            i += n;
            return true;
        }

        unsigned Hex4()
        {
            if (i + 4 > s.size()) { ok = false; return 0; }
            const unsigned value = (unsigned)std::strtoul(s.substr(i, 4).c_str(), nullptr, 16);
            i += 4;
            return value;
        }

        std::string String()
        {
            std::string out;
            ++i; // "
            while (i < s.size() && s[i] != '"')
            {
                const char c = s[i++];
                if (c != '\\') { out += c; continue; }
                if (i >= s.size()) break;
                const char e = s[i++];
                switch (e)
                {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u':
                {
                    unsigned code = Hex4();
                    if (code >= 0xD800 && code < 0xDC00 && s.compare(i, 2, "\\u") == 0)
                    {
                        i += 2;
                        const unsigned low = Hex4();
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                    AppendUtf8(out, code);
                    break;
                }
                default: out += e; break; // " \ /
                }
            }
            if (i >= s.size()) ok = false;
            ++i; // "
            return out;
        }

        Json Value()
        {
            Json v;
            Space();
            if (i >= s.size() || ++depth > 64) { ok = false; return v; }
            const char c = s[i];
            if (c == '{')
            {
                v.type = Json::Type::Object;
                ++i;
                Space();
                if (i < s.size() && s[i] == '}') ++i;
                else
                {
                    while (ok)
                    {
                        Space();
                        if (i >= s.size() || s[i] != '"') { ok = false; break; }
                        std::string key = String();
                        Space();
                        if (i >= s.size() || s[i] != ':') { ok = false; break; }
                        ++i;
                        v.fields.emplace_back(std::move(key), Value());
                        Space();
                        if (i < s.size() && s[i] == ',') { ++i; continue; }
                        if (i < s.size() && s[i] == '}') { ++i; break; }
                        ok = false;
                    }
                }
            }
            else if (c == '[')
            {
                v.type = Json::Type::Array;
                ++i;
                Space();
                if (i < s.size() && s[i] == ']') ++i;
                else
                {
                    while (ok)
                    {
                        v.items.push_back(Value());
                        Space();
                        if (i < s.size() && s[i] == ',') { ++i; continue; }
                        if (i < s.size() && s[i] == ']') { ++i; break; }
                        ok = false;
                    }
                }
            }
            else if (c == '"')
            {
                v.type = Json::Type::String;
                v.text = String();
            }
            else if (Literal("true")) { v.type = Json::Type::Bool; v.boolean = true; }
            else if (Literal("false")) { v.type = Json::Type::Bool; }
            else if (Literal("null")) {}
            else
            {
                char* end = nullptr;
                v.number = std::strtod(s.c_str() + i, &end);
                if (end == s.c_str() + i) ok = false;
                else { v.type = Json::Type::Number; i = (size_t)(end - s.c_str()); }
            }
            --depth;
            return v;
        }
    };
}

const Json& Json::operator[](const char* key) const
{
    for (const auto& field : fields)
    {
        if (field.first == key) return field.second;
    }
    return kNull;
}

const Json& Json::operator[](size_t index) const
{
    return index < items.size() ? items[index] : kNull;
}

bool Json::Has(const char* key) const
{
    for (const auto& field : fields)
    {
        if (field.first == key) return true;
    }
    return false;
}

const std::string& Json::Str() const
{
    return type == Type::String ? text : kEmpty;
}

Json Json::Parse(const std::string& source, bool* ok)
{
    Parser parser{ source };
    Json value = parser.Value();
    if (ok) *ok = parser.ok;
    return parser.ok ? value : Json();
}

std::string JsonQuote(const std::string& value)
{
    std::string out = "\"";
    for (const unsigned char c : value)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20)
            {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                out += buffer;
            }
            else
            {
                out += (char)c;
            }
        }
    }
    return out + "\"";
}
