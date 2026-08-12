#
# Copyright (c) 2026 Fredrik Mellbin
#
# This file is part of VapourSynth.
#
# VapourSynth is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# VapourSynth is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with VapourSynth; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#
#cython: language_level=3

# Only what the bindings actually touch. VSVULKANAPI carries a large surface of entry points
# taking Vulkan handle types, none of which the bindings call, and a cdef extern struct needs
# no more members than are used -- cython emits member accesses, never a layout. Adding a
# member here is therefore all that a future binding needs, and nothing has to track the
# header otherwise.

from libc.stdint cimport uint8_t, uint32_t, int64_t

from vapoursynth cimport VSCore

cdef extern from "include/VSVulkan4.h" nogil:
    enum:
        VSVULKAN_API_VERSION
        VK_UUID_SIZE
        VK_LUID_SIZE

    ctypedef struct VSVulkanCoreInfo:
        char deviceName[256]
        uint32_t apiVersion
        int deviceType
        int64_t deviceMemory
        int64_t budget
        int64_t allocated
        int64_t limit
        uint8_t deviceUUID[16]
        uint8_t deviceLUID[8]
        uint32_t deviceNodeMask
        int deviceLUIDValid
        int exportHandleType
        int semaphoreExportHandleType
        int unifiedMemory

    ctypedef struct VSVulkanDeviceListEntry:
        char deviceName[256]
        uint32_t apiVersion
        int deviceType
        int64_t deviceMemory
        int usable
        char unusableReason[256]
        uint8_t deviceUUID[16]
        uint8_t deviceLUID[8]
        uint32_t deviceNodeMask
        int deviceLUIDValid

    ctypedef struct VSVULKANAPI:
        int enumerateVulkanDevices(VSVulkanDeviceListEntry *entries, int maxEntries, char *errorMessage, int errorMessageSize) nogil
        int setVulkanDevice(VSCore *core, int deviceIndex, char *errorMessage, int errorMessageSize) nogil
        int getVulkanCoreInfo(VSCore *core, VSVulkanCoreInfo *info, char *errorMessage, int errorMessageSize) nogil
        int64_t setMaxVRAMUse(int64_t bytes, VSCore *core) nogil
