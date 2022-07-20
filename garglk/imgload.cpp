/******************************************************************************
 *                                                                            *
 * Copyright (C) 2006-2009 by Tor Andersson.                                  *
 * Copyright (C) 2010 by Ben Cressey, Chris Spiegel.                          *
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

#include <cassert>
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>

#include <jpeglib.h>
#include <png.h>

#include "glk.h"
#include "garglk.h"
#include "gi_blorb.h"

#define giblorb_ID_JPEG      (giblorb_make_id('J', 'P', 'E', 'G'))
#define giblorb_ID_PNG       (giblorb_make_id('P', 'N', 'G', ' '))

static void load_image_png(std::FILE *fl, picture_t *pic);
static void load_image_jpeg(std::FILE *fl, picture_t *pic);

struct PicturePair
{
    picture_t *picture;
    picture_t *scaled;
};

std::map<unsigned long, PicturePair> picstore;

static int gli_piclist_refcount = 0;	/* count references to loaded pictures */

static void gli_piclist_clear()
{
    for (const auto &pair : picstore)
    {
        gli_picture_decrement(pair.second.picture);
        gli_picture_decrement(pair.second.scaled);
    }

    picstore.clear();
}

void gli_piclist_increment()
{
    gli_piclist_refcount++;
}

void gli_piclist_decrement()
{
    if (gli_piclist_refcount > 0 && --gli_piclist_refcount == 0)
        gli_piclist_clear();
}

void gli_picture_increment(picture_t *pic)
{
    if (!pic)
        return;

    pic->refcount++;
}

void gli_picture_decrement(picture_t *pic)
{
    if (!pic)
        return;

    if (pic->refcount > 0 && --pic->refcount == 0)
    {
        delete pic;
    }
}

static void gli_picture_store_original(picture_t *pic)
{
    picstore[pic->id] = PicturePair{pic, nullptr};
}

static void gli_picture_store_scaled(picture_t *pic)
{
    try
    {
        auto &picpair = picstore.at(pic->id);
        gli_picture_decrement(picpair.scaled);
        picpair.scaled = pic;
    }
    catch (const std::out_of_range &)
    {
    }
}

void gli_picture_store(picture_t *pic)
{
    if (!pic)
        return;

    if (!pic->scaled)
        gli_picture_store_original(pic);
    else
        gli_picture_store_scaled(pic);
}

picture_t *gli_picture_retrieve(unsigned long id, bool scaled)
{
    try
    {
        const auto &picpair = picstore.at(id);

        return scaled ? picpair.scaled : picpair.picture;
    }
    catch (const std::out_of_range &)
    {
        return nullptr;
    }
}

picture_t *gli_picture_load(unsigned long id)
{
    picture_t *pic;
    std::FILE *fl;
    bool closeafter;
    glui32 chunktype;

    pic = gli_picture_retrieve(id, false);

    if (pic)
        return pic;

    if (giblorb_get_resource_map() == nullptr)
    {
        unsigned char buf[8];
        std::string filename = gli_workdir + "/PIC" + std::to_string(id);

        closeafter = true;
        fl = std::fopen(filename.c_str(), "rb");
        if (!fl)
            return nullptr;

        if (std::fread(buf, 1, 8, fl) != 8)
        {
            /* Can't read the first few bytes. Forget it. */
            std::fclose(fl);
            return nullptr;
        }

        if (!png_sig_cmp(buf, 0, 8))
        {
            chunktype = giblorb_ID_PNG;
        }
        else if (buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF)
        {
            chunktype = giblorb_ID_JPEG;
        }
        else
        {
            /* Not a readable file. Forget it. */
            std::fclose(fl);
            return nullptr;
        }

        std::rewind(fl);
    }

    else
    {
        long pos;
        giblorb_get_resource(giblorb_ID_Pict, id, &fl, &pos, nullptr, &chunktype);
        if (!fl)
            return nullptr;
        std::fseek(fl, pos, SEEK_SET);
        closeafter = false;
    }

    pic = new picture_t;
    pic->refcount = 1;
    pic->w = 0;
    pic->h = 0;
    pic->id = id;
    pic->scaled = false;

    if (chunktype == giblorb_ID_PNG)
        load_image_png(fl, pic);

    if (chunktype == giblorb_ID_JPEG)
        load_image_jpeg(fl, pic);

    if (closeafter)
        fclose(fl);

    gli_picture_store(pic);

    return pic;
}

static void load_image_jpeg(std::FILE *fl, picture_t *pic)
{
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPROW rowarray[1];
    int n, i;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fl);
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    pic->w = cinfo.output_width;
    pic->h = cinfo.output_height;
    n = cinfo.output_components;
    pic->rgba.resize(pic->w, pic->h, false);

    std::vector<JSAMPLE> row(pic->w * n);
    rowarray[0] = row.data();

    while (cinfo.output_scanline < cinfo.output_height)
    {
        JDIMENSION y = cinfo.output_scanline;
        jpeg_read_scanlines(&cinfo, rowarray, 1);
        if (n == 1)
            for (i = 0; i < pic->w; i++)
            {
                pic->rgba[y][i] = Pixel<4>(row[i], row[i], row[i], 0xff);
            }
        else if (n == 3)
            for (i = 0; i < pic->w; i++)
            {
                pic->rgba[y][i] = Pixel<4>(row[i*3+0], row[i*3+1], row[i*3+2], 0xff);
            }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
}

static void load_image_png(std::FILE *fl, picture_t *pic)
{
    int ix;
    int srcrowbytes;
    png_structp png_ptr = nullptr;
    png_infop info_ptr = nullptr;

    /* Define these before the setjmp() call to ensure destructors are called. */
    std::vector<png_bytep> rowarray;
    std::vector<png_byte> srcdata;

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr)
        return;

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        png_destroy_read_struct(&png_ptr, nullptr, nullptr);
        return;
    }

    if (setjmp(png_jmpbuf(png_ptr)))
    {
        /* If we jump here, we had a problem reading the file */
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        return;
    }

    png_init_io(png_ptr, fl);

    png_read_info(png_ptr, info_ptr);

    pic->w = png_get_image_width(png_ptr, info_ptr);
    pic->h = png_get_image_height(png_ptr, info_ptr);

    png_set_strip_16(png_ptr);
    png_set_packing(png_ptr);
    png_set_expand(png_ptr);
    png_set_gray_to_rgb(png_ptr);

    png_read_update_info(png_ptr, info_ptr);

    srcrowbytes = png_get_rowbytes(png_ptr, info_ptr);

    assert(srcrowbytes == pic->w * 4 || srcrowbytes == pic->w * 3);

    rowarray.resize(pic->h);
    srcdata.resize(pic->w * pic->h * 4);

    pic->rgba.resize(pic->w, pic->h, false);

    for (ix=0; ix<pic->h; ix++)
        rowarray[ix] = &srcdata[ix * pic->w * 4];

    png_read_image(png_ptr, rowarray.data());
    png_read_end(png_ptr, info_ptr);

    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);

    bool has_alpha = srcrowbytes == pic->w * 4;
    int size = has_alpha ? 4 : 3;
    for (int y = 0; y < pic->h; y++)
    {
        for (int x = 0; x < pic->w; x++)
        {
            auto a = has_alpha ? rowarray[y][x * size + 3] : 0xff;
            pic->rgba[y][x] = Pixel<4>(rowarray[y][x * size + 0], rowarray[y][x * size + 1], rowarray[y][x * size + 2], a);
        }
    }
}
