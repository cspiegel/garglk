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
// input events back over the same socket. Frames themselves travel
// through shared memory (see SharedFrames below), being far too large
// to want on the socket.
//
// Each message on the wire is a header of two big-endian 32-bit
// values (message type and payload size, as serialized by
// QDataStream), followed by the payload, which is a sequence of
// QDataStream-serialized values as documented below.

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QLocalSocket>
#include <QString>

#include <cstddef>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

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
    NewWindow = 1,    // bool move; qint32 x, y, width, height, minwidth, minheight; bool fullscreen; bool save_size, save_position; quint32 background (sizes/positions in logical pixels; background is 0x00RRGGBB)
    SetTitle,         // QString title
    Frame,            // qint32 width, height, stride (device pixels); QByteArray packed RGB888 data (used only when there's no shared segment; see FrameShared)
    SetCursor,        // qint32 cursor (a Cursor value)
    FileDialog,       // bool save; QString prompt, filter, start (start is a directory for open, a suggested path for save)
    ToggleFullscreen, // (empty)
    ShowText,         // qint32 style (a TextStyle value); QString title, text; bool rich
    SetBackground,    // quint32 background (0x00RRGGBB)
    FrameShared,      // qint32 slot, width, height, stride (device pixels); pixels are in the slot, not here
    FrameBufferReady, // (empty) the shared segment has been mapped, so its name can be unlinked

    // launcher → interpreter
    Resized,          // qint32 width, height (logical pixels)
    KeyEvent,         // qint32 key; quint32 modifiers; QString text
    MouseEvent,       // qint32 type (QEvent::Type), x, y (logical pixels), button (Qt::MouseButton)
    WheelEvent,       // qint32 pixel_x, pixel_y, degree_x, degree_y; bool page
    FullscreenState,  // bool fullscreen
    FileDialogResult, // QString path (null if canceled)
    FrameBuffer,      // QString name; quint64 slot_size; qint32 slot_count
    FrameDone,        // qint32 slot (the launcher is finished with it)
};

// Frames are passed through shared memory rather than over the socket:
// at the fixed backing scale a frame is tens of megabytes, and sending
// one inline copies it several times in each process. The segment is
// divided into equal, page-aligned slots; the socket carries only which
// slot a frame landed in (FrameShared) and which slots the launcher has
// finished with (FrameDone).
//
// Those two messages are also what synchronizes the two processes, so
// no lock is needed: a slot is written only by the process that owns
// it, the interpreter gives ownership away with FrameShared, and gets
// it back with FrameDone. The socket write and its matching read order
// the two processes' accesses to the slot.
//
// Two slots is enough for the interpreter to always have one to draw
// into: the launcher holds only the slot it is currently displaying (it
// may repaint from it at any time, so it can never hand that one back),
// and releases the previous one as soon as a newer frame replaces it.
inline constexpr int frame_slot_count = 2;

class SharedFrames {
public:
    SharedFrames() = default;
    ~SharedFrames() { detach(); }

    SharedFrames(const SharedFrames &) = delete;
    SharedFrames &operator=(const SharedFrames &) = delete;

    // Launcher side. The name is kept so that it can be unlinked once
    // the interpreter reports having mapped the segment; note that
    // macOS limits shm names to 31 characters.
    bool create(const QString &name, std::size_t slot_size, int slot_count)
    {
        detach();

        // A segment left behind by a process which died before it could
        // unlink its own would otherwise make O_EXCL fail.
        shm_unlink(name.toUtf8().constData());

        int fd = shm_open(name.toUtf8().constData(), O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd == -1) {
            return false;
        }

        m_name = name;

        auto size = page_align(slot_size);
        bool ok = ftruncate(fd, static_cast<off_t>(size * slot_count)) != -1 &&
            map(fd, size, slot_count);
        close(fd);

        if (!ok) {
            detach();
        }

        return ok;
    }

    // Interpreter side; slot_size is the launcher's already-aligned one.
    bool attach(const QString &name, std::size_t slot_size, int slot_count)
    {
        detach();

        int fd = shm_open(name.toUtf8().constData(), O_RDWR, 0);
        if (fd == -1) {
            return false;
        }

        bool ok = map(fd, slot_size, slot_count);
        close(fd);

        return ok;
    }

    // Drop the segment's name. Both mappings stay valid; this only stops
    // the segment outliving the processes using it.
    void unlink()
    {
        if (!m_name.isEmpty()) {
            shm_unlink(m_name.toUtf8().constData());
            m_name.clear();
        }
    }

    void detach()
    {
        unlink();

        if (m_base != nullptr) {
            munmap(m_base, m_slot_size * m_slot_count);
            m_base = nullptr;
        }

        m_slot_size = 0;
        m_slot_count = 0;
    }

    [[nodiscard]] bool valid() const { return m_base != nullptr; }
    [[nodiscard]] std::size_t slot_size() const { return m_slot_size; }
    [[nodiscard]] int slot_count() const { return m_slot_count; }

    [[nodiscard]] unsigned char *slot(int index) const
    {
        return static_cast<unsigned char *>(m_base) + (static_cast<std::size_t>(index) * m_slot_size);
    }

private:
    static std::size_t page_align(std::size_t size)
    {
        auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
        return ((size + page - 1) / page) * page;
    }

    bool map(int fd, std::size_t slot_size, int slot_count)
    {
        auto size = page_align(slot_size);
        void *base = mmap(nullptr, size * slot_count, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (base == MAP_FAILED) {
            return false;
        }

        m_base = base;
        m_slot_size = size;
        m_slot_count = slot_count;

        return true;
    }

    QString m_name;
    void *m_base = nullptr;
    std::size_t m_slot_size = 0;
    int m_slot_count = 0;
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
