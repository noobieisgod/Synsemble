**WARNING: THE CURRENT DESKTOP BUILD IS DEFUNCT AND WILL NOT BE UPDATED.**

**Ever since release v1.1, this desktop client of Synsemble (or AI Meeting Table before) has been abandoned. Due to time constraints, complexity of maintaining two separate platforms, and me being a solo developer, I have decided to shift my entire focus of Synsemble from desktop and mobile to mobile only. The desktop Synsemble app is no longer maintained, so use at your own risk. This repository may occasionally receive large commits, it doesn't mean that it was all written in a single session. Any new commit on this repository is the open source code for the mobile app or any other data regarding Synsemble.**

# Synsemble
Synsemble is a multi-agent AI collaboration app that brings multiple AI roles together to plan, analyze, execute, review, revise, and produce a final result.

Synsemble is not primarily an AI meeting recorder, transcription app, or AI meeting notetaker. Its table and seat metaphor represents multiple AI agents collaborating on a task with assigned providers, models, and roles.

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
- Android arm64 support
- Shared Qt Quick desktop build for development and validation

## How Limits and Continue Work
Users can configure token, round, loop, phase-time, and session-time limits. Reaching a limit pauses the meeting before the pending operation advances. Continue authorizes that pending operation while retaining accumulated token usage and completed responses. Completed provider calls are not replayed, and later limits can pause the meeting again.

## Privacy and API Keys
Users provide their own API keys for supported providers. Android credentials are stored through an Android Keystore-backed bridge. Persisted credentials are never returned to QML or displayed back to the user; the interface shows only whether a credential is Saved.

Prompts, selected conversation context, generated content, and user-selected attachments may be sent directly to the configured provider over HTTPS. Review the [privacy policy](https://noobieisgod.github.io/Synsemble/) before using sensitive material.

See the [mobile interface guide](docs/MOBILE_UI.md) for navigation and the [review report](docs/MOBILE_REVIEW.md) for validation evidence and remaining limitations. [Host screenshots](docs/screenshots/README.md) use synthetic fixtures, not real provider conversations.

## Platform
Android is the only published target left. The release build targets `arm64-v8a`, Android 9 or newer, and target SDK 36.
The Qt Quick application also builds on desktop for development and automated validation. This repository does not claim or distribute a Windows installer for Synsemble 1.1.

## Repository
Historical note: Synsemble was formerly developed under the name AI Meeting Table.

## License
Synsemble is now licensed under the MIT license. Any previous versions using AGPL-3.0 must still follow AGPL-3.0 requirements due to some of Synsemble's dependencies being licensed under AGPL-3.0.
