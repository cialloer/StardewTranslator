#ifndef TRANSLATIONPROVIDER_H
#define TRANSLATIONPROVIDER_H

#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <memory>

class QNetworkAccessManager;

// 一次批量请求的完整结果。调用方必须先检查 isSuccess()，失败时不得写文件。
struct TranslationBatchResult
{
    QStringList translations;
    QString errorMessage;

    bool isSuccess() const { return errorMessage.isEmpty(); }
};

// OpenAI Chat Completions 兼容接口的轻量封装。
// 对象应在工作线程中创建和使用；translateBatch 会阻塞当前工作线程，
// 但不会阻塞 Qt 的 UI 线程。
class TranslationProvider
{
public:
    using StatusCallback = std::function<void(const QString &)>;

    TranslationProvider(QString apiKey,
                        QString model,
                        QString apiUrl,
                        StatusCallback statusCallback = {});
    ~TranslationProvider();

    TranslationProvider(const TranslationProvider &) = delete;
    TranslationProvider &operator=(const TranslationProvider &) = delete;

    // 接受从终端、网页或 .env 粘贴的常见形式，避免重复添加 Bearer 前缀。
    static QString normalizeApiKey(QString apiKey);

    // 将完整端点或明确的 /v1 API Base URL 解析为 Chat Completions 端点。
    // 未知网站的根地址会被拒绝，避免把 API Key 发送到网页前端。
    static QUrl resolveChatCompletionsUrl(const QUrl &inputUrl,
                                          QString *errorMessage = nullptr);

    // 从已解析的 Chat Completions 端点得到 OpenAI 兼容的模型列表地址。
    static QUrl modelListUrl(const QUrl &chatCompletionsUrl);

    // 将模型正文规范化为与输入编号对应的译文列表。兼容数字键对象、字符串
    // 数组、带 id 的对象数组，以及 translations 包装结构。
    static TranslationBatchResult parseTranslationContent(const QString &content,
                                                           qsizetype expectedCount);

    TranslationBatchResult translateBatch(const QStringList &texts,
                                           const QString &targetLanguage);

private:
    QString m_apiKey;
    QString m_model;
    QString m_apiUrl;
    StatusCallback m_statusCallback;
    std::unique_ptr<QNetworkAccessManager> m_manager;
};

#endif // TRANSLATIONPROVIDER_H
