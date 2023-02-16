// Copyright (C) 2006-2009 by Tor Andersson, Jesse McGrew.
// Copyright (C) 2010 by Ben Cressey, Chris Spiegel.
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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "optional.hpp"

#include "glk.h"
#include "garglk.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_LCD_FILTER_H

#ifdef _WIN32
#include <shlwapi.h>
#include <windows.h>
#endif

#define GAMMA_BITS 11
#define GAMMA_MAX ((1 << GAMMA_BITS) - 1)

#define mul255(a, b) ((static_cast<short>(a) * (b) + 127) / 255)
#define mulhigh(a, b) ((static_cast<int>(a) * (b) + (1 << (GAMMA_BITS - 1)) - 1) / GAMMA_MAX)
#define grayscale(r, g, b) ((30 * (r) + 59 * (g) + 11 * (b)) / 100)

struct Bitmap {
    int w, h, lsb, top, pitch;
    std::vector<unsigned char> data;
};

struct FontEntry {
    int adv;
    std::array<Bitmap, GLI_SUBPIX> glyph;
};

class Font {
public:
    Font(FontFace fontface, FT_Face face, const std::string &fontpath);

    const FontEntry &getglyph(glui32 cid);
    int charkern(glui32 c0, glui32 c1);
    const FT_Face &face() {
        return m_face;
    }

    FontFace fontface() {
        return m_fontface;
    }

private:
    FontFace m_fontface;
    FT_Face m_face = nullptr;
    std::unordered_map<glui32, FontEntry> m_entries;
    bool m_make_bold = false;
    bool m_make_oblique = false;
    bool m_kerned = false;
    std::unordered_map<unsigned long long, int> m_kerncache;
};

//
// Globals
//

std::vector<std::string> gli_conf_glyph_substitution_files_prop;
std::vector<std::string> gli_conf_glyph_substitution_files_mono;

static std::array<std::uint16_t, 256> gammamap;
static std::array<unsigned char, 1 << GAMMA_BITS> gammainv;

static std::unordered_map<FontFace, Font> gfont_table;
static std::unordered_map<FontFace, std::vector<Font>> unicode_fonts;

int gli_cellw = 8;
int gli_cellh = 8;

Canvas<3> gli_image_rgb;

static FT_Library ftlib;
static FT_Matrix ftmat;

static bool use_freetype_preset_filter = false;
static FT_LcdFilter freetype_preset_filter = FT_LCD_FILTER_DEFAULT;

void garglk::set_lcdfilter(const std::string &filter)
{
    use_freetype_preset_filter = true;

    if (filter == "none") {
        freetype_preset_filter = FT_LCD_FILTER_NONE;
    } else if (filter == "default") {
        freetype_preset_filter = FT_LCD_FILTER_DEFAULT;
    } else if (filter == "light") {
        freetype_preset_filter = FT_LCD_FILTER_LIGHT;
    } else if (filter == "legacy") {
        freetype_preset_filter = FT_LCD_FILTER_LEGACY;
    } else {
        use_freetype_preset_filter = false;
    }
}

//
// Font loading
//

class FreetypeError : public std::exception {
public:
    FreetypeError(FT_Error err, const std::string &basemsg) {
        // FT_Error_String() was introduced in FreeType 2.10.0.
#if FREETYPE_MAJOR == 2 && FREETYPE_MINOR < 10
        const char *errstr = nullptr;
#else
        // If FreeType was not built with FT_CONFIG_OPTION_ERROR_STRINGS,
        // this will always be nullptr.
        const char *errstr = FT_Error_String(err);
#endif

        if (errstr == nullptr) {
            m_what = basemsg + " (error code " + std::to_string(err) + ")";
        } else {
            m_what = basemsg + ": " + errstr;
        }
    };

    const char *what() const noexcept override {
        return m_what.c_str();
    }

private:
    std::string m_what;
};

const FontEntry &Font::getglyph(glui32 cid)
{
    auto it = m_entries.find(cid);
    if (it == m_entries.end()) {
        FT_Vector v;
        int err;
        glui32 gid;
        int x;
        FontEntry entry;
        std::size_t datasize;

        gid = FT_Get_Char_Index(m_face, cid);
        if (gid == 0) {
            throw std::out_of_range("no glyph for " + std::to_string(gid));
        }

        for (x = 0; x < GLI_SUBPIX; x++) {
            v.x = (x * 64) / GLI_SUBPIX;
            v.y = 0;

            FT_Set_Transform(m_face, nullptr, &v);

            err = FT_Load_Glyph(m_face, gid,
                    FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING);
            if (err != 0) {
                throw FreetypeError(err, "Error in FT_Load_Glyph");
            }

            if (m_make_bold) {
                FT_Outline_Embolden(&m_face->glyph->outline, FT_MulFix(m_face->units_per_EM, m_face->size->metrics.y_scale) / 24);
            }

            if (m_make_oblique) {
                FT_Outline_Transform(&m_face->glyph->outline, &ftmat);
            }

            if (gli_conf_lcd) {
                if (use_freetype_preset_filter) {
                    FT_Library_SetLcdFilter(ftlib, freetype_preset_filter);
                } else {
                    FT_Library_SetLcdFilterWeights(ftlib, gli_conf_lcd_weights.data());
                }

                err = FT_Render_Glyph(m_face->glyph, FT_RENDER_MODE_LCD);
            } else {
                err = FT_Render_Glyph(m_face->glyph, FT_RENDER_MODE_LIGHT);
            }

            if (err != 0) {
                throw FreetypeError(err, "Error in FT_Render_Glyph");
            }

            datasize = m_face->glyph->bitmap.pitch * m_face->glyph->bitmap.rows;
            entry.adv = (m_face->glyph->advance.x * GLI_SUBPIX + 32) / 64;

            entry.glyph[x].lsb = m_face->glyph->bitmap_left;
            entry.glyph[x].top = m_face->glyph->bitmap_top;
            entry.glyph[x].w = m_face->glyph->bitmap.width;
            entry.glyph[x].h = m_face->glyph->bitmap.rows;
            entry.glyph[x].pitch = m_face->glyph->bitmap.pitch;
            entry.glyph[x].data.assign(&m_face->glyph->bitmap.buffer[0], &m_face->glyph->bitmap.buffer[datasize]);
        }

        it = m_entries.emplace(cid, entry).first;
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
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "Software\\Tor Andersson\\Gargoyle", "Directory", RRF_RT_REG_SZ, nullptr, directory, &dsize) != ERROR_SUCCESS) {
        return "";
    }

    return std::string(directory) + "\\" + fallback;
#elif defined(GARGLK_CONFIG_FONT_PATH)
    return std::string(GARGLK_CONFIG_FONT_PATH) + "/" + fallback;
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

static std::string fontface_to_name(FontFace &fontface)
{
    std::string type = fontface.monospace ? "Mono" : "Proportional";
    std::string style = (fontface.bold && fontface.italic) ? "Bold Italic" :
                        (fontface.bold                   ) ? "Bold" :
                        (fontface.italic                 ) ? "Italic" :
                                                             "Regular";

    return type + " " + style;
}

static Font make_font(FontFace fontface, const std::string &fallback)
{
    std::string fontpath;
    std::vector<std::function<std::string(const std::string &path, const std::string &fallback)>> font_paths = {
        font_path_user,
        font_path_fallback_system,
        font_path_fallback_platform,
        font_path_fallback_local,
    };

    std::string path =
        fontface == FontFace::monor() ? gli_conf_mono.r :
        fontface == FontFace::monob() ? gli_conf_mono.b :
        fontface == FontFace::monoi() ? gli_conf_mono.i :
        fontface == FontFace::monoz() ? gli_conf_mono.z :
        fontface == FontFace::propr() ? gli_conf_prop.r :
        fontface == FontFace::propb() ? gli_conf_prop.b :
        fontface == FontFace::propi() ? gli_conf_prop.i :
        fontface == FontFace::propz() ? gli_conf_prop.z :
                                        gli_conf_mono.r;

    FT_Face face;
    if (!std::any_of(font_paths.begin(), font_paths.end(), [&](const auto &get_font_path) {
            fontpath = get_font_path(path, fallback);
            return !fontpath.empty() && FT_New_Face(ftlib, fontpath.c_str(), 0, &face) == 0;
        })) {
        garglk::winabort("Unable to find font " + (fontface.monospace ? gli_conf_monofont : gli_conf_propfont) + " for " + fontface_to_name(fontface) + ", and fallback " + fallback + " not found");
    }

    return {fontface, face, fontpath};
}

static std::vector<Font> make_unifonts(FontFace fontface)
{
    FT_Face face;
    std::vector<Font> fonts;

    auto files = fontface.monospace ?
        gli_conf_glyph_substitution_files_mono :
        gli_conf_glyph_substitution_files_prop;

    for (const auto &datadir : garglk::winappdata()) {
        files.push_back(datadir + "/unifont.otf");
        files.push_back(datadir + "/unifont_upper.otf");
    }

    files.emplace_back("./unifont.otf");
    files.emplace_back("./unifont_upper.otf");

    for (const auto &file : files) {
        if (FT_New_Face(ftlib, file.c_str(), 0, &face) == 0) {
            fonts.emplace_back(fontface, face, file);
        }
    }

    return fonts;
}

Font::Font(FontFace fontface, FT_Face face, const std::string &fontpath) :
    m_fontface(fontface),
    m_face(face)
{
    int err = 0;
    float aspect, size;

    if (m_fontface.monospace) {
        aspect = gli_conf_monoaspect;
        size = gli_conf_monosize;
    } else {
        aspect = gli_conf_propaspect;
        size = gli_conf_propsize;
    }

    auto dot = fontpath.rfind('.');
    if (dot != std::string::npos) {
        std::string afmbuf = fontpath;
        auto ext = afmbuf.substr(dot);
        if (ext == ".pfa" || ext == ".PFA" || ext == ".pfb" || ext == ".PFB") {
            afmbuf.replace(dot, std::string::npos, ".afm");
            FT_Attach_File(m_face, afmbuf.c_str());
            afmbuf.replace(dot, std::string::npos, ".AFM");
            FT_Attach_File(m_face, afmbuf.c_str());
        }
    }

    err = FT_Set_Char_Size(m_face, size * aspect * 64, size * 64, 72, 72);
    if (err != 0) {
        throw FreetypeError(err, "Error in FT_Set_Char_Size for " + fontpath);
    }

    err = FT_Select_Charmap(m_face, ft_encoding_unicode);
    if (err != 0) {
        throw FreetypeError(err, "Error in FT_Select_CharMap for " + fontpath);
    }

    m_kerned = FT_HAS_KERNING(m_face);

    m_make_bold = m_fontface.bold && ((m_face->style_flags & FT_STYLE_FLAG_BOLD) == 0);
    m_make_oblique = m_fontface.italic && ((m_face->style_flags & FT_STYLE_FLAG_ITALIC) == 0);
}

void gli_initialize_fonts()
{
    int err;

    for (int i = 0; i < 256; i++) {
        gammamap[i] = std::round(std::pow(i / 255.0, gli_conf_gamma) * GAMMA_MAX);
    }

    for (int i = 0; i <= GAMMA_MAX; i++) {
        gammainv[i] = std::round(std::pow(i / static_cast<float>(GAMMA_MAX), 1.0 / gli_conf_gamma) * 255.0);
    }

    err = FT_Init_FreeType(&ftlib);
    if (err != 0) {
        throw FreetypeError(err, "Unable to initialize FreeType");
    }

    fontload();
    garglk::fontreplace(gli_conf_monofont, FontType::Monospace);
    garglk::fontreplace(gli_conf_propfont, FontType::Proportional);
    fontunload();

    // If the user provided specific fonts, swap them in
    if (!gli_conf_mono_override.r.empty()) {
        gli_conf_mono.r = gli_conf_mono_override.r;
    }
    if (!gli_conf_mono_override.b.empty()) {
        gli_conf_mono.b = gli_conf_mono_override.b;
    }
    if (!gli_conf_mono_override.i.empty()) {
        gli_conf_mono.i = gli_conf_mono_override.i;
    }
    if (!gli_conf_mono_override.z.empty()) {
        gli_conf_mono.z = gli_conf_mono_override.z;
    }
    if (!gli_conf_prop_override.r.empty()) {
        gli_conf_prop.r = gli_conf_prop_override.r;
    }
    if (!gli_conf_prop_override.b.empty()) {
        gli_conf_prop.b = gli_conf_prop_override.b;
    }
    if (!gli_conf_prop_override.i.empty()) {
        gli_conf_prop.i = gli_conf_prop_override.i;
    }
    if (!gli_conf_prop_override.z.empty()) {
        gli_conf_prop.z = gli_conf_prop_override.z;
    }

    // Set up the Unicode fallback font.
    auto faces = {FontFace::propr(), FontFace::propi(), FontFace::propb(), FontFace::propz(),
                  FontFace::monor(), FontFace::monoi(), FontFace::monob(), FontFace::monoz()};
    for (const auto &fontface : faces) {
        try {
            unicode_fonts.insert({fontface, make_unifonts(fontface)});
        } catch (const std::runtime_error &) {
        }
    }

    // create oblique transform matrix
    ftmat.xx = 0x10000L;
    ftmat.yx = 0x00000L;
    ftmat.xy = 0x03000L;
    ftmat.yy = 0x10000L;

    auto make_entry = [](FontFace face, const std::string &filename) {
        return std::make_pair(face, make_font(face, filename));
    };

    try {
        gfont_table.insert(make_entry(FontFace::monor(), "Gargoyle-Mono.ttf"));
        gfont_table.insert(make_entry(FontFace::monob(), "Gargoyle-Mono-Bold.ttf"));
        gfont_table.insert(make_entry(FontFace::monoi(), "Gargoyle-Mono-Italic.ttf"));
        gfont_table.insert(make_entry(FontFace::monoz(), "Gargoyle-Mono-Bold-Italic.ttf"));
        gfont_table.insert(make_entry(FontFace::propr(), "Gargoyle-Serif.ttf"));
        gfont_table.insert(make_entry(FontFace::propb(), "Gargoyle-Serif-Bold.ttf"));
        gfont_table.insert(make_entry(FontFace::propi(), "Gargoyle-Serif-Italic.ttf"));
        gfont_table.insert(make_entry(FontFace::propz(), "Gargoyle-Serif-Bold-Italic.ttf"));

        const auto &entry = gfont_table.at(FontFace::monor()).getglyph('0');

        gli_cellh = gli_leading;
        gli_cellw = (entry.adv + GLI_SUBPIX - 1) / GLI_SUBPIX;
    } catch (const FreetypeError &e) {
        garglk::winabort(e.what());
    }
}

//
// Drawing
//

void gli_draw_pixel(int x, int y, const Color &rgb)
{
    if (x < 0 || x >= gli_image_rgb.width()) {
        return;
    }
    if (y < 0 || y >= gli_image_rgb.height()) {
        return;
    }
    gli_image_rgb[y][x] = rgb;
}

static void draw_pixel_gamma(int x, int y, unsigned char alpha, const Color &rgb)
{
    if (x < 0 || x >= gli_image_rgb.width()) {
        return;
    }
    if (y < 0 || y >= gli_image_rgb.height()) {
        return;
    }

    std::uint16_t invalf = GAMMA_MAX - (alpha * GAMMA_MAX / 255);
    std::array<std::uint16_t, 3> bg = {
        gammamap[gli_image_rgb[y][x][0]],
        gammamap[gli_image_rgb[y][x][1]],
        gammamap[gli_image_rgb[y][x][2]]
    };
    std::array<std::uint16_t, 3> fg = {
        gammamap[rgb[0]],
        gammamap[rgb[1]],
        gammamap[rgb[2]]
    };

    gli_image_rgb[y][x] = Pixel<3>(gammainv[fg[0] + mulhigh(static_cast<int>(bg[0]) - fg[0], invalf)],
                                   gammainv[fg[1] + mulhigh(static_cast<int>(bg[1]) - fg[1], invalf)],
                                   gammainv[fg[2] + mulhigh(static_cast<int>(bg[2]) - fg[2], invalf)]);
}

static void draw_pixel_lcd_gamma(int x, int y, const unsigned char *alpha, const Color &rgb)
{
    if (x < 0 || x >= gli_image_rgb.width()) {
        return;
    }
    if (y < 0 || y >= gli_image_rgb.height()) {
        return;
    }

    std::array<std::uint16_t, 3> invalf = {
        static_cast<std::uint16_t>(GAMMA_MAX - (alpha[0] * GAMMA_MAX / 255)),
        static_cast<std::uint16_t>(GAMMA_MAX - (alpha[1] * GAMMA_MAX / 255)),
        static_cast<std::uint16_t>(GAMMA_MAX - (alpha[2] * GAMMA_MAX / 255)),
    };
    std::array<std::uint16_t, 3> bg = {
        gammamap[gli_image_rgb[y][x][0]],
        gammamap[gli_image_rgb[y][x][1]],
        gammamap[gli_image_rgb[y][x][2]]
    };
    std::array<std::uint16_t, 3> fg = {
        gammamap[rgb[0]],
        gammamap[rgb[1]],
        gammamap[rgb[2]]
    };

    gli_image_rgb[y][x] = Pixel<3>(gammainv[fg[0] + mulhigh(static_cast<int>(bg[0]) - fg[0], invalf[0])],
                                   gammainv[fg[1] + mulhigh(static_cast<int>(bg[1]) - fg[1], invalf[1])],
                                   gammainv[fg[2] + mulhigh(static_cast<int>(bg[2]) - fg[2], invalf[2])]);
}

static void draw_bitmap_gamma(const Bitmap *b, int x, int y, const Color &rgb)
{
    int i, k, c;
    for (k = 0; k < b->h; k++) {
        for (i = 0; i < b->w; i++) {
            c = b->data[k * b->pitch + i];
            draw_pixel_gamma(x + b->lsb + i, y - b->top + k, c, rgb);
        }
    }
}

static void draw_bitmap_lcd_gamma(const Bitmap *b, int x, int y, const Color &rgb)
{
    int i, j, k;
    for (k = 0; k < b->h; k++) {
        for (i = 0, j = 0; i < b->w; i += 3, j++) {
            draw_pixel_lcd_gamma(x + b->lsb + j, y - b->top + k, b->data.data() + k * b->pitch + i, rgb);
        }
    }
}

void gli_draw_clear(const Color &rgb)
{
    gli_image_rgb.fill(rgb);
}

void gli_draw_rect(int x0, int y0, int w, int h, const Color &rgb)
{
    int x1 = x0 + w;
    int y1 = y0 + h;
    int y;

    x0 = garglk::clamp(x0, 0, gli_image_rgb.width());
    y0 = garglk::clamp(y0, 0, gli_image_rgb.height());
    x1 = garglk::clamp(x1, 0, gli_image_rgb.width());
    y1 = garglk::clamp(y1, 0, gli_image_rgb.height());

    for (y = y0; y < y1; y++) {
        gli_image_rgb[y].fill(rgb, x0, x1);
    }
}

int Font::charkern(glui32 c0, glui32 c1)
{
    FT_Vector v;
    int err;
    int g0, g1;

    if (!m_kerned) {
        return 0;
    }

    unsigned long long key = (static_cast<unsigned long long>(c0) << 32) | c1;
    try {
        return m_kerncache.at(key);
    } catch (const std::out_of_range &) {
    }

    g0 = FT_Get_Char_Index(m_face, c0);
    g1 = FT_Get_Char_Index(m_face, c1);

    if (g0 == 0 || g1 == 0) {
        return 0;
    }

    err = FT_Get_Kerning(m_face, g0, g1, FT_KERNING_UNFITTED, &v);
    if (err != 0) {
        throw FreetypeError(err, "Error in FT_Get_Kerning");
    }

    int value = (v.x * GLI_SUBPIX) / 64.0;
    m_kerncache.emplace(key, value);

    return value;
}

static const std::vector<std::pair<std::vector<glui32>, glui32>> ligatures = {
    {{'f', 'f', 'i'}, UNI_LIG_FFI},
    {{'f', 'f', 'l'}, UNI_LIG_FFL},
    {{'f', 'f'}, UNI_LIG_FF},
    {{'f', 'i'}, UNI_LIG_FI},
    {{'f', 'l'}, UNI_LIG_FL},
};

std::unordered_map<std::pair<FontFace, glui32>, nonstd::optional<Font>> glyph_to_font;

static const FontEntry &get_system_fallback_glyph(FontFace fontface, glui32 c)
{
    std::vector<std::string> get_fonts_by_glyph(FontFace fontface ,unsigned long glyph);
    std::pair<FontFace, glui32> key{fontface, c};

    auto font = glyph_to_font.find(key);
    if (font == glyph_to_font.end()) {
        glyph_to_font[key] = nonstd::nullopt;

        auto file = get_fonts_by_glyph(fontface, c);
        for (const auto &file : get_fonts_by_glyph(fontface, c)) {
            FT_Face face;
            if (FT_New_Face(ftlib, file.c_str(), 0, &face) == 0) {
                try {
                    Font font{fontface, face, file};
                    glyph_to_font.insert({key, font});
                } catch (const FreetypeError &) {
                }
            }
        }
    }

    try {
        auto &font = glyph_to_font.at(key);
        if (font.has_value()) {
            return font->getglyph(c);
        }
    } catch (const std::out_of_range &) {
    }

    throw std::runtime_error("no glyphs");
}

static int gli_string_impl(int x, FontFace fontface, const glui32 *s, std::size_t n, int spw, const std::function<void(int, const std::array<Bitmap, GLI_SUBPIX> &)> &callback)
{
    auto &f = gfont_table.at(fontface);
    bool dolig = !FT_IS_FIXED_WIDTH(f.face());
    int prev = -1;
    glui32 c;

    while (n > 0) {
        auto it = ligatures.end();
        if (dolig) {
            it = std::find_if(ligatures.begin(), ligatures.end(), [s, n](const std::pair<std::vector<glui32>, glui32> &ligentry) {
                const auto &ligature = ligentry.first;
                if (ligature.size() > n) {
                    return false;
                }

                for (std::size_t i = 0; i < ligature.size(); i++) {
                    if (s[i] != ligature[i]) {
                        return false;
                    }
                }

                return true;
            });
        }

        if (it != ligatures.end() && FT_Get_Char_Index(f.face(), it->second) != 0) {
            c = it->second;
            s += it->first.size();
            n -= it->first.size();
        } else {
            c = *s++;
            n--;
        }

        auto glyph = [&f, &fontface](glui32 c) {
            try {
                return &f.getglyph(c);
            } catch (const std::out_of_range &) {
                try {
                    return &get_system_fallback_glyph(fontface, c);
                } catch (const std::runtime_error &) {
                }

                try {
                    for (auto &font : unicode_fonts.at(fontface)) {
                        try {
                            return &font.getglyph(c);
                        } catch (const std::out_of_range &) {
                        }
                    }
                } catch (const std::out_of_range &) {
                }
            }

            throw std::out_of_range("no font found");
        };

        const FontEntry *entry;
        try {
            entry = glyph(c);
        } catch (const std::out_of_range &) {
            entry = glyph('?');
        }

        if (prev != -1) {
            x += f.charkern(prev, c);
        }

        callback(x, entry->glyph);

        if (spw >= 0 && c == ' ') {
            x += spw;
        } else {
            x += entry->adv;
        }

        prev = c;
    }

    return x;
}

int gli_draw_string_uni(int x, int y, FontFace face, const Color &rgb,
                        const glui32 *text, int len, int spacewidth)
{
    return gli_string_impl(x, face, text, len, spacewidth, [&y, &rgb](int x, const std::array<Bitmap, GLI_SUBPIX> &glyphs) {
        int px = x / GLI_SUBPIX;
        int sx = x % GLI_SUBPIX;

        if (gli_conf_lcd) {
            draw_bitmap_lcd_gamma(&glyphs[sx], px, y, rgb);
        } else {
            draw_bitmap_gamma(&glyphs[sx], px, y, rgb);
        }
    });
}

int gli_string_width_uni(FontFace face, const glui32 *text, int len, int spacewidth)
{
    return gli_string_impl(0, face, text, len, spacewidth, [](int, const std::array<Bitmap, GLI_SUBPIX> &) {});
}

void gli_draw_caret(int x, int y)
{
    x = x / GLI_SUBPIX;
    if (gli_caret_shape == 0) {
        gli_draw_rect(x + 0, y + 1, 1, 1, gli_caret_color);
        gli_draw_rect(x - 1, y + 2, 3, 1, gli_caret_color);
        gli_draw_rect(x - 2, y + 3, 5, 1, gli_caret_color);
    } else if (gli_caret_shape == 1) {
        gli_draw_rect(x + 0, y + 1, 1, 1, gli_caret_color);
        gli_draw_rect(x - 1, y + 2, 3, 1, gli_caret_color);
        gli_draw_rect(x - 2, y + 3, 5, 1, gli_caret_color);
        gli_draw_rect(x - 3, y + 4, 7, 1, gli_caret_color);
    } else if (gli_caret_shape == 2) {
        gli_draw_rect(x + 0, y - gli_baseline + 1, 1, gli_leading - 2, gli_caret_color);
    } else if (gli_caret_shape == 3) {
        gli_draw_rect(x + 0, y - gli_baseline + 1, 2, gli_leading - 2, gli_caret_color);
    } else {
        gli_draw_rect(x + 0, y - gli_baseline + 1, gli_cellw, gli_leading - 2, gli_caret_color);
    }
}

void gli_draw_picture(picture_t *pic, int x0, int y0, int dx0, int dy0, int dx1, int dy1)
{
    int x1, y1, sx0, sy0, sx1, sy1;
    int w, h;

    sx0 = 0;
    sy0 = 0;
    sx1 = pic->w;
    sy1 = pic->h;

    x1 = x0 + pic->w;
    y1 = y0 + pic->h;

    if (x1 <= dx0 || x0 >= dx1) {
        return;
    }
    if (y1 <= dy0 || y0 >= dy1) {
        return;
    }
    if (x0 < dx0) {
        sx0 += dx0 - x0;
        x0 = dx0;
    }
    if (y0 < dy0) {
        sy0 += dy0 - y0;
        y0 = dy0;
    }
    if (x1 > dx1) {
        sx1 += dx1 - x1;
    }
    if (y1 > dy1) {
        sy1 += dy1 - y1;
    }

    w = sx1 - sx0;
    h = sy1 - sy0;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            auto existing = gli_image_rgb[y + y0][x + x0];
            unsigned char sa = pic->rgba[y + sy0][x + sx0][3];
            unsigned char na = 255 - sa;
            unsigned char sr = mul255(pic->rgba[y + sy0][x + sx0][0], sa);
            unsigned char sg = mul255(pic->rgba[y + sy0][x + sx0][1], sa);
            unsigned char sb = mul255(pic->rgba[y + sy0][x + sx0][2], sa);
            gli_image_rgb[y + y0][x + x0] = Pixel<3>(sr + mul255(existing[0], na),
                                                     sg + mul255(existing[1], na),
                                                     sb + mul255(existing[2], na));
        }
    }
}
