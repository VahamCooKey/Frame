// Standalone diagnostic probe for the FileLogger path failure (PROMPT-KIMI task A).
// NOT part of the build. Compile manually, e.g.:
//   cl /nologo /EHsc /std:c++20 /Zc:__cplusplus tests\manual_logprobe.cpp ^
//      /I third_party\Qt\6.8.3\msvc2022_64\include ^
//      /I third_party\Qt\6.8.3\msvc2022_64\include\QtCore ^
//      /Fe build-tmp\logprobe.exe /Fo build-tmp\ ^
//      /link /LIBPATH:third_party\Qt\6.8.3\msvc2022_64\lib Qt6Core.lib
// Run with third_party\Qt\6.8.3\msvc2022_64\bin on PATH.
// Results are appended to build-tmp\probe-result.txt (file output is used
// because Qt debug output needs an attached console, which CI shells lack).
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>

namespace {

QTextStream &out()
{
    static QFile file(QStringLiteral("build-tmp/probe-result.txt"));
    if (!file.isOpen())
        file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    static QTextStream stream(&file);
    return stream;
}

void line(const QString &s)
{
    out() << s << '\n';
    out().flush();
}

void probe(const QString &label, const QString &base)
{
    if (base.isEmpty()) {
        line(label + QStringLiteral(" => writableLocation returned EMPTY STRING"));
        return;
    }
    const QString dir = base + QStringLiteral("/Frame/logs");
    const bool mk = QDir().mkpath(dir);
    QFile f(dir + QStringLiteral("/frame.log"));
    const bool open = f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    const QString err = f.errorString();
    const QString name = f.fileName();
    f.close();
    QFile::remove(name); // do not pollute real log locations from the probe
    line(QStringLiteral("%1\n    base        : %2\n    mkpath(dir) : %3\n    dir exists  : "
                        "%4\n    file open   : %5\n    errorString : %6")
             .arg(label, base)
             .arg(mk)
             .arg(QFileInfo::exists(dir))
             .arg(open)
             .arg(err));
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Frame"));
    QCoreApplication::setApplicationName(QStringLiteral("Frame"));

    QFile::remove(QStringLiteral("build-tmp/probe-result.txt"));
    line(QStringLiteral("=== logprobe %1 ===")
             .arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    line(QStringLiteral("env APPDATA      : ") + qEnvironmentVariable("APPDATA"));
    line(QStringLiteral("env LOCALAPPDATA : ") + qEnvironmentVariable("LOCALAPPDATA"));
    line(QStringLiteral("env USERPROFILE  : ") + qEnvironmentVariable("USERPROFILE"));
    line(QStringLiteral("tempPath         : ") + QDir::tempPath());
    line(QStringLiteral("homePath         : ") + QDir::homePath());
    line(QStringLiteral("standardLocations(GenericDataLocation): ")
         + QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)
               .join(QStringLiteral(" | ")));
    line(QStringLiteral("writableLocation(GenericDataLocation) : ")
         + QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
    line(QStringLiteral("writableLocation(AppDataLocation)     : ")
         + QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    line(QStringLiteral("writableLocation(AppLocalDataLocation): ")
         + QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));

    // Replicates exactly what main.cpp's FileLogger originally did.
    probe(QStringLiteral("PROBE GenericDataLocation  "),
          QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
    probe(QStringLiteral("PROBE AppDataLocation      "),
          QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    probe(QStringLiteral("PROBE AppLocalDataLocation "),
          QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    probe(QStringLiteral("PROBE env-APPDATA          "), qEnvironmentVariable("APPDATA"));
    probe(QStringLiteral("PROBE tempPath (current)   "), QDir::tempPath());
    line(QStringLiteral("=== done ==="));
    return 0;
}
