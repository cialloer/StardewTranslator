#ifndef JSONUTILS_H
#define JSONUTILS_H

#include <QByteArray>
#include <QJsonValue>
#include <QStringList>

namespace JsonUtils
{

// 按 JSON 中的遍历顺序提取所有非空字符串。使用列表而不是以原文为键的
// QHash，确保重复原文可以获得独立的翻译，也不会因键冲突而丢失。
QStringList extractStrings(const QJsonValue &value);

// 按与 extractStrings 相同的顺序写回译文。译文数量不足时保留原文，
// 从而避免 API 返回不完整时破坏 JSON 结构。
QJsonValue applyTranslations(const QJsonValue &value, const QStringList &translations);

// 将 Mod 中常见的 JSONC 扩展转换为严格 JSON：移除行/块注释和对象、数组
// 末项后的逗号。所有被移除字符替换为空格并保留换行，因此错误位置仍能映射
// 回原文件；字符串中的 //、/* 和逗号不会被修改。
QByteArray normalizeJsonExtensions(const QByteArray &source);

} // namespace JsonUtils

#endif // JSONUTILS_H
