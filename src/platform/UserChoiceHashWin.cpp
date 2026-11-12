/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
// Adapted from Mozilla browser/components/shell/WindowsUserChoice.cpp.
// Source: https://github.com/mozilla/gecko-dev/blob/master/browser/components/shell/WindowsUserChoice.cpp
// Qt replaces Mozilla allocation, MD5 and base64 utilities. The Windows
// checksum algorithm below is unchanged. This is a preference compatibility
// mechanism; callers must obtain user consent and verify Windows accepted it.
#include "UserChoiceHashWin.h"
#include <QCryptographicHash>
#include <cstring>

QString frameUserChoiceHash(const QString &extension, const QString &sid,
                           const QString &progId, FILETIME timestamp)
{
    SYSTEMTIME minute{};
    if (!FileTimeToSystemTime(&timestamp, &minute)) return {};
    minute.wSecond = 0;
    minute.wMilliseconds = 0;
    if (!SystemTimeToFileTime(&minute, &timestamp)) return {};
    QString input = extension + sid + progId
        + QStringLiteral("%1%2").arg(timestamp.dwHighDateTime, 8, 16, QLatin1Char('0'))
                                      .arg(timestamp.dwLowDateTime, 8, 16, QLatin1Char('0'))
        + QStringLiteral("User Choice set via Windows User Experience {D18B6DD5-6124-4341-9318-804003BAFA0B}");
    input = input.toLower();
    const auto bytes = QByteArray(reinterpret_cast<const char *>(input.utf16()),
                                 (input.size() + 1) * sizeof(char16_t));
    const auto digest = QCryptographicHash::hash(bytes, QCryptographicHash::Md5);
    DWORD md5[4]{};
    std::memcpy(md5, digest.constData(), sizeof(md5));
    const DWORD c0[2][5] = {
        {md5[0] | 1, 0xCF98B111u, 0x87085B9Fu, 0x12CEB96Du, 0x257E1D83u},
        {md5[1] | 1, 0xA27416F5u, 0xD38396FFu, 0x7C932B89u, 0xBFA49F69u}};
    const DWORD c1[2][5] = {
        {md5[0] | 1, 0xEF0569FBu, 0x689B6B9Fu, 0x79F8A395u, 0xC3EFEA97u},
        {md5[1] | 1, 0xC31713DBu, 0xDDCD1F0Fu, 0x59C3AF2Du, 0x35BD1EC9u}};
    auto wordSwap = [](DWORD v) { return (v >> 16) | (v << 16); };
    DWORD h0 = 0, h1 = 0, a0 = 0, a1 = 0;
    for (qsizetype i = 0; i < bytes.size() / 8; ++i) {
        for (size_t j = 0; j < 2; ++j) {
            DWORD value;
            std::memcpy(&value, bytes.constData() + (i * 2 + j) * 4, sizeof(value));
            h0 += value;
            h0 *= c0[j][0];
            h0 = wordSwap(h0) * c0[j][1];
            h0 = wordSwap(h0) * c0[j][2];
            h0 = wordSwap(h0) * c0[j][3];
            h0 = wordSwap(h0) * c0[j][4];
            a0 += h0;
            h1 += value;
            h1 = wordSwap(h1) * c1[j][1] + h1 * c1[j][0];
            h1 = (h1 >> 16) * c1[j][2] + h1 * c1[j][3];
            h1 = wordSwap(h1) * c1[j][4] + h1;
            a1 += h1;
        }
    }
    const DWORD hash[2] = {h0 ^ h1, a0 ^ a1};
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(hash), sizeof(hash)).toBase64());
}
