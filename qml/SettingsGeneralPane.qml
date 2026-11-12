import QtQuick

Column {
    id: root
    spacing: 28
    property string preferenceKey: "general/screenshotDirectory"
    property string screenshotDir: Settings.value(preferenceKey)
    function refreshPreference() { screenshotDir = Settings.value(preferenceKey) }

    SettingGroup {
        title: "Continue watching"
        SettingRow {
            label: "Remember playback position"
            description: "Pick up where you left off."
            SettingToggle {
                id: resumeToggle
                property string preferenceKey: "general/resumeEnabled"
                checked: Settings.flag(preferenceKey)
                onToggled: value => Settings.setValue("general/resumeEnabled", value)
            }
        }
    }

    SettingGroup {
        title: "Files and playlist"
        SettingRow {
            label: "Add videos from the same folder"
            description: "Automatically build a playlist when you open a video."
            SettingToggle {
                property string preferenceKey: "general/folderAutoEnqueue"
                checked: Settings.flag(preferenceKey)
                onToggled: value => Settings.setValue("general/folderAutoEnqueue", value)
            }
        }
        SettingRow {
            label: "Open videos in this window"
            description: "A video opened while Frame is running plays here instead of in a new window. Takes effect on the next launch."
            SettingToggle {
                property string preferenceKey: "general/singleInstance"
                checked: Settings.flag(preferenceKey)
                onToggled: value => Settings.setValue("general/singleInstance", value)
            }
        }
        SettingRow {
            label: "Repeat playlist"
            description: "Start again after the last video."
            SettingToggle {
                property string preferenceKey: "general/playlistRepeat"
                checked: Settings.flag(preferenceKey)
                onToggled: value => Settings.setValue(preferenceKey, value)
            }
        }
    }

    SettingGroup {
        title: "History"
        SettingRow {
            label: "Keep recent files"
            description: "Choose how many files appear in your history."
            SettingChoice {
                property string preferenceKey: "general/recentsLimit"
                options: [
                    { label: "5", value: 5 },
                    { label: "10", value: 10 },
                    { label: "20", value: 20 },
                    { label: "50", value: 50 },
                ]
                value: Settings.value(preferenceKey)
                onActivated: newValue => Settings.setValue("general/recentsLimit", newValue)
            }
        }
        SettingRow {
            label: "Clear watch history"
            description: "Remove the list of recently opened files."
            FlatButton {
                label: "Clear history"
                danger: true
                confirmLabel: "Confirm clear"
                onActivated: Settings.clearRecentFiles()
            }
        }
    }

    SettingGroup {
        title: "Screenshots"
        SettingRow {
            label: "Image format"
            description: "Choose a format for saved screenshots."
            SettingChoice {
                property string preferenceKey: "general/screenshotFormat"
                options: [
                    { label: "PNG", value: "png" },
                    { label: "JPEG", value: "jpg" },
                    { label: "WebP", value: "webp" },
                ]
                value: Settings.value(preferenceKey)
                onActivated: newValue => Settings.setValue(preferenceKey, newValue)
            }
        }
        SettingRow {
            label: "Screenshot directory"
            description: root.screenshotDir !== ""
                         ? root.screenshotDir
                         : "Saved to " + Preferences.defaultScreenshotDirectory + " unless you choose a folder."
            Row {
                spacing: 8
                FlatButton {
                    label: "Choose folder…"
                    onActivated: {
                        var folder = ""
                        if (typeof Native !== "undefined" && Native.openFolderDialog)
                            folder = Native.openFolderDialog()
                        if (folder !== "") {
                            root.screenshotDir = folder
                            Settings.setValue("general/screenshotDirectory", folder)
                        }
                    }
                }
                FlatButton {
                    label: "Use default"
                    visible: root.screenshotDir !== ""
                    onActivated: Settings.setValue("general/screenshotDirectory", "")
                }
            }
        }
        SettingRow {
            label: "File naming"
            description: "How your screenshot files are named. A two-digit counter at the end keeps repeated shots of the same moment apart."
            SettingDropdown {
                property string preferenceKey: "general/screenshotTemplate"
                options: [
                    { label: "Filename + timestamp", value: "Frame_%F_%p" },
                    { label: "Filename + sequence", value: "%F-%n" },
                    { label: "Timestamp only", value: "Frame_%p" },
                    { label: "Filename + full timecode", value: "%F_%P" },
                ]
                value: Settings.value(preferenceKey)
                onActivated: newValue => Settings.setValue(preferenceKey, newValue)
            }
        }
    }
}
