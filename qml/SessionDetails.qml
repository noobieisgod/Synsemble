pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ScrollView {
    id: details
    required property var shell
    contentWidth: availableWidth
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    clip: true
    component PanelSurface: Rectangle { color: details.shell.surfaceColor; radius: 16 }
    GridLayout {
    id: sidePanels
    width: details.availableWidth
    columns: 1
    columnSpacing: 12
    rowSpacing: 12

    PanelSurface {
        visible: details.shell.detailsIndex === 0
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        implicitHeight: seatsPanelContent.implicitHeight + 28
        ColumnLayout {
            id: seatsPanelContent
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Team"; color: details.shell.textColor; font.bold: true; font.pixelSize: 16; Layout.fillWidth: true }
                Label { text: details.shell.seatRows.filter(function (seat) { return seat.occupied; }).length + " agents"; color: details.shell.mutedColor; font.pixelSize: 11 }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: details.shell.seatRows
                    delegate: ItemDelegate {
                        id: agentRow
                        required property int index
                        required property var modelData
                        visible: modelData.occupied
                        Layout.fillWidth: true
                        implicitHeight: Math.max(64, agentContent.implicitHeight + 20)
                        Accessible.name: "Configure " + modelData.displayName
                        onClicked: details.shell.openSeatEditor(index, false)
                        contentItem: RowLayout {
                            id: agentContent
                            spacing: 12
                            Rectangle { Layout.preferredWidth: 10; Layout.preferredHeight: 10; radius: 5; color: agentRow.modelData.color }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Label { text: agentRow.modelData.displayName; color: details.shell.textColor; font.bold: true; Layout.fillWidth: true; wrapMode: Text.Wrap }
                                Label { text: agentRow.modelData.role; color: details.shell.mutedColor; Layout.fillWidth: true; wrapMode: Text.Wrap }
                                Label { text: agentRow.modelData.provider + " · " + agentRow.modelData.model; color: details.shell.mutedColor; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
                            }
                            Label { text: agentRow.modelData.active ? "Active" : "Edit"; color: details.shell.mutedColor }
                        }
                    }
                }
                Button {
                    text: "Add agent"
                    Layout.fillWidth: true
                    implicitHeight: 48
                    Accessible.name: "Add agent"
                    onClicked: details.shell.openAddSeat()
                }
            }
        }
    }

    PanelSurface {
        visible: details.shell.detailsIndex === 0
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        implicitHeight: attachmentPanelContent.implicitHeight + 28
        ColumnLayout {
            id: attachmentPanelContent
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Attachments"; color: details.shell.textColor; font.bold: true; font.pixelSize: 16; Layout.fillWidth: true }
            }
            Label { visible: details.shell.attachmentRows.length === 0 && !details.shell.appController.attachmentImportInProgress; text: "Use the attachment button beside the message composer."; color: details.shell.mutedColor; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Repeater {
                model: details.shell.attachmentRows
                delegate: RowLayout {
                    id: attachmentRow
                    required property var modelData
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { text: attachmentRow.modelData.displayName; color: details.shell.textColor; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        Label { text: attachmentRow.modelData.size + " | " + attachmentRow.modelData.mimeType; color: details.shell.mutedColor; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                    Button { text: "Open"; flat: true; enabled: attachmentRow.modelData.available; Accessible.name: "Open attachment " + attachmentRow.modelData.displayName; onClicked: { if (!details.shell.appController.openAttachment(attachmentRow.modelData.attachmentId)) details.shell.showErrorIfNeeded(); } }
                    Button { text: "Remove"; enabled: details.shell.currentTable.canSubmit !== false; flat: true; Accessible.name: "Remove attachment " + attachmentRow.modelData.displayName; onClicked: { if (!details.shell.appController.removeAttachment(attachmentRow.modelData.attachmentId)) details.shell.showErrorIfNeeded(); } }
                }
            }
            ProgressBar { visible: details.shell.appController.attachmentImportInProgress; indeterminate: true; Layout.fillWidth: true }
            Label { visible: details.shell.appController.attachmentImportInProgress; text: details.shell.appController.attachmentImportStatus; color: details.shell.mutedColor; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }

    PanelSurface {
        visible: details.shell.detailsIndex === 3
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        implicitHeight: generationPanelContent.implicitHeight + 28
        ColumnLayout {
            id: generationPanelContent
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 9
            Label { text: "Generation"; color: details.shell.textColor; font.bold: true; font.pixelSize: 16 }
            Label { text: details.shell.appController.running ? "Session is running" : details.shell.currentTable.phase || "Idle"; color: details.shell.textColor; font.bold: true }
            Label { text: details.shell.appController.running ? "Provider calls follow the configured seat order and hard stops." : "Generation begins when the table starts."; color: details.shell.mutedColor; wrapMode: Text.Wrap; Layout.fillWidth: true }
            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: Math.max(1, Number(details.shell.currentTable.maxTokens || 1))
                value: Number(details.shell.currentTable.usedTokens || 0)
                indeterminate: details.shell.appController.running && Number(details.shell.currentTable.maxTokens || 0) === 0
            }
            Label { text: (details.shell.currentTable.usageEstimated ? "Approximately " : "") + (details.shell.currentTable.usedTokens || 0) + " tokens used"; color: details.shell.mutedColor; font.pixelSize: 11 }
        }
    }

    PanelSurface {
        visible: details.shell.detailsIndex === 1
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        implicitHeight: artifactsPanelContent.implicitHeight + 28
        ColumnLayout {
            id: artifactsPanelContent
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Artifacts"; color: details.shell.textColor; font.bold: true; font.pixelSize: 16; Layout.fillWidth: true }
                Label { text: String(details.shell.artifactRows.length); color: details.shell.mutedColor }
            }
            Label { visible: details.shell.artifactRows.length === 0; text: "No generated artifacts yet."; color: details.shell.mutedColor }
            Repeater {
                model: details.shell.artifactRows
                delegate: Button {
                    id: artifactRow
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 62
                    Accessible.name: "Open " + details.shell.artifactType(modelData.phase) + " artifact"
                    onClicked: {
                        details.shell.artifactPreviewTitle = details.shell.artifactType(modelData.phase) + " | " + modelData.createdAt;
                        details.shell.artifactPreviewBody = details.shell.appController.artifactContent(modelData.versionId) || "Artifact content is unavailable.";
                        details.shell.openArtifact();
                    }
                    background: Rectangle { color: details.shell.raisedColor; border.color: details.shell.lineColor; border.width: 1; radius: 5 }
                    contentItem: Column {
                        spacing: 3
                        Label { width: parent.width; text: details.shell.artifactType(artifactRow.modelData.phase); color: details.shell.textColor; font.bold: true; elide: Text.ElideRight }
                        Label { width: parent.width; text: artifactRow.modelData.summary + " | " + artifactRow.modelData.createdAt; color: details.shell.mutedColor; font.pixelSize: 11; elide: Text.ElideRight }
                    }
                }
            }
            Button {
                text: details.shell.sessionButtonLabel()
                flat: true
                Layout.fillWidth: true
                visible: details.shell.currentTable.nextAction !== "start"
                onClicked: details.shell.activateSession()
            }
        }
    }

    ColumnLayout {
        visible: details.shell.detailsIndex === 3
        Layout.fillWidth: true
        spacing: 14
        Repeater {
            model: [
                ["Elapsed", details.shell.currentTable.elapsed || "00:00"],
                ["Round", details.shell.currentTable.round || 0],
                ["Input tokens", details.shell.currentTable.tokenBreakdownKnown === false ? "Unavailable" : details.shell.currentTable.inputTokens || 0],
                ["Output tokens", details.shell.currentTable.tokenBreakdownKnown === false ? "Unavailable" : details.shell.currentTable.outputTokens || 0],
                ["Total tokens", (details.shell.currentTable.usageEstimated ? "Approx. " : "") + (details.shell.currentTable.usedTokens || 0)]
            ]
            delegate: Label { required property var modelData; text: modelData[0] + ": " + modelData[1]; color: details.shell.textColor; Layout.fillWidth: true }
        }
    }
}

}
