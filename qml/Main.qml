pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "TranscriptScroll.js" as TranscriptScroll

ApplicationWindow {
    id: root
    required property var appController

    width: 420
    height: 860
    minimumWidth: 320
    minimumHeight: 320
    visible: true
    title: "Synsemble"
    color: backgroundColor
    font.family: uiFont
    topPadding: topBar.height
    bottomPadding: 0
    leftPadding: 0
    rightPadding: 0

    property rect keyboardRectangle: InputMethod.visible ? InputMethod.keyboardRectangle : Qt.rect(0, 0, 0, 0)
    // Android can report native pixels while the Quick window uses logical pixels.
    readonly property real keyboardScale: Qt.platform.os === "android" && keyboardRectangle.width > width + 1 ? Screen.devicePixelRatio : 1
    readonly property real bottomInset: Math.max(SafeArea.margins.bottom,
        keyboardRectangle.height > 0 ? Math.max(0, height - keyboardRectangle.y / keyboardScale) : 0)

    property var tableRows: []
    property var currentTable: ({})
    property var seatRows: []
    property var transcriptRows: []
    property var attachmentRows: []
    property var artifactRows: []
    property var logRows: []
    property var modelRefreshRows: []
    property var appearanceSettings: ({})
    property var safeguards: ({})
    property alias transcriptList: conversation.transcriptList
    property alias composer: conversation.composer
    property var drafts: ({})
    property string draftTableId: ""
    property string pickerTableId: ""
    property string editorTableId: ""
    property int detailsIndex: 0
    property url brandMark: darkMode ? "qrc:/branding/icon_light.png" : "qrc:/branding/icon_logo.png"
    property int selectedPage: 1
    property int selectedSettingsPage: 0
    property int editingSeatIndex: -1
    property var editingSeat: ({})
    property bool addingSeat: false
    readonly property var seatColorPresets: [
        { name: "Blue", value: "#4f86c6" },
        { name: "Cyan", value: "#2f9eaa" },
        { name: "Green", value: "#3f956f" },
        { name: "Amber", value: "#b98220" },
        { name: "Orange", value: "#c66a2b" },
        { name: "Red", value: "#bd5454" },
        { name: "Purple", value: "#8169b3" },
        { name: "Pink", value: "#b45582" }
    ]
    property int settingsGeneration: 0
    property string artifactPreviewTitle: ""
    property string artifactPreviewBody: ""
    property string toastMessage: ""
    property string transcriptViewTableId: ""
    property var transcriptScrollStates: ({})
    property var pendingTranscriptRestore: null
    property int transcriptRefreshGeneration: 0

    readonly property bool desktopLayout: width >= 1000
    readonly property bool sideDetails: width >= 1280
    readonly property bool calmTheme: appearanceSettings.colorTheme === "Calm Workspace"
    readonly property bool systemDark: Application.styleHints.colorScheme === Qt.Dark
    readonly property bool darkMode: appearanceSettings.appearance === "Dark"
                                     || (appearanceSettings.appearance === "System" && systemDark)
    FontLoader { id: workspaceFont; source: "qrc:/fonts/Inter.ttf" }
    FontLoader { id: consoleFont; source: "qrc:/fonts/JetBrainsMono-Regular.ttf" }
    FontLoader { source: "qrc:/fonts/JetBrainsMono-Bold.ttf" }
    readonly property string uiFont: appearanceSettings.fontStyle === "Console"
                                     ? consoleFont.name
                                     : appearanceSettings.fontStyle === "Workspace"
                                       ? workspaceFont.name
                                       : Application.font.family
    readonly property color backgroundColor: darkMode
                                                ? (calmTheme ? "#1c1a17" : "#202322")
                                                : (calmTheme ? "#f8f5ef" : "#faf9f6")
    readonly property color surfaceColor: darkMode
                                             ? (calmTheme ? "#29251f" : "#252827")
                                             : (calmTheme ? "#fffdf9" : "#ffffff")
    readonly property color raisedColor: darkMode
                                            ? (calmTheme ? "#231f1b" : "#2c302e")
                                            : (calmTheme ? "#f1e9dd" : "#f0efeb")
    readonly property color lineColor: darkMode
                                          ? (calmTheme ? "#5b5146" : "#414743")
                                          : (calmTheme ? "#d8cdbc" : "#dedfd9")
    readonly property color textColor: darkMode
                                          ? (calmTheme ? "#f1e9df" : "#eeeeea")
                                          : (calmTheme ? "#342a1d" : "#242b27")
    readonly property color mutedColor: darkMode
                                           ? (calmTheme ? "#b9aa98" : "#a7afa8")
                                           : (calmTheme ? "#766652" : "#626b64")
    readonly property color accentColor: calmTheme ? "#a06036" : "#16866c"
    readonly property color accentInkColor: "#ffffff"
    readonly property color dangerColor: darkMode ? "#ff8a83" : "#a92f2a"
    readonly property real transcriptFollowThreshold: 64
    readonly property string activeAgentName: {
        for (var i = 0; i < seatRows.length; ++i)
            if (seatRows[i].seatId === currentTable.activeSeatId) return seatRows[i].displayName;
        return "";
    }

    Material.theme: darkMode ? Material.Dark : Material.Light
    Material.accent: accentColor
    Material.primary: accentColor
    Material.background: backgroundColor
    Material.foreground: textColor

    function refreshTables() { tableRows = appController.tables(); }
    function refreshCurrentTable() {
        var nextId = appController.currentTableId;
        if (draftTableId !== nextId) {
            if (draftTableId) drafts[draftTableId] = composer.text;
            composer.text = drafts[nextId] || "";
            draftTableId = nextId;
            tableActionsMenu.close();
            removeAgentDialog.close();
            recoveryDialog.close();
            seatDialog.close();
            renameDialog.close();
            deleteDialog.close();
            continuationDialog.close();
            artifactDialog.close();
            artifactPreviewTitle = "";
            artifactPreviewBody = "";
        }
        currentTable = appController.currentTable();
    }
    function refreshSeats() { seatRows = appController.seats(); }
    function refreshAttachments() { attachmentRows = appController.attachments(); }
    function refreshArtifacts() { artifactRows = appController.artifacts(); }
    function refreshLogs() { logRows = appController.logs(); }

    function refreshTranscript() {
        var nextTableId = appController.currentTableId || "";
        var previousTableId = transcriptViewTableId;
        var previousState = null;
        if (previousTableId.length > 0) {
            previousState = pendingTranscriptRestore && pendingTranscriptRestore.tableId === previousTableId
                ? pendingTranscriptRestore.state
                : TranscriptScroll.capture(transcriptList, transcriptFollowThreshold);
            transcriptScrollStates[previousTableId] = previousState;
        }
        var restoreState = nextTableId === previousTableId && previousState
            ? previousState
            : transcriptScrollStates[nextTableId] || { follow: true, contentY: 0 };
        var rows = appController.transcript();
        transcriptRefreshGeneration += 1;
        transcriptRows = rows;
        transcriptViewTableId = nextTableId;
        pendingTranscriptRestore = {
            generation: transcriptRefreshGeneration,
            tableId: nextTableId,
            expectedCount: rows.length,
            state: restoreState,
            lastContentHeight: -1,
            stablePasses: 0,
            attempts: 0
        };
        transcriptRestoreTimer.interval = 16;
        transcriptRestoreTimer.restart();
    }

    function refreshSettings() {
        appearanceSettings = appController.settings();
        safeguards = appController.attachmentSafeguards();
        modelRefreshRows = appController.modelRefreshStatuses();
        settingsGeneration += 1;
        Qt.callLater(loadSettingsFields);
    }

    function refreshAll() {
        refreshTables();
        refreshCurrentTable();
        refreshSeats();
        refreshTranscript();
        refreshAttachments();
        refreshArtifacts();
        refreshLogs();
        refreshSettings();
    }

    function showErrorIfNeeded() {
        var message = appController.lastError();
        if (message && message.length > 0) {
            errorDialog.text = message;
            errorDialog.open();
        }
    }

    function showToast(message) {
        toastMessage = message;
        toastTimer.restart();
    }

    function sessionButtonLabel() { return currentTable.actionLabel || "Start task"; }

    function activateSession() {
        if (currentTable.nextAction === "fresh" || currentTable.nextAction === "restart" || currentTable.nextAction === "retry") {
            recoveryDialog.open(); return;
        }
        var ok = currentTable.nextAction === "pause" ? appController.pauseSession() : appController.runOrResume();
        if (!ok) showErrorIfNeeded();
    }

    function artifactType(phase) {
        if (phase === "Planning") return "Plan";
        if (phase === "Execution") return "Working artifact";
        if (phase === "Quality Control") return "Quality review";
        if (phase === "Present" || phase === "Completed") return "Final decision";
        return "Session artifact";
    }

    function openSeatEditor(index, adding) {
        editorTableId = appController.currentTableId;
        editingSeatIndex = index;
        editingSeat = seatRows[index] || ({});
        addingSeat = adding;
        seatNameField.text = adding ? "" : editingSeat.displayName || "";
        providerCombo.currentIndex = editingSeat.providerIndex || 0;
        effortCombo.currentIndex = editingSeat.effortIndex || 0;
        roleCombo.currentIndex = adding && seatRows.filter(function(s) { return s.occupied; }).length === 0 ? 1 : editingSeat.roleIndex || 0;
        var selectedColor = adding ? seatColorPresets[index % seatColorPresets.length].value : editingSeat.color || seatColorPresets[0].value;
        colorCombo.model = seatColorsFor(selectedColor);
        colorCombo.currentIndex = seatColorIndex(colorCombo.model, selectedColor);
        refreshModelCombo(editingSeat.modelId || "");
        seatDialog.open();
    }

    function seatColorsFor(selectedColor) {
        var options = [];
        var found = false;
        for (var i = 0; i < seatColorPresets.length; i++) {
            options.push(seatColorPresets[i]);
            found = found || seatColorPresets[i].value.toLowerCase() === selectedColor.toLowerCase();
        }
        if (!found && /^#[0-9a-fA-F]{6}$/.test(selectedColor)) {
            options.push({ name: "Custom", value: selectedColor.toLowerCase() });
        }
        return options;
    }

    function seatColorIndex(options, selectedColor) {
        for (var i = 0; i < options.length; i++) {
            if (options[i].value.toLowerCase() === selectedColor.toLowerCase()) return i;
        }
        return 0;
    }

    function openAddSeat() {
        for (var i = 0; i < seatRows.length; i++) {
            if (!seatRows[i].occupied) {
                openSeatEditor(i, true);
                return;
            }
        }
        if (seatRows.length < 8) {
            openSeatEditor(seatRows.length, true);
            return;
        }
        errorDialog.text = "This table already uses all eight agents.";
        errorDialog.open();
    }

    function refreshModelCombo(selectedModelId) {
        var models = appController.modelsForProvider(providerCombo.currentIndex);
        modelCombo.model = models;
        var found = 0;
        for (var i = 0; i < models.length; i++) {
            if (models[i].id === selectedModelId) {
                found = i;
                break;
            }
        }
        modelCombo.currentIndex = found;
    }

    function loadSettingsFields() { settingsPanel.loadSettingsFields(); }
    function saveAppearanceFromControls() { settingsPanel.saveAppearanceFromControls(); }
    function saveLimits() { settingsPanel.saveLimits(); }
    function createTable() { createTableDialog.open(); }
    function openArtifact() { artifactDialog.open(); }
    function pickAttachment() { pickerTableId = appController.currentTableId; attachmentDialog.open(); }
    function submitComposer() {
        if (appController.submitTask(composer.text)) {
            composer.text = "";
            drafts[appController.currentTableId] = "";
            hideKeyboardAfterSend();
        } else showErrorIfNeeded();
    }
    function openQuickGuide() { if (!desktopLayout) tableDrawer.close(); quickGuide.open(); }
    function openSettings() { selectedPage = 3; if (!desktopLayout) tableDrawer.close(); }
    function openDetails(index) { detailsIndex = index; detailsSheet.open(); }
    function handleBack() {
        if (errorDialog.visible) { errorDialog.close(); return; }
        if (settingsPanel.closeOpenMenu()) return;
        for (var combo of [roleCombo, providerCombo, modelCombo, effortCombo, colorCombo, detailsCombo]) {
            if (combo.popup.visible) { combo.popup.close(); return; }
        }
        var dialogs = [errorDialog, recoveryDialog, removeAgentDialog, tableActionsMenu, quickGuide, continuationDialog, attachmentDialog, deleteDialog, renameDialog, createTableDialog, artifactDialog, seatDialog];
        for (var i = 0; i < dialogs.length; ++i) {
            if (dialogs[i].visible) { dialogs[i].close(); return; }
        }
        if (detailsSheet.visible) { detailsSheet.close(); return; }
        if (tableDrawer.visible && !desktopLayout) { tableDrawer.close(); return; }
        if (selectedPage === 3) { selectedPage = 1; return; }
    }

    function hideKeyboardAfterSend() {
        composer.focus = false;
        // qmllint disable missing-property
        Qt.inputMethod.hide();
        // qmllint enable missing-property
        Qt.callLater(function () { root.contentItem.forceActiveFocus(); });
    }

    Component.onCompleted: {
        appController.startupInitialRefreshStarted();
        refreshAll();
        appController.startupInitialRefreshCompleted();
        appController.startupPrimaryControlsReady();
        if (!appController.initialized) Qt.callLater(showErrorIfNeeded);
        else if (!appearanceSettings.quickGuideSeen) Qt.callLater(function() { quickGuide.open(); });
    }

    Connections {
        target: root.appController
        function onStateChanged() { root.refreshCurrentTable(); }
        function onTablesChanged() { root.refreshTables(); }
        function onSeatsChanged() { root.refreshSeats(); }
        function onTranscriptChanged() { root.refreshTranscript(); }
        function onAttachmentsChanged() { root.refreshAttachments(); }
        function onArtifactsChanged() { root.refreshArtifacts(); }
        function onLogsChanged() { root.refreshLogs(); }
        function onSettingsChanged() { root.refreshSettings(); }
        function onAttachmentImportFailed() { root.showErrorIfNeeded(); }
        function onContinuationRequested(reason) {
            continuationDialog.text = reason;
            continuationDialog.open();
        }
    }

    Timer {
        id: transcriptRestoreTimer
        interval: 16
        onTriggered: {
            var pending = root.pendingTranscriptRestore;
            if (!pending || pending.generation !== root.transcriptRefreshGeneration) return;
            root.transcriptList.forceLayout();
            pending.attempts += 1;
            var currentHeight = root.transcriptList.contentHeight;
            var countReady = root.transcriptList.count === pending.expectedCount;
            var heightStable = countReady && Math.abs(currentHeight - pending.lastContentHeight) < 0.5;
            pending.stablePasses = heightStable ? pending.stablePasses + 1 : 0;
            pending.lastContentHeight = currentHeight;
            if (pending.stablePasses >= 2) {
                TranscriptScroll.restore(root.transcriptList, pending.state);
                root.transcriptScrollStates[pending.tableId] = TranscriptScroll.capture(root.transcriptList, root.transcriptFollowThreshold);
                root.pendingTranscriptRestore = null;
                root.appController.startupTranscriptVisualStable();
            } else {
                transcriptRestoreTimer.interval = pending.attempts < 12 ? 16 : 100;
                transcriptRestoreTimer.restart();
            }
        }
    }

    Timer { id: toastTimer; interval: 2600; onTriggered: root.toastMessage = "" }

    Shortcut {
        sequences: [StandardKey.Cancel, "Back"]
        // Window shortcuts are blocked by modal Quick popups.
        context: Qt.ApplicationShortcut
        enabled: recoveryDialog.visible || quickGuide.visible || tableActionsMenu.visible || removeAgentDialog.visible || errorDialog.visible || continuationDialog.visible || attachmentDialog.visible
                 || deleteDialog.visible || renameDialog.visible || createTableDialog.visible
                 || artifactDialog.visible || seatDialog.visible || root.selectedPage === 3
                 || detailsSheet.visible || (tableDrawer.visible && !root.desktopLayout)
        onActivated: root.handleBack()
    }

    header: Rectangle {
        id: topBar
        color: root.backgroundColor
        implicitHeight: 64 + root.SafeArea.margins.top
        RowLayout {
            anchors.fill: parent
            anchors.topMargin: root.SafeArea.margins.top
            anchors.leftMargin: 12 + root.SafeArea.margins.left + (root.desktopLayout ? tableDrawer.width : 0)
            anchors.rightMargin: 12 + root.SafeArea.margins.right
            spacing: 8
            Button {
                flat: true; implicitWidth: 48; implicitHeight: 48
                Accessible.name: "Open tables"
                visible: !root.desktopLayout
                onClicked: tableDrawer.open()
                contentItem: Item {
                    Column {
                        anchors.centerIn: parent
                        spacing: 4
                        Repeater { model: 3; Rectangle { width: 18; height: 2; radius: 1; color: root.textColor } }
                    }
                }
            }
            Label { text: root.selectedPage === 3 ? "Settings" : "Synsemble"; color: root.textColor; font.pixelSize: 18; font.bold: true; Layout.fillWidth: true }

        }
    }

    Drawer {
        id: tableDrawer
        parent: Overlay.overlay
        y: 0
        // Material defines per-edge safe-area padding; our content applies it once.
        topPadding: 0
        bottomPadding: 0
        leftPadding: 0
        rightPadding: 0
        objectName: "tableDrawer"
        width: Math.min(root.width * 0.88, 300)
        height: root.height
        modal: !root.desktopLayout
        focus: !root.desktopLayout
        interactive: !root.desktopLayout
        visible: root.desktopLayout
        closePolicy: root.desktopLayout ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: root.raisedColor }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            anchors.topMargin: 12 + root.SafeArea.margins.top
            anchors.leftMargin: 16 + root.SafeArea.margins.left
            anchors.rightMargin: 16 + root.SafeArea.margins.right
            anchors.bottomMargin: 12 + root.bottomInset
            spacing: 12
            Label { text: "Synsemble"; font.pixelSize: 24; font.bold: true; color: root.textColor }
            Button { text: "＋ Create table"; Layout.fillWidth: true; implicitHeight: 48; onClicked: root.createTable() }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.tableRows
                spacing: 4
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    implicitHeight: 64
                    highlighted: modelData.tableId === root.currentTable.tableId
                    text: (modelData.pinned ? "• " : "") + modelData.title
                    onClicked: {
                        root.appController.selectTable(modelData.tableId);
                        root.selectedPage = 1;
                        if (!root.desktopLayout) tableDrawer.close();
                    }
                }
            }
            Button {
                id: tableActionsButton
                objectName: "tableActionsButton"
                text: "Table actions"
                flat: true; Layout.fillWidth: true; implicitHeight: 48
                enabled: Boolean(root.currentTable.tableId)
                onClicked: tableActionsMenu.open()
                Menu {
                    id: tableActionsMenu
                    Material.elevation: 0
                    objectName: "tableActionsMenu"
                    width: Math.min(280, root.width - 32)
                    y: -implicitHeight
                    MenuItem { text: root.currentTable.title || ""; enabled: false }
                    MenuItem { text: "Rename"; onTriggered: { renameField.text = root.currentTable.title || ""; renameDialog.open(); } }
                    MenuItem { text: root.currentTable.pinned ? "Unpin" : "Pin"; onTriggered: root.appController.togglePinCurrentTable() }
                    MenuItem { text: "Duplicate"; onTriggered: { if (!root.appController.duplicateCurrentTable()) root.showErrorIfNeeded(); } }
                    MenuSeparator {}
                    MenuItem { text: "Delete table"; onTriggered: deleteDialog.open(); palette.text: root.dangerColor }
                }
            }
            MenuSeparator { Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                Button { objectName: "drawerGuide"; text: "Quick guide"; flat: true; Layout.fillWidth: true; Layout.preferredWidth: 1; implicitHeight: 48; onClicked: root.openQuickGuide() }
                Button { objectName: "drawerSettings"; text: "Settings"; flat: true; Layout.fillWidth: true; Layout.preferredWidth: 1; implicitHeight: 48; onClicked: root.openSettings() }
            }
        }
    }

    Rectangle {
        id: workspace
        objectName: "workspace"
        anchors.fill: parent
        anchors.leftMargin: (root.desktopLayout ? tableDrawer.width : 0) + root.SafeArea.margins.left
        anchors.rightMargin: root.SafeArea.margins.right + (root.sideDetails && detailsSheet.visible ? detailsSheet.width : 0)
        anchors.bottomMargin: root.bottomInset
        color: root.backgroundColor
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            ColumnLayout {
                objectName: "tableHeader"
                visible: root.selectedPage !== 3 && Boolean(root.currentTable.tableId)
                Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
                spacing: 0
                Label { text: root.currentTable.title || ""; color: root.textColor; font.pixelSize: 20; font.bold: true; wrapMode: Text.Wrap; Layout.fillWidth: true }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: (root.currentTable.statusLabel || root.currentTable.phase || "Idle") + (root.activeAgentName ? " · " + root.activeAgentName : ""); color: root.mutedColor; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.minimumWidth: 0 }
                    Button { text: "Team"; flat: true; implicitHeight: 48; onClicked: root.openDetails(0) }
                    Button { text: "Details"; flat: true; implicitHeight: 48; onClicked: root.openDetails(3) }
                }
            }
            ConversationPage { id: conversation; shell: root; visible: root.selectedPage !== 3; Layout.fillWidth: true; Layout.fillHeight: true }
            SettingsPage { id: settingsPanel; shell: root; visible: root.selectedPage === 3; Layout.fillWidth: true; Layout.fillHeight: true }
        }
    }

    Drawer {
        id: detailsSheet
        parent: Overlay.overlay
        y: 0
        topPadding: 0
        bottomPadding: 0
        leftPadding: 0
        rightPadding: 0
        objectName: "detailsSheet"
        edge: Qt.RightEdge
        width: root.desktopLayout ? Math.min(440, root.width * 0.45) : root.width
        height: root.height
        modal: !root.sideDetails
        focus: true
        background: Rectangle { color: root.backgroundColor }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            anchors.topMargin: 12 + root.SafeArea.margins.top
            anchors.leftMargin: 16 + root.SafeArea.margins.left
            anchors.rightMargin: 16 + root.SafeArea.margins.right
            anchors.bottomMargin: 12 + root.bottomInset
            RowLayout {
                Layout.fillWidth: true
                Label { objectName: "detailsHeading"; text: root.detailsIndex === 0 ? "Team" : "Session details"; font.pixelSize: 22; color: root.textColor; Layout.fillWidth: true }
                Button { text: "Close"; flat: true; implicitHeight: 48; onClicked: detailsSheet.close() }
            }
            ComboBox { id: detailsCombo; objectName: "detailsCategory"; Layout.fillWidth: true; model: ["Team", "Artifacts", "Activity", "Usage and controls"]; currentIndex: root.detailsIndex; onActivated: root.detailsIndex = currentIndex; Accessible.name: "Session detail section" }
            SessionDetails { shell: root; visible: root.detailsIndex !== 2; Layout.fillWidth: true; Layout.fillHeight: true }
            ListView {
                visible: root.detailsIndex === 2
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.logRows
                spacing: 16
                reuseItems: true
                delegate: Column {
                    id: activityRow
                    required property var modelData
                    width: ListView.view.width
                    spacing: 4
                    Label { width: parent.width; text: activityRow.modelData.type + (activityRow.modelData.actorName ? " · " + activityRow.modelData.actorName : "") + " · " + activityRow.modelData.phase + " · " + activityRow.modelData.timestamp; color: root.mutedColor; wrapMode: Text.Wrap }
                    Label { width: parent.width; text: activityRow.modelData.summary; color: root.textColor; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                }
                Label { visible: root.logRows.length === 0; text: "Activity will appear here."; color: root.mutedColor; anchors.centerIn: parent }
            }
            Button { text: root.sessionButtonLabel(); visible: root.detailsIndex === 3 && Boolean(root.currentTable.tableId) && root.currentTable.nextAction !== "start"; implicitHeight: 48; Layout.fillWidth: true; onClicked: root.activateSession() }
            Button { text: "Copy full transcript"; implicitHeight: 48; Layout.fillWidth: true; onClicked: { if (!root.appController.copyFullTranscript()) root.showErrorIfNeeded(); else root.showToast("Transcript copied"); } }
            Button { text: "Stop session"; implicitHeight: 48; Layout.fillWidth: true; enabled: root.appController.running || root.currentTable.phase === "Paused" || root.currentTable.phase === "Needs continuation"; onClicked: { if (!root.appController.stopSession()) root.showErrorIfNeeded(); } }
        }
    }

    Rectangle {
        visible: root.toastMessage.length > 0
        z: 20
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.desktopLayout ? 18 + root.bottomInset : 78 + root.bottomInset
        width: Math.min(parent.width - 24, toastLabel.implicitWidth + 32)
        height: 48
        radius: 6
        color: root.raisedColor
        border.color: root.accentColor
        border.width: 2
        Label { id: toastLabel; anchors.centerIn: parent; text: root.toastMessage; color: root.textColor; Accessible.role: Accessible.AlertMessage }
    }

    Dialog {
        id: seatDialog
        Material.elevation: 0
        objectName: "agentDialog"
        parent: Overlay.overlay
        title: root.addingSeat ? "Add agent" : "Configure agent"
        modal: true
        focus: true
        standardButtons: Dialog.NoButton
        width: Math.min(root.width - root.SafeArea.margins.left - root.SafeArea.margins.right - 24, 560)
        height: Math.min(root.height - root.SafeArea.margins.top - root.bottomInset - 36, 720)
        x: Math.round(root.SafeArea.margins.left + (root.width - root.SafeArea.margins.left - root.SafeArea.margins.right - width) / 2)
        y: Math.round(root.SafeArea.margins.top + (root.height - root.SafeArea.margins.top - root.bottomInset - height) / 2)
        footer: DialogButtonBox {
            Button { text: "Remove agent"; visible: !root.addingSeat; implicitHeight: 48; onClicked: removeAgentDialog.open() }
            Button { text: "Cancel"; implicitHeight: 48; onClicked: seatDialog.close() }
            Button {
                text: "Save agent"
                implicitHeight: 48
                onClicked: {
                    if (root.editorTableId !== root.appController.currentTableId) { seatDialog.close(); return; }
                    var selectedModel = modelCombo.currentValue || "";
                    if (root.appController.saveSeat(root.editingSeatIndex, true, seatNameField.text, providerCombo.currentIndex, selectedModel, effortCombo.currentIndex, roleCombo.currentIndex, colorCombo.currentValue)) seatDialog.close();
                    else seatValidation.text = root.appController.lastError();
                }
            }
        }
        onOpened: seatValidation.text = ""
        contentItem: ScrollView {
            id: seatScroll
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            clip: true
            ColumnLayout {
                width: seatScroll.availableWidth
                spacing: 9
                Label { id: seatValidation; visible: text.length > 0; color: root.dangerColor; Layout.fillWidth: true; wrapMode: Text.Wrap; Accessible.role: Accessible.AlertMessage }
                Label { text: "Agent name"; color: root.textColor }
                TextField { id: seatNameField; objectName: "agentName"; Layout.fillWidth: true; placeholderText: "Display name"; Accessible.name: "Agent name" }
                Label { text: "Role"; color: root.textColor }
                ComboBox { id: roleCombo; Accessible.name: "Agent role"; Layout.fillWidth: true; model: ["Participant", "Final Decision Maker", "Lead Planner", "Lead Executioner", "Lead Quality Control"] }
                Label { text: "One agent must be the Final Decision Maker."; color: root.mutedColor; Layout.fillWidth: true; wrapMode: Text.Wrap }
                Label { text: "Provider"; color: root.textColor }
                ComboBox { id: providerCombo; Accessible.name: "AI provider"; Layout.fillWidth: true; model: ["OpenAI", "Gemini", "Anthropic"]; onActivated: root.refreshModelCombo("") }
                Label { text: "Model"; color: root.textColor }
                ComboBox { id: modelCombo; Accessible.name: "AI model"; Layout.fillWidth: true; textRole: "displayName"; valueRole: "id" }
                Label { text: "Effort"; color: root.textColor }
                ComboBox { id: effortCombo; Accessible.name: "Reasoning effort"; Layout.fillWidth: true; model: ["Auto", "Light", "Balanced", "Deep"] }
                Label { text: "Agent color"; color: root.textColor }
                RowLayout {
                    Layout.fillWidth: true
                    Rectangle { Layout.preferredWidth: 42; Layout.preferredHeight: 42; radius: 6; color: colorCombo.currentValue || root.seatColorPresets[0].value; border.color: root.lineColor; border.width: 1 }
                    ComboBox {
                        id: colorCombo
                        Layout.fillWidth: true
                        textRole: "name"
                        valueRole: "value"
                        Accessible.name: "Agent color"
                        delegate: ItemDelegate {
                            id: colorChoice
                            required property int index
                            required property var modelData
                            width: colorCombo.width
                            highlighted: colorCombo.highlightedIndex === index
                            contentItem: RowLayout {
                                spacing: 10
                                Rectangle { Layout.preferredWidth: 24; Layout.preferredHeight: 24; radius: 4; color: colorChoice.modelData.value; border.color: root.lineColor; border.width: 1 }
                                Label { text: colorChoice.modelData.name; color: root.textColor; Layout.fillWidth: true }
                            }
                        }
                    }
                }
                Label { text: "Agent color appears as an accent and does not replace the speaker name or role."; color: root.mutedColor; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }
    }

    Dialog {
        id: artifactDialog
        Material.elevation: 0
        objectName: "artifactPreview"
        parent: Overlay.overlay
        title: root.artifactPreviewTitle
        modal: true
        focus: true
        standardButtons: Dialog.Close
        width: root.desktopLayout ? 760 : root.width - root.SafeArea.margins.left - root.SafeArea.margins.right
        height: root.height - root.SafeArea.margins.top - root.bottomInset
        x: root.SafeArea.margins.left + (root.width - root.SafeArea.margins.left - root.SafeArea.margins.right - width) / 2
        y: root.SafeArea.margins.top
        contentItem: ScrollView {
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            clip: true
            TextArea {
                text: root.artifactPreviewBody
                textFormat: TextEdit.PlainText
                readOnly: true
                wrapMode: TextArea.Wrap
                color: root.textColor
                Accessible.name: "Artifact preview"
            }
        }
    }

    Dialog {
        id: createTableDialog
        Material.elevation: 0
        property string validationError: ""
        parent: Overlay.overlay
        title: "Create table"
        modal: true
        focus: true
        standardButtons: Dialog.NoButton
        width: Math.min(root.width - root.SafeArea.margins.left - root.SafeArea.margins.right - 24, 460)
        x: Math.round(root.SafeArea.margins.left + (root.width - root.SafeArea.margins.left - root.SafeArea.margins.right - width) / 2)
        y: Math.round(root.SafeArea.margins.top + (root.height - root.SafeArea.margins.top - root.bottomInset - height) / 2)
        onOpened: validationError = ""
        footer: DialogButtonBox {
            Button { text: "Cancel"; implicitHeight: 48; onClicked: createTableDialog.close() }
            Button { text: "Create table"; implicitHeight: 48; onClicked: {
                if (!root.appController.createTable(createTitle.text)) createTableDialog.validationError = root.appController.lastError();
                else { createTableDialog.close(); root.selectedPage = 1; if (!root.desktopLayout) tableDrawer.close(); }
            } }
        }
        ColumnLayout {
            width: parent.width
            Label { text: createTableDialog.validationError; visible: text.length > 0; color: root.dangerColor; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: "Table title"; color: root.textColor }
            TextField { id: createTitle; Layout.fillWidth: true; text: "New table"; selectByMouse: true }
        }
    }

    Dialog {
        id: renameDialog
        Material.elevation: 0
        parent: Overlay.overlay
        property string validationError: ""
        title: "Rename table"
        modal: true
        focus: true
        standardButtons: Dialog.NoButton
        width: Math.min(root.width - 24, 460)
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        onOpened: validationError = ""
        footer: DialogButtonBox {
            Button { text: "Cancel"; implicitHeight: 48; onClicked: renameDialog.close() }
            Button { text: "Save name"; implicitHeight: 48; onClicked: {
                if (root.appController.renameCurrentTable(renameField.text)) renameDialog.close();
                else renameDialog.validationError = root.appController.lastError();
            } }
        }
        ColumnLayout {
            width: parent.width
            TextField { id: renameField; Layout.fillWidth: true; Accessible.name: "Table title" }
            Label { text: renameDialog.validationError; visible: text.length > 0; color: root.dangerColor; Layout.fillWidth: true; wrapMode: Text.Wrap }
        }
    }

    Dialog {
        id: recoveryDialog
        objectName: "recoveryDialog"
        parent: Overlay.overlay
        modal: true; focus: true; Material.elevation: 0
        title: root.currentTable.actionLabel || "Session action"
        width: Math.min(root.width - 32, 440)
        height: Math.min(320, root.height - root.SafeArea.margins.top - root.bottomInset)
        x: (root.width - width) / 2
        y: Math.max(root.SafeArea.margins.top, (root.height - root.bottomInset - height) / 2)
        contentItem: ScrollView {
            id: recoveryScroll
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            clip: true
            Label {
            width: recoveryScroll.availableWidth
            text: root.currentTable.nextAction === "fresh"
                ? "The original table and history will be preserved. Only team and settings are copied. Previous provider work may have occurred. Enter a new task and explicitly start it; additional usage may be incurred."
                : root.currentTable.nextAction === "retry"
                  ? "This explicitly sends the pending operation again and may incur additional usage. Completed turns are not replayed."
                  : "This starts another run with the existing task and history, not just the failed turn. It may incur additional provider usage."
            wrapMode: Text.Wrap; color: root.textColor
            }
        }
        footer: DialogButtonBox {
            Button { text: "Cancel"; implicitHeight: 48; onClicked: recoveryDialog.close() }
            Button {
                objectName: "confirmRecovery"
                text: root.currentTable.actionLabel || "Confirm"; implicitHeight: 48
                onClicked: {
                    var ok = root.currentTable.nextAction === "fresh" ? root.appController.duplicateCurrentTable() : root.appController.runOrResume();
                    if (ok) recoveryDialog.close(); else root.showErrorIfNeeded();
                }
            }
        }
    }

    Dialog {
        id: quickGuide
        objectName: "quickGuide"
        parent: Overlay.overlay
        title: "Welcome to Synsemble"
        modal: true; focus: true; Material.elevation: 0
        width: Math.min(root.width - root.SafeArea.margins.left - root.SafeArea.margins.right, 560)
        height: Math.min(root.height - root.SafeArea.margins.top - root.bottomInset, 540)
        x: root.SafeArea.margins.left + (root.width - root.SafeArea.margins.left - root.SafeArea.margins.right - width) / 2
        y: root.SafeArea.margins.top + (root.height - root.SafeArea.margins.top - root.bottomInset - height) / 2
        onClosed: { if (!root.appearanceSettings.quickGuideSeen && !root.appController.acknowledgeQuickGuide()) root.showErrorIfNeeded(); }
        contentItem: ScrollView {
            id: guideScroll
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            clip: true
            ColumnLayout {
                width: guideScroll.availableWidth; spacing: 16
                Label { text: "An AI team for your tasks, not a meeting recorder."; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Label { text: "1. Save your provider keys in Settings."; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Label { text: "2. Create a table and add agents. Tables organize tasks; each agent brings a role."; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Label { objectName: "guideArtifacts"; text: "3. Describe a task and choose Start task. When the final result is ready, tap Team, then choose Artifacts to read it."; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }
        footer: DialogButtonBox {
            Button { text: "Set up providers"; implicitHeight: 48; onClicked: { quickGuide.close(); root.selectedSettingsPage = 0; root.openSettings(); } }
            Button { text: "Got it"; implicitHeight: 48; onClicked: quickGuide.close() }
        }
    }

    MessageDialog {
        id: removeAgentDialog
        title: "Remove agent"
        text: "Remove this agent from the team? Previous contributions remain in the session history."
        buttons: MessageDialog.Yes | MessageDialog.No
        onButtonClicked: function(button) {
            if (button !== MessageDialog.Yes || root.editorTableId !== root.appController.currentTableId) return;
            if (root.appController.removeAgent(root.editingSeatIndex)) seatDialog.close();
            else root.showErrorIfNeeded();
        }
    }

    MessageDialog {
        id: deleteDialog
        title: "Delete table"
        text: "Delete this meeting table and its stored session data?"
        buttons: MessageDialog.Yes | MessageDialog.No
        onButtonClicked: function (button) { if (button === MessageDialog.Yes && !root.appController.deleteCurrentTable()) root.showErrorIfNeeded(); }
    }

    MessageDialog { id: errorDialog; objectName: "validationDialog"; title: "Action could not be completed"; buttons: MessageDialog.Ok }
    MessageDialog { id: continuationDialog; title: "Continuation required"; buttons: MessageDialog.Ok }

    FileDialog {
        id: attachmentDialog
        title: "Select attachment"
        fileMode: FileDialog.OpenFile
        onAccepted: { if (!root.appController.addAttachmentForTable(root.pickerTableId, selectedFile)) root.showErrorIfNeeded(); }
    }
}
