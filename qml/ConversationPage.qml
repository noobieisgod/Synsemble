pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: page
    required property var shell
    property alias transcriptList: transcriptList
    property alias composer: composer
    color: shell.backgroundColor
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        Label {
            Layout.fillWidth: true
            visible: Boolean(page.shell.currentTable.continuationReason) && page.shell.currentTable.canResume !== false
            text: page.shell.currentTable.continuationReason || ""
            wrapMode: Text.Wrap
            color: page.shell.mutedColor
            Accessible.role: Accessible.AlertMessage
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ListView {
                id: transcriptList
                objectName: "transcript"
                anchors.fill: parent
                clip: true
                spacing: 24
                model: page.shell.transcriptRows
                reuseItems: true
                ScrollBar.vertical: ScrollBar { id: transcriptBar; objectName: "transcriptBar" }
                delegate: ColumnLayout {
                    id: message
                    required property var modelData
                    objectName: "transcriptMessage"
                    width: Math.max(0, transcriptList.width - transcriptBar.width - 8)
                    x: transcriptList.effectiveLayoutDirection === Qt.RightToLeft ? transcriptBar.width + 8 : 0
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4; color: message.modelData.color || page.shell.accentColor }
                        Label { text: message.modelData.speaker; color: page.shell.textColor; font.bold: true; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        Label { text: message.modelData.timestamp; color: page.shell.mutedColor; font.pixelSize: 11 }
                    }
                    Label { visible: !message.modelData.isUser; text: message.modelData.role + " · " + message.modelData.model; color: page.shell.mutedColor; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    TextEdit {
                        Layout.fillWidth: true
                        Layout.preferredHeight: contentHeight
                        text: message.modelData.content
                        textFormat: TextEdit.PlainText
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        color: page.shell.textColor
                        font.pixelSize: 16
                        Accessible.name: "Message from " + message.modelData.speaker
                    }
                }
            }
            ColumnLayout {
                id: welcome
                anchors.centerIn: parent
                width: Math.min(parent.width, 420)
                visible: transcriptList.count === 0
                spacing: parent.height < 350 ? 8 : 16
                Image { visible: welcome.parent.height > 350; source: page.shell.brandMark; Layout.preferredWidth: 64; Layout.preferredHeight: 64; Layout.alignment: Qt.AlignHCenter; fillMode: Image.PreserveAspectFit }
                Label { text: "Synsemble"; font.pixelSize: welcome.parent.height < 220 ? 20 : 30; font.bold: true; color: page.shell.textColor; Layout.alignment: Qt.AlignHCenter }
                Label { visible: welcome.parent.height > 220; text: "Bring a few minds together."; font.pixelSize: 18; color: page.shell.mutedColor; Layout.alignment: Qt.AlignHCenter }
                Label {
                    visible: welcome.parent.height > 300
                    Layout.fillWidth: true
                    text: page.shell.currentTable.tableId ? "Each agent brings a role and a perspective. Build your team, then give them something to work on." : "Create a table for your next idea, question, or project."
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: page.shell.mutedColor
                }
                Button {
                    objectName: "emptyAction"
                    text: page.shell.currentTable.tableId ? "Add agent" : "Create table"
                    visible: !page.shell.currentTable.tableId || page.shell.seatRows.filter(function(s) { return s.occupied; }).length === 0
                    Layout.alignment: Qt.AlignHCenter
                    implicitHeight: 48
                    Material.background: page.shell.accentColor
                    Material.foreground: page.shell.accentInkColor
                    background: Rectangle { color: page.shell.accentColor; radius: 24 }
                    onClicked: page.shell.currentTable.tableId ? page.shell.openAddSeat() : page.shell.createTable()
                }
            }
            Button {
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Latest messages ↓"
                visible: transcriptList.count > 0 && !transcriptList.atYEnd
                implicitHeight: 48
                onClicked: transcriptList.positionViewAtEnd()
            }
        }
        Label { visible: page.shell.appController.attachmentImportInProgress; text: page.shell.appController.attachmentImportStatus; color: page.shell.mutedColor; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Label { objectName: "actionHint"; text: page.shell.currentTable.actionHint || ""; visible: Boolean(page.shell.currentTable.tableId); color: page.shell.mutedColor; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Button { objectName: "providerRecovery"; text: "Providers and models"; visible: Boolean(page.shell.currentTable.latestFailure) && page.shell.currentTable.canResume !== false; flat: true; implicitHeight: 48; onClicked: { page.shell.selectedSettingsPage = 0; page.shell.openSettings(); } }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: composerLayout.implicitHeight + 20
            color: page.shell.raisedColor
            radius: 24
            ColumnLayout {
                id: composerLayout
                anchors.fill: parent
                anchors.margins: 10
                spacing: 2
                ScrollView {
                    contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(132, Math.max(48, composer.implicitHeight))
                    clip: true
                    TextArea {
                        id: composer
                        objectName: "composer"
                        placeholderText: text.length === 0 ? "What would you like to work on?" : ""
                        wrapMode: TextArea.Wrap
                        selectByMouse: true
                        readOnly: page.shell.currentTable.canSubmit === false
                        color: page.shell.textColor
                        background: Item {}
                        Accessible.name: "Task or message"
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        Accessible.name: page.shell.appController.attachmentImportInProgress ? "Cancel attachment import" : "Add attachment"
                        implicitWidth: page.shell.appController.attachmentImportInProgress ? 120 : 48
                        flat: true; implicitHeight: 48
                        enabled: Boolean(page.shell.currentTable.tableId) && page.shell.currentTable.canSubmit !== false
                        onClicked: page.shell.appController.attachmentImportInProgress ? page.shell.appController.cancelAttachmentImport() : page.shell.pickAttachment()
                        contentItem: Item {
                            Label { anchors.centerIn: parent; text: "Cancel import"; visible: page.shell.appController.attachmentImportInProgress; color: page.shell.textColor }
                            Rectangle { anchors.centerIn: parent; width: 18; height: 2; color: page.shell.textColor; visible: !page.shell.appController.attachmentImportInProgress }
                            Rectangle { anchors.centerIn: parent; width: 2; height: 18; color: page.shell.textColor; visible: !page.shell.appController.attachmentImportInProgress }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Button { objectName: "sessionAction"; text: page.shell.sessionButtonLabel(); visible: Boolean(page.shell.currentTable.tableId) && page.shell.currentTable.nextAction !== "start"; implicitHeight: 48; flat: true; onClicked: page.shell.activateSession() }
                    Button { objectName: "sendTask"; visible: page.shell.currentTable.canSubmit !== false; text: page.shell.currentTable.phase === "Idle" ? "Start task" : "Send"; implicitHeight: 48; enabled: Boolean(page.shell.currentTable.tableId) && page.shell.currentTable.canSubmit !== false && composer.text.trim().length > 0; onClicked: page.shell.submitComposer() }
                }
            }
        }
    }
}
