# Synsemble

<img src="branding/synsemble.png" alt="Synsemble symbol" width="80">

## WARNING: THE CURRENT DESKTOP BUILD IS DEFUNCT AND WILL NOT BE UPDATED

Synsemble is a multi-agent AI collaboration app that brings multiple AI roles together to plan, analyze, execute, review, revise, and produce a final result.

Synsemble is not primarily an AI meeting recorder, transcription app, or AI meeting notetaker. Its table and seat metaphor represents multiple AI agents collaborating on a task with assigned providers, models, and roles.

## What Synsemble Does

- Create independent AI tables for separate tasks.
- Start each table with zero seats and add AI seats one at a time.
- Assign providers, models, display names, colors, and roles.
- Give the AI team a task and run it through structured collaboration phases.
- Produce and manage a final artifact.
- Preserve tables, sessions, transcripts, and event history locally.

## Multi-Agent Workflow

Synsemble coordinates agents through planning, research where appropriate, execution, quality control, and decision-making. Agents contribute according to their roles, targeted revisions return to the relevant work, and the final phase produces the authoritative artifact.

## Key Features

- Multiple persistent tables and sessions
- Zero-seat table creation and individual Add Seat workflow
- OpenAI, Google Gemini, and Anthropic providers
- Configurable models, effort levels, names, colors, and collaboration roles
- Secure Android API-key storage with saved-state-only credential status
- Structured multi-agent planning, execution, review, and decision workflows
- Transcript, session history, and event log
- Conversation-first workspace with Team, Artifacts, and Activity details
- Final-result guidance: tap Team, then choose Artifacts
- File attachments with protected app-private storage
- Input, output, and total token telemetry
- User-configurable token, round, loop, phase-time, and session-time limits
- Pause and Continue without replaying completed provider calls
- Portrait, landscape, phone, and tablet layouts
- Light, dark, and system appearance modes
- Android arm64 support
- Shared Qt Quick desktop build for development and validation

## How Limits and Continue Work

Users can configure token, round, loop, phase-time, and session-time limits. Reaching a limit pauses the meeting before the pending operation advances. Continue authorizes that pending operation while retaining accumulated token usage and completed responses. Completed provider calls are not replayed, and later limits can pause the meeting again.

## Privacy and API Keys

Users provide their own API keys for supported providers. Android credentials are stored through an Android Keystore-backed bridge. Persisted credentials are never returned to QML or displayed back to the user; the interface shows only whether a credential is Saved.

Prompts, selected conversation context, generated content, and user-selected attachments may be sent directly to the configured provider over HTTPS. Review the [privacy policy](https://noobieisgod.github.io/Synsemble/) before using sensitive material.

See the [mobile interface guide](docs/MOBILE_UI.md) for navigation and the [review report](docs/MOBILE_REVIEW.md) for validation evidence and remaining limitations. [Host screenshots](docs/screenshots/README.md) use synthetic fixtures, not real provider conversations.

## Platform

Android is the primary published target. The release build targets `arm64-v8a`, Android 9 or newer, and target SDK 36.

The Qt Quick application also builds on desktop for development and automated validation. This repository does not claim or distribute a Windows installer for Synsemble 1.1.

## Building

Requirements:

- CMake 3.21 or newer
- A C++20 compiler
- Qt 6.9 or newer (validated with Qt 6.11.1)
- Ninja or another CMake-supported build tool
- Android SDK with API 36 for Android builds
- An Android NDK compatible with the installed Qt Android kit
- A JDK supported by the Qt-generated Gradle project

Desktop Release build:

```powershell
qt-cmake -S . -B build/desktop -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/desktop
ctest --test-dir build/desktop --output-on-failure
```

Android Release bundle:

```powershell
qt-cmake -S . -B build/android -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DANDROID_PLATFORM=android-28 `
  -DANDROID_SDK_ROOT="$env:ANDROID_SDK_ROOT" `
  -DANDROID_NDK_ROOT="$env:ANDROID_NDK_ROOT" `
  -DBUILD_TESTING=OFF
cmake --build build/android --target aab
```

See [Android deployment](docs/ANDROID_DEPLOYMENT.md), [test plan](docs/TEST_PLAN.md), and [architecture](docs/ARCHITECTURE_REPORT.md) for additional details.

## Testing

Validation includes `qmllint`, the native CTest suite, workflow and hard-stop/Continue tests, provider fixtures, persistence tests, controller tests, QML tests, Java attachment tests, source-hygiene checks, desktop Release compilation, and Android arm64 Release packaging. Tests use fixtures and do not require real provider calls.

## Android Release

- Package: `com.aimeetingtable.myapp`
- Version name: `1.1`
- Version code: `5`
- ABI: `arm64-v8a`
- Build type: Release

Locally generated unsigned AAB files are not ready for Google Play upload. They must be signed with the correct Play upload or release key without changing the package identity.

## Repository

Source: [github.com/noobieisgod/Synsemble](https://github.com/noobieisgod/Synsemble)

Historical note: Synsemble was formerly developed under the name AI Meeting Table.

## License

Synsemble is licensed under the [GNU Affero General Public License v3.0](LICENSE.txt).
