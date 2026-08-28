#include "settingssecurity.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dpapi.h>
#endif

#include <limits>

namespace
{

void clearError(QString *errorMessage)
{
    if (errorMessage != nullptr)
        errorMessage->clear();
}

#ifdef Q_OS_WIN
QByteArray applicationEntropy()
{
    return QByteArrayLiteral("StardewTranslator.ApiKey.v1");
}

bool fitsWindowsBlob(qsizetype size)
{
    return size >= 0
           && static_cast<quint64>(size) <= std::numeric_limits<DWORD>::max();
}
#endif

} // namespace

bool SettingsSecurity::isAvailable()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

QByteArray SettingsSecurity::protectSecret(const QString &secret,
                                           QString *errorMessage)
{
    clearError(errorMessage);
    if (secret.isEmpty())
        return {};

#ifdef Q_OS_WIN
    QByteArray plainText = secret.toUtf8();
    QByteArray entropy = applicationEntropy();
    if (!fitsWindowsBlob(plainText.size()) || !fitsWindowsBlob(entropy.size()))
    {
        if (errorMessage != nullptr)
            *errorMessage = "API Key 长度超出 Windows 加密接口限制。";
        return {};
    }

    DATA_BLOB inputBlob{
        static_cast<DWORD>(plainText.size()),
        reinterpret_cast<BYTE *>(plainText.data())};
    DATA_BLOB entropyBlob{
        static_cast<DWORD>(entropy.size()),
        reinterpret_cast<BYTE *>(entropy.data())};
    DATA_BLOB outputBlob{};

    const BOOL protectedSuccessfully = CryptProtectData(
        &inputBlob,
        L"StardewTranslator API Key",
        &entropyBlob,
        nullptr,
        nullptr,
        CRYPTPROTECT_UI_FORBIDDEN,
        &outputBlob);
    const DWORD protectionError = protectedSuccessfully ? ERROR_SUCCESS : GetLastError();
    SecureZeroMemory(plainText.data(), static_cast<SIZE_T>(plainText.size()));
    if (!protectedSuccessfully)
    {
        if (errorMessage != nullptr)
            *errorMessage = QString("Windows 无法加密 API Key（错误 %1）。")
                                .arg(protectionError);
        return {};
    }

    const QByteArray result(reinterpret_cast<const char *>(outputBlob.pbData),
                            static_cast<qsizetype>(outputBlob.cbData));
    SecureZeroMemory(outputBlob.pbData, outputBlob.cbData);
    LocalFree(outputBlob.pbData);
    return result;
#else
    if (errorMessage != nullptr)
        *errorMessage = "当前平台不支持安全保存 API Key，请使用环境变量或 .env。";
    return {};
#endif
}

QString SettingsSecurity::unprotectSecret(const QByteArray &protectedSecret,
                                          QString *errorMessage)
{
    clearError(errorMessage);
    if (protectedSecret.isEmpty())
        return {};

#ifdef Q_OS_WIN
    QByteArray encryptedData = protectedSecret;
    QByteArray entropy = applicationEntropy();
    if (!fitsWindowsBlob(encryptedData.size()) || !fitsWindowsBlob(entropy.size()))
    {
        if (errorMessage != nullptr)
            *errorMessage = "保存的 API Key 数据无效。";
        return {};
    }

    DATA_BLOB inputBlob{
        static_cast<DWORD>(encryptedData.size()),
        reinterpret_cast<BYTE *>(encryptedData.data())};
    DATA_BLOB entropyBlob{
        static_cast<DWORD>(entropy.size()),
        reinterpret_cast<BYTE *>(entropy.data())};
    DATA_BLOB outputBlob{};
    LPWSTR description = nullptr;

    const BOOL unprotectedSuccessfully = CryptUnprotectData(
        &inputBlob,
        &description,
        &entropyBlob,
        nullptr,
        nullptr,
        CRYPTPROTECT_UI_FORBIDDEN,
        &outputBlob);
    const DWORD unprotectionError = unprotectedSuccessfully ? ERROR_SUCCESS : GetLastError();
    if (description != nullptr)
        LocalFree(description);
    if (!unprotectedSuccessfully)
    {
        if (errorMessage != nullptr)
            *errorMessage = QString("Windows 无法解密保存的 API Key（错误 %1）。")
                                .arg(unprotectionError);
        return {};
    }

    const QString result = QString::fromUtf8(
        reinterpret_cast<const char *>(outputBlob.pbData),
        static_cast<qsizetype>(outputBlob.cbData));
    SecureZeroMemory(outputBlob.pbData, outputBlob.cbData);
    LocalFree(outputBlob.pbData);
    return result;
#else
    if (errorMessage != nullptr)
        *errorMessage = "当前平台不支持读取安全保存的 API Key。";
    return {};
#endif
}
