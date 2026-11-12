import QtQuick

Column {
    id: root
    spacing: 28
    readonly property bool selectedDeviceAvailable: deviceOptions.some(
        option => String(option.value) === String(deviceDropdown.value) && !option.unavailable)
    readonly property var deviceOptions: {
        const raw = Player.audioDevices
        const opts = [{ label: "System default", value: "auto" }]
        const names = new Set(["auto"])
        // A typed QVariantList is a QML sequence, not necessarily a JS Array.
        if (raw) {
            for (let i = 0; i < raw.length; i++) {
                const name = raw[i].name
                if (!name || names.has(name)) continue
                names.add(name)
                opts.push({ label: raw[i].description || name, value: name })
            }
        }
        // Keep the configured choice visible when a device disappears;
        // enumeration must not silently change the user's preference.
        const selected = String(deviceDropdown.value || "auto")
        if (!names.has(selected))
            opts.push({ label: "Unavailable device", value: selected, unavailable: true })
        return opts
    }


    SettingGroup {
        title: "Output"
        SettingRow {
            label: "Output device"
            description: root.selectedDeviceAvailable ? "Choose your speakers or headphones."
                : "The selected device is unavailable. Reconnect it or choose another output."
            SettingDropdown {
                id: deviceDropdown
                objectName: "audioDeviceDropdown"
                property string preferenceKey: "audio/device"
                options: root.deviceOptions
                value: Settings.value(preferenceKey)
                onActivated: newValue => Settings.setValue(preferenceKey, newValue)
            }
        }
        SettingRow {
            label: "Normalize volume"
            description: "Keep loudness more consistent between videos."
            SettingToggle {
                property string preferenceKey: "audio/normalize"
                checked: Settings.flag(preferenceKey)
                onToggled: value => Settings.setValue(preferenceKey, value)
            }
        }
    }

    SettingGroup {
        title: "Track selection"
        SettingRow {
            label: "Preferred audio language"
            description: "Used when a file opens, unless you pick a track this session."
            SettingDropdown {
                property string preferenceKey: "audio/preferredLanguage"
                options: Preferences.languageOptions
                value: Preferences.preferredAudioLanguage
                function refreshPreference() { value = Preferences.preferredAudioLanguage }
                onActivated: newValue => Preferences.preferredAudioLanguage = newValue
            }
        }
    }

    SettingGroup {
        title: "Timing"
        SettingRow {
            label: "Default audio delay"
            shortcutHints: [{ label: "Earlier", command: "add audio-delay -0.1" }, { label: "Later", command: "add audio-delay 0.1" }, { label: "Reset", command: "set audio-delay 0" }]
            description: "Default for every file. Negative values play sound earlier."
            SettingSlider {
                property string preferenceKey: "audio/delayDefault"
                stepSize: 0.05
                decimals: 2
                suffix: " s"
                from: -2.0
                to: 2.0
                value: Settings.value(preferenceKey)
                onMoved: newValue => Settings.setValue(preferenceKey, newValue)
            }
        }
    }
}
