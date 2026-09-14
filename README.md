# EVA-VR

Standalone repository for the EVA-CLIENT native PICO client: [github.com/Noietch/EVA-VR](https://github.com/Noietch/EVA-VR).

EVA-VR is a small native OpenXR client for PICO 4 Ultra. It replaces the
browser/WebXR input layer when controller haptics are required.

The APK is named **EVA-VR** and uses the EVA logo as its launcher icon.
The complete Android/OpenXR source and the required OpenXR sample dependencies
are included in this repository; it does not depend on the EVA-CLIENT source
tree at build time.

## What It Does

- Runs as a native PICO VR application.
- Keeps a live diagnostic panel directly in front of the user's view.
- Shows head and controller `X/Y/Z` positions and `Roll/Pitch/Yaw` angles.
- Shows trigger, squeeze, thumbstick, button, click, and touch states for both controllers.
- Shows whether the EVA host WebSocket is connected.
- Sends the existing EVA `frame` protocol over WebSocket.
- Receives explicit `haptic` messages and invokes native OpenXR controller haptics; `event_ack` is status-only.

## Requirements

- PICO 4 Ultra with USB debugging enabled.
- macOS or Linux development machine.
- Android SDK with platform 29 and build-tools 30.0.3.
- Android NDK `23.2.8568313`.
- Java 17.

Set the SDK and Java paths if they are not already configured:

```bash
export JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home
export ANDROID_HOME="$HOME/Library/Android/sdk"
```

## Install A Released APK

The easiest path is to download the latest signed-by-GitHub release artifact:

- [Latest release](https://github.com/Noietch/EVA-VR/releases/latest)
- [Latest APK download](https://github.com/Noietch/EVA-VR/releases/latest/download/EVA-VR-v0.1.2.apk)

After enabling USB debugging on the PICO 4 Ultra:

```bash
adb devices
adb install -r EVA-VR-v0.1.2.apk
```

The installed app is named **EVA-VR** and its package is
`org.eva.pico.input`. It can also be started from the PICO app library:

```bash
adb shell am start -n org.eva.pico.input/.MainActivity
```

The release APK is the recommended way to try the client. You only need the
source build below when changing the native OpenXR code or the UI.

## Build And Install From Source

The connected PICO 4 Ultra must have USB debugging enabled. From this repository:

```bash
export JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home
export ANDROID_HOME="$HOME/Library/Android/sdk"
./build.sh
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

## Local Haptic Test

This starts the app without a host connection and sends one explicit
test pulse to both controllers:

```bash
adb shell am force-stop org.eva.pico.input
adb shell am start -n org.eva.pico.input/.MainActivity --ez haptic_test true
adb logcat -s "EVA-VR" "OpenXR"
```

Successful native haptic calls are logged by the OpenXR layer as
`EVA-VR haptic ... result=XR_SUCCESS`.

## Connect To EVA

The APK accepts a WebSocket URL through an Android intent extra. The endpoint
can point directly to a Mac/Linux host or to a local ADB reverse tunnel.

Example with the existing EVA node on port `43876`:

```bash
adb reverse tcp:43876 tcp:43876
adb shell am force-stop org.eva.pico.input
adb shell am start -n org.eva.pico.input/.MainActivity \
  --es server_url 'ws://127.0.0.1:43876/ws?token=YOUR_TOKEN'
```

For a host on the same LAN, use its address instead:

```bash
adb shell am start -n org.eva.pico.input/.MainActivity \
  --es server_url 'ws://192.168.1.20:43876/ws?token=YOUR_TOKEN'
```

The in-view panel changes from `HOST: DISCONNECTED` to
`HOST: CONNECTED` after the WebSocket handshake succeeds.

## Wire Protocol

The client sends one `frame` message after each complete left/right input
update. The payload is compatible with the current EVA WebXR node:

```json
{
  "type": "frame",
  "version": 1,
  "seq": 42,
  "client_time_ms": 1234567890,
  "reference_space": "local-floor",
  "controllers": {
    "left": {
      "valid": true,
      "position": [0, 0, 0],
      "orientation_xyzw": [0, 0, 0, 1],
      "buttons": [],
      "axes": [],
      "profiles": ["pico-4-ultra"]
    },
    "right": {
      "valid": true,
      "position": [0, 0, 0],
      "orientation_xyzw": [0, 0, 0, 1],
      "buttons": [],
      "axes": [],
      "profiles": ["pico-4-ultra"]
    }
  }
}
```

The host sends haptics using:

```json
{"type":"haptic","hand":"right","intensity":0.6,"duration_ms":80}
```

`hand` is `left` or `right`, `intensity` is clamped to `0..1`, and duration is
clamped to `1..1000` milliseconds. `event_ack` messages are logged but do not trigger vibration by themselves.
EVA may send a separate explicit `haptic` message when an operation is accepted;
ordinary input frames never trigger vibration.

## Project Layout

- `app/src/main/cpp`: OpenXR renderer, controller input, UI, JNI bridge, and haptics.
- `app/src/main/java`: WebSocket connection and Android lifecycle code.
- `app/src/main/assets`: controller models and fonts used by the diagnostic scene.
- `assets/eva-logo.svg`: source for the EVA-VR launcher icon.
- `build.sh`: reproducible local build entry point.

The OpenXR sample code is included directly under `app/src/main`; there is no
`vendor/OpenXR_Demos` checkout or runtime patch step.

## License And Attribution

The native OpenXR sample portions retain their upstream source headers and
licenses. PICO SDK/OpenXR runtime components remain subject to their respective
PICO and Khronos licenses. EVA-specific bridge and application code is provided
for the EVA project.
