# Synsemble mobile interface

The conversation is the main workspace. The app bar contains Synsemble and the drawer button. A separate table-level header shows the table title, phase, Team and Details. Table controls are hidden in Settings. The drawer contains table switching/creation, one Table actions menu (rename, pin, duplicate, delete), and side-by-side Quick guide/Settings buttons. There is no bottom navigation bar.

At 1000 logical pixels the table drawer stays visible; at 1280 the details panel reserves space beside the conversation. Narrow layouts use an overlay sheet. Major overlays use window coordinates and apply safe-area spacing once, including the table drawer, Team/Details sheet and artifact preview. The bounded Welcome guide is centered within the usable safe area, including keyboard inset. Back dismisses an open dropdown before its owning dialog/page, then other dialogs/menus, then the details sheet, then the transient drawer, then Settings.

## Reachable features

| Feature | Location |
| --- | --- |
| Table create/switch/pin/rename/duplicate/delete | Table drawer |
| Add/edit/remove individual agents | Empty-state Add agent; Team editor with confirmed Remove agent |
| Task/message entry; import/cancel attachment | Composer |
| Attachment open/remove | Team details |
| State-specific action | Composer area and Usage controls; see the mapping below |
| Explicit Stop; full transcript copy | Session-details footer |
| Artifact records and full preview | Artifacts details |
| Sanitized events and diagnostics | Activity details |
| Rounds, elapsed time, input/output/total tokens | Usage details |
| Keys, models, limits, safeguards, appearance | Settings: Providers and models, Workflow limits, Appearance |
| Quick guide | Automatic once after initialization; reopen from the drawer footer |

## Behavioral rules

New tables contain zero agents. Each seat represents one agent with name, provider, model, role, and color. Existing advanced options and decision-making-role validation remain. Invalid edits retain their input and show an error.

The occupied-seat switch is no longer shown. Remove agent reuses the existing inactive slot, preserves prior contributions and stable IDs, and respects decision-maker validation and deferred changes during running sessions. It does not delete transcript history.

Submitting a task starts a valid idle session. Configuration failures preserve the composer. Drafts and transcript scroll positions are kept per table during the current process; drafts are not newly persisted across restart. A stable trailing gutter keeps transcript text clear of its scrollbar. New messages follow only while already near the bottom. Latest messages returns to the end.

The idle composer says Start task with short guidance; active sessions use Send with a separate state-specific Pause/Resume/Continue action. The attachment affordance is a native plus symbol with the accessible name Add attachment. Cancel import remains explicit. Vertical ScrollViews constrain content width; Settings drag regression checks cover all three categories.

Manual Pause resumes with Resume. A configured hard stop offers Continue for exactly one pending operation; completed calls are not replayed and later limits pause again. Outcome-unknown operations expose no replay action, including after restore and Stop. The conservative unknown-outcome handler also discards other remaining callbacks from that run. Locked sessions reject message and attachment mutations, preserve typed drafts, and offer a confirmed fresh table copy with team/settings only. Original history remains intact; nothing is submitted automatically.

Picker completion retains the originating table ID. Existing attachment ownership, size, count, and traversal checks remain. Artifact preview uses existing artifact records without a new persistence schema.

Completed sessions with saved artifacts show: "Your final result is ready. Tap Team, then choose Artifacts to read it." The quick guide also explains this route. Sessions without artifacts do not claim a saved final result; existing recovery guidance and Run again remain unchanged.

Switching tables closes the old table's editor, artifact preview and continuation notice. Startup refuses to initialize after an incomplete database restore rather than treating unreadable records as an empty app or cleaning up their files.

## State/action contract

Presentation fields `nextAction`, `actionLabel`, `actionHint`, `latestFailure`, `canSubmit` and `statusLabel` are derived in C++; no new persisted schema is introduced. The original phase field remains unchanged.

| State | Visible next action | Meaning |
| --- | --- | --- |
| No table | Create table | Explicit empty-table creation |
| Idle | Start task | Validate configuration, then submit and start |
| Research/Planning/Execution/Quality Control/Present | Pause | Existing workflow proceeds; in-flight calls are not replayed |
| Manual pause with a saved operation | Resume | Resume only the pending operation |
| Configured hard stop | Continue | One-operation override; later limits can pause again |
| Rejected unusable response with saved retry command | Retry operation | Confirmation warns of additional usage; no automatic replay |
| Definite rejection while other research/turns continue | Pause + Providers and models | Explain continuation without the failed response; Activity has details |
| Definite failure that ends the run | Run again + Providers and models | Explicit confirmed new run after configuration correction, not an automatic retry of only the failed turn |
| Completed/Stopped/Failed | Run again | Explicit confirmed new run with existing task/history |
| Unknown outcome, including restored or stopped unknown work | Create fresh table | No replay; confirmed team/settings copy with a new stable table ID |
| Paused legacy/incomplete state without a saved operation | Create fresh table | Do not invent a resumable operation |

Sending while paused adds instructions without resuming. Outcome-unknown sessions cannot submit. Fresh-copy and new-run confirmations close when switching tables; save failure keeps the original selection.

## Privacy and appearance

Provider adapters select visible response fields. Presentation filtering additionally removes recognized private envelopes, hidden blocks, auth headers, key patterns, and exact current credentials. Stored context is not rewritten. This is defense in depth, not a guarantee that arbitrary free-form sensitive text can be recognized.

Credentials show Saved/Not saved only. Replacement fields begin empty and clear after successful saving. Workflow decisions remain in C++; monetary telemetry remains absent.

Each provider has an independent Refresh models action and busy/status display on the same page as its key. Saving a key or opening Settings does not automatically refresh. Explicit refresh sends that provider's saved credential to its model-list endpoint. TLS error 6 means a secure connection failure, not proof of an invalid key. Safe categories distinguish issuer, chain, self-signed, validity and hostname failures. Generation transport separately classifies definite rejection/pre-execution failure and uncertain execution. No automatic generation retry remains. Failed refresh retains the previous or built-in model list; it does not switch providers or prove a session request will succeed.

Warm neutral light surfaces, charcoal dark surfaces, muted teal, and native Qt controls replace the dashboard. Calm Workspace retains its warm accent. Light, Dark, System, and existing font preferences are preserved. System typography uses the platform application default unless overridden by the saved font preference.

In-app light/dark marks share the existing geometry and use transparent backgrounds at equal scale; launcher backgrounds are preserved.

Workspace uses bundled Inter and Console uses bundled JetBrains Mono, including bold weights. System remains the platform font. Upstream licenses and font provenance are included in `branding/fonts`. There is no Live preview card or runtime font download. The quick guide uses the existing QSettings namespace (`ui/quickGuideSeen`), never creates tables/agents, and can be dismissed with Back or reopened in Settings.

The monochrome three-stroke symbol has light/dark SVG and PNG variants. Android adaptive and legacy launcher assets are reproduced with `branding/export-icons.ps1`.

## Validation boundaries

See [Mobile review](MOBILE_REVIEW.md) for evidence and remaining work. Offline QML fixtures exercise drafts, pause labels, sheet/drawer Back order, and representative screens. Set `SYNSEMBLE_CAPTURE_DIR` to an existing directory to export screenshots through `test_qml`.

Windows QML tests use native windows with software rendering; other hosts retain offscreen configuration. Real Back-key fixtures cover dialog/sheet/drawer precedence. An isolated Android emulator also verified those dismissals, validation-dialog Back, keyboard/composer separation and retained invalid-task text. The manifest retains Qt's key-event Back dispatch; modal dialogs use zero decorative elevation for software-renderer compatibility.

Host captures and the translated emulator do not establish physical-device graphics, safe-area coverage, TalkBack, large-text or process-death correctness. The review records those remaining checks and emulator rendering defects. An existing debug key was authorized solely for the disposable emulator APK. Release signing, publication, pushes and version changes remain outside this work.
