pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Column {
    id: root
    required property var bindings
    property var draft: ({})
    property var inputTexts: ({})
    property string recordingId: ""
    readonly property var entries: bindings ? bindings.entries : []
    readonly property var validation: bindings ? bindings.validate(draft) : ({ valid: true, error: "" })
    readonly property bool shortcutsValid: validation.valid === true
    readonly property int matchingCount: {
        let count = 0
        for (const entry of entries) {
            if (matches(entry))
                ++count
        }
        return count
    }
    spacing: 14

    function copyValues(values) {
        const copy = []
        for (let i = 0; i < values.length; ++i)
            copy.push(values[i])
        return copy
    }

    function formatValues(values) {
        return bindings.format(values)
    }

    function reset() {
        const values = {}
        const texts = {}
        for (const entry of entries) {
            values[entry.id] = copyValues(entry.sequences)
            texts[entry.id] = formatValues(values[entry.id])
        }
        recordingId = ""
        searchInput.text = ""
        inputTexts = texts
        draft = values
    }

    function setText(id, text) {
        const values = Object.assign({}, draft)
        const texts = Object.assign({}, inputTexts)
        // Keep invalid input in the draft so Save cannot silently discard it.
        values[id] = bindings.parse(text)
        texts[id] = text
        inputTexts = texts
        draft = values
    }

    function restoreDefaults() {
        const values = {}
        const texts = {}
        for (const entry of entries) {
            values[entry.id] = copyValues(entry.defaults)
            texts[entry.id] = formatValues(values[entry.id])
        }
        recordingId = ""
        inputTexts = texts
        draft = values
    }

    function apply() {
        recordingId = ""
        return shortcutsValid && bindings.save(draft)
    }

    function matches(entry) {
        const query = searchInput.text.trim().toLocaleLowerCase()
        return query.length === 0
                || entry.title.toLocaleLowerCase().includes(query)
                || entry.category.toLocaleLowerCase().includes(query)
                || String(inputTexts[entry.id] || "").toLocaleLowerCase().includes(query)
    }

    onVisibleChanged: {
        if (!visible)
            recordingId = ""
    }

    component EditorButton: Button {
        id: control
        implicitHeight: 34
        font.pixelSize: 13
        palette.buttonText: enabled ? Theme.textPrimary : Theme.textMuted
        background: Rectangle {
            radius: 5
            color: control.down || control.hovered ? Theme.buttonHover : Theme.buttonBackground
            border.color: control.activeFocus ? Theme.accent : Theme.transparent
        }
    }

    Label {
        width: parent.width
        text: qsTr("Keyboard shortcuts")
        color: Theme.textPrimary
        font.pixelSize: 18
        font.bold: true
        wrapMode: Text.WordWrap
    }
    Label {
        width: parent.width
        text: qsTr("Edit shortcuts below, or record a key combination. Separate alternatives with a semicolon, for example Ctrl+O; F2. Clear an action to leave it unassigned. Changes take effect when you save.")
        color: Theme.textMuted
        font.pixelSize: 13
        wrapMode: Text.WordWrap
    }
    TextField {
        id: searchInput
        objectName: "shortcutsSearchInput"
        width: parent.width
        implicitHeight: 38
        placeholderText: qsTr("Search actions, categories, or shortcuts")
        Accessible.name: qsTr("Search keyboard shortcuts")
        selectByMouse: true
        color: Theme.textPrimary
        placeholderTextColor: Theme.textMuted
        font.pixelSize: 14
        background: Rectangle {
            color: Theme.searchBackground
            radius: 5
            border.color: searchInput.activeFocus ? Theme.accent : Theme.buttonBackground
        }
    }
    Flow {
        width: parent.width
        spacing: 10
        EditorButton {
            objectName: "shortcutsResetButton"
            text: qsTr("Restore all defaults")
            Accessible.name: qsTr("Restore all keyboard shortcuts to defaults")
            onClicked: root.restoreDefaults()
        }
        Label {
            height: 34
            verticalAlignment: Text.AlignVCenter
            text: qsTr("%1 of %2 actions").arg(root.matchingCount).arg(root.entries.length)
            color: Theme.textMuted
            font.pixelSize: 13
        }
    }
    Label {
        objectName: "shortcutValidationMessage"
        width: parent.width
        visible: !root.shortcutsValid
        text: root.validation.error || qsTr("Correct the shortcut assignments before saving.")
        color: Theme.accent
        font.pixelSize: 13
        wrapMode: Text.WordWrap
        Accessible.role: Accessible.AlertMessage
    }
    Label {
        objectName: "shortcutsEmptyMessage"
        width: parent.width
        visible: root.matchingCount === 0
        text: qsTr("No matching actions")
        color: Theme.textMuted
        wrapMode: Text.WordWrap
    }
    Column {
        id: rows
        width: parent.width
        spacing: 8
        Repeater {
            model: root.entries
            delegate: Rectangle {
                id: row
                required property var modelData
                readonly property bool recording: root.recordingId === modelData.id
                width: rows.width
                implicitHeight: contents.implicitHeight + 24
                visible: root.matches(modelData)
                color: Theme.tabBackground
                border.color: recording ? Theme.accent : Theme.buttonBackground
                radius: 8

                ColumnLayout {
                    id: contents
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.category
                        color: Theme.textMuted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.title
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }
                    TextField {
                        id: shortcutInput
                        objectName: "shortcutInput_" + row.modelData.id
                        Layout.fillWidth: true
                        implicitHeight: 38
                        text: root.inputTexts[row.modelData.id] || ""
                        placeholderText: qsTr("Unassigned")
                        Accessible.name: qsTr("Shortcut for %1").arg(row.modelData.title)
                        selectByMouse: true
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textMuted
                        font.pixelSize: 14
                        onTextChanged: {
                            if (text !== (root.inputTexts[row.modelData.id] || ""))
                                root.setText(row.modelData.id, text)
                        }
                        background: Rectangle {
                            color: Theme.searchBackground
                            radius: 4
                            border.color: shortcutInput.activeFocus ? Theme.accent : Theme.buttonBackground
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        EditorButton {
                            id: recordButton
                            objectName: "shortcutRecord_" + row.modelData.id
                            text: row.recording ? qsTr("Press keys…") : qsTr("Record")
                            Accessible.name: qsTr("Record shortcut for %1").arg(row.modelData.title)
                            Accessible.description: qsTr("Press a key combination to replace this action's shortcuts. Escape cancels recording.")
                            onClicked: {
                                root.recordingId = row.recording ? "" : row.modelData.id
                                forceActiveFocus()
                            }
                            onActiveFocusChanged: {
                                if (!activeFocus && row.recording)
                                    root.recordingId = ""
                            }
                            Keys.priority: Keys.BeforeItem
                            Keys.onPressed: (event) => {
                                if (!row.recording)
                                    return
                                event.accepted = true
                                if (event.key === Qt.Key_Escape) {
                                    root.recordingId = ""
                                    return
                                }
                                const sequence = root.bindings.fromKeyEvent(event.key, event.modifiers)
                                if (sequence.length === 0 || event.isAutoRepeat)
                                    return
                                root.setText(row.modelData.id, root.formatValues([sequence]))
                                root.recordingId = ""
                            }
                        }
                        EditorButton {
                            objectName: "shortcutClear_" + row.modelData.id
                            text: qsTr("Clear")
                            Accessible.name: qsTr("Clear shortcut for %1").arg(row.modelData.title)
                            onClicked: root.setText(row.modelData.id, "")
                        }
                        EditorButton {
                            objectName: "shortcutReset_" + row.modelData.id
                            text: qsTr("Reset")
                            Accessible.name: qsTr("Restore default shortcut for %1").arg(row.modelData.title)
                            onClicked: root.setText(row.modelData.id, root.formatValues(row.modelData.defaults))
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: row.recording
                        text: qsTr("Press a key combination. This replaces the current assignment. Esc cancels.")
                        color: Theme.accent
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
