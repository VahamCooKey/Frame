#pragma once

#include <QString>

namespace LogSanitizer {
// Pure text transformation used at the persistent-log boundary. It never
// changes the URL passed to mpv, screenshot notices, or other application data.
QString sanitize(QString message);
}
