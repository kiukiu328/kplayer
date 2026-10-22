pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root
    required property var bindings
    property var draft: ({})
    property var inputTexts: ({})
    property string recordingId: ""
    property bool appendRecording: false
    property string selectedId: "openMedia"
    property string category: ""
    property bool customizedOnly: false
    property bool confirmingReset: false
    readonly property var entries: AppLanguage.language.length > 0 && bindings ? bindings.entries : []
    readonly property var selectedEntry: entries.find(entry => entry.id === selectedId) || null
    readonly property var validation: bindings ? bindings.validate(draft) : ({ valid: true, error: "" })
    readonly property bool shortcutsValid: validation.valid === true
    readonly property var categories: [qsTr("All categories"), ...new Set(entries.map(entry => entry.category))]
    readonly property var matchingEntries: entries.filter(entry => matches(entry))
    readonly property int matchingCount: matchingEntries.length
    readonly property int customizedCount: entries.filter(entry => isCustomized(entry)).length
    readonly property int changedCount: entries.filter(entry =>
        formatValues(draft[entry.id] || []) !== formatValues(entry.sequences)).length
    readonly property bool sideBySide: width >= 420

    function copyValues(values) {
        return Array.from(values)
    }

    function formatValues(values) {
        return bindings.format(values)
    }

    function isCustomized(entry) {
        return formatValues(draft[entry.id] || []) !== formatValues(entry.defaults)
    }

    function reset() {
        const values = {}
        const texts = {}
        for (const entry of entries) {
            values[entry.id] = copyValues(entry.sequences)
            texts[entry.id] = formatValues(values[entry.id])
        }
        recordingId = ""
        confirmingReset = false
        category = ""
        categoryChoice.currentIndex = 0
        customizedOnly = false
        searchInput.text = ""
        inputTexts = texts
        draft = values
        if (!selectedEntry && entries.length)
            selectedId = entries[0].id
        actionList.positionViewAtBeginning()
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
        confirmingReset = false
        inputTexts = texts
        draft = values
    }

    function apply() {
        recordingId = ""
        return shortcutsValid && bindings.save(draft)
    }

    function matches(entry) {
        if ((category.length && entry.category !== category)
                || (customizedOnly && !isCustomized(entry)))
            return false
        const query = searchInput.text.trim().toLocaleLowerCase()
        return query.length === 0
                || entry.title.toLocaleLowerCase().includes(query)
                || entry.category.toLocaleLowerCase().includes(query)
                || String(inputTexts[entry.id] || "").toLocaleLowerCase().includes(query)
    }

    function selectAction(id) {
        recordingId = ""
        selectedId = id
        const index = matchingEntries.findIndex(entry => entry.id === id)
        if (index >= 0)
            actionList.positionViewAtIndex(index, ListView.Contain)
    }

    function filterChanged() {
        recordingId = ""
        const matchesNow = entries.filter(entry => matches(entry))
        if (matchesNow.length && !matchesNow.some(entry => entry.id === selectedId))
            selectedId = matchesNow[0].id
        if (actionList)
            actionList.positionViewAtBeginning()
    }

    function showIssue(id) {
        searchInput.clear()
        categoryChoice.currentIndex = 0
        category = ""
        customizedOnly = false
        selectAction(id)
    }

    function startRecording(append) {
        appendRecording = append
        recordingId = selectedId
        recordButton.forceActiveFocus()
    }

    onVisibleChanged: {
        if (!visible) {
            recordingId = ""
            confirmingReset = false
        }
    }

    component EditorButton: Button {
        id: control
        implicitHeight: 36
        font.pixelSize: 13
        palette.buttonText: enabled ? Theme.textPrimary : Theme.textMuted
        background: Rectangle {
            radius: 6
            color: control.down || control.hovered ? Theme.buttonHover : Theme.buttonBackground
            border.color: control.activeFocus ? Theme.accent : Theme.transparent
        }
        ToolTip.visible: hovered && ToolTip.text.length > 0
        ToolTip.delay: 600
    }

    Column {
        id: toolbar
        width: parent.width
        spacing: 10
        RowLayout {
            width: parent.width
            spacing: 8
            TextField {
                id: searchInput
                objectName: "shortcutsSearchInput"
                Layout.fillWidth: true
                implicitHeight: 40
                leftPadding: 36
                rightPadding: clearSearch.visible ? 36 : 12
                placeholderText: qsTr("Search actions or keys…")
                Accessible.name: qsTr("Search keyboard shortcuts")
                selectByMouse: true
                color: Theme.textPrimary
                placeholderTextColor: Theme.textMuted
                font.pixelSize: 14
                onTextChanged: root.filterChanged()
                background: Rectangle {
                    color: Theme.searchBackground
                    radius: 6
                    border.color: searchInput.activeFocus ? Theme.accent : Theme.buttonBackground
                }
                PlayerIcon {
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16; height: 16
                    name: "search"
                    color: Theme.textMuted
                }
                IconButton {
                    id: clearSearch
                    objectName: "shortcutsClearSearch"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 34; height: 34
                    iconSize: 14
                    iconName: "close"
                    label: qsTr("Clear search")
                    visible: searchInput.text.length > 0
                    onClicked: { searchInput.clear(); searchInput.forceActiveFocus() }
                }
            }
            EditorButton {
                objectName: "shortcutsResetButton"
                text: qsTr("Restore all defaults")
                enabled: root.customizedCount > 0
                onClicked: root.confirmingReset = !root.confirmingReset
            }
        }
        Flow {
            width: parent.width
            spacing: 10
            ComboBox {
                id: categoryChoice
                objectName: "shortcutsCategoryChoice"
                width: Math.min(210, root.width)
                implicitHeight: 36
                model: root.categories
                Accessible.name: qsTr("Shortcut category")
                onCurrentIndexChanged: {
                    root.category = currentIndex > 0 ? root.categories[currentIndex] : ""
                    root.filterChanged()
                }
                delegate: ItemDelegate {
                    id: option
                    required property string modelData
                    required property int index
                    width: categoryChoice.width
                    text: modelData
                    highlighted: categoryChoice.highlightedIndex === index
                    palette.text: Theme.textPrimary
                    palette.highlightedText: Theme.accent
                    background: Rectangle {
                        color: option.highlighted || option.hovered ? Theme.chromeHover : Theme.searchBackground
                    }
                }
            }
            CheckBox {
                objectName: "shortcutsCustomizedOnly"
                height: 36
                text: qsTr("Customized only")
                checked: root.customizedOnly
                onToggled: { root.customizedOnly = checked; root.filterChanged() }
                font.pixelSize: 13
            }
        }
    }
    Popup {
        id: resetPopup
        x: (root.width - width) / 2
        y: Math.max(0, (root.height - height) / 2)
        width: Math.min(360, root.width)
        padding: 14
        modal: true
        focus: true
        visible: root.confirmingReset
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: root.confirmingReset = false
        background: Rectangle {
            color: Theme.tabBackground
            radius: 8
            border.color: Theme.accent
        }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: qsTr("Restore every shortcut? Changes are kept as a draft until you save.")
                color: Theme.textPrimary
                wrapMode: Text.WordWrap
                font.pixelSize: 13
            }
            RowLayout {
                EditorButton {
                    objectName: "shortcutsConfirmReset"
                    text: qsTr("Restore defaults")
                    onClicked: root.restoreDefaults()
                }
                EditorButton {
                    text: qsTr("Keep changes")
                    onClicked: root.confirmingReset = false
                }
            }
        }
    }
    Item {
        id: workspace
        anchors { top: toolbar.bottom; topMargin: 12; left: parent.left; right: parent.right; bottom: parent.bottom }
        Rectangle {
            id: listPanel
            width: root.sideBySide ? parent.width - editorPanel.width - 12 : parent.width
            height: root.sideBySide ? parent.height : parent.height - editorPanel.height - 10
            color: Theme.searchBackground
            border.color: Theme.buttonBackground
            radius: 8
            clip: true
            ColumnLayout {
                anchors { fill: parent; margins: 1 }
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: (root.width >= 620 ? qsTr("Action / Shortcut") + " · " : "")
                        + qsTr("%1 of %2 actions").arg(root.matchingCount).arg(root.entries.length)
                    padding: 12
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.buttonBackground }
                ListView {
                    id: actionList
                    objectName: "shortcutsActionList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.matchingEntries
                    currentIndex: root.matchingEntries.findIndex(entry => entry.id === root.selectedId)
                    boundsBehavior: Flickable.StopAtBounds
                    activeFocusOnTab: true
                    Accessible.name: qsTr("Shortcut actions")
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    Keys.onPressed: (event) => {
                        if (count && (event.key === Qt.Key_Home || event.key === Qt.Key_End)) {
                            root.selectAction(root.matchingEntries[event.key === Qt.Key_Home ? 0 : count - 1].id)
                            event.accepted = true
                        }
                    }
                    Keys.onReturnPressed: { shortcutInput.forceActiveFocus(); shortcutInput.selectAll() }
                    Keys.onUpPressed: {
                        const index = Math.max(0, currentIndex - 1)
                        if (count) root.selectAction(root.matchingEntries[index].id)
                    }
                    Keys.onDownPressed: {
                        const index = Math.min(count - 1, currentIndex + 1)
                        if (count) root.selectAction(root.matchingEntries[index].id)
                    }
                    delegate: ItemDelegate {
                        id: actionRow
                        required property var modelData
                        required property int index
                        objectName: "shortcutRow_" + modelData.id
                        width: actionList.width
                        height: Math.max(66, rowContent.implicitHeight + 20)
                        highlighted: root.selectedId === modelData.id
                        Accessible.name: modelData.title + ": " + (root.inputTexts[modelData.id] || qsTr("Unassigned"))
                        onClicked: { root.selectAction(modelData.id); actionList.forceActiveFocus() }
                        contentItem: RowLayout {
                            id: rowContent
                            spacing: 8
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Label {
                                    Layout.fillWidth: true
                                    text: actionRow.modelData.title
                                    color: actionRow.highlighted ? Theme.accent : Theme.textPrimary
                                    font.pixelSize: 13
                                    wrapMode: Text.WordWrap
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: actionRow.modelData.category
                                        + (root.isCustomized(actionRow.modelData) ? " · " + qsTr("Customized") : "")
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }
                            Flow {
                                Layout.preferredWidth: Math.min(140, actionList.width * 0.4)
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 4
                                Repeater {
                                    model: (root.draft[actionRow.modelData.id] || []).length
                                        ? root.draft[actionRow.modelData.id] : [qsTr("Unassigned")]
                                    delegate: Rectangle {
                                        id: keyBadge
                                        required property string modelData
                                        width: Math.min(implicitWidth, parent ? parent.width : implicitWidth)
                                        implicitWidth: keyText.implicitWidth + 12
                                        height: 24
                                        radius: 4
                                        color: Theme.tabBackground
                                        border.color: Theme.buttonBackground
                                        Label {
                                            id: keyText
                                            anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                            text: keyBadge.modelData
                                            elide: Text.ElideRight
                                            verticalAlignment: Text.AlignVCenter
                                            color: Theme.textPrimary
                                            font.pixelSize: 12
                                        }
                                    }
                                }
                            }
                        }
                        background: Rectangle {
                            color: actionRow.highlighted ? Theme.panelBackground
                                : actionRow.hovered ? Theme.chromeHover : Theme.transparent
                            Rectangle {
                                width: 3
                                height: parent.height
                                color: Theme.accent
                                visible: actionRow.highlighted
                            }
                            Rectangle {
                                anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                                height: 1
                                color: Theme.tabBackground
                            }
                        }
                    }
                    Label {
                        objectName: "shortcutsEmptyMessage"
                        anchors { fill: parent; margins: 20 }
                        visible: root.matchingCount === 0
                        text: qsTr("No matching actions. Try another search or category.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 13
                    }
                }
            }
        }
        Rectangle {
            id: editorPanel
            x: root.sideBySide ? parent.width - width : 0
            y: root.sideBySide ? 0 : listPanel.height + 10
            width: root.sideBySide ? Math.min(260, root.width * 0.46) : parent.width
            height: root.sideBySide ? parent.height : Math.min(230, parent.height * 0.6)
            color: Theme.tabBackground
            radius: 8
            border.color: root.recordingId.length ? Theme.accent : Theme.buttonBackground
            ScrollView {
                id: editorScroll
                objectName: "shortcutsEditorScroll"
                anchors { fill: parent; margins: 14 }
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: editorScroll.availableWidth
                    spacing: root.height < 350 ? 8 : 12
                    Label {
                        Layout.fillWidth: true
                        text: root.selectedEntry ? root.selectedEntry.category : ""
                        visible: root.height >= 350
                        color: Theme.textMuted
                        font.pixelSize: 12
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.selectedEntry ? root.selectedEntry.title : ""
                        color: Theme.textPrimary
                        font.pixelSize: root.height < 350 ? 16 : 18
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Assigned shortcuts")
                        visible: root.height >= 350
                        color: Theme.textPrimary
                        font.pixelSize: 13
                    }
                    TextField {
                        id: shortcutInput
                        objectName: "shortcutInput_" + root.selectedId
                        Layout.fillWidth: true
                        implicitHeight: 40
                        text: root.inputTexts[root.selectedId] || ""
                        placeholderText: qsTr("Unassigned")
                        Accessible.name: qsTr("Shortcut for %1").arg(root.selectedEntry ? root.selectedEntry.title : "")
                        selectByMouse: true
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textMuted
                        font.pixelSize: 14
                        onTextChanged: {
                            if (text !== (root.inputTexts[root.selectedId] || ""))
                                root.setText(root.selectedId, text)
                        }
                        background: Rectangle {
                            color: Theme.searchBackground
                            radius: 5
                            border.color: shortcutInput.activeFocus ? Theme.accent : Theme.buttonBackground
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        EditorButton {
                            id: recordButton
                            objectName: "shortcutRecord_" + root.selectedId
                            text: root.recordingId.length ? qsTr("Press keys…") : qsTr("Record")
                            Accessible.name: qsTr("Record shortcut for %1").arg(root.selectedEntry ? root.selectedEntry.title : "")
                            onClicked: {
                                if (root.recordingId.length)
                                    root.recordingId = ""
                                else
                                    root.startRecording(false)
                            }
                            onActiveFocusChanged: {
                                if (!activeFocus)
                                    root.recordingId = ""
                            }
                            Keys.priority: Keys.BeforeItem
                            Keys.onPressed: (event) => {
                                if (!root.recordingId.length)
                                    return
                                event.accepted = true
                                if (event.key === Qt.Key_Escape) {
                                    root.recordingId = ""
                                    return
                                }
                                const sequence = root.bindings.fromKeyEvent(event.key, event.modifiers)
                                if (!sequence.length || event.isAutoRepeat)
                                    return
                                const values = root.appendRecording ? root.copyValues(root.draft[root.selectedId] || []) : []
                                if (!values.includes(sequence))
                                    values.push(sequence)
                                root.setText(root.selectedId, root.formatValues(values))
                                root.recordingId = ""
                            }
                        }
                        EditorButton {
                            objectName: "shortcutAdd_" + root.selectedId
                            text: qsTr("Add another")
                            ToolTip.text: qsTr("Record an alternative without replacing existing shortcuts")
                            onClicked: root.startRecording(true)
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: root.height >= 350 || root.recordingId.length > 0
                        text: root.recordingId.length
                            ? (root.appendRecording ? qsTr("Press a key combination to add. Esc cancels.")
                                                    : qsTr("Press a key combination to replace the assignment. Esc cancels."))
                            : qsTr("Record a combination, or type keys separated by semicolons: Ctrl+O; F2.")
                        color: root.recordingId.length ? Theme.accent : Theme.textMuted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        objectName: "shortcutValidationMessage"
                        Layout.fillWidth: true
                        visible: !root.shortcutsValid
                        text: root.validation.error || ""
                        color: Theme.accent
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                        Accessible.role: Accessible.AlertMessage
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: !root.shortcutsValid
                        Repeater {
                            model: [root.validation.commandId || "", root.validation.conflictingId || ""].filter(id => id.length)
                            delegate: EditorButton {
                                id: issueButton
                                required property string modelData
                                objectName: "shortcutIssue_" + modelData
                                text: root.entries.find(entry => entry.id === issueButton.modelData)?.title || modelData
                                width: Math.min(implicitWidth, parent ? parent.width : implicitWidth)
                                onClicked: root.showIssue(modelData)
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.buttonBackground }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Default: %1").arg(root.selectedEntry
                            ? root.formatValues(root.selectedEntry.defaults) : "")
                        color: Theme.textMuted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        EditorButton {
                            objectName: "shortcutClear_" + root.selectedId
                            text: qsTr("Clear")
                            Accessible.name: qsTr("Clear shortcut for %1").arg(root.selectedEntry ? root.selectedEntry.title : "")
                            enabled: (root.draft[root.selectedId] || []).length > 0
                            onClicked: root.setText(root.selectedId, "")
                        }
                        EditorButton {
                            objectName: "shortcutReset_" + root.selectedId
                            text: qsTr("Reset")
                            Accessible.name: qsTr("Restore default shortcut for %1").arg(root.selectedEntry ? root.selectedEntry.title : "")
                            enabled: root.selectedEntry && root.isCustomized(root.selectedEntry)
                            onClicked: root.setText(root.selectedId, root.formatValues(root.selectedEntry.defaults))
                        }
                    }
                }
            }
        }
    }
}
