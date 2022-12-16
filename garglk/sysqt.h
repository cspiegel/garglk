#ifndef GARGLK_SYSQT_H
#define GARGLK_SYSQT_H

#include <QCloseEvent>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMainWindow>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QSettings>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>

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
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
};

class Window : public QMainWindow {
    Q_OBJECT
public:
    Window();

    void refresh() { m_view->refresh(); }

    void start_timer(long);
    bool timed_out() { return m_timed_out; }
    void reset_timeout() { m_timed_out = false; }
    void toggle_menu() {
        if (menuBar()->isVisible())
            menuBar()->hide();
        else
            menuBar()->show();
    }

    const QSettings *settings() { return m_settings; }

protected:
    void closeEvent(QCloseEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void moveEvent(QMoveEvent *) override;

private:
    View *m_view;
    QAction *m_new_action;
    QTimer *m_timer;
    QSettings *m_settings;
    bool m_timed_out = false;
};

#endif
