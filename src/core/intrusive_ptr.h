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

#ifndef INTRUSIVE_PTR_H
#define INTRUSIVE_PTR_H

#include <algorithm>

template<typename T>
class vs_intrusive_ptr {
private:
    T *obj;
public:
    vs_intrusive_ptr(T *ptr = nullptr, bool add_ref = false) noexcept {
        obj = ptr;
        if (add_ref && obj)
            obj->add_ref();
    }

    vs_intrusive_ptr(const vs_intrusive_ptr &ptr) noexcept {
        obj = ptr.obj;
        if (obj)
            obj->add_ref();
    }

    vs_intrusive_ptr(vs_intrusive_ptr &&ptr) noexcept {
        obj = ptr.obj;
        ptr.obj = nullptr;
    }

    ~vs_intrusive_ptr() noexcept {
        if (obj)
            obj->release();
    }

    vs_intrusive_ptr &operator=(vs_intrusive_ptr const &ptr) noexcept {
        if (obj)
            obj->release();
        obj = ptr.obj;
        if (obj)
            obj->add_ref();
        return *this;
    }

    T *operator->() const noexcept {
        return obj;
    }

    T &operator*() const noexcept {
        return *obj;
    }

    operator bool() const noexcept {
        return !!obj;
    }

    T *get() const noexcept {
        return obj;
    }

    void reset() noexcept {
        if (obj) {
            obj->release();
            obj = nullptr;
        }
    }

    void swap(vs_intrusive_ptr &ptr) noexcept {
        std::swap(obj, ptr.obj);
    }
};

#endif