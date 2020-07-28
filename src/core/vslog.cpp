/*
* Copyright (c) 2012-2013 Fredrik Mellbin
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

// This file only exists for V3 compatibility

#include "vslog.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <map>
#include <vector>

struct MessageHandler {
    vs3::VSMessageHandler handler;
    vs3::VSMessageHandlerFree free;
    void *userData;
};

static std::map<int, MessageHandler> messageHandlers;
static int currentHandlerId = 0;
static int globalMessageHandler = -1;
static std::mutex logMutex;

static int vsRemoveMessageHandlerInternal(int id) {
    if (messageHandlers.count(id)) {
        if (messageHandlers[id].free)
            messageHandlers[id].free(messageHandlers[id].userData);
        messageHandlers.erase(id);
        return 1;
    }
    return 0;
}

void vsSetMessageHandler3(vs3::VSMessageHandler handler, void *userData) {
    std::lock_guard<std::mutex> lock(logMutex);
    if (globalMessageHandler >= 0) {
        vsRemoveMessageHandlerInternal(globalMessageHandler);
        globalMessageHandler = -1;
    }
    if (handler) {
        messageHandlers.emplace(currentHandlerId, MessageHandler{ handler, nullptr, userData });
        globalMessageHandler = currentHandlerId++;
    }
}

int vsAddMessageHandler3(vs3::VSMessageHandler handler, vs3::VSMessageHandlerFree free, void *userData) {
    assert(handler);
    std::lock_guard<std::mutex> lock(logMutex);
    messageHandlers.emplace(currentHandlerId, MessageHandler{ handler, free, userData });
    return currentHandlerId++;
}

int vsRemoveMessageHandler3(int id) {
    std::lock_guard<std::mutex> lock(logMutex);
    return vsRemoveMessageHandlerInternal(id);
}

void vsLog3(vs3::VSMessageType type, const char *msg, ...) {
    std::lock_guard<std::mutex> lock(logMutex);
    if (!messageHandlers.empty()) {
        try {
            va_list alist;
            va_start(alist, msg);
            int size = vsnprintf(nullptr, 0, msg, alist);
            va_end(alist);
            std::vector<char> buf(size+1);
            va_start(alist, msg);
            vsnprintf(buf.data(), buf.size(), msg, alist);
            va_end(alist);
            for (const auto &iter : messageHandlers)
                iter.second.handler(type, buf.data(), iter.second.userData);
        } catch (std::bad_alloc &) {
            fprintf(stderr, "Bad alloc exception in log handler\n");
            va_list alist;
            va_start(alist, msg);
            vfprintf(stderr, msg, alist);
            va_end(alist);
            fprintf(stderr, "\n");
        }
    }
}
