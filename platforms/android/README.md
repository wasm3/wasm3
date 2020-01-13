## Build for Android

<p align="center"><img width="50%" src="https://github.com/wasm3/wasm3/raw/master/extra/screenshot-android.png"></p>


Install Android SDK Tools, then:

```sh
export ANDROID_HOME=/opt/android-sdk/
export PATH=$ANDROID_HOME/tools/bin:$PATH
```

```
# On my Ubuntu, I had to:
#export SDKMANAGER_OPTS="-XX:+IgnoreUnrecognizedVMOptions --add-modules java.se.ee"

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

