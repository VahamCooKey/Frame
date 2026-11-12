pragma Singleton
import QtQuick

// main.cpp registers the bundled interface faces before creating the QML
// engine and sets the application default. A failed registration retains
// the platform default. Qt still provides fallback for unsupported scripts.
QtObject {
    readonly property string ui: Qt.application.font.family
}
