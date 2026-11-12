import QtQuick

Column {
    id: root
    spacing: 28

    SettingGroup {
        title: "Track selection"
        SettingRow {
            label: "Preferred subtitle language"
            description: "Used when a file opens, unless you pick a track or Off this session."
            SettingDropdown {
                property string preferenceKey: "subtitles/preferredLanguage"
                options: Preferences.languageOptions
                value: Preferences.preferredSubtitleLanguage
                function refreshPreference() { value = Preferences.preferredSubtitleLanguage }
                onActivated: newValue => Preferences.preferredSubtitleLanguage = newValue
            }
        }
    }

    SettingGroup {
        title: "Style"
        SettingRow {
            label: "Text size"
            description: "Adjust the size of subtitle text."
            SettingSlider {
                property string preferenceKey: "subtitles/fontSize"
                suffix: " px"
                from: 20
                to: 100
                value: Settings.value(preferenceKey)
                onMoved: newValue => {
                    const rounded = Math.round(newValue)
                    Settings.setValue("subtitles/fontSize", rounded)
                }
            }
        }
        SettingRow {
            label: "Text color"
            description: "Choose a readable subtitle color."
            SettingChoice {
                property string preferenceKey: "subtitles/color"
                options: [
                    { label: "White", value: "#FFFFFFFF" },
                    { label: "Yellow", value: "#FFFFFF00" },
                    { label: "Cyan", value: "#FF00FFFF" },
                ]
                value: Settings.value(preferenceKey)
                onActivated: newValue => Settings.setValue(preferenceKey, newValue)
            }
        }
        SettingRow {
            label: "Outline"
            description: "Add contrast around the text."
            SettingSlider {
                property string preferenceKey: "subtitles/borderSize"
                suffix: " px"
                from: 0
                to: 6
                value: Settings.value(preferenceKey)
                onMoved: newValue => {
                    const rounded = Math.round(newValue)
                    Settings.setValue("subtitles/borderSize", rounded)
                }
            }
        }
        SettingRow {
            label: "Vertical position"
            description: "Move subtitles from the top to the bottom."
            SettingSlider {
                property string preferenceKey: "subtitles/position"
                suffix: "%"
                from: 0
                to: 100
                value: Settings.value(preferenceKey)
                onMoved: newValue => {
                    const rounded = Math.round(newValue)
                    Settings.setValue("subtitles/position", rounded)
                }
            }
        }
    }

    SettingGroup {
        title: "Timing and loading"
        SettingRow {
            label: "Default subtitle delay"
            shortcutHints: [{ label: "Earlier", command: "add sub-delay -0.1" }, { label: "Later", command: "add sub-delay 0.1" }]
            description: "Default for every file. Negative values show subtitles earlier."
            SettingSlider {
                property string preferenceKey: "subtitles/delayDefault"
                stepSize: 0.05
                decimals: 2
                suffix: " s"
                from: -5.0
                to: 5.0
                value: Settings.value(preferenceKey)
                onMoved: newValue => Settings.setValue(preferenceKey, newValue)
            }
        }
        SettingRow {
            label: "Turn on subtitles automatically"
            description: "Show the first subtitle track when the file doesn't mark one as default."
            SettingToggle {
                property string preferenceKey: "subtitles/autoSelect"
                checked: Settings.flag(preferenceKey)
                onToggled: value => Settings.setValue(preferenceKey, value)
            }
        }
        SettingRow {
            label: "Load matching subtitle files"
            description: "Automatically find subtitles with the same filename."
            SettingToggle {
                property string preferenceKey: "subtitles/autoLoad"
                checked: Settings.value(preferenceKey) !== "no"
                function refreshPreference() { checked = Settings.value(preferenceKey) !== "no" }
                onToggled: value => {
                    const mode = value ? "exact" : "no"
                    Settings.setValue("subtitles/autoLoad", mode)
                }
            }
        }
    }
}
