import QtQuick

Column {
    id: root
    spacing: 28

    SettingGroup {
        title: "Performance"
        SettingRow {
            label: "Read-ahead cache"
            description: "Amount of media to buffer ahead, especially for streams."
            SettingDropdown {
                property string preferenceKey: "advanced/demuxerMaxBytes"
                options: [
                    { label: "Default", value: "" },
                    { label: "32 MiB", value: "32MiB" },
                    { label: "128 MiB", value: "128MiB" },
                    { label: "512 MiB", value: "512MiB" },
                ]
                value: Settings.value(preferenceKey)
                onActivated: newValue => Settings.setValue(preferenceKey, newValue)
            }
        }
    }

    SettingGroup {
        title: "Troubleshooting"
        SettingRow {
            label: "Log detail"
            description: "Increase detail when troubleshooting playback."
            SettingDropdown {
                property string preferenceKey: "advanced/logLevel"
                options: [
                    { label: "Info (default)", value: "info" },
                    { label: "Warning", value: "warn" },
                    { label: "Debug", value: "debug" },
                    { label: "Trace", value: "trace" },
                ]
                value: Settings.value(preferenceKey)
                onActivated: newValue => Settings.setValue(preferenceKey, newValue)
            }
        }
        SettingRow {
            label: "Log files"
            description: "Open the folder containing the playback log."
            FlatButton {
                label: "Open"
                onActivated: Settings.openLogFolder()
            }
        }
    }

    SettingGroup {
        title: "Text rendering"
        SettingRow {
            label: "Font engine"
            description: "Applies after restarting Frame. Windows only."
            SettingDropdown {
                property string preferenceKey: "interface/fontEngine"
                options: [{label: "System default", value: ""}, {label: "GDI", value: "gdi"}, {label: "FreeType", value: "freetype"}]
                value: Settings.value(preferenceKey)
                onActivated: newValue => Settings.setValue("interface/fontEngine", newValue)
            }
        }
    }
    SettingGroup {
        title: "Reset"
        SettingRow {
            label: "Reset preferences"
            description: "Apply defaults now and clear saved shaders. History, bookmarks, shortcuts, and media files are kept. The font engine changes after restart."
            FlatButton {
                label: "Reset"
                danger: true
                confirmLabel: "Confirm reset"
                onActivated: Settings.resetAllSettings()
            }
        }
    }
}
