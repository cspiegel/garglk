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

#import <AppKit/AppKit.h>

// Declared in sysqt.h (guarded by GARGLK_CONFIG_QT_BROKER); kept in sync
// here rather than including that header, which would drag in Qt headers
// that clash awkwardly with AppKit.
namespace garglk {

void mac_hide_from_dock();
void mac_disable_window_tabbing();

// Disable window tabbing application-wide. Gargoyle has no use for tabs,
// and disabling them keeps AppKit from adding tab-related items ("Show
// Tab Bar", "Move Tab to New Window", etc.) to the Window menu. Call this
// before any window is created so no window ever adopts tabbing.
void mac_disable_window_tabbing()
{
    [NSWindow setAllowsAutomaticWindowTabbing:NO];
}

// In broker mode the interpreter has no window of its own (game windows
// are owned by the launcher), so it should never appear in the Dock or
// the application switcher. Setting QT_MAC_DISABLE_FOREGROUND_APPLICATION_
// TRANSFORM only stops Qt from promoting the process to a foreground app;
// because the interpreter executable lives inside Gargoyle.app, AppKit
// still applies the bundle's (regular) activation policy, which puts an
// icon in the Dock. Force the process to be an accessory ("UIElement")
// app so no icon appears.
void mac_hide_from_dock()
{
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

}
