// Copyright (C) 2006-2009 by Tor Andersson.
// Copyright (C) 2010 by Ben Cressey.
//
// This file is part of Gargoyle.
//
// Gargoyle is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// Gargoyle is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Gargoyle; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#include <cstdint>
#include <memory>
#include <new>

#include <Accelerate/Accelerate.h>

#include "glk.h"
#include "garglk.h"

static vImage_Buffer makevbuf(const void *data, int width, int height)
{
    vImage_Buffer buf;

    buf.width = width;
    buf.height = height;
    buf.rowBytes = width * 4;
    buf.data = const_cast<void *>(data);

    return buf;
}

static void swapcolors(const void *in, void *out, int width, int height, std::array<std::uint8_t, 4> map)
{
    auto src = makevbuf(in, width, height);
    auto dst = makevbuf(out, width, height);

    if (vImagePermuteChannels_ARGB8888(&src, &dst, map.data(), kvImageNoFlags) != kvImageNoError) {
        throw std::bad_alloc();
    }
}

std::shared_ptr<picture_t> gli_picture_scale(const picture_t *src, int newcols, int newrows)
{
    auto dst = gli_picture_retrieve(src->id, true);

    if (dst != nullptr && dst->w == newcols && dst->h == newrows) {
        return dst;
    }

    try {
        // vImage assumes ARGB, but the data is RGBA. Translate to ARGB before scaling.
        auto swapped = std::make_unique<std::uint8_t[]>(src->rgba.size());
        swapcolors(src->rgba.data(), swapped.get(), src->rgba.width(), src->rgba.height(), std::array<std::uint8_t, 4>{3, 0, 1, 2});
        auto vsrc = makevbuf(swapped.get(), src->rgba.width(), src->rgba.height());

        auto resized = std::make_unique<std::uint8_t[]>(newcols * newrows * 4);
        auto vdst = makevbuf(resized.get(), newcols, newrows);

        vImageScale_ARGB8888(&vsrc, &vdst, nullptr, kvImageHighQualityResampling);

        Canvas<4> rgba(newcols, newrows);

        // Swap back from ARGB to RGBA
        swapcolors(resized.get(), rgba.data(), newcols, newrows, std::array<std::uint8_t, 4>{1, 2, 3, 0});

        dst = std::make_shared<picture_t>(src->id, rgba, true);

        gli_picture_store(dst);
    } catch (const std::bad_alloc &) {
        return nullptr;
    }

    return dst;
}
