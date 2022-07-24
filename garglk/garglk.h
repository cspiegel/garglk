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

/*
 * Private header file for the Gargoyle Implementation of the Glk API.
 * Glk API which this implements: version 0.7.3.
 * Glk designed by Andrew Plotkin <erkyrath@eblong.com>
 * http://www.eblong.com/zarf/glk/index.html
 */

#ifndef GARGLK_GARGLK_H
#define GARGLK_GARGLK_H

// The order here is significant: for the time being, at least, the
// macOS code directly indexes an array using these values.
enum FILEFILTERS { FILTER_SAVE, FILTER_TEXT, FILTER_DATA };

#ifdef __cplusplus
#include <array>
#include <cstring>
#include <cstddef>
#include <functional>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

enum class FontFace { MonoR, MonoB, MonoI, MonoZ, PropR, PropB, PropI, PropZ };
enum class FontType { Monospace, Proportional };
enum class FontStyle { Roman, Bold, Italic, BoldItalic };

namespace garglk {

// This represents a possible configuration file (garglk.ini).
struct ConfigFile {
    ConfigFile(const std::string &path_, bool user_) : path(path_), user(user_) {
    }

    // The path to the file itself.
    std::string path;

    // If true, this config file should be considered as a “user” config
    // file, one that a user would reasonably expect to be a config file
    // for general use. This excludes game-specific config files, for
    // example, while considering various possibilities for config
    // files, such as $HOME/.garglkrc or $HOME/.config/garglk.ini.
    bool user;
};

extern std::vector<garglk::ConfigFile> all_configs;

// C++17: std::clamp
template <typename T>
const T &clamp(const T &value, const T &min, const T &max)
{
    return value < min ? min : value > max ? max : value;
}

std::string winopenfile(const char *prompt, enum FILEFILTERS filter);
std::string winsavefile(const char *prompt, enum FILEFILTERS filter);
void winabort(const std::string &msg);
std::string downcase(const std::string &string);
void fontreplace(const std::string &font, FontType type);
std::vector<ConfigFile> configs(const std::string &exedir, const std::string &gamepath);
void config_entries(const std::string &fname, bool accept_bare, const std::vector<std::string> &matches, std::function<void(const std::string &cmd, const std::string &arg)> callback);
std::string user_config();
void set_lcdfilter(const std::string &filter);
std::string winfontpath(const std::string &filename);
std::vector<std::string> winappdata();

namespace theme {
void init();
void set(std::string name);
std::vector<std::string> paths();
std::vector<std::string> names();
}

template <typename T, typename Deleter>
std::unique_ptr<T, Deleter> unique(T *p, Deleter deleter)
{
    return std::unique_ptr<T, Deleter>(p, deleter);
}

}

template <std::size_t N>
class PixelView;

// Represents an N-byte pixel, which in reality is just an array of
// bytes. It is up to the user of this class to determine the layout,
// e.g. BGR, RGBA, etc. In actuality, as far as Gargoyle is concerned,
// this will be either RGB or RGBA. An instance of Pixel owns the pixel
// data; contrast this with PixelView which holds a pointer to a pixel.
template <std::size_t N>
class Pixel {
public:
    template <typename... Args>
    explicit Pixel(Args... args) : m_pixel{static_cast<unsigned char>(args)...} {
    }

    Pixel(const PixelView<N> &other) {
        std::copy(other.data(), other.data() + N, m_pixel.begin());
    }

    bool operator==(const Pixel<N> &other) {
        return m_pixel == other.m_pixel;
    }

    const unsigned char *data() const {
        return m_pixel.data();
    }

    unsigned char operator[](std::size_t i) const {
        return m_pixel[i];
    }

private:
    std::array<unsigned char, N> m_pixel;
};

// Represents a view to existing pixel data (see Pixel). The pixel data
// is *not* owned by instances of this class. Instead, users of this
// class must pass in a pointer to an appropriately-sized buffer (at
// least N bytes) from/to which pixel data will be read/written.
// Assigning a Pixel to a PixelData will overwrite the passed-in pixel
// with the new pixel data.
template <std::size_t N>
class PixelView {
public:
    explicit PixelView(unsigned char *data) : m_data(data) {
    }

    unsigned char operator[](std::size_t i) const {
        return m_data[i];
    }

    const unsigned char *data() const {
        return m_data;
    }

    PixelView &operator=(const Pixel<N> &other) {
        std::memcpy(m_data, other.data(), N);

        return *this;
    }

    // The meaning of this is ambiguous: copy the data, or copy the
    // pointer? To prevent its accidental use, delete it.
    PixelView &operator=(const PixelView<N> *other) = delete;

private:
    unsigned char *m_data;
};

template <std::size_t N>
class Row {
public:
    explicit Row(unsigned char *row) : m_row(row) {
    }

    const Pixel<N> operator[](std::size_t x) const {
        return Pixel<N>(&m_row[x * N]);
    }

    PixelView<N> operator[](std::size_t x) {
        return PixelView<N>(&m_row[x * N]);
    }

    void fill(const Pixel<N> &pixel, int start, int end) {
        auto data = pixel.data();
        for (int i = start; i < end; i++)
            memcpy(&m_row[i * N], data, N);
    }

private:
    unsigned char *m_row;
};

template <std::size_t N>
class Canvas {
public:
    void resize(int width, int height, bool keep) {
        if (keep)
        {
            auto backup = m_pixels;
            int minwidth = std::min(m_width, width);
            int minheight = std::min(m_height, height);

            m_pixels.resize(width * height * N);

            for (int y = 0; y < minheight; y++)
                std::memcpy(&m_pixels[y * width * N], &backup[y * m_width * N], minwidth * N);
        }
        else
        {
            m_pixels.resize(width * height * N);
        }

        m_width = width;
        m_height = height;
        m_stride = width * N;
    }

    int width() {
        return m_width;
    }

    int height() {
        return m_height;
    }

    int stride() {
        return m_stride;
    }

    void fill(const Pixel<N> &pixel) {
        for (int i = 0; i < m_width * m_height; i++)
            memcpy(&m_pixels[i * N], pixel.data(), N);
    }

    bool empty() const {
        return m_pixels.empty();
    }

    Row<N> operator[](std::size_t y) {
        return Row<N>(&m_pixels[y * stride()]);
    }

    unsigned char *data() {
        return m_pixels.data();
    }

    const unsigned char *data() const {
        return m_pixels.data();
    }

    std::size_t size() const {
        return m_pixels.size();
    }

    void clear() {
        m_pixels.clear();
        m_pixels.shrink_to_fit();
    }

private:
    std::vector<unsigned char> m_pixels;
    int m_width = 0;
    int m_height = 0;
    int m_stride = 0;
};

using Color = Pixel<3>;

Color gli_parse_color(const std::string &str);

#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "glk.h"
#include "gi_dispa.h"

/* This macro is called whenever the library code catches an error
 * or illegal operation from the game program.
 */

#define gli_strict_warning(...) do { \
    fputs("Glk library error: ", stderr); \
    fprintf(stderr, __VA_ARGS__); \
    putc('\n', stderr); \
} while(0)

extern bool gli_utf8output, gli_utf8input;

/* Callbacks necessary for the dispatch layer.  */

extern gidispatch_rock_t (*gli_register_obj)(void *obj, glui32 objclass);
extern void (*gli_unregister_obj)(void *obj, glui32 objclass,
    gidispatch_rock_t objrock);
extern gidispatch_rock_t (*gli_register_arr)(void *array, glui32 len,
    char *typecode);
extern void (*gli_unregister_arr)(void *array, glui32 len, char *typecode,
    gidispatch_rock_t objrock);

/* Some useful type declarations. */

typedef struct glk_window_struct window_t;
typedef struct glk_stream_struct stream_t;
typedef struct glk_fileref_struct fileref_t;
typedef struct glk_schannel_struct channel_t;

typedef struct window_blank_s window_blank_t;
typedef struct window_pair_s window_pair_t;
typedef struct window_textgrid_s window_textgrid_t;
typedef struct window_textbuffer_s window_textbuffer_t;
typedef struct window_graphics_s window_graphics_t;

/* ---------------------------------------------------------------------- */
/*
 * Drawing operations and fonts and stuff
 */

/* Some globals for gargoyle */

#define TBLINELEN 300
#define SCROLLBACK 512
#define HISTORYLEN 100

#define GLI_SUBPIX 8
#define gli_zoom_int(x) ((x) * gli_zoom + 0.5)
#define gli_unzoom_int(x) ((x) / gli_zoom + 0.5)

#ifdef __cplusplus
extern std::string gli_program_name;
extern std::string gli_program_info;
extern std::string gli_story_name;
extern std::string gli_story_title;
#endif

extern bool gli_terminated;

extern window_t *gli_rootwin;
extern window_t *gli_focuswin;

extern bool gli_force_redraw;
extern bool gli_more_focus;
extern int gli_cellw;
extern int gli_cellh;

/* Unicode ligatures and smart typography glyphs */
#define UNI_LIG_FF	0xFB00
#define UNI_LIG_FI	0xFB01
#define UNI_LIG_FL	0xFB02
#define UNI_LIG_FFI	0xFB03
#define UNI_LIG_FFL	0xFB04
#define UNI_LSQUO	0x2018
#define UNI_RSQUO	0x2019
#define UNI_LDQUO	0x201c
#define UNI_RDQUO	0x201d
#define UNI_NDASH	0x2013
#define UNI_MDASH	0x2014

typedef struct rect_s rect_t;
typedef struct picture_s picture_t;
typedef struct style_s style_t;

struct rect_s
{
    int x0, y0;
    int x1, y1;
};

#ifdef __cplusplus
struct picture_s
{
    picture_s(unsigned int id_, int w_, int h_, bool scaled_) : w(w_), h(h_), id(id_), scaled(scaled_) {
        rgba.resize(w, h, false);
    }

    int w, h;
    Canvas<4> rgba;
    unsigned long id;
    bool scaled;
};

struct style_s
{
    enum FontFace font;
    Color bg;
    Color fg;
    bool reverse;
};
#endif

#ifdef __cplusplus
extern Canvas<3> gli_image_rgb;
#endif

/*
 * Config globals
 */

#ifdef __cplusplus
extern std::string gli_workdir;
extern std::string gli_workfile;

extern std::array<style_t, style_NUMSTYLES> gli_tstyles;
extern std::array<style_t, style_NUMSTYLES> gli_gstyles;

extern std::array<style_t, style_NUMSTYLES> gli_tstyles_def;
extern std::array<style_t, style_NUMSTYLES> gli_gstyles_def;

extern Color gli_window_color;
extern Color gli_border_color;
extern Color gli_caret_color;
extern Color gli_more_color;
extern Color gli_link_color;

extern Color gli_window_save;
extern Color gli_border_save;
extern Color gli_caret_save;
extern Color gli_more_save;
extern Color gli_link_save;
#endif

extern bool gli_override_fg_set;
extern glui32 gli_override_fg_val;
extern bool gli_override_bg_set;
extern glui32 gli_override_bg_val;
extern bool gli_override_reverse;

extern int gli_link_style;
extern int gli_caret_shape;
extern int gli_wborderx;
extern int gli_wbordery;

extern int gli_wmarginx;
extern int gli_wmarginy;
extern int gli_wmarginx_save;
extern int gli_wmarginy_save;
extern int gli_wpaddingx;
extern int gli_wpaddingy;
extern int gli_tmarginx;
extern int gli_tmarginy;

extern float gli_backingscalefactor;
extern float gli_zoom;

extern bool gli_conf_lcd;
#ifdef __cplusplus
extern std::array<unsigned char, 5> gli_conf_lcd_weights;
#endif

extern bool gli_conf_graphics;
extern bool gli_conf_sound;

extern bool gli_conf_fullscreen;

extern bool gli_conf_speak;
extern bool gli_conf_speak_input;

#ifdef __cplusplus
extern std::string gli_conf_speak_language;
#endif

extern bool gli_conf_stylehint;
extern bool gli_conf_safeclicks;

extern bool gli_conf_justify;
extern int gli_conf_quotes;
extern int gli_conf_dashes;
extern int gli_conf_spaces;
extern bool gli_conf_caps;

extern int gli_cols;
extern int gli_rows;

extern bool gli_conf_lockcols;
extern bool gli_conf_lockrows;

extern bool gli_conf_save_window_size;
extern bool gli_conf_save_window_location;

#ifdef __cplusplus
extern Color gli_scroll_bg;
extern Color gli_scroll_fg;
#endif
extern int gli_scroll_width;

extern int gli_baseline;
extern int gli_leading;

#ifdef __cplusplus
struct gli_font_files {
    std::string r, b, i, z;
};
extern std::string gli_conf_propfont;
extern struct gli_font_files gli_conf_prop, gli_conf_prop_override;
extern std::string gli_conf_monofont;
extern struct gli_font_files gli_conf_mono, gli_conf_mono_override;
#endif

extern float gli_conf_gamma;
extern float gli_conf_propsize;
extern float gli_conf_monosize;
extern float gli_conf_propaspect;
extern float gli_conf_monoaspect;

#ifdef __cplusplus
extern std::vector<glui32> gli_more_prompt;
extern glui32 gli_more_prompt_len;
extern int gli_more_align;
extern FontFace gli_more_font;
#endif

extern bool gli_forceclick;
extern bool gli_copyselect;
extern bool gli_drawselect;
extern bool gli_claimselect;

/*
 * Standard Glk I/O stuff
 */

/* A macro that I can't think of anywhere else to put it. */

#define gli_event_clearevent(evp)  \
    ((evp)->type = evtype_None,    \
    (evp)->win = NULL,    \
    (evp)->val1 = 0,   \
    (evp)->val2 = 0)

void gli_dispatch_event(event_t *event, int polled);

#define MAGIC_WINDOW_NUM (9876)
#define MAGIC_STREAM_NUM (8769)
#define MAGIC_FILEREF_NUM (7698)

#define strtype_File (1)
#define strtype_Window (2)
#define strtype_Memory (3)
#define strtype_Resource (4)

struct glk_stream_struct
{
    glui32 magicnum;
    glui32 rock;

    int type; /* file, window, or memory stream */
    bool unicode; /* one-byte or four-byte chars? Not meaningful for windows */

    glui32 readcount, writecount;
    bool readable, writable;

    /* for strtype_Window */
    window_t *win;

    /* for strtype_File */
    FILE *file;
    glui32 lastop; /* 0, filemode_Write, or filemode_Read */

    /* for strtype_Resource */
    int isbinary;

    /* for strtype_Memory and strtype_Resource. Separate pointers for 
       one-byte and four-byte streams */
    unsigned char *buf;
    unsigned char *bufptr;
    unsigned char *bufend;
    unsigned char *bufeof;
    glui32 *ubuf;
    glui32 *ubufptr;
    glui32 *ubufend;
    glui32 *ubufeof;
    glui32 buflen;
    gidispatch_rock_t arrayrock;

    gidispatch_rock_t disprock;
    stream_t *next, *prev; /* in the big linked list of streams */
};

struct glk_fileref_struct
{
    glui32 magicnum;
    glui32 rock;

    char *filename;
    int filetype;
    bool textmode;

    gidispatch_rock_t disprock;
    fileref_t *next, *prev; /* in the big linked list of filerefs */
};

/*
 * Windows and all that
 */

// For some reason MinGW does "typedef hyper __int64", which conflicts
// with attr_s.hyper below. Unconditionally undefine it here so any
// files which include windows.h will not cause build failures.
#undef hyper

#ifdef __cplusplus
struct attr_t
{
    bool fgset = false;
    bool bgset = false;
    bool reverse = false;
    glui32 style = 0;
    glui32 fgcolor = 0;
    glui32 bgcolor = 0;
    glui32 hyper = 0;
};
#else
typedef struct
{
    bool fgset;
    bool bgset;
    bool reverse;
    glui32 style;
    glui32 fgcolor;
    glui32 bgcolor;
    glui32 hyper;
} attr_t;
#endif

#ifdef __cplusplus
// glk_window_struct needs to be visible to cheapglk, which is C, so it
// stores a pointer to WinImpl for any C++ members; the equivalent C
// struct is empty.
struct WinImpl
{
    WinImpl(Color &bgcolor_, Color &fgcolor_) : bgcolor(std::move(bgcolor_)), fgcolor(std::move(fgcolor_)) {
    }

    std::vector<glui32> line_terminators;
    Color bgcolor;
    Color fgcolor;
};
#else
struct WinImpl
{
};
#endif

struct glk_window_struct
{
    glui32 magicnum;
    glui32 rock;
    glui32 type;

    window_t *parent; /* pair window which contains this one */
    rect_t bbox;
    int yadj;
    union {
        window_textgrid_t *textgrid;
        window_textbuffer_t *textbuffer;
        window_graphics_t *graphics;
        window_blank_t *blank;
        window_pair_t *pair;
    } window;

    stream_t *str; /* the window stream. */
    stream_t *echostr; /* the window's echo stream, if any. */

    bool line_request;
    bool line_request_uni;
    bool char_request;
    bool char_request_uni;
    bool mouse_request;
    bool hyper_request;
    bool more_request;
    bool scroll_request;
    bool image_loaded;

    bool echo_line_input;
    struct WinImpl *impl;

    attr_t attr;

    gidispatch_rock_t disprock;
    window_t *next, *prev; /* in the big linked list of windows */
};

struct window_blank_s
{
    window_t *owner;
};

struct window_pair_s
{
    window_t *owner;
    window_t *child1, *child2;

    /* split info... */
    glui32 dir; /* winmethod_Left, Right, Above, or Below */
    bool vertical, backward; /* flags */
    glui32 division; /* winmethod_Fixed or winmethod_Proportional */
    window_t *key; /* NULL or a leaf-descendant (not a Pair) */
    bool keydamage; /* used as scratch space in window closing */
    glui32 size; /* size value */
    glui32 wborder;  /* winMethod_Border, NoBorder */
};

#ifdef __cplusplus
/* One line of the grid window. */
struct tgline_t
{
    int dirty;
    std::array<glui32, 256> chars;
    std::array<attr_t, 256> attrs;
};

struct window_textgrid_s
{
    window_textgrid_s(window_t *owner_, std::array<style_t, style_NUMSTYLES> styles_) :
        owner(owner_),
        styles(std::move(styles_))
    {
    }

    window_t *owner;

    int width = 0, height = 0;
    std::array<tgline_t, 256> lines;

    int curx = 0, cury = 0; /* the window cursor position */

    /* for line input */
    void *inbuf = nullptr;	/* unsigned char* for latin1, glui32* for unicode */
    int inunicode = false;
    int inorgx = 0, inorgy = 0;
    int inoriglen, inmax;
    int incurs, inlen;
    attr_t origattr;
    gidispatch_rock_t inarrayrock;
    std::vector<glui32> line_terminators;

    /* style hints and settings */
    std::array<style_t, style_NUMSTYLES> styles;
};

struct tbline_t
{
    tbline_t() {
        chars.fill(' ');
    }
    int len = 0;
    bool newline = false, dirty = false, repaint = false;
    std::shared_ptr<picture_t> lpic, rpic;
    glui32 lhyper = 0, rhyper = 0;
    int lm = 0, rm = 0;
    std::array<glui32, TBLINELEN> chars;
    std::array<attr_t, TBLINELEN> attrs;
};

struct window_textbuffer_s
{
    window_textbuffer_s(window_t *owner_, std::array<style_t, style_NUMSTYLES> styles_, int scrollback_) :
        owner(owner_),
        scrollback(scrollback_),
        styles(std::move(styles_))
    {
        lines.resize(scrollback);
        chars = lines[0].chars.data();
        attrs = lines[0].attrs.data();
    }

    window_t *owner;

    int width = -1, height = -1;
    int spaced = 0;
    int dashed = 0;

    std::vector<tbline_t> lines;
    int scrollback = SCROLLBACK;

    int numchars = 0;		/* number of chars in last line: lines[0] */
    glui32 *chars;		/* alias to lines[0].chars */
    attr_t *attrs;		/* alias to lines[0].attrs */

    /* adjust margins temporarily for images */
    int ladjw = 0;
    int ladjn = 0;
    int radjw = 0;
    int radjn = 0;

    /* Command history. */
    glui32 *history[HISTORYLEN] = {nullptr};
    int historypos = 0;
    int historyfirst = 0, historypresent = 0;

    /* for paging */
    int lastseen = 0;
    int scrollpos = 0;
    int scrollmax = 0;

    /* for line input */
    void *inbuf = nullptr;	/* unsigned char* for latin1, glui32* for unicode */
    bool inunicode = false;
    int inmax;
    long infence;
    long incurs;
    attr_t origattr;
    gidispatch_rock_t inarrayrock;

    bool echo_line_input = true;
    std::vector<glui32> line_terminators;

    /* style hints and settings */
    std::array<style_t, style_NUMSTYLES> styles;

    /* for copy selection */
    std::vector<glui32> copybuf;
    int copypos = 0;
};

struct window_graphics_s
{
    window_t *owner;
    Color bgnd;
    int dirty;
    int w, h;
    Canvas<3> rgb;
};
#endif

/* ---------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

extern void gli_initialize_sound(void);
extern void gli_initialize_tts(void);
extern void gli_tts_speak(const glui32 *buf, size_t len);
extern void gli_tts_flush(void);
extern void gli_tts_purge(void);

#ifdef __cplusplus
}
#endif

extern gidispatch_rock_t gli_sound_get_channel_disprock(const channel_t *chan);

/* ---------------------------------------------------------------------- */
/*
 * All the annoyingly boring and tedious prototypes...
 */

extern window_blank_t *win_blank_create(window_t *win);
extern void win_blank_destroy(window_blank_t *dwin);
extern void win_blank_rearrange(window_t *win, rect_t *box);
extern void win_blank_redraw(window_t *win);

extern window_pair_t *win_pair_create(window_t *win, glui32 method, window_t *key, glui32 size);
extern void win_pair_destroy(window_pair_t *dwin);
extern void win_pair_rearrange(window_t *win, rect_t *box);
extern void win_pair_redraw(window_t *win);
extern void win_pair_click(window_pair_t *dwin, int x, int y);

extern window_textgrid_t *win_textgrid_create(window_t *win);
extern void win_textgrid_destroy(window_textgrid_t *dwin);
extern void win_textgrid_rearrange(window_t *win, rect_t *box);
extern void win_textgrid_redraw(window_t *win);
extern void win_textgrid_putchar_uni(window_t *win, glui32 ch);
extern bool win_textgrid_unputchar_uni(window_t *win, glui32 ch);
extern void win_textgrid_clear(window_t *win);
extern void win_textgrid_move_cursor(window_t *win, int xpos, int ypos);
extern void win_textgrid_init_line(window_t *win, char *buf, int maxlen, int initlen);
extern void win_textgrid_init_line_uni(window_t *win, glui32 *buf, int maxlen, int initlen);
extern void win_textgrid_cancel_line(window_t *win, event_t *ev);
extern void win_textgrid_click(window_textgrid_t *dwin, int x, int y);
extern void gcmd_grid_accept_readchar(window_t *win, glui32 arg);
extern void gcmd_grid_accept_readline(window_t *win, glui32 arg);

extern window_textbuffer_t *win_textbuffer_create(window_t *win);
extern void win_textbuffer_destroy(window_textbuffer_t *dwin);
extern void win_textbuffer_rearrange(window_t *win, rect_t *box);
extern void win_textbuffer_redraw(window_t *win);
extern void win_textbuffer_putchar_uni(window_t *win, glui32 ch);
extern bool win_textbuffer_unputchar_uni(window_t *win, glui32 ch);
extern void win_textbuffer_clear(window_t *win);
extern void win_textbuffer_init_line(window_t *win, char *buf, int maxlen, int initlen);
extern void win_textbuffer_init_line_uni(window_t *win, glui32 *buf, int maxlen, int initlen);
extern void win_textbuffer_cancel_line(window_t *win, event_t *ev);
extern void win_textbuffer_click(window_textbuffer_t *dwin, int x, int y);
extern void gcmd_buffer_accept_readchar(window_t *win, glui32 arg);
extern void gcmd_buffer_accept_readline(window_t *win, glui32 arg);
extern bool gcmd_accept_scroll(window_t *win, glui32 arg);

/* Declarations of library internal functions. */

extern void gli_initialize_misc(void);
extern void gli_initialize_windows(void);
extern void gli_initialize_babel(void);

extern window_t *gli_new_window(glui32 type, glui32 rock);
extern void gli_delete_window(window_t *win);
extern window_t *gli_window_iterate_treeorder(window_t *win);

extern void gli_window_rearrange(window_t *win, rect_t *box);
extern void gli_window_redraw(window_t *win);
extern void gli_window_put_char_uni(window_t *win, glui32 ch);
extern bool gli_window_unput_char_uni(window_t *win, glui32 ch);
extern bool gli_window_check_terminator(glui32 ch);
extern void gli_window_refocus(window_t *win);

extern void gli_windows_redraw(void);
extern void gli_windows_size_change(int w, int h);
extern void gli_windows_unechostream(stream_t *str);

extern void gli_window_click(window_t *win, int x, int y);

void gli_redraw_rect(int x0, int y0, int x1, int y1);

void gli_input_handle_key(glui32 key);
void gli_input_handle_click(int x, int y);
void gli_event_store(glui32 type, window_t *win, glui32 val1, glui32 val2);

extern stream_t *gli_new_stream(int type, int readable, int writable,
    glui32 rock);
extern void gli_delete_stream(stream_t *str);
extern stream_t *gli_stream_open_window(window_t *win);
extern strid_t gli_stream_open_pathname(char *pathname, int writemode,
    int textmode, glui32 rock);
extern void gli_stream_set_current(stream_t *str);
extern void gli_stream_fill_result(stream_t *str,
    stream_result_t *result);
extern void gli_stream_echo_line(stream_t *str, char *buf, glui32 len);
extern void gli_stream_echo_line_uni(stream_t *str, glui32 *buf, glui32 len);
extern void gli_streams_close_all(void);

void gli_initialize_fonts(void);
#ifdef __cplusplus
void gli_draw_pixel(int x, int y, const Color &rgb);
void gli_draw_clear(const Color &rgb);
void gli_draw_rect(int x, int y, int w, int h, const Color &rgb);
int gli_draw_string_uni(int x, int y, FontFace face, const Color &rgb, glui32 *text, int len, int spacewidth);
int gli_string_width_uni(FontFace font, const glui32 *text, int len, int spw);
#endif
void gli_draw_caret(int x, int y);
void gli_draw_picture(picture_t *pic, int x, int y, int x0, int y0, int x1, int y1);

void gli_startup(int argc, char *argv[]);

extern void gli_select(event_t *event, int polled);
#ifdef GARGLK_TICK
extern void gli_tick(void);
#endif

void wininit(int *argc, char **argv);
void winopen(void);
void wintitle(void);
void winmore(void);
void winrepaint(int x0, int y0, int x1, int y1);
bool windark(void);
void winexit(void);
void winclipstore(glui32 *text, int len);

void fontload(void);
void fontunload(void);

void giblorb_get_resource(glui32 usage, glui32 resnum, FILE **file, long *pos, long *len, glui32 *type);

#ifdef __cplusplus
std::shared_ptr<picture_t> gli_picture_load(unsigned long id);
void gli_picture_store(std::shared_ptr<picture_t> pic);
std::shared_ptr<picture_t> gli_picture_retrieve(unsigned long id, bool scaled);
std::shared_ptr<picture_t> gli_picture_scale(picture_t *src, int destwidth, int destheight);
#endif
void gli_piclist_increment(void);
void gli_piclist_decrement(void);

window_graphics_t *win_graphics_create(window_t *win);
void win_graphics_destroy(window_graphics_t *cutwin);
void win_graphics_rearrange(window_t *win, rect_t *box);
void win_graphics_get_size(window_t *win, glui32 *width, glui32 *height);
void win_graphics_redraw(window_t *win);
void win_graphics_click(window_graphics_t *dwin, int x, int y);

bool win_graphics_draw_picture(window_graphics_t *cutwin,
  glui32 image, glsi32 xpos, glsi32 ypos,
  bool scale, glui32 imagewidth, glui32 imageheight);
void win_graphics_erase_rect(window_graphics_t *cutwin, bool whole, glsi32 xpos, glsi32 ypos, glui32 width, glui32 height);
void win_graphics_fill_rect(window_graphics_t *cutwin, glui32 color, glsi32 xpos, glsi32 ypos, glui32 width, glui32 height);
void win_graphics_set_background_color(window_graphics_t *cutwin, glui32 color);

glui32 win_textbuffer_draw_picture(window_textbuffer_t *dwin, glui32 image, glui32 align, bool scaled, glui32 width, glui32 height);
glui32 win_textbuffer_flow_break(window_textbuffer_t *win);

void gli_calc_padding(window_t *win, int *x, int *y);

/* unicode case mapping */

typedef glui32 gli_case_block_t[2]; /* upper, lower */
/* If both are 0xFFFFFFFF, you have to look at the special-case table */

typedef glui32 gli_case_special_t[3]; /* upper, lower, title */
/* Each of these points to a subarray of the unigen_special_array
(in cgunicode.c). In that subarray, element zero is the length,
and that's followed by length unicode values. */

typedef glui32 gli_decomp_block_t[2]; /* count, position */
/* The position points to a subarray of the unigen_decomp_array.
   If the count is zero, there is no decomposition. */

void gli_putchar_utf8(glui32 val, FILE *fl);
glui32 gli_getchar_utf8(FILE *fl);
glui32 gli_parse_utf8(const unsigned char *buf, glui32 buflen, glui32 *out, glui32 outlen);

glui32 gli_strlen_uni(const glui32 *s);

void gli_put_hyperlink(glui32 linkval, unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1);
glui32 gli_get_hyperlink(int x, int y);
void gli_clear_selection(void);
bool gli_check_selection(int x0, int y0, int x1, int y1);
bool gli_get_selection(int x0, int y0, int x1, int y1, int *rx0, int *rx1);
void gli_clipboard_copy(glui32 *buf, int len);
void gli_start_selection(int x, int y);
void gli_resize_mask(unsigned int x, unsigned int y);
void gli_move_selection(int x, int y);
void gli_notification_waiting(void);

void attrset(attr_t *attr, glui32 style);
void attrclear(attr_t *attr);
bool attrequal(const attr_t *a1, const attr_t *a2);
#ifdef __cplusplus
Color attrfg(style_t *styles, attr_t *attr);
Color attrbg(style_t *styles, attr_t *attr);

FontFace attrfont(const style_t *styles, const attr_t *attr);
#endif

/* A macro which reads and decodes one character of UTF-8. Needs no
   explanation, I'm sure.

   Oh, okay. The character will be written to *chptr (so pass in "&ch",
   where ch is a glui32 variable). eofcond should be a condition to
   evaluate end-of-stream -- true if no more characters are readable.
   nextch is a function which reads the next character; this is invoked
   exactly as many times as necessary.

   val0, val1, val2, val3 should be glui32 scratch variables. The macro
   needs these. Just define them, you don't need to pay attention to them
   otherwise.

   The macro itself evaluates to true if ch was successfully set, or
   false if something went wrong. (Not enough characters, or an
   invalid byte sequence.)

   This is not the worst macro I've ever written, but I forget what the
   other one was.
*/

#define UTF8_DECODE_INLINE(chptr, eofcond, nextch, val0, val1, val2, val3)  ( \
    (eofcond ? 0 : ( \
        (((val0=nextch) < 0x80) ? (*chptr=val0, 1) : ( \
            (eofcond ? 0 : ( \
                (((val1=nextch) & 0xC0) != 0x80) ? 0 : ( \
                    (((val0 & 0xE0) == 0xC0) ? (*chptr=((val0 & 0x1F) << 6) | (val1 & 0x3F), 1) : ( \
                        (eofcond ? 0 : ( \
                            (((val2=nextch) & 0xC0) != 0x80) ? 0 : ( \
                                (((val0 & 0xF0) == 0xE0) ? (*chptr=(((val0 & 0xF)<<12)  & 0x0000F000) | (((val1 & 0x3F)<<6) & 0x00000FC0) | (((val2 & 0x3F))    & 0x0000003F), 1) : ( \
                                    (((val0 & 0xF0) != 0xF0 || eofcond) ? 0 : (\
                                        (((val3=nextch) & 0xC0) != 0x80) ? 0 : (*chptr=(((val0 & 0x7)<<18)   & 0x1C0000) | (((val1 & 0x3F)<<12) & 0x03F000) | (((val2 & 0x3F)<<6)  & 0x000FC0) | (((val3 & 0x3F))     & 0x00003F), 1) \
                                        )) \
                                    )) \
                                )) \
                            )) \
                        )) \
                )) \
            )) \
        )) \
    )

#endif
