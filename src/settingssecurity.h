#ifndef SETTINGSSECURITY_H
#define SETTINGSSECURITY_H

#include <QByteArray>
#include <QString>

namespace SettingsSecurity
{

// Windows 版本使用当前用户的 DPAPI 凭据保护本地保存的 API Key。
bool isAvailable();
QByteArray protectSecret(const QString &secret, QString *errorMessage = nullptr);
QString unprotectSecret(const QByteArray &protectedSecret,
                        QString *errorMessage = nullptr);

} // namespace SettingsSecurity

#endif // SETTINGSSECURITY_H
