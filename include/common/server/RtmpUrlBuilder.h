#pragma once

#include <QString>
#include <QUrl>

#include <optional>

#include "server/MediaServerTypes.h"

/**
 * @brief 由服务器接入点和流名生成 RTMP 播放/推流 URL。
 *
 * URL 通过 QUrl::setScheme/setHost/setPort/setPath 组装，由 QUrl 负责
 * IPv6 方括号与转义，不做字符串拼接。application 与 streamKey 只允许
 * 单层标识符 `[A-Za-z0-9_-]+`，拒绝任何含路径分隔符的输入。
 *
 * @param endpoint 目标服务器接入点。
 * @param streamKey 单层流名，例如 camera01。
 * @param error 可选输出参数；失败时写入可读的中文错误原因，成功时清空。
 * @return 合法时返回完整 URL；输入非法时返回 std::nullopt。
 */
[[nodiscard]] std::optional<QUrl> buildRtmpUrl(
    const MediaServerEndpoint &endpoint,
    const QString &streamKey,
    QString *error = nullptr
);
