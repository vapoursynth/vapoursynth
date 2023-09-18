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
#include "clocale"

static bool isAsciiPrintable(const std::string &s) {
    for (const auto c : s)
        if (c < 0x20 || c > 0x7E)
            return false;
    return true;
}

static std::string doubleToString(double v) {
    std::string result = std::to_string(v);
    char point = *localeconv()->decimal_point;
    if (point != '.') {
        size_t pos = result.find(point);
        if (pos != std::string::npos)
            result[pos] = '.';
    }
    return result;
}

std::string escapeJSONString(const std::string &s) {
    std::string result;
    result.reserve(s.length() * 2 + 2);
    for (auto c : s) {
        if (c == '\\')
            result += "\\\\";
        else if (c == '\0')
            result += "\\x00";
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
        else if (c == '\v')
            result += "\\v";
        else if (c == '"')
            result += "\\\"";
        else
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
                    jsonStr += (j ? ", " : "");
                    if (typeHint == dtUtf8 || (typeHint == dtUnknown && vsapi->mapGetDataSize(map, key, j, nullptr) < 200 && isAsciiPrintable(std::string(vsapi->mapGetData(map, key, j, nullptr), vsapi->mapGetDataSize(map, key, j, nullptr)))))
                        jsonStr += escapeJSONString(vsapi->mapGetData(map, key, j, nullptr));
                    else
                        jsonStr += "\"[binary data size: " + std::to_string(vsapi->mapGetDataSize(map, key, j, nullptr)) + "]\"";
                }
                break;
            }

            if (numElems > 1)
                jsonStr += "]";
        }
    }
    jsonStr += "}";
    return jsonStr;
}
