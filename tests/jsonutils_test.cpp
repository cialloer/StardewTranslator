#include "jsonutils.h"
#include "settingssecurity.h"
#include "translationprovider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QtTest>

class JsonUtilsTest : public QObject
{
    Q_OBJECT

private slots:
    void extractsNestedAndDuplicateStrings();
    void appliesTranslationsWithoutChangingJsonTypes();
    void preservesEmptyAndUnmatchedStrings();
    void normalizesJsonCommentsAndTrailingCommas();
    void doesNotMaskStructuralJsonErrors();
    void normalizesApiKeys();
    void resolvesApiEndpoints();
    void rejectsUnknownWebsiteRoots();
    void parsesCompatibleTranslationStructures();
    void rejectsUnsafeTranslationStructures();
    void protectsSavedApiKeys();
};

void JsonUtilsTest::extractsNestedAndDuplicateStrings()
{
    QJsonArray input;
    input.append("Hello");
    input.append(QJsonObject{{"line", "Hello"}});
    input.append(QJsonArray{"World", 42, true});

    QCOMPARE(JsonUtils::extractStrings(input), QStringList({"Hello", "Hello", "World"}));
}

void JsonUtilsTest::appliesTranslationsWithoutChangingJsonTypes()
{
    QJsonArray input;
    input.append("Hello");
    input.append(QJsonObject{{"line", "World"}, {"count", 3}});

    const QJsonArray output =
        JsonUtils::applyTranslations(input, {"你好", "世界"}).toArray();

    QCOMPARE(output.at(0).toString(), QString("你好"));
    QCOMPARE(output.at(1).toObject().value("line").toString(), QString("世界"));
    QCOMPARE(output.at(1).toObject().value("count").toInt(), 3);
}

void JsonUtilsTest::preservesEmptyAndUnmatchedStrings()
{
    QJsonArray input{"", "First", "Second"};
    const QJsonArray output = JsonUtils::applyTranslations(input, {"第一"}).toArray();

    QCOMPARE(output.at(0).toString(), QString());
    QCOMPARE(output.at(1).toString(), QString("第一"));
    QCOMPARE(output.at(2).toString(), QString("Second"));
}

void JsonUtilsTest::normalizesJsonCommentsAndTrailingCommas()
{
    const QByteArray source = R"json({
        // A line comment before a value.
        "url": "https://example.com/a//b",
        "literal": "/* text inside a string */",
        "items": ["one", "two",], /* a block comment */
    })json";

    const QByteArray normalized = JsonUtils::normalizeJsonExtensions(source);
    QCOMPARE(normalized.size(), source.size());
    QVERIFY(normalized != source);

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(normalized, &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QCOMPARE(document.object().value("url").toString(),
             QString("https://example.com/a//b"));
    QCOMPARE(document.object().value("literal").toString(),
             QString("/* text inside a string */"));
    QCOMPARE(document.object().value("items").toArray().size(), 2);
}

void JsonUtilsTest::doesNotMaskStructuralJsonErrors()
{
    const QByteArray source = R"json({"items": ["one", "two"})json";
    const QByteArray normalized = JsonUtils::normalizeJsonExtensions(source);
    QCOMPARE(normalized, source);

    QJsonParseError error;
    QJsonDocument::fromJson(normalized, &error);
    QVERIFY(error.error != QJsonParseError::NoError);
}

void JsonUtilsTest::normalizesApiKeys()
{
    QCOMPARE(TranslationProvider::normalizeApiKey("  sk-test-key  "),
             QString("sk-test-key"));
    QCOMPARE(TranslationProvider::normalizeApiKey("Bearer sk-test-key"),
             QString("sk-test-key"));
    QCOMPARE(TranslationProvider::normalizeApiKey("\"Bearer sk-test-key\""),
             QString("sk-test-key"));
    QCOMPARE(TranslationProvider::normalizeApiKey("sk-test\n-key"),
             QString("sk-test-key"));
}

void JsonUtilsTest::resolvesApiEndpoints()
{
    QString error;
    const QUrl micuUrl = TranslationProvider::resolveChatCompletionsUrl(
        QUrl("https://www.micuapi.ai"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(micuUrl, QUrl("https://www.micuapi.ai/v1/chat/completions"));
    QCOMPARE(TranslationProvider::modelListUrl(micuUrl),
             QUrl("https://www.micuapi.ai/v1/models"));

    const QUrl baseUrl = TranslationProvider::resolveChatCompletionsUrl(
        QUrl("https://gateway.example/v1/"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(baseUrl, QUrl("https://gateway.example/v1/chat/completions"));

    const QUrl azureUrl = TranslationProvider::resolveChatCompletionsUrl(
        QUrl("https://example.openai.azure.com/openai/deployments/demo/chat/completions?api-version=2024-10-21"),
        &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(azureUrl.query(), QString("api-version=2024-10-21"));
}

void JsonUtilsTest::rejectsUnknownWebsiteRoots()
{
    QString error;
    const QUrl result = TranslationProvider::resolveChatCompletionsUrl(
        QUrl("https://example.com"), &error);
    QVERIFY(result.isEmpty());
    QVERIFY(!error.isEmpty());
}

void JsonUtilsTest::parsesCompatibleTranslationStructures()
{
    const TranslationBatchResult objectResult =
        TranslationProvider::parseTranslationContent(
            "```json\n{\"0\":\"甲\",\"1\":\"乙\"}\n```", 2);
    QVERIFY2(objectResult.isSuccess(), qPrintable(objectResult.errorMessage));
    QCOMPARE(objectResult.translations, QStringList({"甲", "乙"}));

    const TranslationBatchResult stringArrayResult =
        TranslationProvider::parseTranslationContent("[\"甲\",\"乙\"]", 2);
    QVERIFY2(stringArrayResult.isSuccess(),
             qPrintable(stringArrayResult.errorMessage));
    QCOMPARE(stringArrayResult.translations, QStringList({"甲", "乙"}));

    const TranslationBatchResult objectArrayResult =
        TranslationProvider::parseTranslationContent(
            R"json([{"id":"1","translation":"乙"},{"id":0,"text":"甲"}])json",
            2);
    QVERIFY2(objectArrayResult.isSuccess(),
             qPrintable(objectArrayResult.errorMessage));
    QCOMPARE(objectArrayResult.translations, QStringList({"甲", "乙"}));

    const TranslationBatchResult wrappedResult =
        TranslationProvider::parseTranslationContent(
            R"json({"translations":["甲","乙"]})json", 2);
    QVERIFY2(wrappedResult.isSuccess(), qPrintable(wrappedResult.errorMessage));
    QCOMPARE(wrappedResult.translations, QStringList({"甲", "乙"}));
}

void JsonUtilsTest::rejectsUnsafeTranslationStructures()
{
    const TranslationBatchResult wrongCount =
        TranslationProvider::parseTranslationContent("[\"甲\"]", 2);
    QVERIFY(!wrongCount.isSuccess());
    QVERIFY(wrongCount.errorMessage.contains("需要 2 条"));
    QVERIFY(!wrongCount.errorMessage.contains("no error occurred"));

    const TranslationBatchResult duplicateIds =
        TranslationProvider::parseTranslationContent(
            R"json([{"id":0,"translation":"甲"},{"id":0,"translation":"乙"}])json",
            2);
    QVERIFY(!duplicateIds.isSuccess());
    QVERIFY(duplicateIds.errorMessage.contains("重复"));

    const TranslationBatchResult missingId =
        TranslationProvider::parseTranslationContent(
            R"json([{"translation":"甲"},{"translation":"乙"}])json", 2);
    QVERIFY(!missingId.isSuccess());
    QVERIFY(missingId.errorMessage.contains("id/index"));
}

void JsonUtilsTest::protectsSavedApiKeys()
{
    if (!SettingsSecurity::isAvailable())
        QSKIP("Secure local secret storage is not available on this platform.");

    const QString secret = "sk-settings-roundtrip-test";
    QString error;
    const QByteArray protectedSecret =
        SettingsSecurity::protectSecret(secret, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!protectedSecret.isEmpty());
    QVERIFY(!protectedSecret.contains(secret.toUtf8()));

    QCOMPARE(SettingsSecurity::unprotectSecret(protectedSecret, &error), secret);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QByteArray corrupted = protectedSecret;
    const qsizetype middle = corrupted.size() / 2;
    corrupted[middle] = static_cast<char>(corrupted.at(middle) ^ 0x01);
    QVERIFY(SettingsSecurity::unprotectSecret(corrupted, &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

QTEST_APPLESS_MAIN(JsonUtilsTest)

#include "jsonutils_test.moc"
