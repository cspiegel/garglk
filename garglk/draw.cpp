/******************************************************************************
 *                                                                            *
 * Copyright (C) 2006-2009 by Tor Andersson, Jesse McGrew.                    *
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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "glk.h"
#include "garglk.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_LCD_FILTER_H

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#endif

#define GAMMA_BITS 11
#define GAMMA_MAX ((1 << GAMMA_BITS) - 1)

#define mul255(a,b) ((static_cast<short>(a) * (b) + 127) / 255)
#define mulhigh(a,b) ((static_cast<int>(a) * (b) + (1 << (GAMMA_BITS - 1)) - 1) / GAMMA_MAX)
#define grayscale(r,g,b) ((30 * (r) + 59 * (g) + 11 * (b)) / 100)

struct Bitmap
{
    int w, h, lsb, top, pitch;
    std::vector<unsigned char> data;
};

struct FontEntry
{
    int adv;
    std::array<Bitmap, GLI_SUBPIX> glyph;
};

struct Font
{
    FT_Face face = nullptr;
    std::map<glui32, FontEntry> entries;
    bool make_bold = false;
    bool make_oblique = false;
    bool kerned = false;
    std::map<std::pair<glui32, glui32>, int> kerncache;

    Font(const std::string &path, const std::string &fallback, FontType type, FontStyle style);

    const FontEntry &getglyph(glui32 cid);
    int charkern(glui32 c0, glui32 c1);
};

/*
 * Globals
 */

std::array<unsigned short, 256> gammamap;
std::array<unsigned char, 1 << GAMMA_BITS> gammainv;

static std::map<FontFace, Font> gfont_table;

int gli_cellw = 8;
int gli_cellh = 8;

int gli_image_s = 0;
int gli_image_w = 0;
int gli_image_h = 0;
Canvas<4> gli_image_rgb;

static FT_Library ftlib;
static FT_Matrix ftmat;

static bool use_freetype_preset_filter = false;
static FT_LcdFilter freetype_preset_filter = FT_LCD_FILTER_DEFAULT;

void garglk::set_lcdfilter(const std::string &filter)
{
    use_freetype_preset_filter = true;

    if (filter == "none")
        freetype_preset_filter = FT_LCD_FILTER_NONE;
    else if (filter == "default")
        freetype_preset_filter = FT_LCD_FILTER_DEFAULT;
    else if (filter == "light")
        freetype_preset_filter = FT_LCD_FILTER_LIGHT;
    else if (filter == "legacy")
        freetype_preset_filter = FT_LCD_FILTER_LEGACY;
    else
        use_freetype_preset_filter = false;
}

/*
 * Font loading
 */

// FT_Error_String() was introduced in FreeType 2.10.0.
#if FREETYPE_MAJOR == 2 && FREETYPE_MINOR < 10
#define FT_Error_String(err) nullptr
#endif

static void freetype_error(int err, const std::string &basemsg)
{
    std::ostringstream msg;
    // If FreeType was not built with FT_CONFIG_OPTION_ERROR_STRINGS,
    // this will always be nullptr.
    const char *errstr = FT_Error_String(err);

    if (errstr == nullptr)
        msg << basemsg << " (error code " << err << ")";
    else
        msg << basemsg << ": " << errstr;

    garglk::winabort(msg.str());
}

const FontEntry &Font::getglyph(glui32 cid)
{
    auto it = entries.find(cid);
    if (it == entries.end())
    {
        FT_Vector v;
        int err;
        glui32 gid;
        int x;
        FontEntry entry;
        size_t datasize;

        gid = FT_Get_Char_Index(face, cid);
        if (gid == 0)
            gid = FT_Get_Char_Index(face, '?');

        for (x = 0; x < GLI_SUBPIX; x++)
        {
            v.x = (x * 64) / GLI_SUBPIX;
            v.y = 0;

            FT_Set_Transform(face, nullptr, &v);

            err = FT_Load_Glyph(face, gid,
                    FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING);
            if (err)
                freetype_error(err, "Error in FT_Load_Glyph");

            if (make_bold)
                FT_Outline_Embolden(&face->glyph->outline, FT_MulFix(face->units_per_EM, face->size->metrics.y_scale) / 24);

            if (make_oblique)
                FT_Outline_Transform(&face->glyph->outline, &ftmat);

            if (gli_conf_lcd)
            {
                if (use_freetype_preset_filter)
                    FT_Library_SetLcdFilter(ftlib, freetype_preset_filter);
                else
                    FT_Library_SetLcdFilterWeights(ftlib, gli_conf_lcd_weights.data());

                err = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_LCD);
            }
            else
            {
                err = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_LIGHT);
            }

            if (err)
                freetype_error(err, "Error in FT_Render_Glyph");

            datasize = face->glyph->bitmap.pitch * face->glyph->bitmap.rows;
            entry.adv = (face->glyph->advance.x * GLI_SUBPIX + 32) / 64;

            entry.glyph[x].lsb = face->glyph->bitmap_left;
            entry.glyph[x].top = face->glyph->bitmap_top;
            entry.glyph[x].w = face->glyph->bitmap.width;
            entry.glyph[x].h = face->glyph->bitmap.rows;
            entry.glyph[x].pitch = face->glyph->bitmap.pitch;
            entry.glyph[x].data.assign(&face->glyph->bitmap.buffer[0], &face->glyph->bitmap.buffer[datasize]);
        }

        it = entries.emplace(cid, entry).first;
    }

    return it->second;
}

// Look for a user-specified font. This will be either based on a font
// family (propfont or monofont), or specific font files (e.g. propr,
// monor, etc).
static std::string font_path_user(const std::string &path, const std::string &)
{
    return path;
}

// Look in a system-wide location for the fallback Gargoyle fonts; on
// Unix this is generally somewhere like /usr/share/fonts/gargoyle
// (although this can be changed at build time), and on Windows it's the
// install directory (e.g. "C:\Program Files (x86)\Gargoyle").
static std::string font_path_fallback_system(const std::string &, const std::string &fallback)
{
#ifdef _WIN32
    char directory[256];
    DWORD dsize = sizeof directory;
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "Software\\Tor Andersson\\Gargoyle", "Directory", RRF_RT_REG_SZ, nullptr, directory, &dsize) != ERROR_SUCCESS)
        return "";

    return std::string(directory) + "\\" + fallback;
#elif defined(GARGLK_FONT_PATH)
    return std::string(GARGLK_FONT_PATH) + "/" + fallback;
#else
    return "";
#endif
}

// Look in a platform-specific location for the fonts. This is typically
// the same directory that the executable is in, but can be anything the
// platform code deems appropriate.
static std::string font_path_fallback_platform(const std::string &, const std::string &fallback)
{
    return garglk::winfontpath(fallback);
}

// As a last-ditch effort, look in the current directory for the fonts.
static std::string font_path_fallback_local(const std::string &, const std::string &fallback)
{
    return fallback;
}

static const char *type_to_name(enum FontType type)
{
    if (type == FontType::Monospace)
        return "Mono";
    else
        return "Proportional";
}

static std::string style_to_name(FontStyle style)
{
    switch (style)
    {
        case FontStyle::Roman:
            return "Regular";
        case FontStyle::Bold:
            return "Bold";
        case FontStyle::Italic:
            return "Italic";
        case FontStyle::BoldItalic:
            return "Bold Italic";;
    }

    return "";
}

Font::Font(const std::string &path, const std::string &fallback, FontType type, FontStyle style)
{
    int err = 0;
    std::string fontpath;
    float aspect, size;
    std::string family;
    std::vector<std::function<std::string(const std::string &path, const std::string &fallback)>> font_paths = {
        font_path_user,
        font_path_fallback_system,
        font_path_fallback_platform,
        font_path_fallback_local,
    };

    if (type == FontType::Monospace)
    {
        aspect = gli_conf_monoaspect;
        size = gli_conf_monosize;
        family = gli_conf_monofont;
    }
    else
    {
        aspect = gli_conf_propaspect;
        size = gli_conf_propsize;
        family = gli_conf_propfont;
    }

    if (!std::any_of(font_paths.begin(), font_paths.end(), [&](const auto &get_font_path) {
        fontpath = get_font_path(path, fallback);
        return !fontpath.empty() && FT_New_Face(ftlib, fontpath.c_str(), 0, &face) == 0;
    }))
    {
        garglk::winabort("Unable to find font " + family + " for " + type_to_name(type) + " " + style_to_name(style) + ", and fallback " + fallback + " not found");
    }

    auto dot = fontpath.rfind(".");
    if (dot != std::string::npos)
    {
        std::string afmbuf = fontpath;
        auto ext = afmbuf.substr(dot);
        if (ext == ".pfa" || ext == ".PFA" || ext == ".pfb" || ext == ".PFB")
        {
            afmbuf.replace(dot, std::string::npos, ".afm");
            FT_Attach_File(face, afmbuf.c_str());
            afmbuf.replace(dot, std::string::npos, ".AFM");
            FT_Attach_File(face, afmbuf.c_str());
        }
    }

    err = FT_Set_Char_Size(face, size * aspect * 64, size * 64, 72, 72);
    if (err)
        freetype_error(err, "Error in FT_Set_Char_Size for " + fontpath);

    err = FT_Select_Charmap(face, ft_encoding_unicode);
    if (err)
        freetype_error(err, "Error in FT_Select_CharMap for " + fontpath);

    kerned = FT_HAS_KERNING(face);

    switch (style)
    {
        case FontStyle::Roman:
            make_bold = false;
            make_oblique = false;
            break;

        case FontStyle::Bold:
            make_bold = !(face->style_flags & FT_STYLE_FLAG_BOLD);
            make_oblique = false;
            break;

        case FontStyle::Italic:
            make_bold = false;
            make_oblique = !(face->style_flags & FT_STYLE_FLAG_ITALIC);
            break;

        case FontStyle::BoldItalic:
            make_bold = !(face->style_flags & FT_STYLE_FLAG_BOLD);
            make_oblique = !(face->style_flags & FT_STYLE_FLAG_ITALIC);
            break;
    }
}

void gli_initialize_fonts()
{
    int err;

    for (int i = 0; i < 256; i++)
        gammamap[i] = std::pow(i / 255.0, gli_conf_gamma) * GAMMA_MAX + 0.5;

    for (int i = 0; i <= GAMMA_MAX; i++)
        gammainv[i] = std::pow(i / static_cast<float>(GAMMA_MAX), 1.0 / gli_conf_gamma) * 255.0 + 0.5;

    err = FT_Init_FreeType(&ftlib);
    if (err)
        freetype_error(err, "Unable to initialize FreeType");

    fontload();
    garglk::fontreplace(gli_conf_monofont, FontType::Monospace);
    garglk::fontreplace(gli_conf_propfont, FontType::Proportional);
    fontunload();

    /* If the user provided specific fonts, swap them in */
    if (!gli_conf_mono_override.r.empty()) gli_conf_mono.r = gli_conf_mono_override.r;
    if (!gli_conf_mono_override.b.empty()) gli_conf_mono.b = gli_conf_mono_override.b;
    if (!gli_conf_mono_override.i.empty()) gli_conf_mono.i = gli_conf_mono_override.i;
    if (!gli_conf_mono_override.z.empty()) gli_conf_mono.z = gli_conf_mono_override.z;
    if (!gli_conf_prop_override.r.empty()) gli_conf_prop.r = gli_conf_prop_override.r;
    if (!gli_conf_prop_override.b.empty()) gli_conf_prop.b = gli_conf_prop_override.b;
    if (!gli_conf_prop_override.i.empty()) gli_conf_prop.i = gli_conf_prop_override.i;
    if (!gli_conf_prop_override.z.empty()) gli_conf_prop.z = gli_conf_prop_override.z;

    /* create oblique transform matrix */
    ftmat.xx = 0x10000L;
    ftmat.yx = 0x00000L;
    ftmat.xy = 0x03000L;
    ftmat.yy = 0x10000L;

    gfont_table.clear();
    gfont_table.insert({FontFace::MonoR, Font(gli_conf_mono.r, "Gargoyle-Mono.ttf", FontType::Monospace, FontStyle::Roman)});
    gfont_table.insert({FontFace::MonoB, Font(gli_conf_mono.b, "Gargoyle-Mono-Bold.ttf", FontType::Monospace, FontStyle::Bold)});
    gfont_table.insert({FontFace::MonoI, Font(gli_conf_mono.i, "Gargoyle-Mono-Italic.ttf", FontType::Monospace, FontStyle::Italic)});
    gfont_table.insert({FontFace::MonoZ, Font(gli_conf_mono.z, "Gargoyle-Mono-Bold-Italic.ttf", FontType::Monospace, FontStyle::BoldItalic)});
    gfont_table.insert({FontFace::PropR, Font(gli_conf_prop.r, "Gargoyle-Serif.ttf", FontType::Proportional, FontStyle::Roman)});
    gfont_table.insert({FontFace::PropB, Font(gli_conf_prop.b, "Gargoyle-Serif-Bold.ttf", FontType::Proportional, FontStyle::Bold)});
    gfont_table.insert({FontFace::PropI, Font(gli_conf_prop.i, "Gargoyle-Serif-Italic.ttf", FontType::Proportional, FontStyle::Italic)});
    gfont_table.insert({FontFace::PropZ, Font(gli_conf_prop.z, "Gargoyle-Serif-Bold-Italic.ttf", FontType::Proportional, FontStyle::BoldItalic)});

    const auto &entry = gfont_table.at(FontFace::MonoR).getglyph('0');

    gli_cellh = gli_leading;
    gli_cellw = (entry.adv + GLI_SUBPIX - 1) / GLI_SUBPIX;
}

/*
 * Drawing
 */

void gli_draw_pixel(int x, int y, const Pixel<3> &rgb)
{
    if (x < 0 || x >= gli_image_w)
        return;
    if (y < 0 || y >= gli_image_h)
        return;
    gli_image_rgb[y][x] = Pixel<4>(rgb[2], rgb[1], rgb[0], 0xff);
}

static void draw_pixel_gamma(int x, int y, unsigned char alpha, const Color &rgb)
{
    unsigned short invalf = GAMMA_MAX - (alpha * GAMMA_MAX / 255);
    std::array<unsigned short, 3> bg = {
        gammamap[gli_image_rgb[y][x][0]],
        gammamap[gli_image_rgb[y][x][1]],
        gammamap[gli_image_rgb[y][x][2]]
    };
    std::array<unsigned short, 3> fg = {
        gammamap[rgb[0]],
        gammamap[rgb[1]],
        gammamap[rgb[2]]
    };

    if (x < 0 || x >= gli_image_w)
        return;
    if (y < 0 || y >= gli_image_h)
        return;
    gli_image_rgb[y][x] = Pixel<4>(gammainv[fg[2] + mulhigh(static_cast<int>(bg[0]) - fg[2], invalf)],
                                   gammainv[fg[1] + mulhigh(static_cast<int>(bg[1]) - fg[1], invalf)],
                                   gammainv[fg[0] + mulhigh(static_cast<int>(bg[2]) - fg[0], invalf)],
                                   0xff);
}

static void draw_pixel_lcd_gamma(int x, int y, const unsigned char *alpha, const Color &rgb)
{
    std::array<unsigned short, 3> invalf = {
        static_cast<unsigned short>(GAMMA_MAX - (alpha[0] * GAMMA_MAX / 255)),
        static_cast<unsigned short>(GAMMA_MAX - (alpha[1] * GAMMA_MAX / 255)),
        static_cast<unsigned short>(GAMMA_MAX - (alpha[2] * GAMMA_MAX / 255)),
    };
    std::array<unsigned short, 3> bg = {
        gammamap[gli_image_rgb[y][x][0]],
        gammamap[gli_image_rgb[y][x][1]],
        gammamap[gli_image_rgb[y][x][2]]
    };
    std::array<unsigned short, 3> fg = {
        gammamap[rgb[0]],
        gammamap[rgb[1]],
        gammamap[rgb[2]]
    };

    if (x < 0 || x >= gli_image_w)
        return;
    if (y < 0 || y >= gli_image_h)
        return;
    gli_image_rgb[y][x] = Pixel<4>(gammainv[fg[2] + mulhigh(static_cast<int>(bg[0]) - fg[2], invalf[2])],
                                   gammainv[fg[1] + mulhigh(static_cast<int>(bg[1]) - fg[1], invalf[1])],
                                   gammainv[fg[0] + mulhigh(static_cast<int>(bg[2]) - fg[0], invalf[0])],
                                   0xff);
}

static void draw_bitmap_gamma(const Bitmap *b, int x, int y, const Color &rgb)
{
    int i, k, c;
    for (k = 0; k < b->h; k++)
    {
        for (i = 0; i < b->w; i ++)
        {
            c = b->data[k * b->pitch + i];
            draw_pixel_gamma(x + b->lsb + i, y - b->top + k, c, rgb);
        }
    }
}

static void draw_bitmap_lcd_gamma(const Bitmap *b, int x, int y, const Color &rgb)
{
    int i, j, k;
    for (k = 0; k < b->h; k++)
    {
        for (i = 0, j = 0; i < b->w; i += 3, j ++)
        {
            draw_pixel_lcd_gamma(x + b->lsb + j, y - b->top + k, &b->data[0] + k * b->pitch + i, rgb);
        }
    }
}

void gli_draw_clear(const Color &rgb)
{
    int x, y;

    auto pixel = Pixel<4>(rgb[2], rgb[1], rgb[0], 0xff);
    for (y = 0; y < gli_image_h; y++)
    {
        for (x = 0; x < gli_image_w; x++)
        {
            gli_image_rgb[y][x] = pixel;
        }
    }
}

void gli_draw_rect(int x0, int y0, int w, int h, const Color &rgb)
{
    int x1 = x0 + w;
    int y1 = y0 + h;
    int y;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;

    if (x0 > gli_image_w) x0 = gli_image_w;
    if (y0 > gli_image_h) y0 = gli_image_h;
    if (x1 > gli_image_w) x1 = gli_image_w;
    if (y1 > gli_image_h) y1 = gli_image_h;

    auto pixel = Pixel<4>(rgb[2], rgb[1], rgb[0], 0xff);
    for (y = y0; y < y1; y++)
    {
        gli_image_rgb[y].fill(pixel, x0, x1);
    }
}

int Font::charkern(glui32 c0, glui32 c1)
{
    FT_Vector v;
    int err;
    int g0, g1;

    if (!kerned)
        return 0;

    auto key = std::make_pair(c0, c1);
    try
    {
        return kerncache.at(key);
    }
    catch (const std::out_of_range &)
    {
    }

    g0 = FT_Get_Char_Index(face, c0);
    g1 = FT_Get_Char_Index(face, c1);

    if (g0 == 0 || g1 == 0)
        return 0;

    err = FT_Get_Kerning(face, g0, g1, FT_KERNING_UNFITTED, &v);
    if (err)
        freetype_error(err, "Error in FT_Get_Kerning");

    int value = (v.x * GLI_SUBPIX) / 64.0;
    kerncache.emplace(key, value);

    return value;
}

static const std::vector<std::pair<std::vector<glui32>, glui32>> ligatures = {
    {{'f', 'f', 'i'}, UNI_LIG_FFI},
    {{'f', 'f', 'l'}, UNI_LIG_FFL},
    {{'f', 'f'}, UNI_LIG_FF},
    {{'f', 'i'}, UNI_LIG_FI},
    {{'f', 'l'}, UNI_LIG_FL},
};

static int gli_string_impl(int x, FontFace face, const glui32 *s, size_t n, int spw, std::function<void(int, const std::array<Bitmap, GLI_SUBPIX> &)> callback)
{
    auto &f = gfont_table.at(face);
    bool dolig = !FT_IS_FIXED_WIDTH(f.face);
    int prev = -1;
    glui32 c;

    while (n > 0)
    {
        auto it = ligatures.end();
        if (dolig)
        {
            it = std::find_if(ligatures.begin(), ligatures.end(), [s, n](const std::pair<std::vector<glui32>, glui32> &ligentry) {
                auto ligature = ligentry.first;
                if (ligature.size() > n)
                    return false;

                for (size_t i = 0; i < ligature.size(); i++)
                {
                    if (s[i] != ligature[i])
                        return false;
                }

                return true;
            });
        }

        if (it != ligatures.end() && FT_Get_Char_Index(f.face, it->second) != 0)
        {
            c = it->second;
            s += it->first.size();
            n -= it->first.size();
        }
        else
        {
            c = *s++;
            n--;
        }

        auto entry = f.getglyph(c);

        if (prev != -1)
            x += f.charkern(prev, c);

        callback(x, entry.glyph);

        if (spw >= 0 && c == ' ')
            x += spw;
        else
            x += entry.adv;

        prev = c;
    }

    return x;
}

int gli_draw_string_uni(int x, int y, FontFace face, const Color &rgb,
        glui32 *s, int n, int spw)
{
    return gli_string_impl(x, face, s, n, spw, [y, rgb](int x, const std::array<Bitmap, GLI_SUBPIX> &glyphs) {
        int px = x / GLI_SUBPIX;
        int sx = x % GLI_SUBPIX;

        if (gli_conf_lcd)
            draw_bitmap_lcd_gamma(&glyphs[sx], px, y, rgb);
        else
            draw_bitmap_gamma(&glyphs[sx], px, y, rgb);
    });
}

int gli_string_width_uni(FontFace face, const glui32 *s, int n, int spw)
{
    return gli_string_impl(0, face, s, n, spw, [](int, const std::array<Bitmap, GLI_SUBPIX> &) {});
}

void gli_draw_caret(int x, int y)
{
    x = x / GLI_SUBPIX;
    if (gli_caret_shape == 0)
    {
        gli_draw_rect(x+0, y+1, 1, 1, gli_caret_color);
        gli_draw_rect(x-1, y+2, 3, 1, gli_caret_color);
        gli_draw_rect(x-2, y+3, 5, 1, gli_caret_color);
    }
    else if (gli_caret_shape == 1)
    {
        gli_draw_rect(x+0, y+1, 1, 1, gli_caret_color);
        gli_draw_rect(x-1, y+2, 3, 1, gli_caret_color);
        gli_draw_rect(x-2, y+3, 5, 1, gli_caret_color);
        gli_draw_rect(x-3, y+4, 7, 1, gli_caret_color);
    }
    else if (gli_caret_shape == 2)
    {
        gli_draw_rect(x+0, y-gli_baseline+1, 1, gli_leading-2, gli_caret_color);
    }
    else if (gli_caret_shape == 3)
    {
        gli_draw_rect(x+0, y-gli_baseline+1, 2, gli_leading-2, gli_caret_color);
    }
    else
    {
        gli_draw_rect(x+0, y-gli_baseline+1, gli_cellw, gli_leading-2, gli_caret_color);
    }
}

void gli_draw_picture(picture_t *src, int x0, int y0, int dx0, int dy0, int dx1, int dy1)
{
    int x1, y1, sx0, sy0, sx1, sy1;
    int w, h;

    sx0 = 0;
    sy0 = 0;
    sx1 = src->w;
    sy1 = src->h;

    x1 = x0 + src->w;
    y1 = y0 + src->h;

    if (x1 <= dx0 || x0 >= dx1) return;
    if (y1 <= dy0 || y0 >= dy1) return;
    if (x0 < dx0)
    {
        sx0 += dx0 - x0;
        x0 = dx0;
    }
    if (y0 < dy0)
    {
        sy0 += dy0 - y0;
        y0 = dy0;
    }
    if (x1 > dx1)
    {
        sx1 += dx1 - x1;
    }
    if (y1 > dy1)
    {
        sy1 += dy1 - y1;
    }

    w = sx1 - sx0;
    h = sy1 - sy0;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            auto existing = gli_image_rgb[y + y0][x + x0];
            unsigned char sa = src->rgba[y][x][3];
            unsigned char na = 255 - sa;
            unsigned char sr = mul255(src->rgba[y][x][0], sa);
            unsigned char sg = mul255(src->rgba[y][x][1], sa);
            unsigned char sb = mul255(src->rgba[y][x][2], sa);
            gli_image_rgb[y + y0][x + x0] = Pixel<4>(sb + mul255(existing[0], na),
                                                     sg + mul255(existing[1], na),
                                                     sr + mul255(existing[2], na),
                                                     0xff);
        }
    }
}
