#pragma once

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QDebug>
#include <array>

// Call once on the GUI thread after QGuiApplication exists and before QML.
// Register only the bundled interface faces; Qt keeps normal glyph fallback
// for filenames and other text outside Inter's coverage. This is unrelated to
// mpv's subtitle fonts and does not enumerate the installed font families.
inline bool installInterfaceFonts()
{
    constexpr std::array paths{
        ":/assets/fonts/Inter-Regular.ttf",
        ":/assets/fonts/Inter-Medium.ttf",
        ":/assets/fonts/Inter-SemiBold.ttf"
    };
    std::array<int, paths.size()> ids;
    ids.fill(-1);
    QString family;
    for (size_t i = 0; i < paths.size(); ++i) {
        ids[i] = QFontDatabase::addApplicationFont(QString::fromLatin1(paths[i]));
        const QStringList families = ids[i] >= 0
            ? QFontDatabase::applicationFontFamilies(ids[i]) : QStringList();
        if (families.isEmpty()) {
            for (const int id : ids)
                if (id >= 0) QFontDatabase::removeApplicationFont(id);
            qWarning("fonts: bundled interface font unavailable; retaining platform default");
            return false;
        }
        if (i == 0) family = families.first();
    }

    QFont font = QGuiApplication::font();
    font.setFamily(family);
    // A platform default can carry a named style, which would otherwise
    // override the different weights requested by QML controls.
    font.setStyleName(QString());
    font.setWeight(QFont::Normal);
    QGuiApplication::setFont(font);
    qInfo("fonts: bundled interface family '%s' registered (3 faces)", qPrintable(family));
    return true;
}
