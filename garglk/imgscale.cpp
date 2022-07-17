/******************************************************************************
 *                                                                            *
 * Copyright (C) 2006-2009 by Tor Andersson.                                  *
 * Copyright (C) 2010 by Ben Cressey.                                         *
 *                                                                            *
 * This file is part of Gargoyle.                                             *
 *                                                                            *
 * Gargoyle is free software; you can redistribute it and/or modify           *
 * it under the terms of the GNU General Public License as published by       *
 * the Free Software Foundation; either version 2 of the License, or          *
 * (at your option) any later version.                                        *
 *                                                                            *
 * Gargoyle is distributed in the hope that it will be useful,                *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with Gargoyle; if not, write to the Free Software                    *
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA *
 *                                                                            *
 *****************************************************************************/

/*
 * Image scaling, based on pnmscale.c...
 */

#include <vector>

#include "glk.h"
#include "garglk.h"

picture_t *
gli_picture_scale(picture_t *src, int newcols, int newrows)
{
    /* pnmscale.c - read a portable anymap and scale it
     *
     * Copyright (C) 1989, 1991 by Jef Poskanzer.
     *
     * Permission to use, copy, modify, and distribute this software and its
     * documentation for any purpose and without fee is hereby granted, provided
     * that the above copyright notice appear in all copies and that both that
     * copyright notice and this permission notice appear in supporting
     * documentation.  This software is provided "as is" without express or
     * implied warranty.
     */

#define SCALE 4096
#define HALFSCALE 2048
#define maxval 255

    picture_t *dst;

    dst = gli_picture_retrieve(src->id, true);

    if (dst && dst->w == newcols && dst->h == newrows)
        return dst;

    int row, col;

    int rowsread, needtoreadrow;

    int cols = src->w;
    int rows = src->h;

    float xscale, yscale;
    long sxscale, syscale;

    long fracrowtofill, fracrowleft;

    /* Allocate destination image and scratch space */

    dst = new picture_t;
    dst->refcount = 1;
    dst->w = newcols;
    dst->h = newrows;
    dst->rgba.resize(newcols, newrows);
    dst->id = src->id;
    dst->scaled = true;

    std::vector<Pixel<4>> tempxelrow(cols);
    std::vector<long> rs(cols + 1);
    std::vector<long> gs(cols + 1);
    std::vector<long> bs(cols + 1);
    std::vector<long> as(cols + 1);

    /* Compute all sizes and scales. */

    xscale = static_cast<float>(newcols) / static_cast<float>(cols);
    yscale = static_cast<float>(newrows) / static_cast<float>(rows);
    sxscale = xscale * SCALE;
    syscale = yscale * SCALE;

    rowsread = 1;
    fracrowleft = syscale;
    needtoreadrow = 0;

    for ( col = 0; col < cols; ++col )
        rs[col] = gs[col] = bs[col] = as[col] = HALFSCALE;
    fracrowtofill = SCALE;

    for ( row = 0; row < newrows; ++row )
    {
        /* First scale Y from src->rgba into tempxelrow. */
        {
            while ( fracrowleft < fracrowtofill )
            {
                if ( needtoreadrow )
                    if ( rowsread < rows )
                    {
                        ++rowsread;
                        /* needtoreadrow = 0; */
                    }

                for ( col = 0; col < cols; ++col)
                {
                    rs[col] += fracrowleft * src->rgba[rowsread - 1][col][0] * src->rgba[rowsread - 1][col][3];
                    gs[col] += fracrowleft * src->rgba[rowsread - 1][col][1] * src->rgba[rowsread - 1][col][3];
                    bs[col] += fracrowleft * src->rgba[rowsread - 1][col][2] * src->rgba[rowsread - 1][col][3];
                    as[col] += fracrowleft * src->rgba[rowsread - 1][col][3];
                }

                fracrowtofill -= fracrowleft;
                fracrowleft = syscale;
                needtoreadrow = 1;
            }

            /* Now fracrowleft is >= fracrowtofill, so we can produce a row. */
            if ( needtoreadrow )
                if ( rowsread < rows )
                {
                    ++rowsread;
                    needtoreadrow = 0;
                }

            for ( col = 0; col < cols; ++col )
            {
                long r, g, b, a;
                r = rs[col] + fracrowtofill * src->rgba[rowsread - 1][col][0] * src->rgba[rowsread - 1][col][3];
                g = gs[col] + fracrowtofill * src->rgba[rowsread - 1][col][1] * src->rgba[rowsread - 1][col][3];
                b = bs[col] + fracrowtofill * src->rgba[rowsread - 1][col][2] * src->rgba[rowsread - 1][col][3];
                a = as[col] + fracrowtofill * src->rgba[rowsread - 1][col][3];

                if (!a)
                {
                    r = g = b = a;
                }
                else
                {
                    r /= a;
                    if ( r > maxval ) r = maxval;
                    g /= a;
                    if ( g > maxval ) g = maxval;
                    b /= a;
                    if ( b > maxval ) b = maxval;
                    a /= SCALE;
                    if ( a > maxval ) a = maxval;
                }

                tempxelrow[col] = Pixel<4>(r, g, b, a);
                rs[col] = gs[col] = bs[col] = as[col] = HALFSCALE;
            }

            fracrowleft -= fracrowtofill;
            if ( fracrowleft == 0 )
            {
                fracrowleft = syscale;
                needtoreadrow = 1;
            }
            fracrowtofill = SCALE;
        }

        /* Now scale X from tempxelrow into dst->rgba and write it out. */
        {
            long r, g, b, a;
            long fraccoltofill, fraccolleft;
            int needcol;

            fraccoltofill = SCALE;
            r = g = b = a = HALFSCALE;
            needcol = 0;

            int dstcol = 0;
            for ( col = 0; col < cols; ++col )
            {
                fraccolleft = sxscale;
                while ( fraccolleft >= fraccoltofill )
                {
                    if ( needcol )
                    {
                        dstcol++;
                        r = g = b = a = HALFSCALE;
                    }

                    r += fraccoltofill * tempxelrow[col][0] * tempxelrow[col][3];
                    g += fraccoltofill * tempxelrow[col][1] * tempxelrow[col][3];
                    b += fraccoltofill * tempxelrow[col][2] * tempxelrow[col][3];
                    a += fraccoltofill * tempxelrow[col][3];

                    if (!a)
                    {
                        r = g = b = a;
                    }
                    else
                    {
                        r /= a;
                        if ( r > maxval ) r = maxval;
                        g /= a;
                        if ( g > maxval ) g = maxval;
                        b /= a;
                        if ( b > maxval ) b = maxval;
                        a /= SCALE;
                        if ( a > maxval ) a = maxval;
                    }

                    dst->rgba[row][dstcol] = Pixel<4>(r, g, b, a);

                    fraccolleft -= fraccoltofill;
                    fraccoltofill = SCALE;
                    needcol = 1;
                }

                if ( fraccolleft > 0 )
                {
                    if ( needcol )
                    {
                        dstcol++;
                        r = g = b = a = HALFSCALE;
                        needcol = 0;
                    }

                    r += fraccolleft * tempxelrow[col][0] * tempxelrow[col][3];
                    g += fraccolleft * tempxelrow[col][1] * tempxelrow[col][3];
                    b += fraccolleft * tempxelrow[col][2] * tempxelrow[col][3];
                    a += fraccolleft * tempxelrow[col][3];

                    fraccoltofill -= fraccolleft;
                }
            }

            if ( fraccoltofill > 0 )
            {
                r += fraccoltofill * tempxelrow[cols - 1][0] * tempxelrow[cols - 1][3];
                g += fraccoltofill * tempxelrow[cols - 1][1] * tempxelrow[cols - 1][3];
                b += fraccoltofill * tempxelrow[cols - 1][2] * tempxelrow[cols - 1][3];
                a += fraccoltofill * tempxelrow[cols - 1][3];
            }

            if ( ! needcol )
            {
                if (!a)
                {
                    r = g = b = a;
                }
                else
                {
                    r /= a;
                    if ( r > maxval ) r = maxval;
                    g /= a;
                    if ( g > maxval ) g = maxval;
                    b /= a;
                    if ( b > maxval ) b = maxval;
                    a /= SCALE;
                    if ( a > maxval ) a = maxval;
                }

                dst->rgba[row][dstcol] = Pixel<4>(r, g, b, a);
            }
        }
    }

    gli_picture_store(dst);

    return dst;
}
