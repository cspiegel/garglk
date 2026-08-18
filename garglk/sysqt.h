#ifndef GARGLK_SYSQT_H
#define GARGLK_SYSQT_H

#include <QCloseEvent>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QSettings>
#include <QShowEvent>
#include <QWheelEvent>
#include <QWidget>

namespace garglk {

class View : public QWidget
{
    Q_OBJECT

public:
    explicit View(QWidget *parent) : QWidget(parent) {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setAttribute(Qt::WA_InputMethodEnabled, true);
    }

    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void refresh();

protected:
    void inputMethodEvent(QInputMethodEvent *event) override;
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
};

class Window : public QMainWindow {
    Q_OBJECT
public:
    Window();

    View *view() const { return m_view; }
    void refresh() { m_view->refresh(); }

protected:
    void showEvent(QShowEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void moveEvent(QMoveEvent *) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    bool event(QEvent *) override;
#endif

private:
    // Resizes gli_image_rgb to the physical-pixel size for the given
    // logical size at the view's current devicePixelRatioF(). Shared by
    // resizeEvent() and the extra call triggered from showEvent()/
    // event(), see the comments there for why a second call is needed.
    void updateBufferSize(const QSize &logicalSize);

    View *const m_view;
};

// The settings store, shared by the launcher and the interpreters.
//
// Qt programs have an organization and name that can be set, and
// Gargoyle used to set these to "io.github.garglk" and "Gargoyle". The
// QSettings here follows that. However, Gargoyle now uses an empty
// organization and the name "gargoyle" (on Unix) so that directories
// are more conventionally-named, e.g. /usr/share/gargoyle instead of
// /usr/share/io.github.garglk/Gargoyle. But QSettings _requires_ an
// organization name. Given that this is a setting users aren't ever
// supposed to see anyhow, and that these exact names were used in the
// past, keep them the same so that older configurations can be loaded.
// Ideally this would probably just be "gargoyle" and "gargoyle" but
// aesthetics are nowhere near as important as not losing settings; and
// since nobody is going to see these names in the normal course of
// using Gargoyle, it doesn't really matter anyway.
QSettings &settings();

// Keys under "window/" are written by whichever process owns the window
// (the launcher in broker mode, the interpreter otherwise) and read back
// when a window is opened, so both processes must agree on them.
inline constexpr auto settings_window_size = "window/size";
inline constexpr auto settings_window_position = "window/position";
inline constexpr auto settings_window_fullscreen = "window/fullscreen";

}
#endif
