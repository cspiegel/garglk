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

#ifndef GARGLK_BROKERQT_H
#define GARGLK_BROKERQT_H

// The protocol spoken between the gargoyle launcher and interpreter
// processes when the launcher acts as a window broker (the Qt
// equivalent of the GargoyleApp protocol in sysmac.h).
//
// In broker mode, the launcher is a single long-lived application
// which owns all game windows, the menu bar, and all dialogs.
// Interpreters are spawned with the environment variable
// GARGOYLE_SOCKET set to the name of a QLocalServer; they create no
// windows of their own, but instead render into their RGB buffer as
// usual and ship the resulting frames to the launcher, receiving
// input events back over the same socket.
//
// Each message on the wire is a header of two big-endian 32-bit
// values (message type and payload size, as serialized by
// QDataStream), followed by the payload, which is a sequence of
// QDataStream-serialized values as documented below.

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QLocalSocket>

namespace garglk::broker {

// Interpreters always render at this fixed backing scale, mirroring the
// Cocoa interface's BACKING_SCALE_FACTOR. An interpreter has no idea
// which display its window is on (the launcher owns the window), and a
// window can move between displays of different scale, so rather than
// freeze any one display's device pixel ratio, it renders at this
// constant scale and the launcher lets Qt scale each window's frames to
// whatever screen it is actually on. Every macOS display is 1x or 2x, so
// a fixed 2x is crisp on Retina and cleanly downscaled elsewhere.
inline constexpr double backing_scale = 2.0;

enum class MsgType : quint32 {
    // interpreter → launcher
    NewWindow = 1,    // bool move; qint32 x, y, width, height, minwidth, minheight; bool fullscreen (sizes/positions in logical pixels)
    SetTitle,         // QString title
    Frame,            // qint32 width, height, stride (device pixels); QByteArray packed RGB888 data
    SetCursor,        // qint32 cursor (a Cursor value)
    FileDialog,       // bool save; QString prompt, filter, start (start is a directory for open, a suggested path for save)
    ToggleFullscreen, // (empty)
    ShowText,         // qint32 style (a TextStyle value); QString title, text; bool rich

    // launcher → interpreter
    Resized,          // qint32 width, height (logical pixels)
    KeyEvent,         // qint32 key; quint32 modifiers; QString text
    MouseEvent,       // qint32 type (QEvent::Type), x, y (logical pixels), button (Qt::MouseButton)
    WheelEvent,       // qint32 pixel_x, pixel_y, degree_x, degree_y; bool page
    FullscreenState,  // bool fullscreen
    FileDialogResult, // QString path (null if canceled)
};

enum class Cursor : qint32 {
    Arrow,
    IBeam,
    Hand,
};

enum class TextStyle : qint32 {
    Info,
    Warning,
    Critical,
};

struct Message {
    MsgType type;
    QByteArray payload;
};

template <typename... Args>
QByteArray pack(const Args &...args)
{
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    (out << ... << args);
    return payload;
}

inline void send(QLocalSocket *sock, MsgType type, const QByteArray &payload = QByteArray())
{
    QByteArray header;
    QDataStream out(&header, QIODevice::WriteOnly);
    out << static_cast<quint32>(type) << static_cast<quint32>(payload.size());
    sock->write(header);
    if (!payload.isEmpty()) {
        sock->write(payload);
    }
}

// Move all complete messages out of buffer (leaving any trailing
// partial message in place) into out, which can be any container of
// Message supporting push_back().
template <typename Container>
void extract(QByteArray &buffer, Container &out)
{
    while (true) {
        if (buffer.size() < 8) {
            return;
        }

        QDataStream in(buffer);
        quint32 type, size;
        in >> type >> size;

        if (static_cast<quint32>(buffer.size()) < 8 + size) {
            return;
        }

        out.push_back(Message{static_cast<MsgType>(type), buffer.mid(8, size)});
        buffer.remove(0, 8 + size);
    }
}

}

#endif
