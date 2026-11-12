// Frame's user-approved video/audio default setup. No elevation or policy changes.
#include "UserChoiceHashWin.h"
#include <QCoreApplication>
#include <QStringList>
#include <QTextStream>
#include <QVector>
#include <sddl.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <optional>

namespace {
const QString backupRoot = QStringLiteral("Software\\Frame\\AssociationBackup");
const QString choiceRoot = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\");
const QStringList videos{ "mkv", "mp4", "m4v", "avi", "mov", "webm", "wmv", "flv", "ts", "m2ts", "mpg", "mpeg", "ogv", "3gp" };
const QStringList audio{ "mp3", "m4a", "flac", "wav", "aac", "ogg", "opus" };
LPCWSTR wide(const QString &s) { return reinterpret_cast<LPCWSTR>(s.utf16()); }
struct Key {
    HKEY value{};
    ~Key() { if (value) RegCloseKey(value); }
};
std::optional<QString> read(HKEY root, const QString &path, const QString &name) {
    DWORD size = 0;
    if (RegGetValueW(root, wide(path), wide(name), RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS)
        return std::nullopt;
    QVector<wchar_t> data(size / sizeof(wchar_t) + 1);
    if (RegGetValueW(root, wide(path), wide(name), RRF_RT_REG_SZ, nullptr, data.data(), &size) != ERROR_SUCCESS)
        return std::nullopt;
    return QString::fromWCharArray(data.constData());
}
bool write(HKEY key, const QString &name, const QString &value) {
    return RegSetValueExW(key, wide(name), 0, REG_SZ,
        reinterpret_cast<const BYTE *>(value.utf16()), DWORD((value.size() + 1) * 2)) == ERROR_SUCCESS;
}
QString userSid() {
    HANDLE token{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    QByteArray storage(bytes, '\0');
    const bool ok = GetTokenInformation(token, TokenUser, storage.data(), bytes, &bytes);
    CloseHandle(token);
    if (!ok) return {};
    LPWSTR sid{};
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(storage.data())->User.Sid, &sid)) return {};
    const auto result = QString::fromWCharArray(sid);
    LocalFree(sid);
    return result;
}
QString effective(const QString &extension) {
    DWORD size = 0;
    AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_PROGID, wide(extension), nullptr, nullptr, &size);
    if (!size) return {};
    QVector<wchar_t> data(size);
    if (AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_PROGID, wide(extension), nullptr, data.data(), &size) != S_OK) return {};
    return QString::fromWCharArray(data.constData());
}
bool setChoice(const QString &extension, const QString &progId, const QString &sid) {
    const auto path = choiceRoot + extension + QStringLiteral("\\UserChoice");
    // Recreate the preference key because Windows may deny setting its old
    // values directly. Never alter permissions, Windows protection or policy.
    const auto removed = RegDeleteKeyW(HKEY_CURRENT_USER, wide(path));
    if (removed != ERROR_SUCCESS && removed != ERROR_FILE_NOT_FOUND) return false;
    if (progId.isEmpty()) return true;
    Key key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, wide(path), 0, nullptr, 0,
        KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key.value, nullptr) != ERROR_SUCCESS) return false;
    if (!write(key.value, QStringLiteral("ProgId"), progId)) return false;
    for (int retry = 0; retry < 3; ++retry) {
        FILETIME timestamp{}, after{};
        if (RegQueryInfoKeyW(key.value, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr, nullptr, &timestamp) != ERROR_SUCCESS) return false;
        const auto hash = frameUserChoiceHash(extension, sid, progId, timestamp);
        if (hash.isEmpty() || !write(key.value, QStringLiteral("Hash"), hash)) return false;
        if (RegQueryInfoKeyW(key.value, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr, nullptr, &after) != ERROR_SUCCESS) return false;
        if (frameUserChoiceHash(extension, sid, progId, after) == hash) return true;
    }
    return false;
}
void notify() { SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSH, nullptr, nullptr); }
bool compatible(const QString &sid) {
    // Check the algorithm against an existing Windows-created video preference
    // before changing defaults. Future incompatible formats fail safely.
    bool found = false;
    for (const auto &type : videos) {
        const auto ext = '.' + type;
        const auto path = choiceRoot + ext + QStringLiteral("\\UserChoice");
        const auto progId = read(HKEY_CURRENT_USER, path, QStringLiteral("ProgId"));
        const auto hash = read(HKEY_CURRENT_USER, path, QStringLiteral("Hash"));
        if (!progId || !hash) continue;
        found = true;
        Key key;
        FILETIME timestamp{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, wide(path), 0, KEY_QUERY_VALUE, &key.value) == ERROR_SUCCESS
            && RegQueryInfoKeyW(key.value, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                nullptr, nullptr, nullptr, nullptr, &timestamp) == ERROR_SUCCESS
            && frameUserChoiceHash(ext, sid, *progId, timestamp) == *hash)
            return true;
    }
    return !found;
}
bool restore(const QStringList &types, const QString &sid, bool rollback = false) {
    bool ok = true;
    for (const auto &type : types) {
        const auto ext = '.' + type;
        const auto saved = read(HKEY_CURRENT_USER, backupRoot, ext);
        if (!saved) continue;
        const auto current = read(HKEY_CURRENT_USER, choiceRoot + ext + "\\UserChoice", "ProgId");
        // A user's later choice always wins over the install-time backup.
        if (rollback || (current && current->compare("Frame" + ext, Qt::CaseInsensitive) == 0)) {
            if (!setChoice(ext, *saved, sid)) { ok = false; continue; }
        }
        Key backup;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, wide(backupRoot), 0, KEY_SET_VALUE, &backup.value) == ERROR_SUCCESS)
            RegDeleteValueW(backup.value, wide(ext));
    }
    notify();
    return ok;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout), error(stderr);
    const auto args = app.arguments();
    const auto sid = userSid();
    if (sid.isEmpty()) return 2;
    if (args.size() == 2 && args.at(1) == "--restore") return restore(videos + audio, sid) ? 0 : 4;
    if (args.size() == 2 && args.at(1) == "--check") {
        const bool ok = compatible(sid);
        out << (ok ? "Compatible Windows media preferences\n" : "Windows preference format requires the system chooser\n");
        return ok ? 0 : 3;
    }
    if (args.size() != 4 || args.at(1) != "--set" || args.at(3) != "--user-consent"
        || !QStringList{"video", "audio", "all"}.contains(args.at(2))) {
        error << "Usage: frame-associations --set video|audio|all --user-consent | --restore | --check\n";
        return 2;
    }
    if (!compatible(sid)) return 3;
    const auto types = args.at(2) == "all" ? videos + audio : args.at(2) == "video" ? videos : audio;
    Key backup;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, wide(backupRoot), 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &backup.value, nullptr) != ERROR_SUCCESS) return 4;
    QStringList changed;
    bool ok = true;
    for (const auto &type : types) {
        const auto ext = '.' + type;
        const auto target = "Frame" + ext;
        // Only registered Frame types may be changed by this helper.
        if (!read(HKEY_CURRENT_USER, "Software\\Classes\\" + target + "\\shell\\open\\command", "")) { ok = false; break; }
        const auto previous = read(HKEY_CURRENT_USER, choiceRoot + ext + "\\UserChoice", "ProgId").value_or(QString());
        if (previous.compare(target, Qt::CaseInsensitive) == 0) continue;
        if (!write(backup.value, ext, previous)) { ok = false; break; }
        changed.append(type);
        if (!setChoice(ext, target, sid)) { ok = false; break; }
    }
    notify();
    for (const auto &type : types) {
        const auto ext = '.' + type;
        const auto actual = effective(ext);
        out << ext << ": " << actual << '\n';
        if (actual.compare("Frame" + ext, Qt::CaseInsensitive) != 0) ok = false;
    }
    if (!ok) {
        // Do not leave an unreported partial batch on Windows variants that
        // reject the preference mechanism. Setup can open the native chooser.
        const bool restored = restore(changed, sid, true);
        error << "Windows did not accept the complete batch. "
              << (restored ? "Previous choices restored.\n" : "Open Windows Default apps to review the selection.\n");
        return 5;
    }
    out << "Frame is the default for all " << types.size() << " selected media formats.\n";
    return 0;
}
