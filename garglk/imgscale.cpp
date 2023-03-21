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

#include <memory>

#include "glk.h"
#include "garglk.h"

std::shared_ptr<picture_t> gli_picture_scale(const picture_t *src, int newcols, int newrows)
{
    auto dst = gli_picture_retrieve(src->id, true);

    if (dst != nullptr && dst->w == newcols && dst->h == newrows) {
        return dst;
    }

    try {
        auto rgba = winimagescale(src->rgba, newcols, newrows);
        dst = std::make_shared<picture_t>(src->id, rgba, true);
    } catch (const std::bad_alloc &) {
        return nullptr;
    }

    gli_picture_store(dst);

    return dst;
}
