pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ScrollView {
    id: settingsPage
    required property var shell
    function closeOpenMenu() {
        for (var combo of [categoryCombo, appearanceCombo, themeCombo, fontCombo]) {
            if (combo.popup.visible) { combo.popup.close(); return true; }
        }
        return false;
    }
    function loadSettingsFields() {
        if (!settingsPage.shell.appearanceSettings || settingsPage.shell.appearanceSettings.appearance === undefined) return;
        appearanceCombo.currentIndex = Math.max(0, appearanceCombo.model.indexOf(settingsPage.shell.appearanceSettings.appearance));
        themeCombo.currentIndex = Math.max(0, themeCombo.model.indexOf(settingsPage.shell.appearanceSettings.colorTheme));
        fontCombo.currentIndex = Math.max(0, fontCombo.model.indexOf(settingsPage.shell.appearanceSettings.fontStyle));
        maxPhaseTokens.text = String(settingsPage.shell.appearanceSettings.maxTokensPerPhase);
        maxTotalTokens.text = String(settingsPage.shell.appearanceSettings.maxTotalTokens);
        maxRounds.text = String(settingsPage.shell.appearanceSettings.maxRounds);
        maxLoops.text = String(settingsPage.shell.appearanceSettings.maxExecQcLoops);
        maxPhaseSeconds.text = String(settingsPage.shell.appearanceSettings.maxPhaseSeconds);
        maxSessionSeconds.text = String(settingsPage.shell.appearanceSettings.maxSessionSeconds);
    }

    function saveAppearanceFromControls() {
        if (!settingsPage.shell.appController.saveAppearance(appearanceCombo.currentText, themeCombo.currentText, fontCombo.currentText)) {
            settingsPage.shell.showErrorIfNeeded();
        } else {
            settingsPage.shell.refreshSettings();
        }
    }

    function saveLimits() {
        limitError.text = "";
        var phaseTokens = Number(maxPhaseTokens.text);
        var totalTokens = Number(maxTotalTokens.text);
        var rounds = Number(maxRounds.text);
        var loops = Number(maxLoops.text);
        var phaseSeconds = Number(maxPhaseSeconds.text);
        var sessionSeconds = Number(maxSessionSeconds.text);
        if (![phaseTokens, totalTokens, rounds, loops, phaseSeconds, sessionSeconds].every(function (value) { return value > 0; })) {
            limitError.text = "Every hard stop must be a positive value.";
            return;
        }
        if (totalTokens < phaseTokens) {
            limitError.text = "Total tokens must be at least the per phase limit.";
            return;
        }
        if (sessionSeconds < phaseSeconds) {
            limitError.text = "Session seconds must be at least the phase limit.";
            return;
        }
        if (!settingsPage.shell.appController.saveGlobalBudget(phaseTokens, totalTokens, rounds, loops, phaseSeconds, sessionSeconds)) {
            limitError.text = settingsPage.shell.appController.lastError();
            return;
        }
        settingsPage.shell.showToast("Hard stops saved");
        settingsPage.shell.refreshSettings();
    }


    component PanelSurface: Rectangle { color: settingsPage.shell.surfaceColor; radius: 16 }

    objectName: "settingsScroll"
    contentWidth: availableWidth
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    clip: true
    ColumnLayout {
        width: Math.max(0, settingsPage.availableWidth - 32)
        x: 16
        y: 18
        spacing: 14
        Label { text: "Settings"; color: settingsPage.shell.textColor; font.pixelSize: 30; font.bold: true }
        Label { text: "Provider credentials, model catalogs, hard stops, attachment safeguards, and appearance."; color: settingsPage.shell.mutedColor; wrapMode: Text.Wrap; Layout.fillWidth: true }
        ComboBox {
            id: categoryCombo
            objectName: "settingsCategory"
            visible: !settingsPage.shell.desktopLayout
            Layout.fillWidth: true
            model: ["Providers and models", "Workflow limits", "Appearance"]
            currentIndex: settingsPage.shell.selectedSettingsPage
            onActivated: settingsPage.shell.selectedSettingsPage = currentIndex
            Accessible.name: "Settings category"
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: 14
            ColumnLayout {
                visible: settingsPage.shell.desktopLayout
                Layout.preferredWidth: 190
                Layout.alignment: Qt.AlignTop
                spacing: 4
        Repeater {
                    model: ["Providers and models", "Workflow limits", "Appearance"]
                    delegate: Button {
                        required property int index
                        required property string modelData
                        text: modelData
                        flat: true
                        checkable: true
                        checked: settingsPage.shell.selectedSettingsPage === index
                        Layout.fillWidth: true
                        onClicked: settingsPage.shell.selectedSettingsPage = index
                    }
                }
            }
            StackLayout {
                id: settingsContent
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                currentIndex: settingsPage.shell.selectedSettingsPage

                ColumnLayout {
                    spacing: 12
                    Label { text: "Providers and models"; color: settingsPage.shell.textColor; font.pixelSize: 20; font.bold: true }
                    Label { text: "Keys are stored by the device credential store. They are sent to that provider when you refresh its models or run a session using it."; color: settingsPage.shell.mutedColor; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Flow {
                        id: providerFlow
                        Layout.fillWidth: true
                        spacing: 10
                        Repeater {
                            objectName: "providerRepeater"
                            model: ["OpenAI", "Gemini", "Anthropic"]
                            delegate: PanelSurface {
                                id: providerCard
                                required property int index
                                required property string modelData
                                property bool reveal: false
                                property var catalogStatus: settingsPage.shell.modelRefreshRows[index] || ({})
                                width: providerFlow.width >= 780 ? (providerFlow.width - 20) / 3 : providerFlow.width
                                height: providerContents.implicitHeight + 24
                                ColumnLayout {
                                    id: providerContents
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 8
                                    Label { text: providerCard.modelData; color: settingsPage.shell.textColor; font.bold: true; font.pixelSize: 16 }
                                    Label { text: providerCard.catalogStatus.message || "Models have not been refreshed."; color: settingsPage.shell.mutedColor; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; Layout.minimumWidth: 0 }
                                    Button { objectName: "refreshProvider" + providerCard.index; text: providerCard.catalogStatus.refreshing ? "Refreshing..." : "Refresh models"; Layout.fillWidth: true; implicitHeight: 48; enabled: !providerCard.catalogStatus.refreshing; onClicked: settingsPage.shell.appController.refreshProviderModels(providerCard.index) }
                                    Label { text: "API key"; color: settingsPage.shell.textColor }
                                    TextField {
                                        id: keyField
                                        Layout.fillWidth: true
                                        echoMode: providerCard.reveal ? TextInput.Normal : TextInput.Password
                                        placeholderText: "Enter a new API key"
                                        Accessible.name: providerCard.modelData + " API key"
                                    }
                                    Label {
                                        text: {
                                            settingsPage.shell.settingsGeneration;
                                            return settingsPage.shell.appController.hasCredential(providerCard.index) ? "Saved" : "Not saved";
                                        }
                                        color: settingsPage.shell.mutedColor
                                        font.pixelSize: 11
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Button { text: providerCard.reveal ? "Hide" : "Show"; onClicked: providerCard.reveal = !providerCard.reveal }
                                        Item { Layout.fillWidth: true }
                                        Button {
                                            text: "Clear"
                                            onClicked: {
                                                if (settingsPage.shell.appController.saveApiKey(providerCard.index, "")) {
                                                    keyField.clear();
                                                    providerCard.reveal = false;
                                                    settingsPage.shell.refreshSettings();
                                                    settingsPage.shell.showToast(providerCard.modelData + " key cleared");
                                                } else settingsPage.shell.showErrorIfNeeded();
                                            }
                                        }
                                        Button {
                                            text: "Save"
                                            font.bold: true
                                            enabled: keyField.text.length > 0
                                            onClicked: {
                                                if (settingsPage.shell.appController.saveApiKey(providerCard.index, keyField.text)) {
                                                    keyField.clear();
                                                    providerCard.reveal = false;
                                                    settingsPage.shell.refreshSettings();
                                                    settingsPage.shell.showToast(providerCard.modelData + " key saved");
                                                } else settingsPage.shell.showErrorIfNeeded();
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    spacing: 10
                    Label { text: "Global hard stops"; color: settingsPage.shell.textColor; font.pixelSize: 20; font.bold: true }
                    Label { text: "These mandatory limits stop provider work. Total tokens cannot be lower than per phase tokens, and session seconds cannot be lower than phase seconds."; color: settingsPage.shell.mutedColor; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: settingsContent.width >= 620 ? 2 : 1
                        columnSpacing: 10
                        rowSpacing: 8
                        Label { text: "Max tokens per phase"; color: settingsPage.shell.textColor }
                        TextField { id: maxPhaseTokens; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly; validator: IntValidator { bottom: 1 } }
                        Label { text: "Max total tokens"; color: settingsPage.shell.textColor }
                        TextField { id: maxTotalTokens; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly; validator: IntValidator { bottom: 1 } }
                        Label { text: "Max rounds"; color: settingsPage.shell.textColor }
                        TextField { id: maxRounds; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly; validator: IntValidator { bottom: 1 } }
                        Label { text: "Max Execution / QC loops"; color: settingsPage.shell.textColor }
                        TextField { id: maxLoops; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly; validator: IntValidator { bottom: 1 } }
                        Label { text: "Max phase seconds"; color: settingsPage.shell.textColor }
                        TextField { id: maxPhaseSeconds; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly; validator: IntValidator { bottom: 1 } }
                        Label { text: "Max session seconds"; color: settingsPage.shell.textColor }
                        TextField { id: maxSessionSeconds; Layout.fillWidth: true; inputMethodHints: Qt.ImhDigitsOnly; validator: IntValidator { bottom: 1 } }
                    }
                    Label { id: limitError; color: settingsPage.shell.dangerColor; wrapMode: Text.Wrap; Layout.fillWidth: true; Accessible.role: Accessible.AlertMessage }
                    Button { text: "Save hard stops"; font.bold: true; onClicked: settingsPage.shell.saveLimits() }
                    Label { text: "Attachment safeguards"; color: settingsPage.shell.textColor; font.pixelSize: 20; font.bold: true }
                    Label { text: "These fixed production safeguards are enforced during import and cannot be weakened here."; color: settingsPage.shell.mutedColor; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Repeater {
                        model: [
                            { label: "Maximum attachment size", value: (settingsPage.shell.safeguards.maximumAttachmentMiB || 0) + " MiB hard stop" },
                            { label: "Free space reserve", value: (settingsPage.shell.safeguards.freeSpaceReserveMiB || 0) + " MiB required" },
                            { label: "No progress timeout", value: (settingsPage.shell.safeguards.noProgressTimeoutSeconds || 0) + " seconds" }
                        ]
                        delegate: PanelSurface {
                            id: safeguardRow
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 62
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 11
                                Label { text: safeguardRow.modelData.label; color: settingsPage.shell.textColor; font.bold: true; Layout.fillWidth: true; wrapMode: Text.Wrap }
                                Label { text: safeguardRow.modelData.value; color: settingsPage.shell.mutedColor; horizontalAlignment: Text.AlignRight; wrapMode: Text.Wrap }
                            }
                        }
                    }
                }

                ColumnLayout {
                    spacing: 10
                    Label { text: "Appearance"; color: settingsPage.shell.textColor; font.pixelSize: 20; font.bold: true }
                    Label { text: "Appearance, color theme, and font style are independent and persist across launches."; color: settingsPage.shell.mutedColor; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Label { text: "Appearance"; color: settingsPage.shell.textColor }
                    ComboBox { id: appearanceCombo; Layout.fillWidth: true; model: ["Light", "Dark", "System"]; onActivated: settingsPage.shell.saveAppearanceFromControls() }
                    Label { text: "Color theme"; color: settingsPage.shell.textColor }
                    ComboBox { id: themeCombo; Layout.fillWidth: true; model: ["Signal Session", "Calm Workspace"]; onActivated: settingsPage.shell.saveAppearanceFromControls() }
                    Label { text: "Font style"; color: settingsPage.shell.textColor }
                    ComboBox { id: fontCombo; Layout.fillWidth: true; model: ["System", "Workspace", "Console"]; onActivated: settingsPage.shell.saveAppearanceFromControls() }

                }
            }
        }
        Item { Layout.preferredHeight: 18 }
    }
}
