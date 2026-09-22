# Mobile overhaul review

Status: scoped implementation closeout, updated 2026-09-21. Build/test validation is complete; the explicit device and review limitations below remain. This is not unrestricted release acceptance or a claim that every file is defect-free.
Branch: `codex/synsemble-mobile-overhaul`.

Publication follow-up (2026-09-21): this report and the completed source are being organized into focused commits for Synsemble main. Earlier statements that work was uncommitted/unpushed describe those historical validation runs. The local workspace's build paths and releases are preserved; loose diagnostics now live in `../Builds/Diagnostics/2026-09-21/`. Only byte-identical generated screenshot copies and an unusable zero-byte diagnostic class were removed. The privacy page is `docs/index.html`; GitHub Pages stages only that page, never application source or build outputs.

Pre-publication validation on 2026-09-22: all 10 CTest suites passed (20.89 seconds), including provider fixtures, workflow, persistence, controller, Java attachments, QML and source hygiene. No new application behavior was changed during this organization/publication pass.
Baseline: `a948ca31170668d3d09075708e0401f8a7234907`.
Package/version remain `com.aimeetingtable.myapp`, 1.1 / 5.
No persistence schema, production signing, version, or publication changes. The existing debug key is authorized for the artifact-guidance test APK; the AAB remains unsigned.

## Artifact guidance and physical-phone report (2026-09-21)

The user reports that the APK works fully on their physical phone, including provider connectivity, and attributes the earlier connection failure to the emulator. This is user-reported validation, not a new agent-observed device test. The emulator TLS root cause remains unresolved; no TLS changes were made for this follow-up.

Completed sessions with saved artifacts now explain Team > Artifacts in the conversation hint. The quick guide explains the same route. Existing failure/recovery actions remain unchanged, and no saved-result claim appears without artifacts. Targeted regression tests cover completed sessions with/without artifacts, preserved failure guidance, non-completed states, and the visible quick-guide instruction.

Validation passed: qmllint; desktop Release build; targeted CTest 3/3 (controller, QML, source hygiene), including 68 QML cases; Android arm64 Release/assembleRelease and offline bundleRelease. Bundletool 1.18.3 verified the bundle and manifest; archive CRC, package/version/label/ABI, native stripping, APK alignment/signature and compiled guidance checks passed. AAB signing was explicitly checked: unsigned. Content scans found no app test libraries/fixtures, signing material, private key blocks, detected provider API keys or personal build paths. Standard dependency runtime diagnostics and AAB native-symbol metadata remain part of the normal pipeline.

Local deliverables, outside Git: `Android/Releases/v1.1/Synsemble-v1.1-artifact-guidance-test.apk` (existing debug key) and `Android/Releases/v1.1/Synsemble-v1.1-artifact-guidance-unsigned.aab`, with individual SHA-256 files and `Synsemble-v1.1-artifact-guidance-build-info.txt`. Original Gradle outputs and older releases are preserved. The AAB requires the correct upload-key signature before Play upload. These builds include the uncommitted overhaul, not just baseline commit a948ca3. No new device installation, provider call, commit, push, release or upload was performed for this follow-up.

## Provider recovery and UI corrections (2026-09-21)

**Remaining uncertainty:** the exact cause of the user's original OpenAI-only TLS failure and the photographed generation failure cannot be established from old generic diagnostics. Existing outcome-unknown sessions remain locked; they are not retrospectively reclassified. Physical-device graphics/accessibility acceptance remains outstanding.

Confirmed corrections:

- Welcome is vertically centered inside the usable safe area, while full-height drawers/sheets remain top-aligned. Quick guide and Settings share the drawer footer.
- The in-app light-mode PNG background came from the exporter. Both marks now have transparent gaps/backgrounds and equal optical scale. Launcher geometry/backgrounds remain unchanged.
- Dropdown text uses Providers and models / Usage and controls. Transcript delegates reserve a stable scrollbar gutter. A device-found Back edge case was corrected: dropdown dismissal precedes its Settings page or Team/Details owner.
- One production classifier now handles Qt HTTP/network combinations and is reused by synthetic transport. Explicit rejection codes take precedence; DNS/refusal/TLS failures are definite only without a sent request. Ambiguous server failure, lost replies, timeout and post-submission cancellation remain unknown. Automatic redirects and generation replay are disabled. Attachment preparation failures explicitly say no generation request was sent, without claiming an upload could not exist.
- Activity includes existing actor and phase fields plus safe provider/error context. Fixed TLS categories are shared with model refresh. No raw response, certificate, header or credential is exposed.
- The state/action mapping is documented in MOBILE_UI.md and covered by native and QML tests. Definite failures follow existing skip/continue or stop semantics, with visible setup and explicit new-run actions. Unknown sessions offer only confirmed fresh-table recovery and reject new messages/attachments. No data schema was added.

Validation: full desktop Release build and CTest passed 10/10 (17.44 seconds), including Java, controller, workflow/Continue, persistence, provider fixtures, QML and hygiene. Real loopback HTTP fixtures reproduce Qt HTTP rejection errors and verify single dispatch. Android arm64 Release/assembleRelease passed. The final UI-only dropdown correction uses targeted QML/lint checks and incremental Release packaging rather than another full native test run.

### Credential-free TLS evidence

A separate diagnostic APK outside the source repository used the same Qt 6.11.1 arm64 runtime and pinned OpenSSL libraries on the task-owned read-only Android 37.1 emulator. It opened TLS sockets only, with **no HTTP requests, API keys or tasks**. Results at 2026-09-21T08:55:52Z:

- TLS support available; backend openssl; OpenSSL 3.1.8; 120 default CA certificates.
- api.openai.com: QSslError 10, SelfSignedCertificateInChain; handshake rejected.
- api.anthropic.com: the same self-signed-chain rejection.
- generativelanguage.googleapis.com: certificate verification succeeded.

This confirms an untrusted self-signed-chain failure in this test environment. It does not establish whether the user's original environment has the same chain or explain why their Anthropic refresh succeeded. No certificate was trusted or bypassed, and no app trust-store patch is justified by this evidence. Network interception/trust configuration remains an environmental investigation, not a silently completed fix.

Diagnostic source/build/APK remain in ../Builds/tls-probe-* and are excluded from Git and the delivered application. The diagnostic package uses its own application ID and cannot access Synsemble data.

### Device UI evidence

On the 1280x2856 isolated emulator, the guide bounds were y=660..2279, centered between safe bounds y=156..2784. Quick guide and Settings both occupy y=2604..2747 in the drawer. The actual Settings dropdown rendered Providers and models without the mnemonic artifact; setup navigation worked without credentials. After the final correction, Android Back closed the Settings dropdown while retaining Settings. The installed Details dropdown also rendered Usage and controls correctly; Back closed its menu while retaining Session details. A synthetic zero-agent table showed Start task. Host tests cover the same dismissal behavior. Final artifact identities are recorded below.

## Previous approved usability follow-up (2026-09-20)

The amended usability plan is implemented on the same uncommitted branch:

- App navigation and table controls now occupy separate levels. The initial composer uses Start task, with separate Pause/Resume/Continue controls only when applicable. A native plus opens attachments. No-table state hides session actions.
- The drawer's wrapping action buttons became one native Table actions menu. Menu Back and switching-table cleanup are covered by QML tests. Menu elevation is zero for opaque software-renderer behavior.
- Every major overlay is window-aligned. Material Drawer defines its own per-edge safe-area padding, so overriding generic padding alone was insufficient. Both drawers now explicitly clear all four native padding edges and apply system insets once in their contents. Team and Details share this corrected shell; guide/artifact overlays retain their explicit safe-area positioning.
- Settings, details, composer and editor ScrollViews constrain content width. Horizontal drag tests cover all Settings categories.
- Workspace/Console use bundled Inter/JetBrains Mono instead of unavailable Windows font names. System stays native. Font resources, licenses and fixed source provenance are included. Live preview was removed.
- Agent removal reuses the validated inactive-slot path without changing stable IDs or old contributions. The occupancy switch is gone. Decision-maker validation, persistence and slot reuse have regression coverage.
- The one-screen quick guide appears automatically once, persists acknowledgement in the existing settings namespace, supports Back and reopening, and routes to provider setup without creating data or making requests.
- Providers & models is one page. Each provider refreshes independently with its own busy/status flag. Duplicate clicks on a busy provider do not dispatch more requests; one provider does not cancel another. Existing all-provider refresh remains available internally, but is not required by the UI.
- Network error 6 now explains TLS connection failure without blaming the key. Fixed safe categories distinguish certificate failures, HTTP 401/403/429, timeout and DNS/connectivity. No certificate bypass, HTTP downgrade, credential exposure or automatic session retry was introduced.

Validation: desktop and Android arm64 Release builds and QML lint passed; the comprehensive CTest pass was 10/10 (17.14 seconds). Follow-up UI-only corrections used targeted QML tests, ending with 50 passing cases and 23 refreshed screen fixtures. Tests cover independent provider requests, credential-loader isolation, TLS/certificate/authentication/timeout messages, retained catalogs, first-run persistence, agent removal, font loading, panel padding, horizontal drags and menu Back.

Final installed-APK checks on the isolated 1280x2856 emulator confirmed Team and Session details headings at y=222, their Close controls at y=192, and the drawer heading at y=192, below the status-bar boundary at y=156. The previous excess Material inset is removed. Android Back dismissed Details without exiting. The zero-agent New table and acknowledged guide survived APK replacement and restart. Per-provider refresh controls and automatic first-run guide routing were also inspected without supplying credentials or calling providers.

OpenAI connectivity is **not certified fixed**: the original user's TLS failure was not reproduced with their key or network. Packaged OpenSSL/crypto libraries and the Qt OpenSSL plugin are present; the isolated emulator clock was checked. No real provider calls were made in this follow-up. Safe diagnostics now make a user-initiated retry actionable. Physical ARM64 graphics, full accessibility and environmental TLS troubleshooting remain release-validation limits, not silently completed work.

## Confirmed findings and corrections

| Severity | Location and evidence | Impact / smallest correction | Regression evidence |
| --- | --- | --- | --- |
| High | controller `flushCurrentSession` previously saved only selected table | Backgrounding could leave another table's pending results unsaved. Flush all pending IDs; retain failed IDs. | `backgroundFlushSavesOriginatingTables` |
| High | runner unknown-outcome branch previously allowed progression; old async test expected another request | A user could resume after an indeterminate provider result. Persist an unknown marker in existing command payload; guard start/resume/Stop. | `outcomeUnknownDoesNotRecordConfirmedUsage`, `outcomeUnknownCannotResumeAfterRestore` |
| High | controller transcript/copy/log presentation returned unfiltered stored strings | Private fields could reach display/copy. Filter presentation collections and artifact text; retain original stored context. | `presentationRedactsPrivateContent`, provider extraction fixtures |
| Medium | QML file-picker accepted against current selection | Switching tables while picker was open could attach to another table. Capture originating stable ID. | Existing stale/deleted import tests; picker interaction still needs device validation |
| High | runner dispatch and application restore did not durably distinguish in-flight work | Process death could replay uncertain work. Checkpoint before dispatch; stop dispatch on save failure; restore in-flight work as non-replayable. | `checkpointPreventsUnrecordedDispatch`, `interruptedRequestsRestoreWithoutReplay` |
| High | database restore returned empty/partial collections on SQL failure | Startup cleanup could delete owned attachments or accept incomplete state. Return explicit failure and stop initialization before cleanup; block table creation before initialization. | `failedRestoreIsNotAnEmptyDatabase`, `uninitializedStorageCannotCreateTable` |
| High | database failed COMMIT did not roll back | Subsequent saves/deletes could remain trapped in a failed transaction. Roll back both failure paths. | `failedCommitCanBeRetried`, deferred-FK fault injection |
| Medium | queued manual-pause command existed only in runner memory | Restart could lose the pending operation. Retain command in existing persisted continuation field. | `manualPauseRestoresExactPendingCommand` |
| Medium | runner removed pending request before validating callback identity | A mismatched callback could consume a legitimate pending response. Validate identity before removal. | `checkpointPreventsUnrecordedDispatch` |
| Medium | startup cleanup treated every `.part` suffix as disposable | A referenced attachment with that legitimate filename could be removed. Ownership takes precedence. | `startupCleanupPreservesOwnedAttachments` |
| Medium | Android credential save returned before preferences persistence | UI could show Saved before durable storage succeeded. Return synchronous commit result, preserving encryption and namespace. | Android Release compilation; device failure injection pending |
| Medium | table duplication retained usage and pending-run counters | New copy could display old totals or stale runtime state. Reset usage/runtime fields and apply pending seat configuration. | `duplicateResetsRuntimeUsage` |
| Medium | full transcript sanitization operated on the joined display string | Speaker/timestamp prefixes could obscure private JSON envelopes. Sanitize each entry before formatting; recognize embedded private envelopes. | Expanded `presentationRedactsPrivateContent` |
| Medium | artifact writer accepted partial writes | Metadata could reference incomplete output. QSaveFile, exact byte count, commit before publishing. | Existing artifact/persistence tests; disk-failure injection still needed |
| Medium | composer only sent messages | Valid idle task did not start workflow. Validate first, then append and start; preserve failed draft. | `validComposerStartsAndPausesBeforeNetwork`, invalid-task native/QML fixtures |
| Medium | seat dialog accepted/closed even when save failed | Configuration edits could be lost. Explicit save footer closes only on success; retain error. | Static review; full editor interaction coverage outstanding |
| Low | uppercase legacy branding escaped prior literal check | Old branding remained in production UI. New mark and case-insensitive production branding hygiene. | source-hygiene suite |
| Low | Activity eagerly constructed event delegates | Large histories created unnecessary UI objects. Native ListView reuse instead of eager delegates. | `tst_performance.qml.in`, 2,000-event render comparison |
| Low | empty database automatically created a sample table | The planned welcome state could not appear. Leave it empty until explicit Create table. | `newTablesStartEmptyAndPersist`, QML welcome fixture |
| Low | advertised Qt minimum predates SafeArea | Supported configuration was inaccurate. Require Qt 6.9; validate with 6.11.1. | CMake and README; minimum-toolchain run still pending |
| Low | variable-height transcript delegates refine their extent after positioning | Follow-at-bottom could stop short. Re-layout newly instantiated end delegates before final positioning. | Existing strict `test_modelResetFollowsWhenNearBottom` failed before and passes after |
| Medium | artifact and continuation dialogs survived table switching | Old table content could remain over the new table. Close table-scoped previews/notices on selection change and clear preview text. | `test_switchingTableClosesOldArtifact` |
| High | Android Back exited from the agent dialog; window-scoped shortcuts were blocked by modal popups, and the custom manifest omitted Qt's Back compatibility flag | Use an application-scoped shortcut, focus transient controls, and retain Qt's key-event dispatch with `enableOnBackInvokedCallback=false`. No custom Java navigation layer. | Real Back-key QML tests; emulator dialog, sheet, drawer and validation dismissal; manifest hygiene assertion |
| Medium | Android keyboard geometry was in native pixels while the QML window was in logical pixels | Composer was obscured despite adjustResize. Normalize geometry and reserve actual keyboard overlap for content and dialogs. | `test_keyboardKeepsComposerAboveOverlap`; emulator composer/send bounds above keyboard |
| Medium | new test `.qml` files were included by Android's QML import scanner | Release package acquired QtTest/QuickTest dependencies. Keep fixtures as `.qml.in`, materialize only in the temporary host test directory. | Hygiene rejects test `.qml`; fresh Release APK listing excludes QtTest/QuickTest, fixtures and qmltooling |
| Medium | Material dialog elevation produced a transparent body with the host software renderer | Fields overlapped underlying content. Set native dialogs' decorative elevation to zero, retaining their normal opaque background. | Refreshed `agent-editor.png`, 39-case QML run |
| Low | Android font did not render the drawer's Unicode menu glyph | Table switching had an invisible visual affordance. Three native rectangle strokes replace the glyph; accessible name retained. | Host capture and emulator Open tables interaction |

The unknown-outcome correction is deliberately conservative: other outstanding callbacks in that run are discarded, not applied after pausing. The expanded `outcomeUnknownDoesNotRecordConfirmedUsage` fixture now sends two requests and verifies that a late second response changes neither transcript nor usage and cannot authorize replay.

## Review coverage and limitations

Production paths examined include the QML shell/editor/settings, controller exports and lifecycle, runner transitions/request generations/continuation, workflow engine, database serialization, application context saves and attachment cleanup, provider visible-text extraction/usage, artifact/import services, Java content access and credentials, and CMake/test integration.

The [inventory](MOBILE_REVIEW_INVENTORY.txt) enumerates tracked and newly delivered files, including historical prototypes and store images, with area-level coverage. This is not a completed line-by-line review of every file. Generated builds and the separate Windows repository are excluded. Further non-blocking investigation is deferred per the closeout scope.

### Unconfirmed or incomplete investigations

- Native fixtures cover interrupted-request and manual-pause restoration; actual Android process-death behavior remains unverified.
- Android secure-store failure injection, lifecycle, content grants, cancellation, and temporary-file cleanup require additional device tests. Credential writes now use synchronous commit; do not describe them as asynchronous.
- Attachment hashing now checks QCryptographicHash's device-read result. Filesystem read-failure injection remains outstanding.
- Provider response-size memory bounds and attachment provider grant-map growth need measured abuse fixtures.
- CMake now requires Qt 6.9 for SafeArea. Tested toolchain is Qt 6.11.1; a minimum-version build remains unverified.
- Presentation sanitization handles recognized structures and known credentials, not arbitrary sensitive prose. Expand adversarial Activity/copy fixtures before claiming exhaustive redaction.
- Historical store graphics/screenshots and browser prototypes remain unchanged. They are not current production branding or evidence of the redesign.

## Repeatable measurements

`measure_mobile` creates an isolated synthetic namespace with five tables, 10,000 messages and 10,000 events. Each message has 256 characters. Results are medians of seven synchronous operations in microseconds, Release Qt 6.11.1 / MinGW on the same host.

| Operation | Baseline | Updated |
| --- | ---: | ---: |
| Restore all tables | 51,702 | 44,730 |
| Switch five tables | 5,235 | 5,129 |
| Export 2,000 transcript rows | 12,194 | 22,869 |
| Export 2,000 Activity rows | 9,241 | 13,997 |
| Flush selected table | 541 | 703 |
| Process working set, bytes | 40,747,008 | 40,620,032 |

The baseline executable was linked against the pre-overhaul Release test-core archive before subsequent runner layout changes. Do not relink that old archive using current headers. This is a local diagnostic comparison, not a portable benchmark guarantee. Repeat in isolated builds for acceptance.

The collection-export regressions are real: presentation sanitization adds work. Regex guards and in-place map sanitization reduced transcript export from 36,048 to 22,869 microseconds and Activity from 23,874 to 13,997. No speculative cache was introduced. Working-set differences are too small for a memory claim; flush timings are sub-millisecond and need repetition. Restore is not time-to-first-frame.

The separate QML measurement fixture compares original and updated sources under the same host/software renderer, Material style, seven medians and 2,000 rows. The matched-style run measured transcript rendering at 14/12 ms, Activity at 349/7 ms, and shell construction plus capture at 40/41 ms (baseline/updated). Earlier default-style measurements were 16/14, 474/9 and 45/56 ms. Activity improvement is consistent; the matched-style shell difference is not evidence of a meaningful startup regression. These are host diagnostics, not Android frame-rate or cold-start claims. Benchmark window creation is isolated from the independent scroll-test harness after combining them caused window-lifecycle interference.

Reproduce with `cmake --build <desktop-build> --target measure_mobile`, then run `measure_mobile` with the Qt runtime on PATH. It makes no provider calls and removes its own synthetic tables. For QML, set `SYNSEMBLE_MEASURE_QML=1`, `QT_QUICK_BACKEND=software`, `QT_QUICK_CONTROLS_STYLE=Material`, and run `test_qml`; set `SYNSEMBLE_QML_BASELINE_DIR` to an isolated copy of baseline QML for comparison. Native measurements predate the final UI-only Back/elevation changes.

## Initial overhaul validation record

This historical pass preceded the usability follow-up above. Current artifact identities and follow-up results are listed below; the device limitations remain applicable.

- Fresh desktop Release configuration/build and the final incremental Release build succeeded, Qt 6.11.1 / MinGW.
- Fresh Android arm64 Release configuration/build, native optimization/stripping, and final `assembleRelease` succeeded. Artifacts remain outside source. A separate copy was debug-signed for the authorized emulator test only.
- QML lint of all four production components succeeded.
- Comprehensive final CTest pass: 10/10 suites (11.82 seconds), covering native workflows/Continue, persistence, controller, provider fixtures, Java attachment copier, startup, QML and source hygiene. After the Android-only manifest and dialog-elevation corrections, affected QML/hygiene suites passed again; native code was unchanged. No repeated full-suite runs for those UI-only changes.
- Expanded QML direct run: 39 cases passed, including real Back key events and 19 screen fixtures. Final screenshots are in [screenshots](screenshots/README.md), captured at scale 0.65 so portrait-tablet and wide windows fit the host. They are host fixtures, not physical-device screenshots.
- Final APK metadata confirms `com.aimeetingtable.myapp`, Synsemble, version 1.1/code 5, `arm64-v8a`. Archive inspection excludes QtTest, QuickTest, test QML, qmltooling and keystores.
- Qt Android import scanning still warns about unavailable Windows/NativeStyle imports; Release packaging succeeds. No new dependency or workaround was added for these non-blocking warnings.

### Android interaction evidence

The existing saved AVD contained an app signed by a different certificate. It was not uninstalled or overwritten. Testing used a separate task-owned userdata image with `-read-only -no-snapshot`; the existing Android debug key was used only after explicit authorization. The emulator runs Android 37.1 x86_64 with ARM64 native translation, not physical ARM64 hardware.

- A synthetic zero-agent table survived APK replacement and app restart.
- Actual `KEYCODE_BACK` closes the agent dialog while retaining its Team sheet, then closes the sheet while retaining the app. Drawer and validation-message dismissal also passed.
- With the keyboard open, composer bounds were y=1476..1619 and Send y=1626..1769, above keyboard y=1848 on the 1280x2856 display.
- Submitting `Keep this task` without agents displayed configuration validation. Back closed it without exiting, and the draft remained visible. No provider was configured in the emulator.
- Host Back regressions use QTest key events rather than only calling the QML handler. Qt's standard Android manifest setting is covered by source hygiene. The application-scoped shortcut addresses Qt's [modal-popup shortcut filtering](https://github.com/qt/qtdeclarative/blob/v6.11.1/src/quicktemplates/qquickshortcutcontext.cpp); the manifest matches the installed Qt Android template.

No real credentials were used in tests. An earlier composer fixture changed the effective budget field instead of its authoritative override and may have attempted network requests with a fake key. Zero provider contact cannot be certified for that earlier run. The corrected controller fixture sets the authoritative override; controller tests now also route accidental transport to localhost port 9.

## Remaining work and delivery limits

- **Release-validation gap:** this translated Android 37.1 emulator renders diagonal clipping/stripes in some rounded controls and the logo. It occurred with both host GPU and SwiftShader. A temporary Qt software-renderer experiment produced blank app content and was removed. Cause is unproven; do not infer physical-device correctness or ship a renderer override. Verify a supported physical ARM64 device before visual release approval.
- **Accessibility/device coverage:** TalkBack navigation, large system text, all touch targets, full safe-area/rotation matrix, attachment-picker lifecycle and process-death recovery remain unverified on a physical device. Fixed pixel sizes remain in some presentation elements; complete font-scaling acceptance before claiming it.
- **Review coverage:** historical assets/prototypes are inventoried, not exhaustively reviewed or redesigned. Additional form failure states, artifact disk-failure injection, provider memory-bound/grant-map stress tests and minimum Qt 6.9 build are remaining work, not new implementation scope.
- **Performance:** preserve the measured sanitization cost above. Physical startup, scrolling frame-time and repeated-memory measurements remain outstanding. Do not label host collection timings as device benchmarks.

Delivery includes the conversation-first shell, Team/details/Settings components, branding assets and fonts, focused persistence/workflow/privacy fixes, regression fixtures, performance fixtures/comparisons, UI guide, inventory and 23 current host screenshots (plus two historical baselines). No new schema or dedicated desktop redesign was introduced. This closes the requested implementation pass with the above limitations, not every original release-acceptance item.

### Local delivery evidence

Paths below are relative to the Android source checkout and remain outside Git:

- Desktop Release build: `../Builds/build-synsemble-overhaul-desktop`.
- Android Release build: `../Builds/build-synsemble-overhaul-android-final`.
- Unsigned diagnostic APK: `../Builds/build-synsemble-overhaul-android-final/android-build/build/outputs/apk/release/android-build-release-unsigned.apk`.
- Unsigned APK size: 28,775,636 bytes; SHA-256: `9d5b0fbddcdd1c59a7517635cdf638b5df957a3be284f856ba4b9fab64adfae7`.
- Current disposable debug-signed test copy: `../Builds/Synsemble-recovery-emulator-only.apk`; 28,796,223 bytes; SHA-256: `fa49ef3d44f6fee2e8d3f11e53f7c2214df3e99f6fdc8484c373dcff0930201a`. Signature verification and update installation succeeded. This supersedes the older usability/overhaul test APKs and is not release-signed or Play-ready.
- Historical screenshot run: `../Builds/Diagnostics/2026-09-21/recovery-qml-results.txt`; 67 passed, zero failed, 23 screen fixtures. QML lint passed. The later artifact-guidance run passed 68 QML cases.
- Matched performance runs: `../Builds/Diagnostics/2026-09-21/overhaul-material-performance-results.txt` and `../Builds/Diagnostics/2026-09-21/overhaul-material-baseline-performance-results.txt`.
- Android interaction dumps and diagnostic captures: `../Builds/Diagnostics/2026-09-21/android-*.xml` / `android-*.png`. These preserve the emulator limitations rather than presenting them as approved production screenshots.
- Latest usability interaction dump: `../Builds/Diagnostics/2026-09-21/recovery-ui.xml`; transient diagnostic evidence, not source-controlled or a release artifact.

No commits or pushes were made. No release artifacts belong in Git history.
