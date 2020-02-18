# Wasm Synth

<p align="center"><img width="80%" src="image.png"></p>

Here is some music created by [Peter Salomonsen](https://petersalomonsen.com/) in his [wasm music experiment](https://petersalomonsen.com/webassemblymusic/livecodev2/):

- **Hondarribia (Wasm Summit opening track)**, `hondarribia.wasm` │ [SoundCloud](https://soundcloud.com/psalomo/hondarribia)
- **WASM song**, `wasm-song.wasm` │ [SoundCloud](https://soundcloud.com/psalomo/wasm-song)
- **Shuffle Chill**, `shuffle-chill.wasm` │ [SoundCloud](https://soundcloud.com/psalomo/shuffle-chill)
- **"WebChip" music**, `webchip-music.wasm` │ [SoundCloud](https://soundcloud.com/psalomo/webchip-music)

Check out Peter's [excellent talk on WebAssembly Summit 2020](https://www.youtube.com/watch?v=WZp0sPDvWfw&t=18670).

### Running

Generate a `wav` file:
```
wasm3 hondarribia.wasm | sox -S -t raw -b 32 -e float -r 44100 -c 2 - hondarribia.wav
```

Play live (choppy on most machines):
```
wasm3 hondarribia.wasm | sox -S -t raw -b 32 -e float -r 44100 -c 2 - -d
```
