/*
* Copyright (c) 2020 Fredrik Mellbin
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

#include "printgraph.h"
#include <set>
#include <map>
#include <algorithm>
#include <cstring>
#include <climits>

static std::string mangleNode(VSNodeRef *node, const VSAPI *vsapi) {
    return "n" + std::to_string(reinterpret_cast<uintptr_t>(node)) + "_" + std::to_string(vsapi->getNodeIndex(node));
}

static std::string mangleFrame(VSNodeRef *node, int level, const VSAPI *vsapi) {
    return "s" + std::to_string(reinterpret_cast<uintptr_t>(vsapi->getNodeCreationFunctionArguments(node, level)));
}

static int getMaxLevel(VSNodeRef *node, const VSAPI *vsapi) {
    for (int i = 0; i < INT_MAX; i++) {
        if (!vsapi->getNodeCreationFunctionArguments(node, i))
            return i - 1;
    }
    return 0;
}

static int getMinRealLevel(VSNodeRef *node, const VSAPI *vsapi) {
    int level = 0;
    while (vsapi->getNodeCreationFunctionArguments(node, level) && vsapi->getNodeCreationFunctionName(node, level) && !strcmp(vsapi->getNodeCreationFunctionName(node, level), ""))
        level++;
    return level;
}

static std::string printVSMap(const VSMap *args, int maxPrintLength, const VSAPI *vsapi) {
    int numKeys = vsapi->mapNumKeys(args);
    std::string setArgsStr;
    for (int i = 0; i < numKeys; i++) {
        const char *key = vsapi->mapGetKey(args, i);
        int numElems = vsapi->mapNumElements(args, key);

        setArgsStr += "\\n";
        setArgsStr += key;
        setArgsStr += "=";

        switch (vsapi->mapGetType(args, key)) {
            case ptInt:
                for (int j = 0; j < std::min(maxPrintLength, numElems); j++)
                    setArgsStr += (j ? ", " : "") + std::to_string(vsapi->mapGetInt(args, key, j, nullptr));
                if (numElems > maxPrintLength)
                    setArgsStr += ", <" + std::to_string(numElems - maxPrintLength) + ">";
                break;
            case ptFloat:
                for (int j = 0; j < std::min(maxPrintLength, numElems); j++)
                    setArgsStr += (j ? ", " : "") + std::to_string(vsapi->mapGetFloat(args, key, j, nullptr));
                if (numElems > maxPrintLength)
                    setArgsStr += ", <" + std::to_string(numElems - maxPrintLength) + ">";
                break;
            case ptData:
                for (int j = 0; j < std::min(maxPrintLength, numElems); j++)
                    setArgsStr += std::string(j ? ", " : "") + (vsapi->mapGetDataType(args, key, j, nullptr) == dtUtf8 ? vsapi->mapGetData(args, key, j, nullptr) : ("[binary data " + std::to_string(vsapi->mapGetDataSize(args, key, j, nullptr)) + " bytes]"));
                if (numElems > maxPrintLength)
                    setArgsStr += ", <" + std::to_string(numElems - maxPrintLength) + ">";
                break;
            case ptVideoNode:
                for (int j = 0; j < std::min(maxPrintLength, numElems); j++) {
                    VSNodeRef *ref = vsapi->mapGetNode(args, key, j, nullptr);
                    const VSVideoInfo *vi = vsapi->getVideoInfo(ref);
                    char formatName[32];
                    vsapi->getVideoFormatName(&vi->format, formatName);
                    setArgsStr += (j ? ", [" : " [") + std::string(formatName) + ":" + std::to_string(vi->width) + "x" + std::to_string(vi->height) + "]";
                    vsapi->freeNode(ref);
                }
                if (numElems > maxPrintLength)
                    setArgsStr += ", <" + std::to_string(numElems - maxPrintLength) + ">";
                break;
            case ptAudioNode:
                for (int j = 0; j < std::min(maxPrintLength, numElems); j++) {
                    VSNodeRef *ref = vsapi->mapGetNode(args, key, j, nullptr);
                    const VSAudioInfo *ai = vsapi->getAudioInfo(ref);
                    char formatName[32];
                    vsapi->getAudioFormatName(&ai->format, formatName);
                    setArgsStr += (j ? ", [" : " [") + std::string(formatName) + ":" + std::to_string(ai->sampleRate) + ":" + std::to_string(ai->format.channelLayout) + "]";
                    vsapi->freeNode(ref);
                }
                if (numElems > maxPrintLength)
                    setArgsStr += ", <" + std::to_string(numElems - maxPrintLength) + ">";
                break;
            default:
                setArgsStr += "<" + std::to_string(numElems) + ">";
        }
    }
    return setArgsStr;
}

static void printNodeGraphHelper(std::set<std::string> &lines, std::map<std::string, std::set<std::string>> &nodes, std::set<VSNodeRef *> &visited, VSNodeRef *node, const VSAPI *vsapi) {
    if (!visited.insert(node).second)
        return;

    int maxLevel = getMaxLevel(node, vsapi);
    int minRealLevel = getMinRealLevel(node, vsapi);

    std::string setArgsStr = printVSMap(vsapi->getNodeCreationFunctionArguments(node, minRealLevel), 5, vsapi);

    std::string thisNode = mangleNode(node, vsapi);
    std::string thisFrame = mangleFrame(node, 0, vsapi);
    std::string baseFrame = mangleFrame(node, maxLevel, vsapi);

    if (minRealLevel != 0) {
        nodes[baseFrame].insert("label=\"" + std::string(vsapi->getNodeCreationFunctionName(node, minRealLevel)) + setArgsStr + "\"");
        nodes[baseFrame].insert(thisFrame + " [label=\"" + std::string(vsapi->getNodeCreationFunctionName(node, minRealLevel)) + "\", shape=octagon]");
    } else {
        nodes[baseFrame].insert(thisFrame + " [label=\"" + std::string(vsapi->getNodeCreationFunctionName(node, minRealLevel)) + setArgsStr + "\", shape=box]");
    }

    nodes[baseFrame].insert(thisNode + " [label=\"" + std::string(vsapi->getNodeName(node)) + "#" + std::to_string(vsapi->getNodeIndex(node)) + "\", shape=oval]");
    lines.insert(thisFrame + " -> " + thisNode);

    const VSMap *args = vsapi->getNodeCreationFunctionArguments(node, 0);
    int numKeys = vsapi->mapNumKeys(args);
    for (int i = 0; i < numKeys; i++) {
        const char *key = vsapi->mapGetKey(args, i);
        int numElems = vsapi->mapNumElements(args, key);
        switch (vsapi->mapGetType(args, key)) {
            case ptVideoNode:
            case ptAudioNode:
                for (int j = 0; j < numElems; j++) {
                    VSNodeRef *ref = vsapi->mapGetNode(args, key, j, nullptr);
                    lines.insert(mangleNode(ref, vsapi) +  " -> " + thisFrame);
                    printNodeGraphHelper(lines, nodes, visited, ref, vsapi);
                    vsapi->freeNode(ref);
                }
                break;
            default:
                break;
        }
    }
}

std::string printFullNodeGraph(VSNodeRef *node, const VSAPI *vsapi) {
    std::map<std::string, std::set<std::string>> nodes;
    std::set<std::string> lines;
    std::set<VSNodeRef *> visited;
    std::string s = "digraph {\n";
    printNodeGraphHelper(lines, nodes, visited, node, vsapi);
    for (const auto &iter : nodes) {
        if (iter.second.size() > 1) {
            s += "  subgraph cluster_" +iter.first  + " {\n";
            for (const auto &iter2 : iter.second)
                s += "    " + iter2 + "\n";
            s += "  }\n";
        } else if (iter.second.size() <= 2) {
            for (const auto &iter2 : iter.second)
                s += "  " + iter2 + "\n";
        }
    }

    for (const auto &iter : lines)
        s += "  " + iter + "\n";
    s += "}";
    return s;
}
