import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".." as ThemeControls

RowLayout {
    id: root

    property string label
    property string value
    property bool running: false
    property string sliderObjectName: ""
    property real from: 0
    property real to: 100
    property real stepSize: 1
    property int decimals: 0
    property string suffix: ""
    readonly property bool boundedSlider: true
    readonly property real numericValue: {
        const parsed = Number(root.value)
        return Number.isFinite(parsed)
                ? Math.max(root.from, Math.min(root.to, parsed))
                : root.from
    }

    signal edited(string value)

    Layout.fillWidth: true
    spacing: 10

    function formatNumber(number) {
        let formatted = number.toFixed(Math.max(0, root.decimals))
        while (formatted.indexOf(".") >= 0 && formatted.endsWith("0"))
            formatted = formatted.slice(0, -1)
        if (formatted.endsWith("."))
            formatted = formatted.slice(0, -1)
        return formatted === "-0" ? "0" : formatted
    }

    Label {
        Layout.fillWidth: true
        text: root.label
        wrapMode: Text.WordWrap
    }

    ThemeControls.ThemedSlider {
        id: slider

        objectName: root.sliderObjectName
        Layout.preferredWidth: 132
        from: root.from
        to: root.to
        stepSize: root.stepSize
        snapMode: Slider.SnapAlways
        value: root.numericValue
        enabled: !root.running
        live: true
        onMoved: root.edited(root.formatNumber(value))
    }

    Label {
        Layout.preferredWidth: 52
        horizontalAlignment: Text.AlignRight
        text: root.formatNumber(root.numericValue) + root.suffix
        font.family: "monospace"
    }
}
