/******************************************************************************
 *                                                                            *
 * Copyright (C) 2006-2009 by Tor Andersson.                                  *
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

#include <cstring>

#include "glk.h"
#include "garglk.h"

static bool isprop(enum FontFace f)
{
    return f == FontFace::PropR || f == FontFace::PropI || f == FontFace::PropB || f == FontFace::PropZ;
}

static bool isbold(enum FontFace f)
{
    return f == FontFace::PropB || f == FontFace::PropZ || f == FontFace::MonoB || f == FontFace::MonoZ;
}

static bool isitalic(enum FontFace f)
{
    return f == FontFace::PropI || f == FontFace::PropZ || f == FontFace::MonoI || f == FontFace::MonoZ;
}

static enum FontFace makefont(bool p, bool b, bool i)
{
    if ( p && !b && !i) return FontFace::PropR;
    if ( p && !b &&  i) return FontFace::PropI;
    if ( p &&  b && !i) return FontFace::PropB;
    if ( p &&  b &&  i) return FontFace::PropZ;
    if (!p && !b && !i) return FontFace::MonoR;
    if (!p && !b &&  i) return FontFace::MonoI;
    if (!p &&  b && !i) return FontFace::MonoB;
    if (!p &&  b &&  i) return FontFace::MonoZ;
    return FontFace::PropR;
}

void glk_stylehint_set(glui32 wintype, glui32 style, glui32 hint, glsi32 val)
{
    style_t *styles;
    bool p, b, i;

    if (wintype == wintype_AllTypes)
    {
        glk_stylehint_set(wintype_TextGrid, style, hint, val);
        glk_stylehint_set(wintype_TextBuffer, style, hint, val);
        return;
    }

    if (wintype == wintype_TextGrid)
        styles = gli_gstyles.data();
    else if (wintype == wintype_TextBuffer)
        styles = gli_tstyles.data();
    else
        return;

    if (!gli_conf_stylehint)
        return;

    switch (hint)
    {
        case stylehint_TextColor:
            styles[style].fg[0] = (val >> 16) & 0xff;
            styles[style].fg[1] = (val >> 8) & 0xff;
            styles[style].fg[2] = (val) & 0xff;
            break;

        case stylehint_BackColor:
            styles[style].bg[0] = (val >> 16) & 0xff;
            styles[style].bg[1] = (val >> 8) & 0xff;
            styles[style].bg[2] = (val) & 0xff;
            break;

        case stylehint_ReverseColor:
            styles[style].reverse = (val != 0);
            break;

        case stylehint_Proportional:
            if (wintype == wintype_TextBuffer)
            {
                p = val > 0;
                b = isbold(styles[style].font);
                i = isitalic(styles[style].font);
                styles[style].font = makefont(p, b, i);
            }
            break;

        case stylehint_Weight:
            p = isprop(styles[style].font);
            b = val > 0;
            i = isitalic(styles[style].font);
            styles[style].font = makefont(p, b, i);
            break;

        case stylehint_Oblique:
            p = isprop(styles[style].font);
            b = isbold(styles[style].font);
            i = val > 0;
            styles[style].font = makefont(p, b, i);
            break;
    }

    if (wintype == wintype_TextBuffer &&
            style == style_Normal &&
            hint == stylehint_BackColor)
    {
        std::memcpy(gli_window_color, styles[style].bg, 3);
    }

    if (wintype == wintype_TextBuffer &&
            style == style_Normal &&
            hint == stylehint_TextColor)
    {
        std::memcpy(gli_more_color, styles[style].fg, 3);
        std::memcpy(gli_caret_color, styles[style].fg, 3);
    }
}

void glk_stylehint_clear(glui32 wintype, glui32 style, glui32 hint)
{
    style_t *styles;
    style_t *defaults;

    if (wintype == wintype_AllTypes)
    {
        glk_stylehint_clear(wintype_TextGrid, style, hint);
        glk_stylehint_clear(wintype_TextBuffer, style, hint);
        return;
    }

    if (wintype == wintype_TextGrid)
    {
        styles = gli_gstyles.data();
        defaults = gli_gstyles_def.data();
    }
    else if (wintype == wintype_TextBuffer)
    {
        styles = gli_tstyles.data();
        defaults = gli_tstyles_def.data();
    }
    else
    {
        return;
    }

    if (!gli_conf_stylehint)
        return;

    switch (hint)
    {
        case stylehint_TextColor:
            styles[style].fg[0] = defaults[style].fg[0];
            styles[style].fg[1] = defaults[style].fg[1];
            styles[style].fg[2] = defaults[style].fg[2];
            break;

        case stylehint_BackColor:
            styles[style].bg[0] = defaults[style].bg[0];
            styles[style].bg[1] = defaults[style].bg[1];
            styles[style].bg[2] = defaults[style].bg[2];
            break;

        case stylehint_ReverseColor:
            styles[style].reverse = defaults[style].reverse;
            break;

        case stylehint_Proportional:
        case stylehint_Weight:
        case stylehint_Oblique:
            styles[style].font = defaults[style].font;
            break;
    }
}

glui32 glk_style_distinguish(winid_t win, glui32 styl1, glui32 styl2)
{
    if (win->type == wintype_TextGrid)
    {
        window_textgrid_t *dwin = win->window.textgrid;
        return std::memcmp(&dwin->styles[styl1], &dwin->styles[styl2], sizeof(style_t));
    }
    if (win->type == wintype_TextBuffer)
    {
        window_textbuffer_t *dwin = win->window.textbuffer;
        return std::memcmp(&dwin->styles[styl1], &dwin->styles[styl2], sizeof(style_t));
    }
    return 0;
}

glui32 glk_style_measure(winid_t win, glui32 style, glui32 hint, glui32 *result)
{
    style_t *styles;

    if (win->type == wintype_TextGrid)
        styles = win->window.textgrid->styles.data();
    else if (win->type == wintype_TextBuffer)
        styles = win->window.textbuffer->styles.data();
    else
        return false;

    switch (hint)
    {
        case stylehint_Indentation:
        case stylehint_ParaIndentation:
            *result = 0;
            return true;

        case stylehint_Justification:
            *result = stylehint_just_LeftFlush;
            return true;

        case stylehint_Size:
            *result = 1;
            return true;

        case stylehint_Weight:
            *result =
                (styles[style].font == FontFace::PropB ||
                 styles[style].font == FontFace::PropZ ||
                 styles[style].font == FontFace::MonoB ||
                 styles[style].font == FontFace::MonoZ);
            return true;

        case stylehint_Oblique:
            *result =
                (styles[style].font == FontFace::PropI ||
                 styles[style].font == FontFace::PropZ ||
                 styles[style].font == FontFace::MonoI ||
                 styles[style].font == FontFace::MonoZ);
            return true;

        case stylehint_Proportional:
            *result =
                (styles[style].font == FontFace::PropR ||
                 styles[style].font == FontFace::PropI ||
                 styles[style].font == FontFace::PropB ||
                 styles[style].font == FontFace::PropZ);
            return true;

        case stylehint_TextColor:
            *result =
                (styles[style].fg[0] << 16) |
                (styles[style].fg[1] << 8) |
                (styles[style].fg[2]);
            return true;

        case stylehint_BackColor:
            *result =
                (styles[style].bg[0] << 16) |
                (styles[style].bg[1] << 8) |
                (styles[style].bg[2]);
            return true;

        case stylehint_ReverseColor:
            *result = styles[style].reverse;
            return true;
    }

    return false;
}
