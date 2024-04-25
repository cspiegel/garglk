// Copyright (C) 2006-2009 by Tor Andersson, Jesse McGrew.
// Copyright (C) 2010 by Ben Cressey, Chris Spiegel, Jörg Walter.
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

#include "glk.h"
#include "garglk.h"

// A grid of characters. We store the window as a list of lines.
// Within a line, just store an array of characters and an array
// of style bytes, the same size.

void window_textgrid_t::touch(int line)
{
    int y = bbox.y0 + line * gli_leading;
    lines[line].dirty = true;
    winrepaint(bbox.x0, y, bbox.x1, y + gli_leading);
}

std::unique_ptr<window_textgrid_t> win_textgrid_create(glui32 type, glui32 rock)
{
    return std::make_unique<window_textgrid_t>(type, rock, gli_gstyles);
}

void window_textgrid_t::rearrange(const rect_t *box)
{
    int newwid, newhgt;
    int k;
    bbox = *box;

    newwid = (box->x1 - box->x0) / gli_cellw;
    newhgt = (box->y1 - box->y0) / gli_cellh;

    if (newwid == width && newhgt == height) {
        return;
    }

    for (k = height; k < newhgt; k++) {
        lines[k].chars.fill(' ');
        lines[k].attrs.fill(attr_t{});
    }

    attr.clear();
    width = newwid;
    height = newhgt;

    for (k = 0; k < height; k++) {
        touch(k);
        std::fill(lines[k].chars.begin() + width, lines[k].chars.end(), ' ');
        auto *attr_end = (&lines[k].attrs[0]) + lines[k].attrs.size();
        for (auto *attr = &lines[k].attrs[width]; attr < attr_end; ++attr) {
            attr->clear();
        }
    }
}

void window_textgrid_t::redraw()
{
    tgline_t *ln;
    int x0, y0;
    int x, y, w;
    int i, a, b, k, o;
    glui32 link;

    x0 = bbox.x0;
    y0 = bbox.y0;

    for (i = 0; i < height; i++) {
        ln = &lines[i];
        if (ln->dirty || gli_force_redraw) {
            ln->dirty = false;

            x = x0;
            y = y0 + i * gli_leading;

            // clear any stored hyperlink coordinates
            gli_put_hyperlink(0, x0, y, x0 + gli_cellw * width, y + gli_leading);

            a = 0;
            for (b = 0; b < width; b++) {
                if (ln->attrs[a] != ln->attrs[b]) {
                    link = ln->attrs[a].hyper;
                    auto font = ln->attrs[a].font(m_styles);
                    Color fgcolor = link != 0 ? gli_link_color : ln->attrs[a].fg(m_styles);
                    Color bgcolor = ln->attrs[a].bg(m_styles);
                    w = (b - a) * gli_cellw;
                    gli_draw_rect(x, y, w, gli_leading, bgcolor);
                    o = x;
                    for (k = a; k < b; k++) {
                        gli_draw_string_uni(o * GLI_SUBPIX,
                                y + gli_baseline, font, fgcolor,
                                &ln->chars[k], 1, -1);
                        o += gli_cellw;
                    }
                    if (link != 0) {
                        if (gli_underline_hyperlinks) {
                            gli_draw_rect(x, y + gli_baseline + 1, w,
                                        1, gli_link_color);
                        }
                        gli_put_hyperlink(link, x, y, x + w, y + gli_leading);
                    }
                    x += w;
                    a = b;
                }
            }
            link = ln->attrs[a].hyper;
            auto font = ln->attrs[a].font(m_styles);
            Color fgcolor = link != 0 ? gli_link_color : ln->attrs[a].fg(m_styles);
            Color bgcolor = ln->attrs[a].bg(m_styles);
            w = (b - a) * gli_cellw;
            w += bbox.x1 - (x + w);
            gli_draw_rect(x, y, w, gli_leading, bgcolor);
            o = x;
            for (k = a; k < b; k++) {
                gli_draw_string_uni(o * GLI_SUBPIX,
                                    y + gli_baseline, font, fgcolor,
                                    &ln->chars[k], 1, -1);
                o += gli_cellw;
            }
            if (link != 0) {
                if (gli_underline_hyperlinks) {
                    gli_draw_rect(x, y + gli_baseline + 1, w,
                                1, gli_link_color);
                }
                gli_put_hyperlink(link, x, y, x + w, y + gli_leading);
            }
        }
    }
}

void window_textgrid_t::put_char_uni(glui32 ch)
{
    tgline_t *ln;

    // Canonicalize the cursor position. That is, the cursor may have been
    // left outside the window area; wrap it if necessary.
    if (curx < 0) {
        curx = 0;
    } else if (curx >= width) {
        curx = 0;
        cury++;
    }
    if (cury < 0) {
        cury = 0;
    } else if (cury >= height) {
        return; // outside the window
    }

    if (ch == '\n') {
        // a newline just moves the cursor.
        cury++;
        curx = 0;
        return;
    }

    touch(cury);

    ln = &(lines[cury]);
    ln->chars[curx] = ch;
    ln->attrs[curx] = this->attr;

    curx++;
    // We can leave the cursor outside the window, since it will be
    // canonicalized next time a character is printed.
}

bool window_textgrid_t::unput_char_uni(glui32 ch)
{
    tgline_t *ln;
    int oldx = curx, oldy = cury;

    // Move the cursor back.
    if (curx >= width) {
        curx = width - 1;
    } else {
        curx--;
    }

    // Canonicalize the cursor position. That is, the cursor may have been
    // left outside the window area; wrap it if necessary.
    if (curx < 0) {
        curx = width - 1;
        cury--;
    }
    if (cury < 0) {
        cury = 0;
    } else if (cury >= height) {
        return false; // outside the window
    }

    if (ch == '\n') {
        // a newline just moves the cursor.
        if (curx == width - 1) {
            return true; // deleted a newline
        }
        curx = oldx;
        cury = oldy;
        return false; // it wasn't there
    }

    ln = &(lines[cury]);
    if (glk_char_to_upper(ln->chars[curx]) == glk_char_to_upper(ch)) {
        ln->chars[curx] = ' ';
        ln->attrs[curx].clear();
        touch(cury);
        return true; // deleted the char
    } else {
        curx = oldx;
        cury = oldy;
        return false; // it wasn't there
    }
}

void window_textgrid_t::clear()
{
    int k;

    attr.fgcolor = gli_override_fg;
    attr.bgcolor = gli_override_bg;
    attr.reverse = false;

    for (k = 0; k < height; k++) {
        touch(k);
        lines[k].chars.fill(' ');
        lines[k].attrs.fill(attr_t{});
    }

    curx = 0;
    cury = 0;
}

void window_textgrid_t::move_cursor(int xpos, int ypos)
{
    // If the values are negative, they're really huge positive numbers --
    // remember that they were cast from glui32. So set them huge and
    // let canonicalization take its course.
    if (xpos < 0) {
        xpos = 32767;
    }
    if (ypos < 0) {
        ypos = 32767;
    }

    curx = xpos;
    cury = ypos;
}

void window_textgrid_t::click(int sx, int sy)
{
    int x = sx - bbox.x0;
    int y = sy - bbox.y0;

    if (line_request || char_request
        || line_request_uni || char_request_uni
        || more_request || scroll_request) {
        gli_focuswin = this;
    }

    if (mouse_request) {
        gli_event_store(evtype_MouseInput, this, x / gli_cellw, y / gli_leading);
        mouse_request = false;
        if (gli_conf_safeclicks) {
            gli_forceclick = true;
        }
    }

    if (hyper_request) {
        glui32 linkval = gli_get_hyperlink(sx, sy);
        if (linkval != 0) {
            gli_event_store(evtype_Hyperlink, this, linkval, 0);
            hyper_request = false;
            if (gli_conf_safeclicks) {
                gli_forceclick = true;
            }
        }
    }
}

// Prepare the window for line input.
void window_textgrid_t::init_impl(void *buf, int maxlen, int initlen, bool unicode)
{
    inunicode = unicode;
    inoriglen = maxlen;
    if (maxlen > (width - curx)) {
        maxlen = (width - curx);
    }

    inbuf = buf;
    inmax = maxlen;
    inlen = 0;
    incurs = 0;
    inorgx = curx;
    inorgy = cury;
    origattr = attr;
    attr.set(style_Input);

    if (initlen > maxlen) {
        initlen = maxlen;
    }

    if (initlen != 0) {
        int ix;
        tgline_t *ln = &(lines[inorgy]);

        for (ix = 0; ix < initlen; ix++) {
            ln->attrs[inorgx + ix].set(style_Input);
            if (unicode) {
                ln->chars[inorgx + ix] = (static_cast<glui32 *>(buf))[ix];
            } else {
                ln->chars[inorgx + ix] = (static_cast<unsigned char *>(buf))[ix];
            }
        }

        incurs += initlen;
        inlen += initlen;
        curx = inorgx + incurs;
        cury = inorgy;

        touch(inorgy);
    }

    if (gli_register_arr != nullptr) {
        inarrayrock = (*gli_register_arr)(inbuf, inoriglen, const_cast<char *>(unicode ? "&+#!Iu" : "&+#!Cn"));
    }
}

void window_textgrid_t::init_line(char *buf, int maxlen, int initlen)
{
    init_impl(buf, maxlen, initlen, false);
}

void window_textgrid_t::init_line_uni(glui32 *buf, int maxlen, int initlen)
{
    init_impl(buf, maxlen, initlen, true);
}

// Abort line input, storing whatever's been typed so far.
void window_textgrid_t::cancel_line(event_t *ev)
{
    int ix;
    tgline_t *ln = &(lines[inorgy]);

    if (inbuf == nullptr) {
        return;
    }

    if (!inunicode) {
        for (ix = 0; ix < inlen; ix++) {
            glui32 ch = ln->chars[inorgx + ix];
            if (ch > 0xff) {
                ch = '?';
            }
            (static_cast<char *>(inbuf))[ix] = static_cast<char>(ch);
        }
        if (echostr != nullptr) {
            gli_stream_echo_line(echostr, static_cast<char *>(inbuf), inlen);
        }
    } else {
        for (ix = 0; ix < inlen; ix++) {
            (static_cast<glui32 *>(inbuf))[ix] = ln->chars[inorgx + ix];
        }
        if (echostr != nullptr) {
            gli_stream_echo_line_uni(echostr, static_cast<glui32 *>(inbuf), inlen);
        }
    }

    cury = inorgy + 1;
    curx = 0;
    attr = origattr;

    ev->type = evtype_LineInput;
    ev->win = this;
    ev->val1 = inlen;
    ev->val2 = 0;

    line_request = false;
    line_request_uni = false;
    inbuf = nullptr;
    inoriglen = 0;
    inmax = 0;
    inorgx = 0;
    inorgy = 0;

    if (gli_unregister_arr != nullptr) {
        const char *typedesc = (inunicode ? "&+#!Iu" : "&+#!Cn");
        (*gli_unregister_arr)(inbuf, inoriglen, const_cast<char *>(typedesc), inarrayrock);
    }
}

// Keybinding functions.

// Any key, during character input. Ends character input.
void window_textgrid_t::accept_readchar(glui32 arg)
{
    glui32 key;

    switch (arg) {
    case keycode_Erase:
        key = keycode_Delete;
        break;
    case keycode_MouseWheelUp:
    case keycode_MouseWheelDown:
        return;
    default:
        key = arg;
    }

    if (key > 0xff && key < (0xffffffff - keycode_MAXVAL + 1)) {
        if (!(char_request_uni) || key > 0x10ffff) {
            key = keycode_Unknown;
        }
    }

    char_request = false;
    char_request_uni = false;
    gli_event_store(evtype_CharInput, this, key, 0);
}

// Return or enter, during line input. Ends line input.
void window_textgrid_t::acceptline(glui32 keycode)
{
    int ix;
    tgline_t *ln = &(lines[inorgy]);

    if (inbuf == nullptr) {
        return;
    }

    if (!inunicode) {
        for (ix = 0; ix < inlen; ix++) {
            (static_cast<char *>(inbuf))[ix] = static_cast<char>(ln->chars[inorgx + ix]);
        }
        if (echostr != nullptr) {
            gli_stream_echo_line(echostr, static_cast<char *>(inbuf), inlen);
        }
    } else {
        for (ix = 0; ix < inlen; ix++) {
            (static_cast<glui32 *>(inbuf))[ix] = ln->chars[inorgx + ix];
        }
        if (echostr != nullptr) {
            gli_stream_echo_line_uni(echostr, static_cast<glui32 *>(inbuf), inlen);
        }
    }

    cury = inorgy + 1;
    curx = 0;
    attr = origattr;

    if (!line_terminators.empty()) {
        glui32 val2 = keycode;
        if (val2 == keycode_Return) {
            val2 = 0;
        }
        gli_event_store(evtype_LineInput, this, inlen, val2);
    } else {
        gli_event_store(evtype_LineInput, this, inlen, 0);
    }
    line_request = false;
    line_request_uni = false;
    inbuf = nullptr;
    inoriglen = 0;
    inmax = 0;
    inorgx = 0;
    inorgy = 0;

    if (gli_unregister_arr != nullptr) {
        const char *typedesc = (inunicode ? "&+#!Iu" : "&+#!Cn");
        (*gli_unregister_arr)(inbuf, inoriglen, const_cast<char *>(typedesc), inarrayrock);
    }
}

// Any regular key, during line input.
void window_textgrid_t::accept_readline(glui32 arg)
{
    int ix;
    tgline_t *ln = &(lines[inorgy]);

    if (inbuf == nullptr) {
        return;
    }

    if (!line_terminators.empty() && gli_window_check_terminator(arg)) {
        if (std::find(line_terminators.begin(), line_terminators.end(), arg) != line_terminators.end()) {
            acceptline(arg);
            return;
        }
    }

    switch (arg) {

    // Delete keys, during line input.

    case keycode_Delete:
        if (inlen <= 0) {
            return;
        }
        if (incurs <= 0) {
            return;
        }
        for (ix = incurs; ix < inlen; ix++) {
            ln->chars[inorgx + ix - 1] = ln->chars[inorgx + ix];
        }
        ln->chars[inorgx + inlen - 1] = ' ';
        incurs--;
        inlen--;
        break;

    case keycode_Erase:
        if (inlen <= 0) {
            return;
        }
        if (incurs >= inlen) {
            return;
        }
        for (ix = incurs; ix < inlen - 1; ix++) {
            ln->chars[inorgx + ix] = ln->chars[inorgx + ix + 1];
        }
        ln->chars[inorgx + inlen - 1] = ' ';
        inlen--;
        break;

    case keycode_Escape:
        if (inlen <= 0) {
            return;
        }
        for (ix = 0; ix < inlen; ix++) {
            ln->chars[inorgx + ix] = ' ';
        }
        inlen = 0;
        incurs = 0;
        break;

    // Cursor movement keys, during line input.

    case keycode_Left:
        if (incurs <= 0) {
            return;
        }
        incurs--;
        break;

    case keycode_Right:
        if (incurs >= inlen) {
            return;
        }
        incurs++;
        break;

    case keycode_Home:
        if (incurs <= 0) {
            return;
        }
        incurs = 0;
        break;

    case keycode_End:
        if (incurs >= inlen) {
            return;
        }
        incurs = inlen;
        break;

    case keycode_Return:
        acceptline(arg);
        return;

    default:
        if (inlen >= inmax) {
            return;
        }

        if (arg < 32 || arg > 0xff) {
            return;
        }

        if (gli_conf_caps && (arg > 0x60 && arg < 0x7b)) {
            arg -= 0x20;
        }

        for (ix = inlen; ix > incurs; ix--) {
            ln->chars[inorgx + ix] = ln->chars[inorgx + ix - 1];
        }
        ln->attrs[inorgx + inlen].set(style_Input);
        ln->chars[inorgx + incurs] = arg;

        incurs++;
        inlen++;
    }

    curx = inorgx + incurs;
    cury = inorgy;

    touch(inorgy);
}
