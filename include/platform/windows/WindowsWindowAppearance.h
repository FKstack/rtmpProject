#pragma once

#include <QObject>

class QApplication;

/**
 * @brief 为 Windows 顶层窗口应用原生深色标题栏。
 *
 * 该对象只观察 QApplication 的顶层 QWidget 事件，不接管窗口拖动、缩放、系统菜单
 * 或非客户区绘制。系统不支持深色标题栏属性时调用安全失败并保留原生外观。
 */
class WindowsWindowAppearance final : public QObject
{
public:
    explicit WindowsWindowAppearance(QApplication &application);
    ~WindowsWindowAppearance() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QApplication *application_ = nullptr;
};
