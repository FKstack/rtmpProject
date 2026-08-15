#include "windows/WindowsWindowAppearance.h"

#include <QApplication>
#include <QEvent>
#include <QWidget>

#include <dwmapi.h>
#include <windows.h>

namespace {

constexpr DWORD kImmersiveDarkModeAttribute = 20;
constexpr DWORD kLegacyImmersiveDarkModeAttribute = 19;

bool usesNativeTitleBar(const QWidget *widget)
{
    if (widget == nullptr || !widget->isWindow() ||
        widget->windowFlags().testFlag(Qt::FramelessWindowHint)) {
        return false;
    }

    const Qt::WindowType type = widget->windowType();
    return type == Qt::Window || type == Qt::Dialog || type == Qt::Tool;
}

void applyDarkTitleBar(QWidget *widget)
{
    if (!usesNativeTitleBar(widget)) {
        return;
    }

    const HWND window = reinterpret_cast<HWND>(widget->winId());
    if (window == nullptr) {
        return;
    }

    const BOOL enabled = TRUE;
    HRESULT result = DwmSetWindowAttribute(
        window,
        kImmersiveDarkModeAttribute,
        &enabled,
        sizeof(enabled)
    );
    if (FAILED(result)) {
        (void)DwmSetWindowAttribute(
            window,
            kLegacyImmersiveDarkModeAttribute,
            &enabled,
            sizeof(enabled)
        );
    }
}

} // namespace

WindowsWindowAppearance::WindowsWindowAppearance(QApplication &application)
    : application_(&application)
{
    application_->installEventFilter(this);
}

WindowsWindowAppearance::~WindowsWindowAppearance()
{
    if (application_ != nullptr) {
        application_->removeEventFilter(this);
    }
}

bool WindowsWindowAppearance::eventFilter(QObject *watched, QEvent *event)
{
    if (event != nullptr &&
        (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange ||
         event->type() == QEvent::PaletteChange)) {
        applyDarkTitleBar(qobject_cast<QWidget *>(watched));
    }
    return QObject::eventFilter(watched, event);
}
