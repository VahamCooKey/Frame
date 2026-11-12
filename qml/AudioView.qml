import QtQuick
import QtQuick.Effects

// Audio files: cover, title, artist · album and a whole-track waveform,
// centred over an opaque backdrop (mpv would otherwise stretch the cover
// across the window). The waveform is a seek target; the bottom controls
// are unchanged. AudioInfo (C++) supplies tags, cover and peaks.
Rectangle {
    id: view
    color: "#000000"

    // Space the bottom controls occupy, kept constant so the stack does not
    // jump when they fade.
    property real bottomReserve: 96
    property string fallbackTitle: ""

    readonly property real available: height - bottomReserve
    readonly property bool showArt: available >= 380
    readonly property bool showWave: available >= 200
    readonly property real artSize: Math.round(Math.max(140, Math.min(300, width * 0.3, available * 0.42)))
    readonly property string subtitle: [AudioInfo.artist, AudioInfo.album].filter(s => s).join(" · ")

    Column {
        id: stack
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(16, Math.round((view.available - height) / 2))
        width: Math.min(660, view.width - 80)
        spacing: 0

        Item {
            visible: view.showArt
            width: view.artSize; height: view.artSize
            anchors.horizontalCenter: parent.horizontalCenter

            Rectangle {
                id: artShape
                anchors.fill: parent
                radius: 10
                color: "#161616"
                border.width: cover.status === Image.Ready ? 0 : 1
                border.color: "#2C2C2C"
                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: "#000000"
                    shadowOpacity: 0.6
                    shadowBlur: 1.0
                    shadowVerticalOffset: 14
                }

                Image {
                    anchors.centerIn: parent
                    width: Math.round(parent.width * 0.24); height: width
                    visible: cover.status !== Image.Ready
                    source: "qrc:/assets/icons/audio.svg"
                    sourceSize: Qt.size(width * 2, height * 2)
                    opacity: 0.32
                }
            }

            Image {
                id: cover
                anchors.fill: parent
                source: AudioInfo.coverSource
                sourceSize: Qt.size(800, 800)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                smooth: true
                mipmap: true
                visible: false
            }
            Rectangle {
                id: coverMask
                anchors.fill: parent
                radius: 10
                visible: false
                layer.enabled: true
            }
            MultiEffect {
                anchors.fill: parent
                source: cover
                opacity: cover.status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 180 } }
                maskEnabled: true
                maskSource: coverMask
                maskThresholdMin: 0.5
                maskSpreadAtMin: 1.0
            }
        }

        Item { width: 1; height: view.showArt ? 26 : 0 }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: AudioInfo.title || view.fallbackTitle
            color: Colors.text
            font.family: Fonts.ui
            font.pixelSize: 22
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Item { width: 1; height: view.subtitle ? 6 : 0 }

        Text {
            visible: view.subtitle !== ""
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: view.subtitle
            color: Colors.textDim
            font.family: Fonts.ui
            font.pixelSize: 14
            elide: Text.ElideRight
        }

        Item { width: 1; height: view.showWave ? 32 : 0 }

        Item {
            id: wave
            objectName: "audioWaveform"
            visible: view.showWave
            width: parent.width
            height: 48

            readonly property int barWidth: 3
            readonly property int gap: 2
            readonly property int barCount: Math.max(1, Math.floor((width + gap) / (barWidth + gap)))
            readonly property var heights: {
                const peaks = AudioInfo.peaks
                const out = []
                for (let i = 0; i < barCount; ++i) {
                    let v = 0
                    if (peaks.length > 0) {
                        const from = Math.floor(i * peaks.length / barCount)
                        const to = Math.max(from + 1, Math.floor((i + 1) * peaks.length / barCount))
                        for (let j = from; j < to; ++j) v = Math.max(v, peaks[j])
                    }
                    out.push(Math.max(2, Math.round(v * height)))
                }
                return out
            }
            property real dragFraction: -1
            readonly property real fraction: dragFraction >= 0 ? dragFraction
                : Player.duration > 0
                  ? Math.max(0, Math.min(1, (Player.seekPending ? Player.seekTarget : Player.position) / Player.duration))
                  : 0

            component Bars: Row {
                property color tint
                spacing: wave.gap
                Repeater {
                    model: wave.barCount
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: wave.barWidth
                        height: wave.heights[index] || 2
                        radius: 1.5
                        color: tint
                    }
                }
            }

            Bars { height: wave.height; tint: "#4A4A4A"; opacity: AudioInfo.peaks.length > 0 ? 1 : 0.6 }
            Item {
                height: wave.height
                width: Math.round(wave.width * wave.fraction)
                clip: true
                Bars { height: wave.height; tint: Colors.text }
            }

            MouseArea {
                anchors.fill: parent
                anchors.topMargin: -8; anchors.bottomMargin: -8
                enabled: Player.duration > 0
                cursorShape: Qt.PointingHandCursor
                function fractionAt(x) { return Math.max(0, Math.min(1, x / wave.width)) }
                onPressed: mouse => wave.dragFraction = fractionAt(mouse.x)
                onPositionChanged: mouse => { if (pressed) wave.dragFraction = fractionAt(mouse.x) }
                onReleased: {
                    Player.seek(wave.dragFraction * Player.duration)
                    wave.dragFraction = -1
                }
                onCanceled: wave.dragFraction = -1
            }
        }
    }
}
