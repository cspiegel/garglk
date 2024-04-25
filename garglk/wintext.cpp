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
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

#include "glk.h"
#include "garglk.h"

// how many pixels we add to left/right margins
#define SLOP (2 * GLI_SUBPIX)

static bool
put_picture(window_textbuffer_t *dwin, const std::shared_ptr<picture_t> &pic, glui32 align, glui32 linkval);

void window_textbuffer_t::touch(int line)
{
    int y = bbox.y0 + gli_tmarginy + (height - line - 1) * gli_leading;
    lines[line].dirty = true;
    gli_clear_selection();
    winrepaint(bbox.x0, y - 2, bbox.x1, y + gli_leading + 2);
}

void window_textbuffer_t::touchscroll()
{
    int i;
    gli_clear_selection();
    winrepaint(bbox.x0, bbox.y0, bbox.x1, bbox.y1);
    for (i = 0; i < scrollmax; i++) {
        lines[i].dirty = true;
    }
}

std::vector<char> gli_get_text(window_textbuffer_t *dwin)
{
    int s = dwin->scrollmax < SCROLLBACK ? dwin->scrollmax : SCROLLBACK - 1;

    std::vector<char> text;
    for (int lineidx = s; lineidx >= 0; lineidx--) {
        auto line = dwin->lines[lineidx];
        for (int charidx = 0; charidx < line.len; charidx++) {
            std::array<char, 4> buf;
            auto n = gli_encode_utf8(line.chars[charidx], buf.data(), 4);
            for (int i = 0; i < n; i++) {
                text.push_back(buf[i]);
            }
        }
        text.push_back(0x0a); // Unicode linefeed
    }

    return text;
}

void window_textbuffer_t::reflow()
{
    int inputbyte = -1;
    attr_t curattr;
    attr_t oldattr;
    int i, k, p, s;
    int x;

    if (height < 4 || width < 20) {
        return;
    }

    lines[0].len = numchars;

    std::vector<attr_t> attrbuf;
    std::vector<glui32> charbuf;
    std::vector<int> alignbuf;
    std::vector<std::shared_ptr<picture_t>> pictbuf;
    std::vector<glui32> hyperbuf;
    std::vector<int> offsetbuf;

    // allocate temp buffers
    try {
        attrbuf.resize(SCROLLBACK * TBLINELEN);
        charbuf.resize(SCROLLBACK * TBLINELEN);
        alignbuf.resize(SCROLLBACK);
        pictbuf.resize(SCROLLBACK);
        hyperbuf.resize(SCROLLBACK);
        offsetbuf.resize(SCROLLBACK);
    } catch (const std::bad_alloc &) {
        return;
    }

    // copy text to temp buffers

    oldattr = attr;
    curattr.clear();

    x = 0;
    p = 0;
    s = scrollmax < SCROLLBACK ? scrollmax : SCROLLBACK - 1;

    for (k = s; k >= 0; k--) {
        if (k == 0 && line_request) {
            inputbyte = p + infence;
        }

        if (lines[k].lpic) {
            offsetbuf[x] = p;
            alignbuf[x] = imagealign_MarginLeft;
            pictbuf[x] = lines[k].lpic;
            hyperbuf[x] = lines[k].lhyper;
            x++;
        }

        if (lines[k].rpic) {
            offsetbuf[x] = p;
            alignbuf[x] = imagealign_MarginRight;
            pictbuf[x] = lines[k].rpic;
            hyperbuf[x] = lines[k].rhyper;
            x++;
        }

        for (i = 0; i < lines[k].len; i++) {
            attrbuf[p] = curattr = lines[k].attrs[i];
            charbuf[p] = lines[k].chars[i];
            p++;
        }

        if (lines[k].newline) {
            attrbuf[p] = curattr;
            charbuf[p] = '\n';
            p++;
        }
    }

    offsetbuf[x] = -1;

    // clear window

    clear();

    // and dump text back

    x = 0;
    for (i = 0; i < p; i++) {
        if (i == inputbyte) {
            break;
        }
        attr = attrbuf[i];

        if (offsetbuf[x] == i) {
            put_picture(this, pictbuf[x], alignbuf[x], hyperbuf[x]);
            x++;
        }

        put_char_uni(charbuf[i]);
    }

    // terribly sorry about this...
    lastseen = 0;
    scrollpos = 0;

    if (inputbyte != -1) {
        infence = numchars;
        put_text_uni(charbuf.data() + inputbyte, p - inputbyte, numchars, 0);
        incurs = numchars;
    }

    attr = oldattr;

    touchscroll();
}

void window_textbuffer_t::rearrange(const rect_t *box)
{
    int newwid, newhgt;
    int rnd;

    bbox = *box;

    newwid = (box->x1 - box->x0 - gli_tmarginx * 2 - gli_scroll_width) / gli_cellw;
    newhgt = (box->y1 - box->y0 - gli_tmarginy * 2) / gli_cellh;

    // align text with bottom
    rnd = newhgt * gli_cellh + gli_tmarginy * 2;
    yadj = (box->y1 - box->y0 - rnd);
    bbox.y0 += (box->y1 - box->y0 - rnd);

    if (newwid != width) {
        width = newwid;
        reflow();
    }

    if (newhgt != height) {
        // scroll up if we obscure new lines
        if (lastseen >= newhgt - 1) {
            scrollpos += (height - newhgt);
        }

        height = newhgt;

        // keep window within 'valid' lines
        if (scrollpos > scrollmax - height + 1) {
            scrollpos = scrollmax - height + 1;
        }
        if (scrollpos < 0) {
            scrollpos = 0;
        }
        touchscroll();

        // allocate copy buffer
        copybuf.resize(height * TBLINELEN);
        std::fill(copybuf.begin(), copybuf.end(), 0);

        copypos = 0;
    }
}

int window_textbuffer_t::calcwidth(const glui32 *chars, const attr_t *attrs, int startchar, int numchars, int spw)
{
    int w = 0;
    int a, b;

    a = startchar;
    for (b = startchar; b < numchars; b++) {
        if (attrs[a] != attrs[b]) {
            w += gli_string_width_uni(attrs[a].font(m_styles),
                    chars + a, b - a, spw);
            a = b;
        }
    }

    w += gli_string_width_uni(attrs[a].font(m_styles),
            chars + a, b - a, spw);

    return w;
}

int window_textbuffer_t::calcwidth( const std::array<glui32, TBLINELEN> &chars, const std::array<attr_t, TBLINELEN> &attrs, int startchar, int numchars, int spw)
{
    return calcwidth(chars.data(), attrs.data(), startchar, numchars, spw);
}

void window_textbuffer_t::redraw()
{
    tbline_t ln;
    int linelen;
    int nsp, spw, pw;
    int x0, y0, x1, y1;
    int x, y, w;
    int a, b;
    glui32 link;
    int i;
    int hx0, hx1, hy0, hy1;
    bool selbuf, selrow, selchar;
    // NOTE: GCC complains these might be used uninitialized; they're
    // not, but do this to silence the warning.
    int sx0 = 0, sx1 = 0;
    bool selleft = false, selright = false;
    int tx, tsc, tsw, lsc, rsc;

    lines[0].len = numchars;

    x0 = (bbox.x0 + gli_tmarginx) * GLI_SUBPIX;
    x1 = (bbox.x1 - gli_tmarginx - gli_scroll_width) * GLI_SUBPIX;
    y0 = bbox.y0 + gli_tmarginy;
    y1 = bbox.y1 - gli_tmarginy;

    pw = x1 - x0 - 2 * GLI_SUBPIX;

    // check if any part of buffer is selected
    selbuf = gli_check_selection(x0 / GLI_SUBPIX, y0, x1 / GLI_SUBPIX, y1);

    for (i = scrollpos + height - 1; i >= scrollpos; i--) {
        // top of line
        y = y0 + (height - (i - scrollpos) - 1) * gli_leading;

        // check if part of line is selected
        if (selbuf) {
            int ux, uy;
            selrow = gli_get_selection(x0 / GLI_SUBPIX, y,
                    x1 / GLI_SUBPIX, y + gli_leading,
                    &ux, &uy);
            sx0 = ux;
            sx1 = uy;
            selleft = (sx0 == x0 / GLI_SUBPIX);
            selright = (sx1 == x1 / GLI_SUBPIX);
        } else {
            selrow = false;
        }

        // mark selected line dirty
        if (selrow) {
            lines[i].dirty = true;
        }

        ln = lines[i];

        // skip if we can
        if (!ln.dirty && !ln.repaint && !gli_force_redraw && scrollpos == 0) {
            continue;
        }

        // repaint previously selected lines if needed
        if (ln.repaint && !gli_force_redraw) {
            gli_redraw_rect(x0 / GLI_SUBPIX, y, x1 / GLI_SUBPIX, y + gli_leading);
        }

        // keep selected line dirty and flag for repaint
        if (!selrow) {
            lines[i].dirty = false;
            lines[i].repaint = false;
        } else {
            lines[i].repaint = true;
        }

        // leave bottom line blank for [more] prompt
        if (i == scrollpos && i > 0) {
            continue;
        }

        linelen = ln.len;

        // kill spaces at the end unless they're a different color
        Color color = gli_override_bg.has_value() ? gli_window_color : bgcolor;
        while (i > 0 && linelen > 1 && ln.chars[linelen - 1] == ' '
                && ln.attrs[linelen - 1].bgcolor == color
                && !ln.attrs[linelen - 1].reverse) {
            linelen--;
        }

        // kill characters that would overwrite the scroll bar
        while (linelen > 1 && calcwidth(ln.chars, ln.attrs, 0, linelen, -1) >= pw) {
            linelen--;
        }

        // count spaces and width for justification
        if (gli_conf_justify && !ln.newline && i > 0) {
            for (a = 0, nsp = 0; a < linelen; a++) {
                if (ln.chars[a] == ' ') {
                    nsp++;
                }
            }
            w = calcwidth(ln.chars, ln.attrs, 0, linelen, 0);
            if (nsp != 0) {
                spw = (x1 - x0 - ln.lm - ln.rm - 2 * SLOP - w) / nsp;
            } else {
                spw = 0;
            }
        } else {
            spw = -1;
        }

        // find and highlight selected characters
        if (selrow && !gli_claimselect) {
            lsc = 0;
            rsc = 0;
            selchar = false;
            // optimized case for all chars selected
            if (selleft && selright) {
                rsc = linelen > 0 ? linelen - 1 : 0;
                selchar = ((calcwidth(ln.chars, ln.attrs, lsc, rsc, spw) / GLI_SUBPIX) != 0);
            } else {
                // optimized case for leftmost char selected
                if (selleft) {
                    tsc = linelen > 0 ? linelen - 1 : 0;
                    selchar = ((calcwidth(ln.chars, ln.attrs, lsc, tsc, spw) / GLI_SUBPIX) != 0);
                } else {
                    // find the substring contained by the selection
                    tx = (x0 + SLOP + ln.lm) / GLI_SUBPIX;
                    // measure string widths until we find left char
                    for (tsc = 0; tsc < linelen; tsc++) {
                        tsw = calcwidth(ln.chars, ln.attrs, 0, tsc, spw) / GLI_SUBPIX;
                        if (tsw + tx >= sx0 ||
                                (tsw + tx + GLI_SUBPIX >= sx0 && ln.chars[tsc] != ' ')) {
                            lsc = tsc;
                            selchar = true;
                            break;
                        }
                    }
                }
                if (selchar) {
                    // optimized case for rightmost char selected
                    if (selright) {
                        rsc = linelen > 0 ? linelen - 1 : 0;
                    } else {
                        // measure string widths until we find right char
                        for (tsc = lsc; tsc < linelen; tsc++) {
                            tsw = calcwidth(ln.chars, ln.attrs, lsc, tsc, spw) / GLI_SUBPIX;
                            if (tsw + sx0 < sx1) {
                                rsc = tsc;
                            }
                        }
                        if (lsc != 0 && rsc == 0) {
                            rsc = lsc;
                        }
                    }
                }
            }
            // reverse colors for selected chars
            if (selchar) {
                for (tsc = lsc; tsc <= rsc; tsc++) {
                    ln.attrs[tsc].reverse = !ln.attrs[tsc].reverse;
                    copybuf[copypos] = ln.chars[tsc];
                    copypos++;
                }
            }
            // add newline if we reach the end of the line
            if (ln.len == 0 || ln.len == (rsc + 1)) {
                copybuf[copypos] = '\n';
                copypos++;
            }
        }

        // clear any stored hyperlink coordinates
        gli_put_hyperlink(0, x0 / GLI_SUBPIX, y,
                x1 / GLI_SUBPIX, y + gli_leading);

        // fill in background colors
        color = gli_override_bg.has_value() ? gli_window_color : bgcolor;
        gli_draw_rect(x0 / GLI_SUBPIX, y,
                (x1 - x0) / GLI_SUBPIX, gli_leading,
                color);

        x = x0 + SLOP + ln.lm;
        a = 0;
        for (b = 0; b < linelen; b++) {
            if (ln.attrs[a] != ln.attrs[b]) {
                link = ln.attrs[a].hyper;
                auto font = ln.attrs[a].font(m_styles);
                color = ln.attrs[a].bg(m_styles);
                w = gli_string_width_uni(font, &ln.chars[a], b - a, spw);
                gli_draw_rect(x / GLI_SUBPIX, y,
                        w / GLI_SUBPIX, gli_leading,
                        color);
                if (link != 0) {
                    if (gli_underline_hyperlinks) {
                        gli_draw_rect(x / GLI_SUBPIX + 1, y + gli_baseline + 1,
                                w / GLI_SUBPIX + 1, 1,
                                gli_link_color);
                    }
                    gli_put_hyperlink(link, x / GLI_SUBPIX, y,
                            x / GLI_SUBPIX + w / GLI_SUBPIX,
                            y + gli_leading);
                }
                x += w;
                a = b;
            }
        }
        link = ln.attrs[a].hyper;
        auto font = ln.attrs[a].font(m_styles);
        color = ln.attrs[a].bg(m_styles);
        w = gli_string_width_uni(font, &ln.chars[a], b - a, spw);
        gli_draw_rect(x / GLI_SUBPIX, y, w / GLI_SUBPIX,
                gli_leading, color);
        if (link != 0) {
            if (gli_underline_hyperlinks) {
                gli_draw_rect(x / GLI_SUBPIX + 1, y + gli_baseline + 1,
                        w / GLI_SUBPIX + 1, 1,
                        gli_link_color);
            }
            gli_put_hyperlink(link, x / GLI_SUBPIX, y,
                    x / GLI_SUBPIX + w / GLI_SUBPIX,
                    y + gli_leading);
        }
        x += w;

        color = gli_override_bg.has_value() ? gli_window_color : bgcolor;
        gli_draw_rect(x / GLI_SUBPIX, y,
                x1 / GLI_SUBPIX - x / GLI_SUBPIX, gli_leading,
                color);

        //
        // draw caret
        //

        if (gli_focuswin == this && i == 0 && (line_request || line_request_uni)) {
            w = calcwidth(chars, attrs, 0, incurs, spw);
            if (w < pw - gli_caret_shape * 2 * GLI_SUBPIX) {
                gli_draw_caret(x0 + SLOP + ln.lm + w, y + gli_baseline);
            }
        }

        //
        // draw text
        //

        x = x0 + SLOP + ln.lm;
        a = 0;
        for (b = 0; b < linelen; b++) {
            if (ln.attrs[a] != ln.attrs[b]) {
                link = ln.attrs[a].hyper;
                font = ln.attrs[a].font(m_styles);
                color = link != 0 ? gli_link_color : ln.attrs[a].fg(m_styles);
                x = gli_draw_string_uni(x, y + gli_baseline,
                        font, color, &ln.chars[a], b - a, spw);
                a = b;
            }
        }
        link = ln.attrs[a].hyper;
        font = ln.attrs[a].font(m_styles);
        color = link != 0 ? gli_link_color : ln.attrs[a].fg(m_styles);
        gli_draw_string_uni(x, y + gli_baseline,
                font, color, &ln.chars[a], linelen - a, spw);
    }

    //
    // draw more prompt
    //

    if (scrollpos != 0 && height > 1) {
        x = x0 + SLOP;
        y = y0 + (height - 1) * gli_leading;

        gli_put_hyperlink(0, x0 / GLI_SUBPIX, y,
                x1/GLI_SUBPIX, y + gli_leading);

        Color color = gli_override_bg.has_value() ? gli_window_color : bgcolor;
        gli_draw_rect(x / GLI_SUBPIX, y,
                x1 / GLI_SUBPIX - x / GLI_SUBPIX, gli_leading,
                color);

        w = gli_string_width_uni(gli_more_font,
                gli_more_prompt.data(), gli_more_prompt_len, -1);

        if (gli_more_align == 1) { // center
            x = x0 + SLOP + (x1 - x0 - w - SLOP * 2) / 2;
        } else if (gli_more_align == 2) { // right
            x = x1 - SLOP - w;
        }

        color = gli_override_fg.has_value() ? gli_more_color : fgcolor;
        gli_draw_string_uni(x, y + gli_baseline,
                gli_more_font, color,
                gli_more_prompt.data(), gli_more_prompt_len, -1);
        y1 = y; // don't want pictures overdrawing "[more]"

        // try to claim the focus
        more_request = true;
        gli_more_focus = true;
    } else {
        more_request = false;
        y1 = y0 + height * gli_leading;
    }

    //
    // draw the images
    //

    for (i = 0; i < scrollback; i++) {
        ln = lines[i];

        y = y0 + (height - (i - scrollpos) - 1) * gli_leading;

        if (ln.lpic) {
            if (y < y1 && y + ln.lpic->h > y0) {
                gli_draw_picture(ln.lpic.get(),
                        x0 / GLI_SUBPIX, y,
                        x0 / GLI_SUBPIX, y0, x1 / GLI_SUBPIX, y1);
                link = ln.lhyper;
                hy0 = y > y0 ? y : y0;
                hy1 = y + ln.lpic->h < y1 ? y + ln.lpic->h : y1;
                hx0 = x0 / GLI_SUBPIX;
                hx1 = x0 / GLI_SUBPIX + ln.lpic->w < x1 / GLI_SUBPIX
                            ? x0 / GLI_SUBPIX + ln.lpic->w
                            : x1 / GLI_SUBPIX;
                gli_put_hyperlink(link, hx0, hy0, hx1, hy1);
            }
        }

        if (ln.rpic) {
            if (y < y1 && y + ln.rpic->h > y0) {
                gli_draw_picture(ln.rpic.get(),
                        x1 / GLI_SUBPIX - ln.rpic->w, y,
                        x0 / GLI_SUBPIX, y0, x1 / GLI_SUBPIX, y1);
                link = ln.rhyper;
                hy0 = y > y0 ? y : y0;
                hy1 = y + ln.rpic->h < y1 ? y + ln.rpic->h : y1;
                hx0 = x1 / GLI_SUBPIX - ln.rpic->w > x0 / GLI_SUBPIX
                            ? x1 / GLI_SUBPIX - ln.rpic->w
                            : x0 / GLI_SUBPIX;
                hx1 = x1 / GLI_SUBPIX;
                gli_put_hyperlink(link, hx0, hy0, hx1, hy1);
            }
        }
    }

    //
    // Draw the scrollbar
    //

    // try to claim scroll keys
    scroll_request = scrollmax > height;

    if (scroll_request && gli_scroll_width != 0) {
        int t0, t1;
        x0 = bbox.x1 - gli_scroll_width;
        x1 = bbox.x1;
        y0 = bbox.y0 + gli_tmarginy;
        y1 = bbox.y1 - gli_tmarginy;

        gli_put_hyperlink(0, x0, y0, x1, y1);

        y0 += gli_scroll_width / 2;
        y1 -= gli_scroll_width / 2;

        // pos = thbot, pos - ht = thtop, max = wtop, 0 = wbot
        t0 = (scrollmax - scrollpos) - (height - 1);
        t1 = (scrollmax - scrollpos);
        if (scrollmax > height) {
            t0 = t0 * (y1 - y0) / scrollmax + y0;
            t1 = t1 * (y1 - y0) / scrollmax + y0;
        } else {
            t0 = t1 = y0;
        }

        gli_draw_rect(x0 + 1, y0, x1 - x0 - 2, y1 - y0, gli_scroll_bg);
        gli_draw_rect(x0 + 1, t0, x1 - x0 - 2, t1 - t0, gli_scroll_fg);

        for (i = 0; i < gli_scroll_width / 2 + 1; i++) {
            gli_draw_rect(x0 + gli_scroll_width / 2-i,
                    y0 - gli_scroll_width / 2 + i,
                    i * 2, 1, gli_scroll_fg);
            gli_draw_rect(x0 + gli_scroll_width / 2 - i,
                    y1 + gli_scroll_width / 2 - i,
                    i * 2, 1, gli_scroll_fg);
        }
    }

    // send selected text to clipboard
    if (selbuf && copypos != 0) {
        gli_claimselect = true;
        gli_clipboard_copy(copybuf.data(), copypos);
        for (i = 0; i < copypos; i++) {
            copybuf[i] = 0;
        }
        copypos = 0;
    }

    // no more prompt means all text has been seen
    if (!more_request) {
        lastseen = 0;
    }
}

void window_textbuffer_t::scrollresize()
{
    int i;

    lines.resize(scrollback + SCROLLBACK);

    chars = lines[0].chars.data();
    attrs = lines[0].attrs.data();

    for (i = scrollback; i < (scrollback + SCROLLBACK); i++) {
        lines[i].dirty = false;
        lines[i].repaint = false;
        lines[i].lm = 0;
        lines[i].rm = 0;
        lines[i].lpic.reset();
        lines[i].rpic.reset();
        lines[i].lhyper = 0;
        lines[i].rhyper = 0;
        lines[i].len = 0;
        lines[i].newline = false;
        lines[i].chars.fill(' ');
        lines[i].attrs.fill(attr_t{});
    }

    scrollback += SCROLLBACK;
}

void window_textbuffer_t::scrolloneline(bool forced)
{
    int i;

    lastseen++;
    scrollmax++;

    if (scrollmax > scrollback - 1
            || lastseen > scrollback - 1) {
        scrollresize();
    }

    if (lastseen >= height) {
        scrollpos++;
    }

    if (scrollpos > scrollmax - height + 1) {
        scrollpos = scrollmax - height + 1;
    }
    if (scrollpos < 0) {
        scrollpos = 0;
    }

    if (forced) {
        dashed = 0;
    }
    spaced = 0;

    lines[0].len = numchars;
    lines[0].newline = forced;

    for (i = scrollback - 1; i > 0; i--) {
        lines[i] = lines[i - 1];
        if (i < height) {
            touch(i);
        }
    }

    if (radjn != 0) {
        radjn--;
    }
    if (radjn == 0) {
        radjw = 0;
    }
    if (ladjn != 0) {
        ladjn--;
    }
    if (ladjn == 0) {
        ladjw = 0;
    }

    touch(0);
    lines[0].len = 0;
    lines[0].newline = false;
    lines[0].lm = ladjw;
    lines[0].rm = radjw;
    lines[0].lpic.reset();
    lines[0].rpic.reset();
    lines[0].lhyper = 0;
    lines[0].rhyper = 0;
    lines[0].chars.fill(' ');
    lines[0].attrs.fill(attr_t{});

    numchars = 0;

    touchscroll();
}

// only for input text
void window_textbuffer_t::put_text(const char *buf, int len, int pos, int oldlen)
{
    int diff = len - oldlen;

    if (numchars + diff >= TBLINELEN) {
        return;
    }

    if (diff != 0 && pos + oldlen < numchars) {
        std::memmove(chars + pos + len,
                chars + pos + oldlen,
                (numchars - (pos + oldlen)) * 4);
        std::memmove(attrs + pos + len,
                attrs + pos + oldlen,
                (numchars - (pos + oldlen)) * sizeof(attr_t));
    }
    if (len > 0) {
        int i;
        for (i = 0; i < len; i++) {
            chars[pos + i] = static_cast<unsigned char>(buf[i]);
            attrs[pos + i].set(style_Input);
        }
    }
    numchars += diff;

    if (inbuf != nullptr) {
        if (incurs >= pos + oldlen) {
            incurs += diff;
        } else if (incurs >= pos) {
            incurs = pos + len;
        }
    }

    touch(0);
}

void window_textbuffer_t::put_text_uni(glui32 *buf, int len, int pos, int oldlen)
{
    int diff = len - oldlen;

    if (numchars + diff >= TBLINELEN) {
        return;
    }

    if (diff != 0 && pos + oldlen < numchars) {
        std::memmove(chars + pos + len,
                chars + pos + oldlen,
                (numchars - (pos + oldlen)) * 4);
        std::memmove(attrs + pos + len,
                attrs + pos + oldlen,
                (numchars - (pos + oldlen)) * sizeof(attr_t));
    }
    if (len > 0) {
        int i;
        std::memmove(chars + pos, buf, len * 4);
        for (i = 0; i < len; i++) {
            attrs[pos + i].set(style_Input);
        }
    }
    numchars += diff;

    if (inbuf != nullptr) {
        if (incurs >= pos + oldlen) {
            incurs += diff;
        } else if (incurs >= pos) {
            incurs = pos + len;
        }
    }

    touch(0);
}

// Return true if a following quotation mark should be an opening mark,
// false if it should be a closing mark. Opening quotation marks will
// appear following an open parenthesis, open square bracket, or
// whitespace.
static bool leftquote(std::uint32_t c)
{
    switch(c) {
    case '(': case '[':

    // The following are Unicode characters in the "Separator, Space" category.
    case 0x0020: case 0x00a0: case 0x1680: case 0x2000:
    case 0x2001: case 0x2002: case 0x2003: case 0x2004:
    case 0x2005: case 0x2006: case 0x2007: case 0x2008:
    case 0x2009: case 0x200a: case 0x202f: case 0x205f:
    case 0x3000:
        return true;
    default:
        return false;
    }
}

void window_textbuffer_t::put_char_uni(glui32 ch)
{
    std::array<glui32, TBLINELEN> bchars;
    std::array<attr_t, TBLINELEN> battrs;
    int pw;
    int bpoint;
    int saved;
    int i;
    int linelen;

    // Don't speak if the current text style is input, under the
    // assumption that the interpreter is trying to display the user's
    // input. This is how Bocfel uses style_Input, and without this
    // test, extraneous input text is spoken. Other formats/interpreters
    // don't have this issue, but since this affects all Z-machine
    // games, it's probably worth the hacky solution here. If there are
    // Glulx games which use input style for text that the user did not
    // enter, that text will not get spoken. If that turns out to be a
    // problem, a new Gargoyle-specific function will probably be needed
    // that Bocfel can use to signal that it's writing input text from
    // the user vs input text from elsewhere.
    //
    // Note that this already affects history playback in Bocfel: since
    // it styles previous user input with style_Input during history
    // playback, the user input won't be spoken. That's annoying but
    // probably not quite as important as getting the expected behavior
    // during normal gameplay.
    //
    // See https://github.com/garglk/garglk/issues/356
    if (attr.style != style_Input) {
        gli_tts_speak(&ch, 1);
    }

    pw = (bbox.x1 - bbox.x0 - gli_tmarginx * 2 - gli_scroll_width) * GLI_SUBPIX;
    pw = pw - 2 * SLOP - radjw - ladjw;

    Color color = gli_override_bg.has_value() ? gli_window_color : bgcolor;

    // oops ... overflow
    if (numchars + 1 >= TBLINELEN) {
        scrolloneline(false);
    }

    if (ch == '\n') {
        scrolloneline(true);
        return;
    }

    if (gli_conf_quotes != 0) {
        // fails for 'tis a wonderful day in the '80s
        if (gli_conf_quotes == 2 && ch == '\'') {
            if (numchars == 0 || leftquote(chars[numchars - 1])) {
                ch = UNI_LSQUO;
            }
        }

        if (ch == '`') {
            ch = UNI_LSQUO;
        }

        if (ch == '\'') {
            ch = UNI_RSQUO;
        }

        if (ch == '"') {
            if (numchars == 0 || leftquote(chars[numchars - 1])) {
                ch = UNI_LDQUO;
            } else {
                ch = UNI_RDQUO;
            }
        }
    }

    // This tracks whether the font "should" be monospace, not whether
    // the font file itself is actually monospace: if the font is monor,
    // monob, monoi, or monoz, then this will be true, regardless of
    // what font the user actually set as the monospace font.
    bool monospace = gli_tstyles[attr.style].font.monospace;

    if (gli_conf_dashes != 0 && !monospace) {
        if (ch == '-') {
            dashed++;
            if (dashed == 2) {
                numchars--;
                if (gli_conf_dashes == 2) {
                    ch = UNI_NDASH;
                } else {
                    ch = UNI_MDASH;
                }
            }
            if (dashed == 3) {
                numchars--;
                ch = UNI_MDASH;
                dashed = 0;
            }
        } else {
            dashed = 0;
        }
    }

    if (gli_conf_spaces != 0 && !monospace
            && m_styles[attr.style].bg == color
            && !m_styles[attr.style].reverse) {
        // turn (period space space) into (period space)
        if (gli_conf_spaces == 1) {
            if (ch == '.') {
                spaced = 1;
            } else if (ch == ' ' && spaced == 1) {
                spaced = 2;
            } else if (ch == ' ' && spaced == 2) {
                spaced = 0;
                return;
            } else {
                spaced = 0;
            }
        }

        // turn (per sp x) into (per sp sp x)
        else if (gli_conf_spaces == 2) {
            if (ch == '.') {
                spaced = 1;
            } else if (ch == ' ' && spaced == 1) {
                spaced = 2;
            } else if (ch != ' ' && spaced == 2) {
                spaced = 0;
                put_char_uni(' ');
            } else {
                spaced = 0;
            }
        }
    }

    chars[numchars] = ch;
    attrs[numchars] = attr;
    numchars++;

    // kill spaces at the end for line width calculation
    linelen = numchars;
    while (linelen > 1 && chars[linelen - 1] == ' '
            && attrs[linelen - 1].bgcolor == color
            && !attrs[linelen - 1].reverse) {
        linelen--;
    }

    if (calcwidth(chars, attrs, 0, linelen, -1) >= pw) {
        bpoint = numchars;

        for (i = numchars - 1; i > 0; i--) {
            if (chars[i] == ' ') {
                bpoint = i + 1; // skip space
                break;
            }
        }

        saved = numchars - bpoint;

        std::memcpy(bchars.data(), chars + bpoint, saved * 4);
        std::memcpy(battrs.data(), attrs + bpoint, saved * sizeof(attr_t));
        numchars = bpoint;

        scrolloneline(false);

        std::memcpy(chars, bchars.data(), saved * 4);
        std::memcpy(attrs, battrs.data(), saved * sizeof(attr_t));
        numchars = saved;
    }

    touch(0);
}

bool window_textbuffer_t::unput_char_uni(glui32 ch)
{
    if (numchars > 0 && glk_char_to_upper(chars[numchars - 1]) == glk_char_to_upper(ch)) {
        numchars--;
        touch(0);
        return true;
    }
    return false;
}

void window_textbuffer_t::clear()
{
    int i;

    attr.fgcolor = gli_override_fg;
    attr.bgcolor = gli_override_bg;
    attr.reverse = false;

    ladjw = radjw = 0;
    ladjn = radjn = 0;

    spaced = 0;
    dashed = 0;

    numchars = 0;

    for (i = 0; i < scrollback; i++) {
        lines[i].len = 0;

        lines[i].lpic.reset();
        lines[i].rpic.reset();

        lines[i].lhyper = 0;
        lines[i].rhyper = 0;
        lines[i].lm = 0;
        lines[i].rm = 0;
        lines[i].newline = false;
        lines[i].dirty = true;
        lines[i].repaint = false;
    }

    lastseen = 0;
    scrollpos = 0;
    scrollmax = 0;

    for (i = 0; i < height; i++) {
        touch(i);
    }
}

// Prepare the window for line input.
void window_textbuffer_t::init_impl(void *buf, int maxlen, int initlen, bool unicode)
{
    int pw;

    gli_tts_flush();

    // because '>' prompt is ugly without extra space
    if (numchars != 0 && chars[numchars - 1] == '>') {
        put_char_uni(' ');
    }
     if (numchars != 0 && chars[numchars - 1] == '?') {
        put_char_uni(' ');
    }

    // make sure we have some space left for typing...
    pw = (bbox.x1 - bbox.x0 - gli_tmarginx * 2) * GLI_SUBPIX;
    pw = pw - 2 * SLOP - radjw + ladjw;
    if (calcwidth(chars, attrs, 0, numchars, -1) >= pw * 3 / 4) {
        put_char_uni('\n');
    }

    inbuf = buf;
    inunicode = unicode;
    inmax = maxlen;
    infence = numchars;
    incurs = numchars;
    origattr = attr;
    attr.set(style_Input);

    if (initlen != 0) {
        touch(0);
        if (unicode) {
            put_text_uni(static_cast<glui32 *>(buf), initlen, incurs, 0);
        } else {
            put_text(static_cast<char *>(buf), initlen, incurs, 0);
        }
    }

    if (gli_register_arr != nullptr) {
        inarrayrock = (*gli_register_arr)(inbuf, maxlen, const_cast<char *>(unicode ? "&+#!Iu" : "&+#!Cn"));
    }
}

void window_textbuffer_t::init_line(char *buf, int maxlen, int initlen)
{
    init_impl(buf, maxlen, initlen, false);
}

void window_textbuffer_t::init_line_uni(glui32 *buf, int maxlen, int initlen)
{
    init_impl(buf, maxlen, initlen, true);
}

// Abort line input, storing whatever's been typed so far.
void window_textbuffer_t::cancel_line(event_t *ev)
{
    int ix;
    int len;

    if (inbuf == nullptr) {
        return;
    }

    len = numchars - infence;
    if (echostr != nullptr) {
        gli_stream_echo_line_uni(echostr, chars + infence, len);
    }

    if (len > inmax) {
        len = inmax;
    }

    if (!inunicode) {
        for (ix = 0; ix < len; ix++) {
            glui32 ch = chars[infence + ix];
            if (ch > 0xff) {
                ch = '?';
            }
            (static_cast<char *>(inbuf))[ix] = static_cast<char>(ch);
        }
    } else {
        for (ix = 0; ix < len; ix++) {
            (static_cast<glui32 *>(inbuf))[ix] = chars[infence + ix];
        }
    }

    attr = origattr;

    ev->type = evtype_LineInput;
    ev->win = this;
    ev->val1 = len;
    ev->val2 = 0;

    line_request = false;
    line_request_uni = false;
    inbuf = nullptr;
    inmax = 0;

    if (echo_line_input) {
        put_char_uni('\n');
    } else {
        numchars = infence;
        touch(0);
    }

    if (gli_unregister_arr != nullptr) {
        const char *typedesc = (inunicode ? "&+#!Iu" : "&+#!Cn");
        (*gli_unregister_arr)(inbuf, inmax, const_cast<char *>(typedesc), inarrayrock);
    }
}

// Keybinding functions.

// Any key, when text buffer is scrolled.
bool window_textbuffer_t::accept_scroll(glui32 arg)
{
    int pageht = height - 2; // 1 for prompt, 1 for overlap
    bool startpos = scrollpos != 0;

    switch (arg) {
    case keycode_PageUp:
        scrollpos += pageht;
        break;
    case keycode_End:
        scrollpos = 0;
        break;
    case keycode_Up:
        scrollpos++;
        break;
    case keycode_Down:
    case keycode_Return:
        scrollpos--;
        break;
    case keycode_MouseWheelUp:
        scrollpos += 3;
        startpos = true;
        break;
    case keycode_MouseWheelDown:
        scrollpos -= 3;
        startpos = true;
        break;
    case ' ':
    case keycode_PageDown:
        if (pageht != 0) {
            scrollpos -= pageht;
        } else {
            scrollpos = 0;
        }
        break;
    }

    if (scrollpos > scrollmax - height + 1) {
        scrollpos = scrollmax - height + 1;
    }
    if (scrollpos < 0) {
        scrollpos = 0;
    }
    touchscroll();

    return startpos || scrollpos != 0;
}

// Any key, during character input. Ends character input.
void window_textbuffer_t::accept_readchar(glui32 arg)
{
    glui32 key;

    if (height < 2) {
        scrollpos = 0;
    }

    if (scrollpos != 0
            || arg == keycode_PageUp
            || arg == keycode_MouseWheelUp) {
        accept_scroll(arg);
        return;
    }

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

    gli_tts_purge();

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
void window_textbuffer_t::acceptline(glui32 keycode)
{
    int ix;
    int len;

    if (inbuf == nullptr) {
        return;
    }

    len = numchars - infence;
    if (echostr != nullptr) {
        gli_stream_echo_line_uni(echostr, chars + infence, len);
    }

    gli_tts_purge();
    if (gli_conf_speak_input) {
        gli_tts_speak(chars + infence, len);
        std::array<glui32, 1> newline = {'\n'};
        gli_tts_speak(newline.data(), 1);
    }

    // Store in history.
    // A history entry should not repeat the string from the entry before it.
    if (len != 0) {
        // If the iterator's not at the beginning, that means the user is in the
        // middle of a history cycle. If that's the case, the first history
        // entry is the currently-typed text, which is no longer relevant. Drop it.
        if (history_it != history.begin()) {
            history.pop_front();
        }

        std::vector<glui32> line(&chars[infence], &chars[numchars]);
        if (history.empty() || history.front() != line) {
            history.push_front(line);
        }

        while (history.size() > HISTORYLEN) {
            history.pop_back();
        }

        history_it = history.begin();
    }

    // Store in event buffer.

    if (len > inmax) {
        len = inmax;
    }

    if (!inunicode) {
        for (ix = 0; ix < len; ix++) {
            glui32 ch = chars[infence + ix];
            if (ch > 0xff) {
                ch = '?';
            }
            (static_cast<char *>(inbuf))[ix] = static_cast<char>(ch);
        }
    } else {
        for (ix = 0; ix < len; ix++) {
            (static_cast<glui32 *>(inbuf))[ix] = chars[infence + ix];
        }
    }

    attr = origattr;

    if (!line_terminators.empty()) {
        glui32 val2 = keycode;
        if (val2 == keycode_Return) {
            val2 = 0;
        }
        gli_event_store(evtype_LineInput, this, len, val2);
    } else {
        gli_event_store(evtype_LineInput, this, len, 0);
    }
    line_request = false;
    line_request_uni = false;
    inbuf = nullptr;
    inmax = 0;

    if (echo_line_input) {
        put_char_uni('\n');
    } else {
        numchars = infence;
        touch(0);
    }

    if (gli_unregister_arr != nullptr) {
        const char *typedesc = (inunicode ? "&+#!Iu" : "&+#!Cn");
        (*gli_unregister_arr)(inbuf, inmax, const_cast<char *>(typedesc), inarrayrock);
    }
}

// Any key, during line input.
void window_textbuffer_t::accept_readline(glui32 arg)
{
    if (height < 2) {
        scrollpos = 0;
    }

    if (scrollpos != 0
            || arg == keycode_PageUp
            || arg == keycode_MouseWheelUp) {
        accept_scroll(arg);
        return;
    }

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

    // History keys (up and down)

    case keycode_Up:
        // There is no stored history, so do nothing.
        if (history.empty()) {
            return;
        }

        // There is stored history, and this is the start of a cycle through
        // it. Store the currently-typed text (which may be empty) at the
        // front of the history buffer, and point the iterator there.
        if (history_it == history.begin()) {
            history.emplace_front(&chars[infence], &chars[numchars]);
            history_it = history.begin();
        }

        // The iterator is on the current history entry, so load the
        // previous (older) one, if any (if not, that means this is the end
        // of history, so do nothing).
        if (history_it + 1 != history.end()) {
            ++history_it;
            put_text_uni(history_it->data(), history_it->size(), infence, numchars - infence);
        }
        break;

    case keycode_Down:
        // Already at the beginning (i.e. not actively cycling through
        // history), so do nothing.
        if (history_it == history.begin()) {
            return;
        }

        // Load the next (newer) history entry.
        --history_it;
        put_text_uni(history_it->data(), history_it->size(), infence, numchars - infence);

        // If we're at the beginning now, we're done cycling, and have
        // reloaded the user's currently-typed text. Since cycling is over,
        // drop the text from the history and repoint the iterator to the
        // previous history entry.
        if (history_it == history.begin()) {
            history.pop_front();
            history_it = history.begin();
        }
        break;

    // Cursor movement keys, during line input.

    case keycode_Left:
        if (incurs <= infence) {
            return;
        }
        incurs--;
        break;

    case keycode_Right:
        if (incurs >= numchars) {
            return;
        }
        incurs++;
        break;

    case keycode_Home:
        if (incurs <= infence) {
            return;
        }
        incurs = infence;
        break;

    case keycode_End:
        if (incurs >= numchars) {
            return;
        }
        incurs = numchars;
        break;

    case keycode_SkipWordLeft:
        while (incurs > infence && chars[incurs - 1] == ' ') {
            incurs--;
        }
        while (incurs > infence && chars[incurs - 1] != ' ') {
            incurs--;
        }
        break;

    case keycode_SkipWordRight:
        while (incurs < numchars && chars[incurs] != ' ') {
            incurs++;
        }
        while (incurs < numchars && chars[incurs] == ' ') {
            incurs++;
        }
        break;

    // Delete keys, during line input.

    case keycode_Delete:
        if (incurs <= infence) {
            return;
        }
        put_text_uni(nullptr, 0, incurs - 1, 1);
        break;

    case keycode_Erase:
        if (incurs >= numchars) {
            return;
        }
        put_text_uni(nullptr, 0, incurs, 1);
        break;

    case keycode_Escape:
        if (infence >= numchars) {
            return;
        }
        put_text_uni(nullptr, 0, infence, numchars - infence);
        break;

    // Regular keys

    case keycode_Return:
        acceptline(arg);
        break;

    default:
        if (arg >= 32 && arg <= 0x10FFFF) {
            if (gli_conf_caps && (arg > 0x60 && arg < 0x7b)) {
                arg -= 0x20;
            }
            put_text_uni(&arg, 1, incurs, 0);
        }
        break;
    }

    touch(0);
}

static bool put_picture(window_textbuffer_t *dwin, const std::shared_ptr<picture_t> &pic, glui32 align, glui32 linkval)
{
    if (align == imagealign_MarginRight) {
        if (dwin->lines[0].rpic || dwin->numchars != 0) {
            return false;
        }

        dwin->radjw = (pic->w + gli_tmarginx) * GLI_SUBPIX;
        dwin->radjn = (pic->h + gli_cellh - 1) / gli_cellh;
        dwin->lines[0].rpic = pic;
        dwin->lines[0].rm = dwin->radjw;
        dwin->lines[0].rhyper = linkval;
    }

    else {
        if (align != imagealign_MarginLeft && dwin->numchars != 0) {
            dwin->put_char_uni('\n');
        }

        if (dwin->lines[0].lpic || dwin->numchars != 0) {
            return false;
        }

        dwin->ladjw = (pic->w + gli_tmarginx) * GLI_SUBPIX;
        dwin->ladjn = (pic->h + gli_cellh - 1) / gli_cellh;
        dwin->lines[0].lpic = pic;
        dwin->lines[0].lm = dwin->ladjw;
        dwin->lines[0].lhyper = linkval;

        if (align != imagealign_MarginLeft) {
            win_textbuffer_flow_break(dwin);
        }
    }

    return true;
}

bool win_textbuffer_draw_picture(window_textbuffer_t *dwin,
    glui32 image, glui32 align, bool scaled, glui32 width, glui32 height)
{
    glui32 hyperlink;

    auto pic = gli_picture_load(image);

    if (!pic) {
        return false;
    }

    if (!dwin->image_loaded) {
        gli_piclist_increment();
        dwin->image_loaded = true;
    }

    if (scaled) {
        pic = gli_picture_scale(pic.get(), width, height);
    } else {
        pic = gli_picture_scale(pic.get(), gli_zoom_int(pic->w), gli_zoom_int(pic->h));
    }

    hyperlink = dwin->attr.hyper;

    return put_picture(dwin, pic, align, hyperlink);
}

void win_textbuffer_flow_break(window_textbuffer_t *dwin)
{
    while (dwin->ladjn != 0 || dwin->radjn != 0) {
        dwin->put_char_uni('\n');
    }
}

void window_textbuffer_t::click(int sx, int sy)
{
    bool gh = false;
    bool gs = false;

    if (line_request || char_request
        || line_request_uni || char_request_uni
        || more_request || scroll_request) {
        gli_focuswin = this;
    }

    if (hyper_request) {
        glui32 linkval = gli_get_hyperlink(sx, sy);
        if (linkval != 0) {
            gli_event_store(evtype_Hyperlink, this, linkval, 0);
            hyper_request = false;
            if (gli_conf_safeclicks) {
                gli_forceclick = true;
            }
            gh = true;
        }
    }

    if (sx > bbox.x1 - gli_scroll_width) {
        if (sy < bbox.y0 + gli_tmarginy + gli_scroll_width) {
            accept_scroll(keycode_Up);
        } else if (sy > bbox.y1 - gli_tmarginy - gli_scroll_width) {
            accept_scroll(keycode_Down);
        } else if (sy < (bbox.y0 + bbox.y1) / 2) {
            accept_scroll(keycode_PageUp);
        } else {
            accept_scroll(keycode_PageDown);
        }
        gs = true;
    }

    if (!gh && !gs) {
        gli_copyselect = true;
        gli_start_selection(sx, sy);
    }
}
