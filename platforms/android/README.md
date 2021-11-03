## Build for Android

<p align="center"><img width="50%" src="https://github.com/wasm3/wasm3/raw/main/extra/screenshot-android.png"></p>

Install OpenJDK 15, [Android Command Line Tools](https://developer.android.com/studio#cmdline-tools), then:

```sh
export ANDROID_HOME=/opt/android-sdk/
export PATH=$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH
```

Install NDK:
```sh
sdkmanager --install ndk-bundle platform-tools
```

Build:
```sh
./gradlew build
```

Install on device:
```
adb install -r ./app/build/outputs/apk/debug/app-debug.apk
```

