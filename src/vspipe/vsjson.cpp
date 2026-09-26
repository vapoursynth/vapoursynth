/*
* Copyright (c) 2023 Fredrik Mellbin
*
* This file is part of VapourSynth.
*
* VapourSynth is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* VapourSynth is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with VapourSynth; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include "vsjson.h"
#include <charconv>
#include <cmath>
#include <system_error>

static bool isAsciiPrintable(const std::string &s) {
    for (const auto c : s)
        if (c < 0x20 || c > 0x7E)
            return false;
    return true;
}

/* JSON has to be UTF-8, and a utf8 hint on a value is a promise the core never checks. */
static bool isValidUtf8(const std::string &s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t cont;
        unsigned char lo = 0x80, hi = 0xBF;
        if (c < 0x80) { i++; continue; }
        else if (c >= 0xC2 && c <= 0xDF) cont = 1;
        else if (c == 0xE0) { cont = 2; lo = 0xA0; }
        else if (c >= 0xE1 && c <= 0xEC) cont = 2;
        else if (c == 0xED) { cont = 2; hi = 0x9F; }
        else if (c >= 0xEE && c <= 0xEF) cont = 2;
        else if (c == 0xF0) { cont = 3; lo = 0x90; }
        else if (c >= 0xF1 && c <= 0xF3) cont = 3;
        else if (c == 0xF4) { cont = 3; hi = 0x8F; }
        else return false;
        if (i + cont >= s.size())
            return false;
        for (size_t k = 1; k <= cont; k++) {
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if (cc < (k == 1 ? lo : 0x80) || cc > (k == 1 ? hi : 0xBF))
                return false;
        }
        i += cont + 1;
    }
    return true;
}

static std::string doubleToString(double v) {
    /* JSON has no way to write these as numbers, and emitting them bare produces a file no
       parser accepts, so they turn into null. */
    if (!std::isfinite(v))
        return "null";
    char buffer[64];
    auto res = std::to_chars(buffer, buffer + sizeof(buffer), v);
    if (res.ec != std::errc())
        return "null";
    return std::string(buffer, res.ptr - buffer);
}

std::string escapeJSONString(const std::string &s) {
    std::string result;
    result.reserve(s.length() * 2 + 2);
    for (auto c : s) {
        if (c == '\\')
            result += "\\\\";
        else if (c == '\b')
            result += "\\b";
        else if (c == '\f')
            result += "\\f";
        else if (c == '\n')
            result += "\\n";
        else if (c == '\r')
            result += "\\r";
        else if (c == '\t')
            result += "\\t";
        else if (c == '"')
            result += "\\\"";
        else if (static_cast<unsigned char>(c) < 0x20) {
            static const char hex[] = "0123456789abcdef";
            unsigned char uc = static_cast<unsigned char>(c);
            result += "\\u00";
            result += hex[(uc >> 4) & 0xF];
            result += hex[uc & 0xF];
        } else
            result += c;
    }
    return "\"" + result + "\"";
}

std::string convertVSMapToJSON(const VSMap *map, const VSAPI *vsapi) {
    int numKeys = vsapi->mapNumKeys(map);
    std::string jsonStr = "{";
    for (int i = 0; i < numKeys; i++) {
        const char *key = vsapi->mapGetKey(map, i);
        int numElems = vsapi->mapNumElements(map, key);

        if (i)
            jsonStr += ", ";
        jsonStr += escapeJSONString(key) + ": ";
        
        if (numElems == 0) {
            jsonStr += "null";
        } else {
            if (numElems > 1)
                jsonStr += "[";

            switch (vsapi->mapGetType(map, key)) {
            case ptInt:
                for (int j = 0; j < numElems; j++)
                    jsonStr += (j ? ", " : "") + std::to_string(vsapi->mapGetInt(map, key, j, nullptr));
                break;
            case ptFloat:
                for (int j = 0; j < numElems; j++)
                    jsonStr += (j ? ", " : "") + doubleToString(vsapi->mapGetFloat(map, key, j, nullptr));
                break;
            case ptData:
                for (int j = 0; j < numElems; j++) {
                    int typeHint = vsapi->mapGetDataTypeHint(map, key, j, nullptr);
                    std::string data(vsapi->mapGetData(map, key, j, nullptr), vsapi->mapGetDataSize(map, key, j, nullptr));
                    jsonStr += (j ? ", " : "");
                    if ((typeHint == dtUtf8 && isValidUtf8(data)) || (typeHint == dtUnknown && data.size() < 200 && isAsciiPrintable(data)))
                        jsonStr += escapeJSONString(data);
                    else
                        jsonStr += "\"[binary data size: " + std::to_string(data.size()) + "]\"";
                }
                break;
            default:
                for (int j = 0; j < numElems; j++)
                    jsonStr += (j ? ", " : "") + std::string("\"[unrepresentable type]\"");
                break;
            }

            if (numElems > 1)
                jsonStr += "]";
        }
    }
    jsonStr += "}";
    return jsonStr;
}
