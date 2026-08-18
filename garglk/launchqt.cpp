//
// Copyright (C) 2006-2009 by Tor Andersson.
// Copyright (C) 2009 by Baltasar García Perez-Schofield.
// Copyright (C) 2010 by Ben Cressey.
// Copyright (C) 2021 by Chris Spiegel.
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
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>

#ifdef _WIN32
#include <cstdio>

#include <windows.h>
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QList>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QVector>

#ifdef GARGLK_CONFIG_QT_BROKER
#include <QAction>
#include <QCloseEvent>
#include <QColor>
#include <QEvent>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QGuiApplication>
#include <QImage>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPoint>
#include <QProcessEnvironment>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QWheelEvent>
#include <QWidget>

#include <vector>

#include "brokerqt.h"
#include "dockmac.h"
#include "sysqt.h"
#endif

#include "garglk.h"
#include "garversion.h"
#include "launcher.h"

#include GARGLKINI_H

static const char *AppName = "Gargoyle " GARGOYLE_VERSION;

namespace {

class Filter {
public:
    Filter(QString name, QStringList extensions) : m_name(std::move(name)), m_extensions(std::move(extensions)) {}

    [[nodiscard]] QString format() const {
        return QString("%1 Games (%2)")
            .arg(m_name, format_extensions().join(" "));
    }

    [[nodiscard]] QStringList format_extensions() const {
        QList<QString> mapped_extensions;
        std::transform(m_extensions.begin(), m_extensions.end(),
                std::back_inserter(mapped_extensions),
                [](const QString &ext) { return QString("*.") + ext; });
        return mapped_extensions;
    }

private:
    QString m_name;
    QStringList m_extensions;
};

}

void garglk::winmsg(const std::string &msg)
{
    QMessageBox::critical(nullptr, "Error", msg.c_str());
}

static QString winbrowsefile()
{
    const QVector<Filter> filters = {
        Filter("Adrift", {"taf"}),
        Filter("AdvSys", {"dat"}),
        Filter("AGT", {"agx", "d$$"}),
        Filter("Alan", {"acd", "a3c"}),
        Filter("Glulx", {"ulx", "blb", "blorb", "glb", "gblorb"}),
        Filter("Hugo", {"hex"}),
        Filter("JACL", {"jacl", "j2"}),
        Filter("Level 9", {"l9", "sna"}),
        Filter("Magnetic Scrolls", {"mag"}),
        Filter("TADS", {"gam", "t3"}),
        Filter("Z-code", {"z1", "z2", "z3", "z4", "z5", "z6", "z7", "z8", "zlb", "zblorb"}),
    };
    QList<QString> mapped_filters;
    std::transform(filters.begin(), filters.end(),
            std::back_inserter(mapped_filters),
            [](const Filter &filter) { return filter.format(); });

    QStringList all_extensions;
    for (const auto &filter : filters) {
        all_extensions << filter.format_extensions();
    }

    QString filter_string = QString("All Games (%1);;All Files (*);;%2")
        .arg(all_extensions.join(" "), mapped_filters.join(";;"));

    // Hide filter details because the sheer number in "All Games" makes
    // the dialog ridiculously wide (Qt probably should cut it off, but
    // it doesn't, so try to compensate here).
    QFileDialog::Options options(QFileDialog::HideNameFilterDetails);
#ifdef GARGLK_CONFIG_NO_NATIVE_FILE_DIALOGS
    options |= QFileDialog::DontUseNativeDialog;
#endif

#ifdef GARGLK_CONFIG_QT_BROKER
    // On macOS, reopen in the directory of the last game that was opened,
    // matching the Cocoa interface. (Elsewhere the start directory is left
    // empty so the native dialog or portal can remember it itself, which
    // is the expected platform behavior, e.g. via XDG portals on Linux.)
    QString start = garglk::settings().value("file/last_open_directory").toString();

    QString filename = QFileDialog::getOpenFileName(nullptr, AppName, start, filter_string, nullptr, options);

    if (!filename.isEmpty()) {
        garglk::settings().setValue("file/last_open_directory", QFileInfo(filename).absolutePath());
    }

    return filename;
#else
    return QFileDialog::getOpenFileName(nullptr, AppName, "", filter_string, nullptr, options);
#endif
}

#ifdef GARGLK_CONFIG_QT_BROKER

// Broker mode (used on macOS): this launcher is a single long-lived
// application which owns the menu bar and all game windows, behaving
// like a normal Mac application: it can run multiple games at once,
// and stays running after the last game window closes. Interpreters
// are spawned with GARGOYLE_SOCKET set, causing them to create no
// window of their own, instead shipping rendered frames here and
// receiving input events back (see brokerqt.h for the protocol). This
// is the Qt equivalent of the Cocoa launcher in launchmac.mm.

namespace {

namespace broker = garglk::broker;

QLocalServer *broker_server = nullptr;
QString broker_name;
// Fixed render scale shared with the interpreters; see broker::backing_scale.
const double broker_dpr = garglk::broker::backing_scale;
bool game_launched = false;

class GameView : public QWidget {
public:
    explicit GameView(QLocalSocket *sock, QWidget *parent) :
        QWidget(parent),
        m_sock(sock)
    {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setAttribute(Qt::WA_InputMethodEnabled, true);
        setAutoFillBackground(true);
    }

    // The frame always lags the window by a round trip, so the area a
    // resize has just exposed (and the whole window, until the first
    // frame arrives) is painted by Qt rather than by the game. Fill it
    // with the game's own background instead of the default widget
    // color.
    void set_background(QRgb background)
    {
        auto pal = palette();
        pal.setColor(QPalette::Window, QColor(background));
        setPalette(pal);
    }

    // Frames rendered before the window has been opened would be sized
    // for a layout the game never sees, so suppress the resize
    // notifications Qt sends while the window is still being set up.
    void start()
    {
        m_started = true;
    }

    void set_frame(qint32 width, qint32 height, qint32 stride, const QByteArray &data)
    {
        m_data = data;
        m_frame = QImage(reinterpret_cast<const uchar *>(m_data.constData()), width, height, stride, QImage::Format_RGB888);
        m_frame.setDevicePixelRatio(broker_dpr);
        update();
    }

    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override
    {
        switch (query) {
        case Qt::ImEnabled:
            return QVariant(true);
        default:
            return QVariant();
        }
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        if (!m_frame.isNull()) {
            QPainter painter(this);
            // Frames are rendered at a fixed scale (broker::backing_scale)
            // and Qt scales them to this window's actual screen DPR, so
            // smooth the result for displays where that isn't 1:1.
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(QPoint(0, 0), m_frame);
        }
        event->accept();
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);

        if (!m_started) {
            return;
        }

        broker::send(m_sock, broker::MsgType::Resized, broker::pack(
            static_cast<qint32>(event->size().width()),
            static_cast<qint32>(event->size().height())));
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        broker::send(m_sock, broker::MsgType::KeyEvent, broker::pack(
            static_cast<qint32>(event->key()),
            static_cast<quint32>(static_cast<int>(event->modifiers())),
            event->text()));
        event->accept();
    }

    // Handle compose key events (probably other input method events
    // too); see the corresponding code in sysqt.cpp.
    void inputMethodEvent(QInputMethodEvent *event) override
    {
        if (!event->commitString().isEmpty()) {
            broker::send(m_sock, broker::MsgType::KeyEvent, broker::pack(
                static_cast<qint32>(0),
                static_cast<quint32>(0),
                event->commitString()));
        }
        event->accept();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        send_mouse(QEvent::MouseButtonPress, event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        // Qt sends this instead of the second press; the interpreter
        // counts clicks itself, so pass it along as a normal press.
        send_mouse(QEvent::MouseButtonPress, event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        send_mouse(QEvent::MouseMove, event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        send_mouse(QEvent::MouseButtonRelease, event);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        broker::send(m_sock, broker::MsgType::WheelEvent, broker::pack(
            static_cast<qint32>(event->pixelDelta().x()),
            static_cast<qint32>(event->pixelDelta().y()),
            static_cast<qint32>(event->angleDelta().x()),
            static_cast<qint32>(event->angleDelta().y()),
            event->modifiers() == Qt::ShiftModifier));
        event->accept();
    }

private:
    void send_mouse(QEvent::Type type, QMouseEvent *event)
    {
        broker::send(m_sock, broker::MsgType::MouseEvent, broker::pack(
            static_cast<qint32>(type),
            static_cast<qint32>(event->pos().x()),
            static_cast<qint32>(event->pos().y()),
            static_cast<qint32>(event->button())));
        event->accept();
    }

    QLocalSocket *m_sock;
    QByteArray m_data;
    QImage m_frame;
    bool m_started = false;
};

class GameWindow : public QMainWindow {
public:
    explicit GameWindow(QLocalSocket *sock) :
        m_sock(sock),
        m_view(new GameView(sock, this))
    {
        setCentralWidget(m_view);
        setAttribute(Qt::WA_DeleteOnClose);
        m_sock->setParent(this);

        QObject::connect(m_sock, &QLocalSocket::readyRead, this, [this]() {
            m_buffer.append(m_sock->readAll());

            std::vector<broker::Message> messages;
            broker::extract(m_buffer, messages);

            for (const auto &msg : messages) {
                dispatch(msg);
            }
        });

        // The interpreter exited (or crashed), so close its window.
        QObject::connect(m_sock, &QLocalSocket::disconnected, this, [this]() {
            close();
        });
    }

    void send_key(Qt::KeyboardModifiers modifiers, int key, const QString &text)
    {
        broker::send(m_sock, broker::MsgType::KeyEvent, broker::pack(
            static_cast<qint32>(key),
            static_cast<quint32>(static_cast<int>(modifiers)),
            text));
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        // Cut the connection; the interpreter exits when it notices.
        m_sock->abort();
        event->accept();
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QMainWindow::resizeEvent(event);

        if (m_save_size) {
            garglk::settings().setValue(garglk::settings_window_size, event->size());
        }

        if (m_save_position || m_save_size) {
            garglk::settings().setValue(garglk::settings_window_fullscreen, isFullScreen());
        }
    }

    void moveEvent(QMoveEvent *event) override
    {
        if (m_save_position) {
            garglk::settings().setValue(garglk::settings_window_position, event->pos());
        }

        event->accept();
    }

    void changeEvent(QEvent *event) override
    {
        QMainWindow::changeEvent(event);

        if (event->type() == QEvent::WindowStateChange) {
            broker::send(m_sock, broker::MsgType::FullscreenState, broker::pack(isFullScreen()));
        }
    }

private:
    void dispatch(const broker::Message &msg)
    {
        QDataStream in(msg.payload);

        switch (msg.type) {
        case broker::MsgType::NewWindow: {
            bool move, fullscreen;
            qint32 x, y, width, height, minwidth, minheight;
            quint32 background;
            in >> move >> x >> y >> width >> height >> minwidth >> minheight >> fullscreen
               >> m_save_size >> m_save_position >> background;

            m_view->set_background(background);

            setMinimumSize(minwidth, minheight);
            resize(width, height);
            if (move) {
                this->move(x, y);
            }

            if (fullscreen) {
                showFullScreen();
            } else {
                show();
            }

            // The interpreter is waiting on the actual window size to
            // set up its canvas. This is the first size it is told
            // about: the view suppresses the resizes Qt delivers while
            // the window is being laid out and shown, since acting on
            // those would produce an arrange event before the game has
            // even started.
            m_view->start();
            broker::send(m_sock, broker::MsgType::Resized, broker::pack(
                static_cast<qint32>(m_view->width()),
                static_cast<qint32>(m_view->height())));
            broker::send(m_sock, broker::MsgType::FullscreenState, broker::pack(isFullScreen()));
            break;
        }
        case broker::MsgType::SetTitle: {
            QString title;
            in >> title;
            setWindowTitle(title);
            break;
        }
        case broker::MsgType::Frame: {
            qint32 width, height, stride;
            QByteArray data;
            in >> width >> height >> stride >> data;
            m_view->set_frame(width, height, stride, data);
            break;
        }
        case broker::MsgType::SetBackground: {
            quint32 background;
            in >> background;
            m_view->set_background(background);
            m_view->update();
            break;
        }
        case broker::MsgType::SetCursor: {
            qint32 cursor;
            in >> cursor;

            switch (static_cast<broker::Cursor>(cursor)) {
            case broker::Cursor::Arrow:
                m_view->unsetCursor();
                break;
            case broker::Cursor::IBeam:
                m_view->setCursor(Qt::IBeamCursor);
                break;
            case broker::Cursor::Hand:
                m_view->setCursor(Qt::PointingHandCursor);
                break;
            }
            break;
        }
        case broker::MsgType::ToggleFullscreen:
            if (isFullScreen()) {
                if (m_fullscreen_from_maximized) {
                    showMaximized();
                } else {
                    showNormal();
                }
            } else {
                m_fullscreen_from_maximized = isMaximized();
                showFullScreen();
            }
            break;
        case broker::MsgType::ShowText: {
            qint32 style;
            QString title, text;
            bool rich;
            in >> style >> title >> text >> rich;

            auto text_style = static_cast<broker::TextStyle>(style);

            auto icon = QMessageBox::Icon::Information;
            switch (text_style) {
            case broker::TextStyle::Info:
                icon = QMessageBox::Icon::Information;
                break;
            case broker::TextStyle::Warning:
                icon = QMessageBox::Icon::Warning;
                break;
            case broker::TextStyle::Critical:
                icon = QMessageBox::Icon::Critical;
                break;
            }

            // A fatal error is immediately followed by the interpreter
            // exiting, which closes this window; so give that box no
            // parent, or the last thing a dying game says would be
            // destroyed along with the window. Anything else is shown as
            // a sheet on this game's window, which leaves the other
            // games running (an application-modal box would freeze them).
            bool fatal = text_style == broker::TextStyle::Critical;

            auto *box = new QMessageBox(icon, title, text, QMessageBox::Ok, fatal ? nullptr : this);
            box->setTextFormat(rich ? Qt::TextFormat::RichText : Qt::TextFormat::PlainText);
            box->setAttribute(Qt::WA_DeleteOnClose);

            if (fatal) {
                box->show();
                box->raise();
                box->activateWindow();
            } else {
                box->setWindowModality(Qt::WindowModal);
                box->open();
            }
            break;
        }
        case broker::MsgType::FileDialog: {
            bool save;
            QString prompt, filter, start;
            in >> save >> prompt >> filter >> start;

            // Shown asynchronously and window-modally, i.e. as a sheet
            // on this game's window. The interpreter is already blocked
            // waiting for the answer, so there is nothing to gain by
            // blocking the launcher as well - and doing so would freeze
            // every other game that happens to be running.
            auto *dialog = new QFileDialog(this, prompt);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setWindowModality(Qt::WindowModal);
            dialog->setNameFilters(filter.split(";;"));
#ifdef GARGLK_CONFIG_NO_NATIVE_FILE_DIALOGS
            dialog->setOption(QFileDialog::DontUseNativeDialog);
#endif

            if (save) {
                // For a save, start is a suggested path rather than a
                // directory.
                QFileInfo suggestion(start);
                dialog->setAcceptMode(QFileDialog::AcceptSave);
                dialog->setFileMode(QFileDialog::AnyFile);
                dialog->setDirectory(suggestion.absolutePath());
                dialog->selectFile(suggestion.fileName());
            } else {
                dialog->setAcceptMode(QFileDialog::AcceptOpen);
                dialog->setFileMode(QFileDialog::ExistingFile);
                dialog->setDirectory(start);
            }

            QObject::connect(dialog, &QDialog::finished, this, [this, dialog](int result) {
                QString filename;
                const auto selected = dialog->selectedFiles();
                if (result == QDialog::Accepted && !selected.isEmpty()) {
                    filename = selected.first();
                }

                broker::send(m_sock, broker::MsgType::FileDialogResult, broker::pack(filename));
            });

            dialog->open();
            break;
        }
        default:
            break;
        }
    }

    QLocalSocket *m_sock;
    GameView *m_view;
    QByteArray m_buffer;
    bool m_fullscreen_from_maximized = false;
    bool m_save_size = false;
    bool m_save_position = false;
};

void start_broker()
{
    broker_name = QString("gargoyle-%1").arg(QCoreApplication::applicationPid());

    QLocalServer::removeServer(broker_name);
    broker_server = new QLocalServer();
    broker_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!broker_server->listen(broker_name)) {
        garglk::winmsg("Unable to listen on local socket: " + broker_server->errorString().toStdString());
        std::exit(EXIT_FAILURE);
    }

    QObject::connect(broker_server, &QLocalServer::newConnection, broker_server, []() {
        while (auto *sock = broker_server->nextPendingConnection()) {
            new GameWindow(sock);
        }
    });
}

void add_recent(const QString &game)
{
    auto &settings = garglk::settings();
    auto games = settings.value("recent/games").toStringList();
    auto path = QFileInfo(game).absoluteFilePath();

    games.removeAll(path);
    games.prepend(path);
    while (games.size() > 10) {
        games.removeLast();
    }

    settings.setValue("recent/games", games);
}

bool launch_game(const QString &story)
{
    if (story.isEmpty()) {
        return false;
    }

    game_launched = true;

    if (!garglk::rungame(story.toStdString())) {
        return false;
    }

    add_recent(story);

    return true;
}

void create_menubar()
{
    // On macOS a parentless menu bar becomes the global menu bar,
    // shared by all windows.
    auto *menubar = new QMenuBar(nullptr);

    auto *file_menu = menubar->addMenu("File");

    auto *open = file_menu->addAction("Open...");
    open->setShortcut(QKeySequence::Open);
    QObject::connect(open, &QAction::triggered, open, []() {
        launch_game(winbrowsefile());
    });

    auto *recent_menu = file_menu->addMenu("Open Recent");
    QObject::connect(recent_menu, &QMenu::aboutToShow, recent_menu, [recent_menu]() {
        recent_menu->clear();

        const auto games = garglk::settings().value("recent/games").toStringList();
        for (const auto &game : games) {
            auto *action = recent_menu->addAction(QFileInfo(game).fileName());
            QObject::connect(action, &QAction::triggered, action, [game]() {
                launch_game(game);
            });
        }

        if (games.isEmpty()) {
            recent_menu->addAction("No Recent Games")->setEnabled(false);
        } else {
            recent_menu->addSeparator();
            auto *clear = recent_menu->addAction("Clear Menu");
            QObject::connect(clear, &QAction::triggered, clear, []() {
                garglk::settings().remove("recent/games");
            });
        }
    });

    auto *close = file_menu->addAction("Close");
    close->setShortcut(QKeySequence::Close);
    QObject::connect(file_menu, &QMenu::aboutToShow, close, [close]() {
        close->setEnabled(dynamic_cast<GameWindow *>(QApplication::activeWindow()) != nullptr);
    });
    QObject::connect(close, &QAction::triggered, close, []() {
        auto *window = dynamic_cast<GameWindow *>(QApplication::activeWindow());
        if (window != nullptr) {
            window->close();
        }
    });

    auto *edit_menu = menubar->addMenu("Edit");

    // As with the Cocoa launcher, cut/copy/paste are delivered to the
    // focused game as the corresponding key combination.
    auto forward = [&edit_menu](const QString &name, QKeySequence::StandardKey shortcut, int key, const QString &text) {
        auto *action = edit_menu->addAction(name);
        action->setShortcut(shortcut);
        QObject::connect(action, &QAction::triggered, action, [key, text]() {
            auto *window = dynamic_cast<GameWindow *>(QApplication::activeWindow());
            if (window != nullptr) {
                window->send_key(Qt::ControlModifier, key, text);
            }
        });
    };

    forward("Cut", QKeySequence::Cut, Qt::Key_X, "x");
    forward("Copy", QKeySequence::Copy, Qt::Key_C, "c");
    forward("Paste", QKeySequence::Paste, Qt::Key_V, "v");

    edit_menu->addSeparator();

    auto *config = edit_menu->addAction("Edit Configuration...");
    config->setMenuRole(QAction::PreferencesRole);
    config->setShortcut(QKeySequence::Preferences);
    QObject::connect(config, &QAction::triggered, config, []() {
        gli_edit_config();
    });

    // Hand the native menu to AppKit so it can keep the window list in
    // sync with the real NSWindows backing Qt's game windows. From here
    // on the items in this menu belong to AppKit: this QMenu must never
    // gain a QAction, since Qt resyncing it would wipe them out.
    auto *window_menu = menubar->addMenu("Window");
    garglk::mac_configure_window_menu(window_menu->toNSMenu());
}

class GargoyleApplication : public QApplication {
public:
    using QApplication::QApplication;

protected:
    bool event(QEvent *event) override
    {
        // Sent when a game is opened via Finder, the Dock, etc.
        if (event->type() == QEvent::FileOpen) {
            auto *open_event = static_cast<QFileOpenEvent *>(event);
            auto file = open_event->file();

            // Qt also turns a garglk:// URL (a scheme registered by
            // launcher.plist) into a QFileOpenEvent, but file() is empty
            // for those, since it's not a file: URL. Recover the path the
            // same way the Cocoa launcher does: strip the scheme and
            // decode.
            if (file.isEmpty()) {
                static const QString scheme = "garglk://";
                auto url = open_event->url().toString();
                if (url.startsWith(scheme)) {
                    file = QUrl::fromPercentEncoding(url.mid(scheme.size()).toUtf8());
                }
            }

            launch_game(file);
            return true;
        }

        return QApplication::event(event);
    }
};

}

#endif

bool garglk::winterp(const std::string &exe, const std::vector<std::string> &flags, const std::string &game)
{
    // Find the directory that contains the interpreters. By default
    // this is GARGLK_CONFIG_INTERPRETER_DIR but if that is not set, it
    // is the containing directory of the gargoyle executable.
    //
    // For development purposes, the environment variable
    // $GARGLK_INTERPRETER_DIR can be set to the interpreter build
    // directory to allow the gargoyle binary to load the newly-built
    // interpreters instead of the system-wide interpreters (or instead
    // of failing if there are no interpreters installed). If this is
    // set, the standard directory will *not* be used at all, even if no
    // interpreter is found.
    QString interpreter_dir = std::getenv("GARGLK_INTERPRETER_DIR");
    if (interpreter_dir.isNull()) {
#ifdef GARGLK_CONFIG_INTERPRETER_DIR
        interpreter_dir = GARGLK_CONFIG_INTERPRETER_DIR;
#else
        interpreter_dir = QCoreApplication::applicationDirPath();
#endif
    }

    QString argv0 = QDir(interpreter_dir).absoluteFilePath(exe.c_str());

    QStringList args;
    for (const auto &flag : flags) {
        args.push_back(QString::fromStdString(flag));
    }
    args.push_back(QString::fromStdString(game));

#ifdef GARGLK_CONFIG_QT_BROKER
    // Spawn the interpreter and return immediately: its window is
    // managed by this process (see GameWindow above), and multiple
    // games can run at once.
    auto *proc = new QProcess();

    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("GARGOYLE_SOCKET", broker_name);
    // Game windows belong to the launcher, so don't let interpreter
    // processes show up in the Dock.
    env.insert("QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM", "1");
    proc->setProcessEnvironment(env);

    proc->setProcessChannelMode(QProcess::ForwardedChannels);
    QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), proc, &QObject::deleteLater);
    proc->start(argv0, args);

    if (!proc->waitForStarted(5000)) {
        garglk::winmsg("Could not start interpreter " + argv0.toStdString());
        proc->deleteLater();
        return false;
    }

    return true;
#else
    QProcess proc;
    proc.setProcessChannelMode(QProcess::ForwardedChannels);
    proc.start(argv0, args);

    if (!proc.waitForStarted(5000)) {
        garglk::winmsg("Could not start interpreter " + argv0.toStdString());
        return false;
    }

    proc.waitForFinished(-1);

    if (proc.exitStatus() != QProcess::NormalExit) {
        return false;
    } else {
        return proc.exitCode() == 0;
    }
#endif
}

static QString parse_args(const QApplication &app)
{
    QCommandLineParser parser;

    // Manually add -h to avoid --help-all: don't show Qt-specific
    // options, as this only affects the launcher. If the user selects a
    // style, for example, that won't carry on to the interpreter, as
    // it's a separate program. Qt options are still _supported_ (as
    // they're passed to QApplication's constructor), but at least don't
    // advertise their existence.
    //
    // The "right" approach would be to synthesize empty arguments for
    // Qt and then parse arguments with something like getopt_long(),
    // but that's a GNU extension and would have to be pulled in from
    // glibc, musl libc, or similar. This is good enough.
    parser.addOptions({
        {{"d", "dump-config"}, "Dump the default config file to standard out."},
        {{"e", "edit-config"}, "Edit the configuration file."},
        {{"h", "help"}, "Displays help on commandline options."},
        {{"m", "migrate-config"}, "Move a legacy configuration file to the preferred location."},
        {{"p", "paths"}, "Displays configuration file and theme paths."},
        {{"t", "themes"}, "Displays all available color themes."},
    });

    parser.addVersionOption();
    parser.addPositionalArgument("STORY", "The story/game file to run. If not provided, a file chooser will be displayed.", "[STORY]");
    parser.process(app);

    auto positional = parser.positionalArguments();

    if (positional.size() > 1) {
        std::cerr << "warning: extra positional arguments are ignored." << std::endl;
    }

    QString gamefile = positional.isEmpty() ?
        "" :
        positional.first();

    if (parser.isSet("d")) {
        std::cout << garglkini;
        std::exit(0);
    } else if (parser.isSet("e")) {
        gli_edit_config();
        std::exit(0);
    } else if (parser.isSet("h")) {
        std::cout << parser.helpText().toStdString() << std::endl;
        std::exit(0);
    } else if (parser.isSet("m")) {
        auto configs = garglk::configs("");
        configs.erase(std::remove_if(configs.begin(), configs.end(), [](const auto &config) {
            return config.type != garglk::ConfigFile::Type::User;
        }), configs.end());

        if (configs.empty()) {
            std::cerr << "Unable to determine configuration file locations.\n";
            std::exit(1);
        }

        auto preferred = QString::fromStdString(configs.front().path);
        if (QFile::exists(preferred)) {
            std::cout << "Preferred configuration file " << preferred.toStdString() << " already exists.\n";
        } else {
            std::vector<garglk::ConfigFile> existing;

            std::copy_if(configs.begin(), configs.end(), std::back_inserter(existing), [&preferred](const auto &config) {
                auto path = QString::fromStdString(config.path);
                return path != preferred && QFile::exists(path);
            });

            if (existing.empty()) {
                std::cout << "No existing configuration files found.\n";
            } else if (existing.size() != 1) {
                std::cout << "Won't migrate, found multiple existing configuration files:\n\n";
                for (const auto &config : existing) {
                    std::cout << config.path << std::endl;
                }
            } else {
                auto old = existing.front().path;
                std::cout << "Renaming " << old << " to " << preferred.toStdString() << std::endl;
                QFile file(QString::fromStdString(old));
                if (!file.rename(preferred)) {
                    std::cerr << "Unable to rename file: " << file.errorString().toStdString() << std::endl;
                    std::exit(1);
                }
            }
        }

        std::exit(0);
    } else if (parser.isSet("p")) {
        // Convert to native separators and return absolute path.
        auto canonicalize = [](const std::string &path) {
            auto qpath = QString::fromStdString(path);
            qpath = QDir(qpath).absolutePath();
            return QDir::toNativeSeparators(qpath).toStdString();
        };

        std::cout << "Configuration file paths:\n\n";
        for (const auto &config : garglk::configs(gamefile.toStdString())) {
            auto path = canonicalize(config.path);
            auto type = QString::fromStdString(config.format_type());

            std::cout << path << " " << type.toStdString() << std::endl;
        }

        std::cout << "\nTheme paths:\n\n";
        auto theme_paths = garglk::theme::paths();
        std::reverse(theme_paths.begin(), theme_paths.end());
        for (const auto &path : theme_paths) {
            std::cout << canonicalize(path) << std::endl;
        }

        std::exit(0);
    } else if (parser.isSet("t")) {
        for (const auto &theme_name : garglk::theme::names()) {
            std::cout << theme_name << std::endl;
        }

        std::exit(0);
    }

    return gamefile;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    // The WIN32 CMake flag builds a GUI subsystem executable, which has
    // no console attached. If running from a terminal (cmd, PowerShell),
    // attach to it so that stdout/stderr output from --help, --paths,
    // etc. is visible.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
    }
#endif

#ifdef GARGLK_CONFIG_QT_BROKER
    GargoyleApplication app(argc, argv);
#else
    QApplication app(argc, argv);
#endif

    QApplication::setApplicationName("gargoyle");
    QApplication::setApplicationVersion(GARGOYLE_VERSION);

    garglk::theme::init();

    auto story = parse_args(app);

#ifdef _WIN32
    // Resolve the story path while the working directory it might be
    // relative to is still current.
    if (!story.isEmpty()) {
        story = QFileInfo(story).absoluteFilePath();
    }

    // On Windows, when started from the start menu, Gargoyle's CWD is
    // set to the install directory. That means that default file
    // dialogs open there, which is not a useful location. If the CWD is
    // in fact there, change dir to the desktop. If the CWD is anywhere
    // else, assume it's the user's doing and leave it alone.
    if (QDir::currentPath() == QCoreApplication::applicationDirPath()) {
        auto desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        if (!desktop.isEmpty()) {
            QDir::setCurrent(desktop);
        }
    }
#endif

#ifdef GARGLK_CONFIG_QT_BROKER
    gli_read_config(argc, argv);

    // Stay running after the last game window closes, like a normal
    // Mac application; quitting is done explicitly (e.g. ⌘Q).
    QApplication::setQuitOnLastWindowClosed(false);

    // Disable tabbing before any window exists (see the function).
    garglk::mac_disable_window_tabbing();

    start_broker();
    create_menubar();

    if (!story.isEmpty()) {
        launch_game(story);
    } else {
        // Show a file chooser once the event loop is running, unless
        // a game was already opened via an Apple event (e.g. a file
        // double-clicked in Finder) by then.
        QTimer::singleShot(0, []() {
            if (!game_launched) {
                launch_game(winbrowsefile());
            }
        });
    }

    return QApplication::exec();
#else
    if (story.isEmpty()) {
        story = winbrowsefile();
    }

    if (story.isEmpty()) {
        return 1;
    }

    gli_read_config(argc, argv);

    // run story file
    return garglk::rungame(story.toStdString()) ? 0 : 1;
#endif
}
