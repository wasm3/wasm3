## Build for Android

<p align="center"><img width="50%" src="https://github.com/wasm3/wasm3/raw/main/extra/screenshot-android.png"></p>

Install JDK 17 and the [Android Command Line Tools](https://developer.android.com/studio#cmdline-tools), then:

```sh
export ANDROID_HOME=/opt/android-sdk/
export PATH=$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH
```

**Note:** the JDK version is not a free choice. This project builds with Android Gradle
Plugin 9.4 on Gradle 9.7.1, which requires JDK 17 at minimum. CI uses 17.

Install NDK:
```sh
sdkmanager --install platform-tools "ndk;<version>"
```

The project pins neither the NDK nor the CMake version (`ndkVersion` and the `cmake`
`version` in `app/build.gradle` are commented out), so the plugin's own defaults are used
and it downloads them if they are missing. Pin them there if a build has to be
reproducible. The old `ndk-bundle` package the NDK used to come from is deprecated and no
longer installable.

Build:
```sh
./gradlew build
```

Install on device:
```
adb install -r ./app/build/outputs/apk/debug/app-debug.apk
```

