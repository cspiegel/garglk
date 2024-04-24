// Copyright (C) 2006-2009 by Tor Andersson.
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

#include "glk.h"
#include "garglk.h"

std::unique_ptr<window_pair_t> win_pair_create(glui32 type, glui32 rock, glui32 method, window_t *key, glui32 size)
{
    return std::make_unique<window_pair_t>(type, rock, method, key, size);
}

void window_pair_t::rearrange(const rect_t *box)
{
    rect_t box1, box2;
    int min, diff, split, splitwid, max;
    window_t *key;
    window_t *ch1, *ch2;

    bbox = *box;

    if (vertical) {
        min = bbox.x0;
        max = bbox.x1;
    } else {
        min = bbox.y0;
        max = bbox.y1;
    }
    diff = max - min;

    // We now figure split.
    if (vertical) {
        splitwid = gli_wpaddingx; // want border?
    } else {
        splitwid = gli_wpaddingy; // want border?
    }

    switch (division) {
    case winmethod_Proportional:
        split = (diff * size) / 100;
        break;

    case winmethod_Fixed:
        key = this->key;
        if (key == nullptr) {
            split = 0;
        } else {
            switch (key->type) {
            case wintype_TextBuffer:
                if (vertical) {
                    split = size * gli_cellw + gli_tmarginx * 2;
                } else {
                    split = size * gli_cellh + gli_tmarginy * 2;
                }
                break;
            case wintype_TextGrid:
                if (vertical) {
                    split = size * gli_cellw;
                } else {
                    split = size * gli_cellh;
                }
                break;
            case wintype_Graphics:
                split = gli_zoom_int(size);
                break;
            default:
                split = 0;
                break;
            }
        }
        break;

    default:
        split = diff / 2;
        break;
    }

    if (!backward) {
        split = max - split - splitwid;
    } else {
        split = min + split;
    }

    if (min >= max) {
        split = min;
    } else {
        if (split < min) {
            split = min;
        } else if (split > max - splitwid) {
            split = max - splitwid;
        }
    }

    // TODO: constrain bboxes by wintype

    if (vertical) {
        box1.x0 = bbox.x0;
        box1.x1 = split;
        box2.x0 = split + splitwid;
        box2.x1 = bbox.x1;
        box1.y0 = bbox.y0;
        box1.y1 = bbox.y1;
        box2.y0 = bbox.y0;
        box2.y1 = bbox.y1;
    } else {
        box1.y0 = bbox.y0;
        box1.y1 = split;
        box2.y0 = split + splitwid;
        box2.y1 = bbox.y1;
        box1.x0 = bbox.x0;
        box1.x1 = bbox.x1;
        box2.x0 = bbox.x0;
        box2.x1 = bbox.x1;
    }

    if (!backward) {
        ch1 = child1;
        ch2 = child2;
    } else {
        ch1 = child2;
        ch2 = child1;
    }

    gli_window_rearrange(ch1, &box1);
    gli_window_rearrange(ch2, &box2);
}

void window_pair_t::redraw()
{
    window_t *child;
    int x0, y0, x1, y1;

    gli_window_redraw(child1);
    gli_window_redraw(child2);

    if (!backward) {
        child = child1;
    } else {
        child = child2;
    }

    x0 = child->bbox.x0;
    y0 = child->yadj != 0 ? child->bbox.y0 - child->yadj : child->bbox.y0;
    x1 = child->bbox.x1;
    y1 = child->bbox.y1;

    if (vertical) {
        int xbord = wborder ? gli_wborderx : 0;
        int xpad = (gli_wpaddingx - xbord) / 2;
        gli_draw_rect(x1 + xpad, y0, xbord, y1 - y0, gli_border_color);
    } else {
        int ybord = wborder ? gli_wbordery : 0;
        int ypad = (gli_wpaddingy - ybord) / 2;
        gli_draw_rect(x0, y1 + ypad, x1 - x0, ybord, gli_border_color);
    }
}

void win_pair_click(window_pair_t *dwin, int x, int y)
{
    int x0, y0, x1, y1;

    if (dwin == nullptr) {
        return;
    }

    x0 = dwin->child1->bbox.x0;
    y0 = dwin->child1->bbox.y0;
    x1 = dwin->child1->bbox.x1;
    y1 = dwin->child1->bbox.y1;
    if (x >= x0 && x <= x1 && y >= y0 && y <= y1) {
        gli_window_click(dwin->child1, x, y);
    }

    x0 = dwin->child2->bbox.x0;
    y0 = dwin->child2->bbox.y0;
    x1 = dwin->child2->bbox.x1;
    y1 = dwin->child2->bbox.y1;
    if (x >= x0 && x <= x1 && y >= y0 && y <= y1) {
        gli_window_click(dwin->child2, x, y);
    }
}
