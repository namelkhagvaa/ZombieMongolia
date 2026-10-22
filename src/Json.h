#pragma once

#include <string>
#include <utility>
#include <vector>

// Жижиг JSON утга ба задлагч: backend-ийн (Django API) хариуг уншихад. Объект, массив, тэмдэгт мөр (\uXXXX-ийг
// UTF-8 болгоно), тоо, true/false, null. Байхгүй түлхүүр, индексэд хоосон (Null) утга буцаана.
struct Json
{
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> fields;

    const Json& operator[](const char* key) const;
    const Json& operator[](size_t index) const;
    bool Has(const char* key) const;
    size_t Size() const { return type == Type::Array ? items.size() : fields.size(); }

    int Int(int fallback = 0) const { return type == Type::Number ? (int)number : fallback; }
    bool Bool() const { return type == Type::Bool ? boolean : false; }
    const std::string& Str() const;

    // Алдаатай бол ok = false, Null утга.
    static Json Parse(const std::string& source, bool* ok = nullptr);
};

// Хүсэлтийн биед: тэмдэгт мөрийг JSON-ийн "..." болгоно.
std::string JsonQuote(const std::string& value);
