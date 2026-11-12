import QtQuick

Column {
    id: root
    spacing: 28
    SettingGroup {
        title: "Color"
        SettingRow {
            label: "Accent color"
            description: "Colors active controls only."
            SettingChoice {
                id: accentChoice
                property string preferenceKey: "interface/accentColor"
                options: Colors.accents.map(a => ({ label: a.label, name: a.name, value: a.color, swatch: a.color }))
                value: String(Colors.accent).toUpperCase()
                function refreshPreference() {
                    value = Qt.binding(() => String(Colors.accent).toUpperCase())
                }
                onActivated: newValue => {
                    Colors.accent = newValue
                    Settings.setValue("interface/accentColor", newValue)
                }
            }
        }
    }
    SettingGroup {
        title: "Player controls"
        SettingRow {
            label: "Playlist animation"
            description: "Smooth scrolling between pages. System follows your device settings."
            SettingChoice {
                property string preferenceKey: "interface/playlistMotion"
                options: [
                    { label: "System", value: "system" },
                    { label: "On", value: "on" },
                    { label: "Off", value: "off" }
                ]
                value: Settings.value(preferenceKey)
                onActivated: newValue => Settings.setValue("interface/playlistMotion", newValue)
            }
        }
        SettingRow {
            label: "Hide controls after"
            description: "Fade out the controls while you watch."
            SettingChoice {
                property string preferenceKey: "interface/osdHideMs"
                options: [
                    { label: "0.8 s", value: 800 }, { label: "1.4 s", value: 1400 },
                    { label: "2.5 s", value: 2500 }, { label: "4 s", value: 4000 }
                ]
                value: Settings.value(preferenceKey)
                onActivated: newValue => Settings.setValue("interface/osdHideMs", newValue)
            }
        }
    }
    SettingGroup {
        title: "Media names"
        SettingRow {
            label: "Simplify filenames"
            description: "Remove release tags from titles and playlists."
            SettingToggle {
                property string preferenceKey: "interface/cleanMediaNames"
                checked: MediaNames.cleanupEnabled
                onToggled: value => {
                    Settings.setValue("interface/cleanMediaNames", value)
                    MediaNames.cleanupEnabled = value
                }
            }
        }
    }
}
