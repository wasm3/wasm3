## Build for Android

Install Android SDK Tools, adb.

```sh
export ANDROID_HOME=/opt/android-sdk/
export PATH=$ANDROID_HOME/tools/bin:$PATH

./gradlew build
```

```
export SDKMANAGER_OPTS="-XX:+IgnoreUnrecognizedVMOptions --add-modules java.se.ee"
sdkmanager --install ndk-bundle
```

Install on device:
```
adb install -r ./app/build/outputs/apk/debug/app-debug.apk
```

