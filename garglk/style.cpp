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

#include <stdexcept>

#include "glk.h"
#include "garglk.h"

static bool isprop(FontFace f)
{
    return f == FontFace::PropR || f == FontFace::PropI || f == FontFace::PropB || f == FontFace::PropZ;
}

static bool isbold(FontFace f)
{
    return f == FontFace::PropB || f == FontFace::PropZ || f == FontFace::MonoB || f == FontFace::MonoZ;
}

static bool isitalic(FontFace f)
{
    return f == FontFace::PropI || f == FontFace::PropZ || f == FontFace::MonoI || f == FontFace::MonoZ;
}

static FontFace makefont(bool p, bool b, bool i)
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

void glk_stylehint_set(glui32 wintype, glui32 styl, glui32 hint, glsi32 val)
{
    bool p, b, i;

    if (!gli_conf_stylehint)
        return;

    if (wintype == wintype_AllTypes)
    {
        glk_stylehint_set(wintype_TextGrid, styl, hint, val);
        glk_stylehint_set(wintype_TextBuffer, styl, hint, val);
        return;
    }

    if (wintype != wintype_TextGrid && wintype != wintype_TextBuffer)
        return;

    try
    {
        style_t &style = wintype == wintype_TextGrid ? gli_gstyles.at(styl) :
                                                       gli_tstyles.at(styl);

        switch (hint)
        {
            case stylehint_TextColor:
                style.fg = Color((val >> 16) & 0xff,
                                 (val >> 8) & 0xff,
                                 (val) & 0xff);
                break;

            case stylehint_BackColor:
                style.bg = Color((val >> 16) & 0xff,
                                 (val >> 8) & 0xff,
                                 (val) & 0xff);
                break;

            case stylehint_ReverseColor:
                style.reverse = (val != 0);
                break;

            case stylehint_Proportional:
                if (wintype == wintype_TextBuffer)
                {
                    p = val != 0;
                    b = isbold(style.font);
                    i = isitalic(style.font);
                    style.font = makefont(p, b, i);
                }
                break;

            case stylehint_Weight:
                p = isprop(style.font);
                b = val != 0;
                i = isitalic(style.font);
                style.font = makefont(p, b, i);
                break;

            case stylehint_Oblique:
                p = isprop(style.font);
                b = isbold(style.font);
                i = val != 0;
                style.font = makefont(p, b, i);
                break;
        }

        if (wintype == wintype_TextBuffer &&
                styl == style_Normal &&
                hint == stylehint_BackColor)
        {
            gli_window_color = style.bg;
        }

        if (wintype == wintype_TextBuffer &&
                styl == style_Normal &&
                hint == stylehint_TextColor)
        {
            gli_more_color = style.fg;
            gli_caret_color = style.fg;
        }
    }
    catch (const std::out_of_range &)
    {
    }
}

void glk_stylehint_clear(glui32 wintype, glui32 styl, glui32 hint)
{
    if (!gli_conf_stylehint)
        return;

    if (wintype == wintype_AllTypes)
    {
        glk_stylehint_clear(wintype_TextGrid, styl, hint);
        glk_stylehint_clear(wintype_TextBuffer, styl, hint);
        return;
    }

    if (wintype != wintype_TextGrid && wintype != wintype_TextBuffer)
        return;

    try
    {
        style_t &style = wintype == wintype_TextGrid ? gli_gstyles.at(styl) :
                                                       gli_tstyles.at(styl);

        const style_t &def = wintype == wintype_TextGrid ? gli_gstyles_def.at(styl) :
                                                           gli_tstyles_def.at(styl);

        switch (hint)
        {
        case stylehint_TextColor:
            style.fg = def.fg;
            break;

        case stylehint_BackColor:
            style.bg = def.bg;
            break;

        case stylehint_ReverseColor:
            style.reverse = def.reverse;
            break;

        case stylehint_Proportional:
        case stylehint_Weight:
        case stylehint_Oblique:
            style.font = def.font;
            break;
        }
    }
    catch (const std::out_of_range &)
    {
    }
}

glui32 glk_style_distinguish(winid_t win, glui32 styl1, glui32 styl2)
{
    try
    {
        if (win->type == wintype_TextGrid)
        {
            window_textgrid_t *dwin = win->window.textgrid;
            return dwin->styles.at(styl1) != dwin->styles.at(styl2);
        }
        if (win->type == wintype_TextBuffer)
        {
            window_textbuffer_t *dwin = win->window.textbuffer;
            return dwin->styles.at(styl1) != dwin->styles.at(styl2);
        }
    }
    catch (const std::out_of_range &)
    {
    }

    return false;
}

glui32 glk_style_measure(winid_t win, glui32 styl, glui32 hint, glui32 *result)
{
    if (win->type != wintype_TextGrid && win->type != wintype_TextBuffer)
        return false;

    try
    {
        const style_t &style = win->type == wintype_TextGrid ? win->window.textgrid->styles.at(styl) :
                                                               win->window.textbuffer->styles.at(styl);

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
                *result = isbold(style.font);
                return true;

            case stylehint_Oblique:
                *result = isitalic(style.font);
                return true;

            case stylehint_Proportional:
                *result = isprop(style.font);
                return true;

            case stylehint_TextColor:
                *result =
                    (style.fg[0] << 16) |
                    (style.fg[1] << 8) |
                    (style.fg[2]);
                return true;

            case stylehint_BackColor:
                *result =
                    (style.bg[0] << 16) |
                    (style.bg[1] << 8) |
                    (style.bg[2]);
                return true;

            case stylehint_ReverseColor:
                *result = style.reverse;
                return true;
        }
    }
    catch (const std::out_of_range &)
    {
    }

    return false;
}
