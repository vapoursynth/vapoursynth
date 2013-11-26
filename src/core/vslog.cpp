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

#include "vslog.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <mutex>
#include <vector>

static VSMessageHandler messageHandler = nullptr;
static void *messageUserData = nullptr;
static std::mutex logMutex;

void vsSetMessageHandler(VSMessageHandler handler, void *userData) {
    std::lock_guard<std::mutex> lock(logMutex);
    if (handler) {
        messageHandler = handler;
        messageUserData = userData;
    } else {
        messageHandler = nullptr;
        messageUserData = nullptr;
    }
}

void vsLog(const char *file, long line, VSMessageType type, const char *msg, ...) {
    std::lock_guard<std::mutex> lock(logMutex);
    if (messageHandler) {
        va_list alist;
        va_start(alist, msg);
        try {
            int size = vsnprintf(nullptr, 0, msg, alist);
            std::vector<char> buf(size+1);
            vsnprintf(buf.data(), buf.size(), msg, alist);
            messageHandler(type, buf.data(), messageUserData);
        } catch (std::bad_alloc &) {
            fprintf(stderr, "Bad alloc exception in log handler\n");
            vfprintf(stderr, msg, alist);
            fprintf(stderr, "\n");
        }
        va_end(alist);
    } else {
        va_list alist;
        va_start(alist, msg);
        vfprintf(stderr, msg, alist);
        fprintf(stderr, "\n");
        va_end(alist);
    }

    if (type == mtFatal) {
        assert(false);
        abort();
    }
}