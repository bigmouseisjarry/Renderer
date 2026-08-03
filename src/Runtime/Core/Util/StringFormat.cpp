#include <sstream>
#include <iomanip>
#include <bitset>
#include "StringFormat.h"

static const char HEX_CHARS_UPPER[] = "0123456789ABCDEF";
static const char HEX_CHARS_LOWER[] = "0123456789abcdef";

std::string ToHex(const uint8_t *data, size_t len, bool upper, char separator)
{
    const char *hex_chars = upper ? HEX_CHARS_UPPER : HEX_CHARS_LOWER;

    std::string output;
    output.reserve(3 * len);
    for (size_t i = 0; i < len; i++)
    {
        const char temp = data[i];
        output.push_back(hex_chars[temp / 16]);
        output.push_back(hex_chars[temp % 16]);
        output.push_back(separator);
    }

    return output;
}

std::string ToHex(uint8_t value, bool upper, const std::string &prefix)
{
    const char *hex_chars = upper ? HEX_CHARS_UPPER : HEX_CHARS_LOWER;
    std::string text = prefix;
    int c1 = value / 16;
    int c2 = value % 16;
    text.push_back(hex_chars[c1]);
    text.push_back(hex_chars[c2]);
    return text;
}
std::string ToHex(uint16_t value, bool upper, const std::string &prefix)
{
    std::string text = prefix;
    text += ToHex((uint8_t)((value >> 8) & 0xFF), upper);
    text += ToHex((uint8_t)(value & 0xFF), upper);
    return text;
}

std::string ToHex(uint32_t value, bool upper, const std::string &prefix)
{
    std::string text = prefix;
    text += ToHex((uint8_t)((value >> 24) & 0xFF), upper);
    text += ToHex((uint8_t)((value >> 16) & 0xFF), upper);
    text += ToHex((uint8_t)((value >> 8) & 0xFF), upper);
    text += ToHex((uint8_t)(value & 0xFF), upper);
    return text;
}

std::string ToHex(uint64_t value, bool upper, const std::string &prefix)
{
    std::string text = prefix;
    text += ToHex((uint8_t)((value >> 56) & 0xFF), upper);
    text += ToHex((uint8_t)((value >> 48) & 0xFF), upper);
    text += ToHex((uint8_t)((value >> 40) & 0xFF), upper);
    text += ToHex((uint8_t)((value >> 32) & 0xFF), upper);
    text += ToHex((uint8_t)((value >> 24) & 0xFF), upper);
    text += ToHex((uint8_t)((value >> 16) & 0xFF), upper);
    text += ToHex((uint8_t)((value >> 8) & 0xFF), upper);
    text += ToHex((uint8_t)(value & 0xFF), upper);
    return text;
}
