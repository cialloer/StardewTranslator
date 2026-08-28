#include "jsonutils.h"

#include <QJsonArray>
#include <QJsonObject>

namespace
{

void collectStrings(const QJsonValue &value, QStringList &strings)
{
    if (value.isObject())
    {
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it)
            collectStrings(it.value(), strings);
        return;
    }

    if (value.isArray())
    {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array)
            collectStrings(item, strings);
        return;
    }

    if (value.isString() && !value.toString().trimmed().isEmpty())
        strings.append(value.toString());
}

QJsonValue replaceStrings(const QJsonValue &value,
                          const QStringList &translations,
                          qsizetype &translationIndex)
{
    if (value.isObject())
    {
        QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            it.value() = replaceStrings(it.value(), translations, translationIndex);
        return object;
    }

    if (value.isArray())
    {
        QJsonArray array = value.toArray();
        for (qsizetype index = 0; index < array.size(); ++index)
            array[index] = replaceStrings(array.at(index), translations, translationIndex);
        return array;
    }

    if (!value.isString() || value.toString().trimmed().isEmpty())
        return value;

    if (translationIndex >= translations.size())
        return value;

    return translations.at(translationIndex++);
}

} // namespace

QStringList JsonUtils::extractStrings(const QJsonValue &value)
{
    QStringList strings;
    collectStrings(value, strings);
    return strings;
}

QJsonValue JsonUtils::applyTranslations(const QJsonValue &value,
                                        const QStringList &translations)
{
    qsizetype translationIndex = 0;
    return replaceStrings(value, translations, translationIndex);
}

QByteArray JsonUtils::normalizeJsonExtensions(const QByteArray &source)
{
    QByteArray normalized = source;
    enum class ScanState
    {
        Normal,
        String,
        LineComment,
        BlockComment
    };

    ScanState state = ScanState::Normal;
    bool escaped = false;
    for (qsizetype index = 0; index < normalized.size(); ++index)
    {
        const char current = normalized.at(index);
        const char next = index + 1 < normalized.size()
                              ? normalized.at(index + 1)
                              : '\0';

        if (state == ScanState::String)
        {
            if (escaped)
                escaped = false;
            else if (current == '\\')
                escaped = true;
            else if (current == '"')
                state = ScanState::Normal;
            continue;
        }

        if (state == ScanState::LineComment)
        {
            if (current == '\r' || current == '\n')
                state = ScanState::Normal;
            else
                normalized[index] = ' ';
            continue;
        }

        if (state == ScanState::BlockComment)
        {
            if (current == '*' && next == '/')
            {
                normalized[index] = ' ';
                normalized[index + 1] = ' ';
                ++index;
                state = ScanState::Normal;
            }
            else if (current != '\r' && current != '\n')
            {
                normalized[index] = ' ';
            }
            continue;
        }

        if (current == '"')
        {
            state = ScanState::String;
            escaped = false;
        }
        else if (current == '/' && next == '/')
        {
            normalized[index] = ' ';
            normalized[index + 1] = ' ';
            ++index;
            state = ScanState::LineComment;
        }
        else if (current == '/' && next == '*')
        {
            normalized[index] = ' ';
            normalized[index + 1] = ' ';
            ++index;
            state = ScanState::BlockComment;
        }
    }

    // 注释已替换为空格，因此只需跳过标准空白即可判断逗号后是否直接闭合。
    const auto isJsonWhitespace = [](char character) {
        return character == ' ' || character == '\t'
               || character == '\r' || character == '\n';
    };
    bool insideString = false;
    escaped = false;
    for (qsizetype index = 0; index < normalized.size(); ++index)
    {
        const char current = normalized.at(index);
        if (insideString)
        {
            if (escaped)
                escaped = false;
            else if (current == '\\')
                escaped = true;
            else if (current == '"')
                insideString = false;
            continue;
        }

        if (current == '"')
        {
            insideString = true;
            continue;
        }
        if (current != ',')
            continue;

        qsizetype nextToken = index + 1;
        while (nextToken < normalized.size()
               && isJsonWhitespace(normalized.at(nextToken)))
        {
            ++nextToken;
        }
        if (nextToken < normalized.size()
            && (normalized.at(nextToken) == '}' || normalized.at(nextToken) == ']'))
        {
            normalized[index] = ' ';
        }
    }

    return normalized;
}
