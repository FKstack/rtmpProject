#pragma once

#include <QString>

#include "core/Singleton.h"

class QApplication;

/** @brief 标识本次成功应用样式时实际使用的来源。 */
enum class StyleSource {
    None,
    ExternalFile,
    QtResource,
};

/**
 * @brief 描述一次样式加载请求。
 *
 * 未指定外部目录时，StyleLoader 使用应用程序目录下的 `styles` 文件夹。样式文件名
 * 只能是单个 `.qss` 文件名，不能包含路径分隔符。
 */
struct StyleLoadOptions {
    QString styleFileName = QStringLiteral("app.qss");
    QString externalStyleDirectory;
};

/** @brief 描述样式加载和应用后的最终状态。 */
struct StyleLoadResult {
    bool applied = false;
    StyleSource source = StyleSource::None;
    QString resolvedPath;
    QString errorMessage;
};

/**
 * @brief 负责加载并应用应用级 QSS 的进程级服务。
 *
 * 外部 QSS 优先于内置 QRC 资源，便于部署后调整界面；资源样式作为回退，保证外部
 * 文件缺失时程序仍能保持默认主题。该类不持有 QWidget 指针，也不实现运行时热重载。
 *
 * @thread 样式应用会同步修改 QApplication，必须在 Qt UI 线程中调用。
 */
class StyleLoader final : public Singleton<StyleLoader>
{
    friend class Singleton<StyleLoader>;

public:
    /**
     * @brief 从外部文件或 QRC 资源加载并应用应用级 QSS。
     *
     * 先读取外部 `<应用目录>/styles/<文件名>`；外部文件不存在或不可读时回退到
     * `:/styles/<文件名>`。两者都无法读取时保留 QApplication 当前样式不变。
     *
     * @param application 需要应用样式的 QApplication。
     * @param options 样式文件名和可选的外部样式目录。
     * @return 样式来源、最终路径和失败原因。
     * @warning 该函数进行同步文件 I/O，仅应在应用启动或用户主动切换主题时调用。
     * @thread 必须在 Qt UI 线程中调用。
     */
    [[nodiscard]] StyleLoadResult applyApplicationStyle(
        QApplication &application,
        const StyleLoadOptions &options = {}
    ) const;

private:
    StyleLoader() = default;
    ~StyleLoader() = default;
};
