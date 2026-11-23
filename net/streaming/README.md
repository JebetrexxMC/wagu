# Test stream for the MP radio (infinite-streaming)

[JebetrexxMC/infinite-streaming](https://github.com/JebetrexxMC/infinite-streaming) serves HLS / DASH
streams that never end, which is a good way to test the MP URL radio player (`src/audio/sound.cpp`,
UrlThread) for long sessions without a real radio station.

```sh
git clone --depth 1 https://github.com/JebetrexxMC/infinite-streaming
cd infinite-streaming && docker compose up -d   # see the repo README for ports and stream paths
```

Then point an MP radio URL at the HLS playlist the server prints (use the PC / phone LAN IP, not
localhost, when the game runs on a phone). Add it to `net/wires.txt` as an `http` wire to watch it with
`go/mplatency`.
