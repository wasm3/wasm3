## Build for iOS

<p align="center"><img width="50%" src="https://github.com/wasm3/wasm3/raw/main/extra/screenshot-ios.png"><br>
  Wasm3 running on <b>iPhone 8</b>
</p>


Install Xcode, then open and build the project as usual.

**Note:** Xcode runs apps in `Debug` mode by default. Select `Release` mode for significantly better performance.

You can also build the project from command line:

```sh
xcodebuild build -scheme wasm3 -project wasm3.xcodeproj -configuration Release -destination 'platform=iOS Simulator,name=iPhone 11,OS=13.3'
```

