#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "server/MediaServerTypes.h"

/**
 * @brief 读取 media-server.ini 中的服务器接入点与摄像头档案。
 *
 * 纯读取工具：所有解析失败都回退到默认值并通过 warnings 输出可读原因，
 * 不抛异常、不修改配置文件，也不与播放器或服务器进程交互。
 */
class MediaServerConfiguration final
{
public:
    /**
     * @brief 从 INI 文件读取 [server] 段，构造媒体服务器接入点。
     *
     * @param iniPath INI 文件路径；文件不存在或字段非法时回退默认值。
     * @param warnings 可选输出参数，追加所有回退原因；文件不存在时也会
     *                 追加一条说明。
     * @return 解析后的接入点；任何单项非法只回退该项，不影响其他字段。
     */
    [[nodiscard]] static MediaServerEndpoint loadEndpoint(
        const QString &iniPath,
        QStringList *warnings = nullptr
    );

    /**
     * @brief 从 INI 文件解析全部 [cameraN] 段为摄像头档案列表。
     *
     * 只解析和校验，不接入播放器。重复 cameraId、重复 streamKey、
     * 非法标识符或超过 16 路的条目会被跳过并记录 warning。
     *
     * @param iniPath INI 文件路径；文件不存在时返回空列表。
     * @param warnings 可选输出参数，追加所有跳过原因。
     * @return 校验通过的档案列表，最多 16 条，按段名排序。
     */
    [[nodiscard]] static QList<CameraStreamProfile> loadCameraProfiles(
        const QString &iniPath,
        QStringList *warnings = nullptr
    );
};
