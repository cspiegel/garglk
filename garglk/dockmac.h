// Copyright (C) 2026 by Chris Spiegel.
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

#ifndef GARGLK_DOCKMAC_H
#define GARGLK_DOCKMAC_H

// AppKit bits needed by the Qt broker on macOS, implemented in
// dockmac.mm. This header deliberately pulls in neither Qt nor AppKit,
// so that it can be included both by the Objective-C++ implementation
// and by the Qt sources which use it (the two sets of headers clash).

namespace garglk {

// Keep the (windowless) interpreter process out of the Dock and the
// application switcher when running under the broker.
void mac_hide_from_dock();

// Disable window tabbing application-wide. Must be called before any
// window is created.
void mac_disable_window_tabbing();

// Hand a native menu (an NSMenu, as returned by QMenu::toNSMenu()) to
// AppKit to manage as the application's Window menu.
void mac_configure_window_menu(void *menu);

}

#endif
