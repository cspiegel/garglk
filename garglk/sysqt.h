#ifndef GARGLK_SYSQT_H
#define GARGLK_SYSQT_H

#include <QCloseEvent>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPaintEvent>
#include <QResizeEvent>
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

#ifdef GARGLK_CONFIG_QT_BROKER
// Keep the (windowless) interpreter process out of the Dock and app
// switcher when running under the broker; defined in dockmac.mm.
void mac_hide_from_dock();
#endif

}
#endif
