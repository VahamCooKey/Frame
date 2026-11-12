import QtQuick

Column {
    id: root
    spacing: 28
    signal applyShaderRequested()

    SettingGroup {
        title: "Decoding"
        SettingRow {
            label: "Hardware decoding"
            description: "Use your graphics card for smoother playback."
            SettingDropdown {
                property string preferenceKey: "video/hwdec"
                options: [
                    { label: "Auto", value: "auto" },
                    { label: "Off", value: "no" },
                    { label: "Auto (copy-back)", value: "auto-copy" },
                ]
                value: Settings.value(preferenceKey)
                onActivated: newValue => Settings.setValue(preferenceKey, newValue)
            }
        }
        SettingRow {
            label: "Deinterlace"
            description: "Smooth interlaced footage from TV or DVD sources."
            SettingToggle {
                property string preferenceKey: "video/deinterlace"
                checked: Settings.flag(preferenceKey)
                onToggled: value => Settings.setValue(preferenceKey, value)
            }
        }
    }

    SettingGroup {
        title: "Picture"
        SettingRow {
            label: "Aspect ratio"
            shortcutHints: [{ label: "Cycle aspect ratio", command: "script-message app-cycle-aspect" }]
            description: "Default for every file. The shortcut changes only the current one."
            SettingChoice {
                property string preferenceKey: "video/aspectOverride"
                options: [
                    { label: "Original", value: "no" },
                    { label: "16:9", value: "16:9" },
                    { label: "4:3", value: "4:3" },
                    { label: "2.35:1", value: "2.35" },
                ]
                value: Preferences.defaultAspect
                function refreshPreference() { value = Preferences.defaultAspect }
                onActivated: newValue => Settings.setValue(preferenceKey, newValue)
            }
        }
        SettingRow {
            label: "Playback sync"
            description: "Keep motion in step with your display."
            SettingDropdown {
                property string preferenceKey: "video/videoSync"
                options: [
                    { label: "Match display", value: "display-resample" },
                    { label: "Audio", value: "audio" },
                    { label: "Display VDrop", value: "display-vdrop" },
                ]
                value: Settings.value(preferenceKey)
                onActivated: newValue => Settings.setValue(preferenceKey, newValue)
            }
        }
    }

    SettingGroup {
        title: "Shaders"
        SettingRow {
            label: "Video shader"
            shortcutHints: [{ label: "Choose shader", command: "script-message app-shader-dialog" }]
            description: "Apply a saved .glsl or .hook shader."
            FlatButton {
                label: "Choose file…"
                onActivated: root.applyShaderRequested()
            }
        }
        SettingRow {
            label: "Enable shaders"
            description: "Keep saved shaders available while temporarily disabling their effect."
            disabledReason: "Choose a shader first."
            SettingToggle {
                property string preferenceKey: "video/shadersEnabled"
                checked: Preferences.shadersEnabled
                enabled: Preferences.shaders.length > 0
                onToggled: value => Preferences.shadersEnabled = value
            }
        }
        Repeater {
            model: Preferences.shaders
            delegate: SettingRow {
                required property string modelData
                label: modelData.replace(/\\/g, "/").split("/").pop()
                description: modelData
                FlatButton {
                    label: "Remove"
                    onActivated: Preferences.removeShader(modelData)
                }
            }
        }
        SettingRow {
            label: "Reset shaders"
            description: "Clear the saved shader list. Shader files stay on disk."
            disabledReason: "No saved shaders."
            FlatButton {
                label: "Remove all"
                danger: true
                enabled: Preferences.shaders.length > 0 || !Preferences.shadersEnabled
                onActivated: Preferences.resetShaders()
            }
        }
    }
}
