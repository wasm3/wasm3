## Build for Android

<p align="center"><img width="50%" src="https://github.com/wasm3/wasm3/raw/main/extra/screenshot-android.png"></p>

Install Android SDK Tools, then:

```sh
export ANDROID_HOME=/opt/android-sdk/
export PATH=$ANDROID_HOME/tools/bin:$ANDROID_HOME/platform-tools:$PATH
```

Install NDK:
```sh
sdkmanager --install ndk-bundle
```

Build:
```sh
./gradlew build
```

Install on device:
```
adb install -r ./app/build/outputs/apk/debug/app-debug.apk
```

