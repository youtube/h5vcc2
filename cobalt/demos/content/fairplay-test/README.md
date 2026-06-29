# HLS URL Player Test Page

Test page for validating HLS playback via AVPlayer on tvOS.
Open [hls-urlplayer-test.html](hls-urlplayer-test.html) to run.

## Files

- `hls-urlplayer-test.html` - Main test page (clear and encrypted HLS modes)
- `hls-test-common.js` - Playback controls, event monitoring, manifest parsing
- `hls-test-fairplay.js` - FairPlay DRM config and EME session management
- `hls-test-common.css` - Styles
- `m3u8-parser.min.js` - HLS manifest parser (local copy to avoid CORS)

## Running locally

Start a server from this directory:

    python3 -m http.server 8080 --bind 0.0.0.0

Find your IP:

    ipconfig getifaddr en0

Open in browser to verify:

    http://<your-ip>:8080/hls-urlplayer-test.html?mode=clear

## Deploying to tvOS device

Pass the test page URL and allow insecure origin:

    --url=http://<your-ip>:8080/hls-urlplayer-test.html?mode=clear
    --unsafely-treat-insecure-origin-as-secure=http://<your-ip>:8080

## URL parameters

- `?mode=clear` - Clear HLS (default)
- `?mode=encrypted` - FairPlay encrypted HLS

## Test stream sources

- Stream & credentials: [cdn.amö.be/TestVectors/.../shaka-mpd.html](https://cdn.amö.be/TestVectors/Cmaf/protected_1080p_h264_cbcs/shaka-mpd.html)
- Test vectors repo: [Axinom/public-test-vectors](https://github.com/Axinom/public-test-vectors)
- FairPlay demo players: [Dash-Industry-Forum/dash.js#4844](https://github.com/Dash-Industry-Forum/dash.js/issues/4844)

## Browser support

- **Safari** - Clear and encrypted HLS both work natively
- **Chrome** - Clear HLS only (no native FairPlay support)
- **Cobalt (tvOS)** - Clear and encrypted HLS via AVPlayer URL player pipeline
