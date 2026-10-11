pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Column {
    id: root
    spacing: 16
    property alias language: languageCodes.text
    property alias trackNumber: fallbackNumber.value
    property alias trackName: fallbackName.text
    property alias overrideStyle: styleOverride.checked
    readonly property string normalizedLanguage: language.trim().replace(/\s+/g, "").toLowerCase()
    property bool fallbackExpanded: false
    property int nextRuleId: 0
    property int ruleRevision: 0
    property int draggedRuleId: -1
    property point dragPosition
    property Flickable viewport
    signal revealRequested(real y, real height)
    signal focusRuleRequested(int ruleId)
    readonly property bool rulesValid: {
        // Role edits do not change the model count; also observe the edit revision.
        root.ruleRevision
        for (let i = 0; i < rules.count; ++i) {
            const rule = rules.get(i)
            if (root.ruleError(rule.kind, rule.value).length > 0)
                return false
        }
        return true
    }

    ListModel { id: rules }

    function reset(savedLanguage, savedNumber, savedName, savedRules, savedOverride) {
        language = savedLanguage
        trackNumber = savedNumber
        trackName = savedName
        overrideStyle = savedOverride
        fallbackExpanded = savedNumber > 0 || savedName.length > 0
        draggedRuleId = -1
        rules.clear()
        for (const line of savedRules.split("\n")) {
            const colon = line.indexOf(":")
            if (colon >= 0) {
                rules.append({ ruleId: nextRuleId++, kind: line.slice(0, colon).trim().toLowerCase(),
                               value: line.slice(colon + 1).trim() })
            }
        }
        ++ruleRevision
    }

    function serializedRules() {
        const lines = []
        for (let i = 0; i < rules.count; ++i) {
            const rule = rules.get(i)
            if (rule.value.trim().length > 0)
                lines.push(rule.kind + ":" + rule.value.trim())
        }
        return lines.join("\n")
    }

    function ruleError(kind, value) {
        if (value.trim().length === 0)
            return ""
        if (kind !== "subtrackname" && kind !== "tracknum")
            return qsTr("Choose a rule type to use this rule.")
        if (kind === "tracknum" && (!/^\d+$/.test(value.trim())
                || Number(value) < 1 || Number(value) > 2147483647))
            return qsTr("Enter a whole track number greater than 0.")
        return ""
    }

    function editRule(index, role, value) {
        rules.setProperty(index, role, value)
        ++ruleRevision
    }

    function moveRule(from, to) {
        if (to < 0 || to >= rules.count || from === to)
            return
        rules.move(from, to, 1)
        ++ruleRevision
    }

    function dragRule(index, y) {
        // Compare actual row centers, so wrapped names and validation messages
        // do not change the destination or make a row jump back and forth.
        let target = index
        for (let i = 0; i < ruleRepeater.count; ++i) {
            const row = ruleRepeater.itemAt(i)
            if (i < index && y <= row.y + row.height / 2) {
                target = i
                break
            }
            if (i > index && y >= row.y + row.height / 2)
                target = i
        }
        moveRule(index, target)
    }

    Timer {
        interval: 40
        repeat: true
        running: root.draggedRuleId >= 0 && root.visible
        onTriggered: {
            if (!root.viewport)
                return
            const local = root.mapFromItem(null, root.dragPosition.x, root.dragPosition.y)
            const position = root.mapToItem(root.viewport, local.x, local.y)
            const delta = position.y < 36 ? -12 : position.y > root.viewport.height - 36 ? 12 : 0
            if (delta === 0)
                return
            root.viewport.contentY = Math.max(0, Math.min(
                root.viewport.contentHeight - root.viewport.height, root.viewport.contentY + delta))
            const point = ruleRows.mapFromItem(null, root.dragPosition.x, root.dragPosition.y)
            for (let i = 0; i < rules.count; ++i) {
                if (rules.get(i).ruleId === root.draggedRuleId) {
                    root.dragRule(i, point.y)
                    break
                }
            }
        }
    }

    component Hint: Label {
        width: parent.width
        font.pixelSize: 13
        color: Theme.textMuted
        wrapMode: Text.WordWrap
    }

    component Section: Rectangle {
        id: section
        default property alias contents: sectionColumn.data
        property string heading
        implicitHeight: sectionColumn.implicitHeight + 36
        color: Theme.tabBackground
        border.color: Theme.buttonBackground
        radius: 12
        Column {
            id: sectionColumn
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 18 }
            spacing: 12
            Label {
                width: parent.width
                text: section.heading
                color: Theme.textPrimary
                font.pixelSize: 18
                font.bold: true
                wrapMode: Text.WordWrap
            }
        }
    }

    Section {
        width: root.width
        heading: qsTr("Priority rules")
        Hint { text: qsTr("Checked from top to bottom. The first match wins.") }
        Rectangle {
            objectName: "subtitleRulesEmptyState"
            visible: rules.count === 0
            width: parent.width
            implicitHeight: emptyMessage.implicitHeight + 32
            color: Theme.searchBackground
            radius: 8
            Column {
                id: emptyMessage
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                spacing: 6
                Label {
                    width: parent.width
                    text: qsTr("No rules yet")
                    color: Theme.textPrimary
                    font.bold: true
                }
                Hint { text: qsTr("Add a rule to prefer a track name or number, or use the fallback below.") }
            }
        }
        Column {
            id: ruleRows
            objectName: "subtitleRuleRows"
            width: parent.width
            spacing: 8
            Repeater {
                id: ruleRepeater
                model: rules
                delegate: Rectangle {
                    id: row
                    required property int index
                    required property int ruleId
                    required property string kind
                    required property string value
                    readonly property string error: root.ruleError(kind, value)
                    readonly property bool dragging: root.draggedRuleId === ruleId
                    readonly property bool compact: width < 390
                    function focusValue() {
                        ruleValue.forceActiveFocus()
                        root.revealRequested(mapToItem(root, 0, 0).y, height)
                    }
                    Connections {
                        target: root
                        function onFocusRuleRequested(ruleId) {
                            if (row.ruleId === ruleId)
                                row.focusValue()
                        }
                    }
                    width: ruleRows.width
                    implicitHeight: rowContents.implicitHeight + 20
                    color: row.dragging ? Theme.panelBackground : Theme.searchBackground
                    border.color: row.dragging || row.error.length > 0 ? Theme.accent
                                  : grip.activeFocus || ruleValue.activeFocus || typeChoice.activeFocus
                                    ? Theme.buttonHover : Theme.transparent
                    radius: 8
                    ColumnLayout {
                        id: rowContents
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Item {
                                id: grip
                                objectName: "subtitleRuleGrip" + row.index
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 40
                                activeFocusOnTab: true
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("Reorder rule %1").arg(row.index + 1)
                                ToolTip.visible: dragArea.containsMouse || grip.activeFocus
                                ToolTip.delay: 600
                                ToolTip.text: qsTr("Drag to reorder. Alt+Up / Alt+Down also moves this rule.")
                                Keys.onPressed: (event) => {
                                    if ((event.modifiers & Qt.AltModifier)
                                            && (event.key === Qt.Key_Up || event.key === Qt.Key_Down)) {
                                        root.moveRule(row.index, row.index + (event.key === Qt.Key_Up ? -1 : 1))
                                        event.accepted = true
                                    }
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    color: dragArea.pressed || dragArea.containsMouse ? Theme.buttonBackground : Theme.transparent
                                    border.color: grip.activeFocus ? Theme.accent : Theme.transparent
                                    radius: 4
                                }
                                Column {
                                    anchors.centerIn: parent
                                    spacing: 3
                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: row.index + 1
                                        font.pixelSize: 12
                                        font.bold: true
                                        color: row.index === 0 ? Theme.accent : Theme.textPrimary
                                    }
                                    Row {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        spacing: 3
                                        Repeater {
                                            model: 2
                                            delegate: Column {
                                                spacing: 3
                                                Repeater {
                                                    model: 3
                                                    delegate: Rectangle { width: 2; height: 2; radius: 1; color: Theme.textMuted }
                                                }
                                            }
                                        }
                                    }
                                }
                                MouseArea {
                                    id: dragArea
                                    objectName: "subtitleRuleDragHandle" + row.index
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    preventStealing: true
                                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                    onPressed: (mouse) => {
                                        grip.forceActiveFocus()
                                        root.draggedRuleId = row.ruleId
                                        root.dragPosition = mapToItem(null, mouse.x, mouse.y)
                                    }
                                    onPositionChanged: (mouse) => {
                                        if (pressed) {
                                            root.dragPosition = mapToItem(null, mouse.x, mouse.y)
                                            root.dragRule(row.index, mapToItem(ruleRows, mouse.x, mouse.y).y)
                                        }
                                    }
                                    onReleased: root.draggedRuleId = -1
                                    onCanceled: root.draggedRuleId = -1
                                }
                            }
                            Label {
                                visible: row.compact
                                Layout.fillWidth: true
                                text: qsTr("Rule %1").arg(row.index + 1)
                                color: Theme.textPrimary
                                font.pixelSize: 13
                            }
                            Item {
                                id: editorSlot
                                visible: !row.compact
                                Layout.fillWidth: true
                                implicitHeight: editor.implicitHeight
                            }
                            ToolButton {
                                id: removeButton
                                objectName: "removeSubtitleRuleButton" + row.index
                                Layout.preferredWidth: 30
                                Layout.preferredHeight: 36
                                Accessible.name: qsTr("Remove rule %1").arg(row.index + 1)
                                ToolTip.text: Accessible.name
                                ToolTip.visible: hovered || activeFocus
                                ToolTip.delay: 600
                                contentItem: PlayerIcon { name: "close"; color: Theme.textMuted }
                                onClicked: {
                                    rules.remove(row.index)
                                    ++root.ruleRevision
                                }
                            }
                        }
                        Item {
                            id: compactEditorSlot
                            visible: row.compact
                            Layout.fillWidth: true
                            implicitHeight: editor.implicitHeight
                        }
                        Hint {
                            visible: row.error.length > 0
                            Layout.fillWidth: true
                            text: row.error
                            color: Theme.accent
                        }
                    }
                    RowLayout {
                        id: editor
                        parent: row.compact ? compactEditorSlot : editorSlot
                        width: parent.width
                        spacing: 8
                        ComboBox {
                            id: typeChoice
                            objectName: "subtitleRuleType" + row.index
                            Layout.preferredWidth: row.compact ? 120 : 140
                            Layout.preferredHeight: 40
                            textRole: "label"
                            Accessible.name: qsTr("Rule type")
                            model: [
                                { label: qsTr("Track name"), code: "subtrackname" },
                                { label: qsTr("Track number"), code: "tracknum" }
                            ].concat(row.kind !== "subtrackname" && row.kind !== "tracknum"
                                     ? [{ label: qsTr("Choose type"), code: row.kind }] : [])
                            currentIndex: row.kind === "subtrackname" ? 0 : row.kind === "tracknum" ? 1 : 2
                            onActivated: root.editRule(row.index, "kind", model[currentIndex].code)
                            delegate: ItemDelegate {
                                id: typeOption
                                required property var model
                                required property int index
                                objectName: typeChoice.objectName + "Option" + index
                                width: ListView.view.width
                                text: model.label
                                highlighted: typeChoice.highlightedIndex === index
                                palette.text: Theme.textPrimary
                                palette.highlightedText: Theme.accent
                                background: Rectangle {
                                    color: typeOption.highlighted || typeOption.hovered ? Theme.buttonBackground : Theme.searchBackground
                                }
                            }
                        }
                        Label { text: ":"; color: Theme.textMuted }
                        TextArea {
                            id: ruleValue
                            objectName: "subtitleRuleValue" + row.index
                            Layout.fillWidth: true
                            Layout.minimumWidth: 60
                            Layout.minimumHeight: 40
                            text: row.value
                            textFormat: TextEdit.PlainText
                            font.pixelSize: 14
                            placeholderText: row.kind === "tracknum" ? qsTr("e.g. 2") : qsTr("e.g. Signs & Songs")
                            Accessible.name: row.kind === "tracknum" ? qsTr("Track number") : qsTr("Track name or filename")
                            selectByMouse: true
                            KeyNavigation.priority: KeyNavigation.BeforeItem
                            KeyNavigation.tab: removeButton
                            KeyNavigation.backtab: typeChoice
                            wrapMode: TextEdit.Wrap
                            inputMethodHints: row.kind === "tracknum" ? Qt.ImhDigitsOnly : Qt.ImhNone
                            onTextChanged: {
                                if (text !== row.value)
                                    root.editRule(row.index, "value", text.replace(/[\r\n]+/g, " "))
                            }
                            background: Rectangle {
                                color: Theme.chromeBackground
                                radius: 4
                                border.color: ruleValue.activeFocus ? Theme.accent : Theme.buttonBackground
                            }
                        }
                    }
                }
            }
        }
        Button {
            objectName: "addSubtitleRuleButton"
            text: qsTr("Add rule")
            onClicked: {
                const id = root.nextRuleId++
                rules.append({ ruleId: id, kind: "subtrackname", value: "" })
                ++root.ruleRevision
                Qt.callLater(() => root.focusRuleRequested(id))
            }
        }
        Hint {
            visible: rules.count > 0
            text: qsTr("Names match a track title or subtitle filename, ignoring case. Blank rules are skipped.")
        }
    }

    Section {
        width: root.width
        heading: qsTr("When no rule matches")
        Label {
            text: qsTr("Preferred languages")
            color: Theme.textPrimary
            font.pixelSize: 14
        }
        TextField {
            id: languageCodes
            objectName: "subtitleLanguageCodes"
            width: parent.width
            placeholderText: qsTr("e.g. zh,en or jpn")
            selectByMouse: true
            Accessible.name: qsTr("Preferred languages")
        }
        Hint { text: qsTr("Comma-separated language codes, in priority order. Leave blank for automatic selection.") }
        ToolButton {
            id: fallbackToggle
            objectName: "subtitleFallbackToggle"
            width: parent.width
            text: qsTr("Fallback track overrides")
            checkable: true
            checked: root.fallbackExpanded
            onClicked: root.fallbackExpanded = checked
            Accessible.name: text
            contentItem: RowLayout {
                spacing: 8
                PlayerIcon {
                    name: root.fallbackExpanded ? "chevronDown" : "chevronRight"
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                }
                Label {
                    text: fallbackToggle.text
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
        Column {
            width: parent.width
            spacing: 10
            visible: root.fallbackExpanded
            Label {
                text: qsTr("Preferred track name")
                color: Theme.textPrimary
                font.pixelSize: 14
            }
            TextField {
                id: fallbackName
                objectName: "subtitleTrackNameInput"
                width: parent.width
                placeholderText: qsTr("Track title or external subtitle filename")
                selectByMouse: true
                Accessible.name: qsTr("Preferred track name")
            }
            RowLayout {
                width: parent.width
                Label {
                    text: qsTr("Preferred track number")
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                SpinBox {
                    id: fallbackNumber
                    objectName: "subtitleTrackNumberInput"
                    Layout.preferredWidth: Math.min(150, root.width / 2)
                    from: 0
                    to: 9999
                    editable: true
                    Accessible.name: qsTr("Preferred track number")
                }
            }
            Hint {
                text: qsTr("A matching name is tried before the track number, then languages. Leave the name blank and number at 0 to use languages only.")
            }
        }
    }

    Section {
        width: root.width
        heading: qsTr("Subtitle appearance")
        CheckBox {
            id: styleOverride
            objectName: "subtitleStyleOverrideInput"
            width: parent.width
            text: qsTr("Override ASS subtitle styles")
            contentItem: Label {
                text: styleOverride.text
                leftPadding: styleOverride.indicator.width + styleOverride.spacing
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.WordWrap
                color: Theme.textPrimary
                font.pixelSize: 14
            }
        }
        Hint { text: qsTr("Use KPlayer's font and alignment for styled subtitles. Turn off to keep the original styling. Bitmap subtitles keep their original appearance.") }
    }
    Hint { text: qsTr("Changes apply when you save, including to future files.") }
}
