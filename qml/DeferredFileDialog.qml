import QtQuick
import QtQuick.Dialogs

// Non-Windows file pickers (A29, plan §4.4): QtQuick.Dialogs behind the same
// lazy Loader the other five Deferred* dialogs use, so neither startup nor
// the first Open command pays for the module. Windows keeps its COM dialogs
// in NativeWindowWin32 — deferred loading means the import only resolves
// when a non-Windows build first opens a picker.
Item {
    id: root

    property bool active: false
    // A FileDialog is not a visual child; keep the wrapper transparent.
    visible: false

    // Re-emitted as raw URLs (DropArea's contract); main.qml converts them
    // with the same urlToPath() it already uses for drag-and-drop.
    signal accepted(var files)

    readonly property var mediaFilters: [
        "Video files (*.mp4 *.mkv *.avi *.mov *.webm *.m4v *.flv *.wmv *.ts *.mpg *.mpeg)"
    ]
    readonly property var shaderFilters: [
        "Shaders (*.glsl *.hook)",
        "All files (*)"
    ]
    readonly property var subtitleFilters: [
        "Subtitle files (*.srt *.ass *.ssa *.vtt *.sub *.idx *.sup *.smi *.txt)",
        "All files (*)"
    ]

    // "media", "shader", "subtitle", or "folder" — set by the open functions,
    // read by main.qml's accepted() handler to route the result.
    property string mode: "media"
    property bool activeFolder: false

    function openMediaFiles() {
        root.mode = "media"
        root.openDialog(FileDialog.OpenFiles, root.mediaFilters)
    }

    function openShader() {
        root.mode = "shader"
        root.openDialog(FileDialog.OpenFile, root.shaderFilters)
    }

    function openSubtitle() {
        root.mode = "subtitle"
        root.openDialog(FileDialog.OpenFile, root.subtitleFilters)
    }

    function openFolder() {
        root.mode = "folder"
        if (!root.activeFolder)
            root.activeFolder = true
        else if (folderLoader.item)
            folderLoader.item.open()
    }

    function openDialog(fileMode, filters) {
        root.active = true
        // First call loads the Loader synchronously above; both paths end
        // with the dialog configured and open.
        if (loader.item) {
            loader.item.fileMode = fileMode
            loader.item.nameFilters = filters
            loader.item.open()
        }
    }

    Loader {
        id: loader
        active: root.active
        asynchronous: false
        sourceComponent: Component {
            FileDialog {
                title: qsTr("Open")
                onRejected: root.active = false
            }
        }

        onLoaded: item.accepted.connect(function() {
            // accepted() has no arguments. Copy the selection before
            // deactivating the Loader, which destroys its dialog.
            const files = Array.from(item.selectedFiles)
            root.active = false
            root.accepted(files)
        })
    }

    Loader {
        id: folderLoader
        active: root.activeFolder
        asynchronous: false
        sourceComponent: Component {
            FolderDialog {
                title: qsTr("Choose Folder")
                onRejected: root.activeFolder = false
            }
        }

        onLoaded: {
            item.accepted.connect(function() {
                const files = [String(item.selectedFolder)]
                root.activeFolder = false
                root.accepted(files)
            })
            item.open()
        }
    }
}
