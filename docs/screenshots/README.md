# Mobile host captures

Generated from offline QML fixtures using Qt 6.11.1, Material controls and software rendering. These show the actual production components, not mockups. Window scale 0.65 lets portrait-tablet and wide fixtures fit the host display. Logical test sizes are recorded in `tests/qml/tst_mobile.qml.in`. Refreshed at closeout on 2026-09-21.

- Conversation: `conversation.png`, `phone-light.png`, `phone-dark.png`
- Responsive layouts: `landscape.png`, `tablet.png`, `wide.png`
- Initial and session states: `welcome.png`, `running.png`, `manual-pause.png`, `hard-stop.png`, `unknown-outcome.png`, `completed.png`, `failed.png`
- Supporting UI: `team.png`, `agent-editor.png`, `artifacts.png`, `activity.png`, `usage.png`, `settings.png`
- Usability follow-up: `drawer.png`, `table-actions.png`, `quick-guide.png`, `appearance.png`

Two earlier baseline captures, `baseline-transcript.png` and `baseline-activity.png`, are retained for comparison. They are not current branding or part of the 23 refreshed screens. The latest fixture run passed 67 cases, including the state/action mapping, locked-session fresh-copy recovery, provider-specific refresh, transcript gutter, transparency, guide centering and dropdown Back priority.

Some supporting screens deliberately show empty states. Host captures do not prove Android keyboard, safe-area, accessibility, hardware Back or lifecycle behavior. Device acceptance is tracked in `../MOBILE_REVIEW.md`.
