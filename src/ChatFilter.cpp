#include "ChatFilter.h"

#include <cstdint>
#include <vector>

namespace
{
    enum class Match
    {
        Word,     // бүтэн үг л ("хуй", гэхдээ "хуйх" биш)
        Prefix,   // үгийн эхлэл ("гичий", "гичийнүүд")
        Contains  // үгийн хаана ч ("пизд", "fuck": нийлмэл үгэнд ч); хэллэг (зайтай) ч болно
    };

    struct Bad
    {
        const char* text; // жижиг үсгээр
        Match match;
    };

    // Ганц тусгай үг биш, нийтлэг хараал, доромжлол. Энгийн үгтэй давхцахаас сэргийлж богино үгсийг Word-оор шүүнэ.
    const Bad kBad[] = {
        // Монгол, орос (кирилл)
        { "пизд", Match::Contains }, { "пзд", Match::Contains }, { "хуй", Match::Word }, { "хуйн", Match::Word },
        { "хуйл", Match::Word }, { "хуйгаа", Match::Word }, { "бля", Match::Word }, { "блят", Match::Contains },
        { "бляд", Match::Contains }, { "сука", Match::Prefix }, { "сучк", Match::Contains }, { "ебан", Match::Contains },
        { "ёбан", Match::Contains }, { "ебат", Match::Contains }, { "ебал", Match::Contains }, { "уеба", Match::Contains },
        { "гичий", Match::Prefix }, { "янхан", Match::Prefix }, { "новш", Match::Prefix }, { "өлөгчин", Match::Prefix },
        { "шээс", Match::Word }, { "баас", Match::Word }, { "пидор", Match::Contains }, { "педик", Match::Contains },
        { "ээжийг чинь", Match::Contains }, { "эхийг чинь", Match::Contains }, { "ээжийн чинь", Match::Contains },
        { "эхийн чинь", Match::Contains },
        // Англи
        { "fuck", Match::Contains }, { "fck", Match::Word }, { "shit", Match::Prefix }, { "bitch", Match::Prefix },
        { "cunt", Match::Contains }, { "dick", Match::Word }, { "dicks", Match::Word }, { "cock", Match::Word },
        { "pussy", Match::Prefix }, { "whore", Match::Prefix }, { "slut", Match::Prefix }, { "bastard", Match::Prefix },
        { "asshole", Match::Contains }, { "nigg", Match::Contains }, { "fagg", Match::Contains }, { "fag", Match::Word },
        { "retard", Match::Prefix }, { "motherf", Match::Contains }, { "wtf", Match::Word }, { "stfu", Match::Word },
    };

    std::vector<uint32_t> Decode(const std::string& text)
    {
        std::vector<uint32_t> out;
        for (size_t i = 0; i < text.size();)
        {
            const unsigned char c = (unsigned char)text[i];
            uint32_t code = c;
            size_t length = 1;
            if (c >= 0xF0) { code = c & 0x07; length = 4; }
            else if (c >= 0xE0) { code = c & 0x0F; length = 3; }
            else if (c >= 0xC0) { code = c & 0x1F; length = 2; }
            if (i + length > text.size()) { code = '?'; length = 1; }
            for (size_t k = 1; k < length; ++k) code = (code << 6) | ((unsigned char)text[i + k] & 0x3F);
            out.push_back(code);
            i += length;
        }
        return out;
    }

    std::string Encode(const std::vector<uint32_t>& codes)
    {
        std::string out;
        for (uint32_t c : codes)
        {
            if (c < 0x80) out += (char)c;
            else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
            else if (c < 0x10000)
            {
                out += (char)(0xE0 | (c >> 12));
                out += (char)(0x80 | ((c >> 6) & 0x3F));
                out += (char)(0x80 | (c & 0x3F));
            }
            else
            {
                out += (char)(0xF0 | (c >> 18));
                out += (char)(0x80 | ((c >> 12) & 0x3F));
                out += (char)(0x80 | ((c >> 6) & 0x3F));
                out += (char)(0x80 | (c & 0x3F));
            }
        }
        return out;
    }

    uint32_t Lower(uint32_t c)
    {
        if (c >= 'A' && c <= 'Z') return c + 32;
        if (c >= 0x410 && c <= 0x42F) return c + 0x20; // А-Я
        if (c >= 0x400 && c <= 0x40F) return c + 0x50; // Ё ...
        if (c == 0x4E8) return 0x4E9;                  // Ө
        if (c == 0x4AE) return 0x4AF;                  // Ү
        return c;
    }

    bool Letter(uint32_t c)
    {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c >= 0x430 && c <= 0x4FF);
    }
}

std::string FilterChat(const std::string& text)
{
    std::vector<uint32_t> codes = Decode(text);
    std::vector<uint32_t> lower(codes.size());
    for (size_t i = 0; i < codes.size(); ++i) lower[i] = Lower(codes[i]);
    std::vector<bool> hide(codes.size(), false);

    for (const Bad& bad : kBad)
    {
        const std::vector<uint32_t> word = Decode(bad.text);
        if (word.empty() || word.size() > lower.size()) continue;
        for (size_t at = 0; at + word.size() <= lower.size(); ++at)
        {
            bool same = true;
            for (size_t k = 0; k < word.size() && same; ++k) same = lower[at + k] == word[k];
            if (!same) continue;
            const bool startsWord = at == 0 || !Letter(lower[at - 1]);
            const size_t end = at + word.size();
            const bool endsWord = end == lower.size() || !Letter(lower[end]);
            if (bad.match == Match::Word && !(startsWord && endsWord)) continue;
            if (bad.match == Match::Prefix && !startsWord) continue;
            // Бүтэн үгийг (хэллэг бол хэллэгийг) нууна
            size_t from = at, to = end;
            while (from > 0 && Letter(lower[from - 1])) --from;
            while (to < lower.size() && Letter(lower[to])) ++to;
            for (size_t k = from; k < to; ++k) hide[k] = hide[k] || codes[k] != ' ';
        }
    }
    for (size_t i = 0; i < codes.size(); ++i)
    {
        if (hide[i]) codes[i] = '*';
    }
    return Encode(codes);
}
