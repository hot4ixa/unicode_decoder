#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <Windows.h>

enum Encoding {
    ENC_UTF8_BOM,
    ENC_UTF16_LE,
    ENC_UTF16_BE,
    ENC_UTF32_LE,
    ENC_UTF32_BE,
    ENC_NO_BOM,
    ENC_UNKNOWN
};

Encoding detect_encoding(const std::vector<unsigned char>& d) {
    // Определение кодировки по маркеру последовательности байтов
    if (d.size() >= 3 &&
        d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) return Encoding::ENC_UTF8_BOM;

    if (d.size() >= 2 && d[0] == 0xFF && d[1] == 0xFE) return Encoding::ENC_UTF16_LE;

    if (d.size() >= 2 && d[0] == 0xFE && d[1] == 0xFF) return Encoding::ENC_UTF16_BE;

    if (d.size() >= 4 && d[0] == 0xFF && d[1] == 0xFE
        && d[2] == 0x00 && d[3] == 0x00) return Encoding::ENC_UTF32_LE;

    if (d.size() >= 4 && d[0] == 0x00 && d[1] == 0x00
        && d[2] == 0xFE && d[3] == 0xFF) return Encoding::ENC_UTF32_BE;

    return Encoding::ENC_UNKNOWN;
}

bool is_valid_utf8(const std::vector<unsigned char>& data)
{
    size_t i = 0;
    while (i < data.size())
    {
        unsigned char c = data[i];

        // ASCII
        if (c <= 0x7F) { i++; continue; }

        int bytes = 0;
        if ((c & 0xE0) == 0xC0) bytes = 2;      // 110xxxxx
        else if ((c & 0xF0) == 0xE0) bytes = 3; // 1110xxxx
        else if ((c & 0xF8) == 0xF0) bytes = 4; // 11110xxx
        else return false;                      // не начало UTF-8

        if (i + bytes > data.size()) return false;

        for (int j = 1; j < bytes; j++)
        {
            if ((data[i + j] & 0xC0) != 0x80)
                return false;
        }

        i += bytes;
    }
    return true;
}

void append_utf8(std::string& out, char32_t cp)
{
    if (cp <= 0x7F) // 1 байт 0xxxxxxx
    {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp <= 0x7FF) // 2 байта 110xxxxx 10xxxxxx
    {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0xFFFF) // 3 байта 1110xxxx 10xxxxxx 10xxxxxx
    {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else // 4 байта 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

char16_t read16_le(const unsigned char* p)
{
    return p[0] | (p[1] << 8);
}

char16_t read16_be(const unsigned char* p)
{
    return (p[0] << 8) | p[1];
}

std::string utf16_to_utf8(const std::vector<unsigned char>& data,
    bool isLE,
    int offset)
{
    std::string out;

    for (size_t i = offset; i + 1 < data.size(); i += 2)
    {
        char16_t w = isLE ? read16_le(&data[i]) : read16_be(&data[i]);

        if (w >= 0xD800 && w <= 0xDBFF)
        {
            if (i + 3 >= data.size()) break;

            char16_t w2 = isLE
                ? read16_le(&data[i + 2])
                : read16_be(&data[i + 2]);

            if (w2 < 0xDC00 || w2 > 0xDFFF)
            {
                continue;
            }

            char32_t cp = 0x10000 + (((w - 0xD800) << 10) | (w2 - 0xDC00));

            append_utf8(out, cp);
            i += 2;
        }
        else
        {
            append_utf8(out, w);
        }
    }

    return out;
}

char32_t read32_le(const unsigned char* p)
{
    return  (char32_t)p[0]
        | ((char32_t)p[1] << 8)
        | ((char32_t)p[2] << 16)
        | ((char32_t)p[3] << 24);
}

char32_t read32_be(const unsigned char* p)
{
    return  ((char32_t)p[0] << 24)
        | ((char32_t)p[1] << 16)
        | ((char32_t)p[2] << 8)
        | (char32_t)p[3];
}

std::string utf32_to_utf8(const std::vector<unsigned char>& data,
    bool isLE,
    int offset)
{
    std::string out;

    for (size_t i = offset; i + 3 < data.size(); i += 4) {
        char32_t cp = isLE ? read32_le(&data[i]) : read32_be(&data[i]);
        append_utf8(out, cp);
    }

    return out;
}

std::string convert_to_utf8(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());

    if (d.empty()) return "";

    Encoding enc = detect_encoding(d);

    switch (enc) {
    case Encoding::ENC_UTF8_BOM:
        return std::string(d.begin() + 3, d.end());

    case Encoding::ENC_UTF16_LE:
        return utf16_to_utf8(d, true, 2);

    case Encoding::ENC_UTF16_BE:
        return utf16_to_utf8(d, false, 2);

    case Encoding::ENC_UTF32_LE:
        return utf32_to_utf8(d, true, 4);

    case Encoding::ENC_UTF32_BE:
        return utf32_to_utf8(d, false, 4);

    default:
        // UTF-8 без BOM или ANSI
        return std::string(d.begin(), d.end());
    }
}

int main(int argc, char** argv)
{
    /*if (argc != 2)
    {
        std::cout << "Usage: $programName <path>\n";
        return 1;
    }*/

    std::string root = "S:\\!MY_FOLDER\\decoding_tests";//argv[1];

    namespace fs = std::filesystem;

    int converted = 0;

    for (auto& p : fs::recursive_directory_iterator(root))
    {
        if (!p.is_regular_file()) continue;

        std::string ext = p.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext != ".cpp" && ext != ".c" && ext != ".h") continue;

        std::cout << p.path() << "\n";

        std::string out = convert_to_utf8(p.path().string());

        std::ofstream ofs(p.path(), std::ios::binary);
        ofs.write(out.data(), out.size());
        ofs.close();

        converted++;
    }

    std::cout << "\n=== Statistics ===\n";
    std::cout << "Converted:         " << converted << "\n";

    return 0;
}
