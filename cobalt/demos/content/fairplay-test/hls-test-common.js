// Shared utilities for HLS URL Player test page (clear HLS playback).
// DRM/FairPlay code is in hls-test-fairplay.js.
//
// Element ID prefixes: si-* (Stream Info), pi-* (Player Info),
// cap-* (Platform Capabilities). di-* (DRM Info) is in fairplay.js.
var HLSTest = (function() {
    var logEl = document.getElementById('log');

    // --- Logging and formatting ---

    function log(msg, cls) {
        var line = '[' + new Date().toISOString().slice(11, 23) + '] ' + msg;
        console.log(line);
        var span = document.createElement('span');
        span.className = cls || '';
        span.textContent = line + '\n';
        logEl.appendChild(span);
        logEl.scrollTop = logEl.scrollHeight;
    }

    function fmtTime(s) {
        if (!isFinite(s)) return '--:--';
        var m = Math.floor(s / 60);
        var sec = Math.floor(s % 60);
        return m + ':' + (sec < 10 ? '0' : '') + sec;
    }

    function highlightRateBtn(rate) {
        var map = {0.5: 'btn-rate-05', 1: 'btn-rate-1', 1.5: 'btn-rate-15', 2: 'btn-rate-2'};
        Object.values(map).forEach(function(id) {
            var el = document.getElementById(id);
            if (el) el.classList.remove('active');
        });
        if (map[rate]) document.getElementById(map[rate]).classList.add('active');
    }

    // --- Playback controls and UI state ---

    function updateButtonStates(video) {
        var btnPlay = document.getElementById('btn-play');
        var btnPause = document.getElementById('btn-pause');
        var btnMute = document.getElementById('btn-mute');
        var btnUnmute = document.getElementById('btn-unmute');

        if (video.paused) {
            btnPlay.classList.remove('active');
            btnPause.classList.add('active');
        } else {
            btnPlay.classList.add('active');
            btnPause.classList.remove('active');
        }
        if (video.muted) {
            btnMute.classList.add('active');
            btnUnmute.classList.remove('active');
        } else {
            btnMute.classList.remove('active');
            btnUnmute.classList.add('active');
        }
    }

    function initControls(video) {
        var seekBar = document.getElementById('seek-bar');
        var isSeeking = false;

        document.getElementById('btn-play').onclick = function() {
            log('CMD: video.play()', 'ok');
            video.play().catch(function(e) { log('play() rejected: ' + e.message, 'error'); });
        };
        document.getElementById('btn-pause').onclick = function() {
            log('CMD: video.pause()', 'ok');
            video.pause();
        };
        document.getElementById('btn-seek-back').onclick = function() {
            var t = Math.max(0, video.currentTime - 10);
            log('CMD: seek to ' + t.toFixed(1), 'ok');
            video.currentTime = t;
        };
        document.getElementById('btn-seek-fwd').onclick = function() {
            var maxSeek = isFinite(video.duration) ? video.duration : Infinity;
            var t = Math.min(maxSeek, video.currentTime + 10);
            log('CMD: seek to ' + t.toFixed(1), 'ok');
            video.currentTime = t;
        };
        [0.5, 1, 1.5, 2].forEach(function(rate) {
            var map = {0.5: 'btn-rate-05', 1: 'btn-rate-1', 1.5: 'btn-rate-15', 2: 'btn-rate-2'};
            document.getElementById(map[rate]).onclick = function() {
                log('CMD: playbackRate = ' + rate, 'ok');
                video.playbackRate = rate;
                highlightRateBtn(rate);
            };
        });
        document.getElementById('btn-mute').onclick = function() {
            log('CMD: muted = true', 'ok');
            video.muted = true;
        };
        document.getElementById('btn-unmute').onclick = function() {
            log('CMD: muted = false', 'ok');
            video.muted = false;
        };

        seekBar.addEventListener('input', function() { isSeeking = true; });
        seekBar.addEventListener('change', function() {
            if (isFinite(video.duration)) {
                var t = (seekBar.value / 100) * video.duration;
                log('CMD: seek bar -> ' + t.toFixed(1), 'ok');
                video.currentTime = t;
            }
            isSeeking = false;
        });

        // Seek bar keyboard handling for tvOS remote:
        // - Left/right: seek by 5 seconds (works with long press / key repeat)
        // - Up/down: move focus to other controls
        seekBar.addEventListener('keydown', function(e) {
            if (e.key === 'ArrowLeft' || e.keyCode === 37) {
                e.preventDefault();
                video.currentTime = Math.max(0, video.currentTime - 5);
            } else if (e.key === 'ArrowRight' || e.keyCode === 39) {
                e.preventDefault();
                var maxSeek = isFinite(video.duration) ? video.duration : Infinity;
                video.currentTime = Math.min(maxSeek, video.currentTime + 5);
            } else if (e.key === 'ArrowUp' || e.keyCode === 38) {
                e.preventDefault();
                document.getElementById('btn-play').focus();
            } else if (e.key === 'ArrowDown' || e.keyCode === 40) {
                e.preventDefault();
                var modeBtns = document.querySelectorAll('#mode-selector .mode-btn');
                if (modeBtns.length > 0) modeBtns[0].focus();
            }
        });

        var readyStateNames = ['HAVE_NOTHING', 'HAVE_METADATA', 'HAVE_CURRENT_DATA',
                               'HAVE_FUTURE_DATA', 'HAVE_ENOUGH_DATA'];
        var networkStateNames = ['NETWORK_EMPTY', 'NETWORK_IDLE',
                                 'NETWORK_LOADING', 'NETWORK_NO_SOURCE'];

        // Update timeline, info panels, and button states every 250ms
        setInterval(function() {
            if (video.readyState >= 1) {
                var timeDisplay = document.getElementById('time-display');
                var bufferedInfo = document.getElementById('buffered-info');

                if (!isSeeking && isFinite(video.duration) && video.duration > 0) {
                    seekBar.value = (video.currentTime / video.duration) * 100;
                }
                timeDisplay.textContent = fmtTime(video.currentTime) + ' / ' + fmtTime(video.duration);

                var ranges = [];
                for (var i = 0; i < video.buffered.length; i++) {
                    ranges.push(fmtTime(video.buffered.start(i)) + '-' + fmtTime(video.buffered.end(i)));
                }
                bufferedInfo.textContent = 'Buffered: ' +
                    (ranges.length > 0 ? ranges.join(', ') : 'none');

                updateButtonStates(video);

                // Update info panels
                if (document.getElementById('si-duration')) {
                    document.getElementById('si-duration').textContent = isFinite(video.duration)
                        ? video.duration.toFixed(1) + 's' : 'N/A';
                    document.getElementById('si-resolution').textContent = video.videoWidth && video.videoHeight
                        ? video.videoWidth + 'x' + video.videoHeight : '--';

                    document.getElementById('pi-ready').textContent = (readyStateNames[video.readyState] || '?') +
                        ' (' + video.readyState + ')';
                    document.getElementById('pi-network').textContent = (networkStateNames[video.networkState] || '?') +
                        ' (' + video.networkState + ')';
                    document.getElementById('pi-paused').textContent = video.paused ? 'yes' : 'no';
                    document.getElementById('pi-rate').textContent = video.playbackRate;
                    document.getElementById('pi-time').textContent = video.currentTime.toFixed(1) + 's';
                    document.getElementById('pi-volume').textContent = video.volume.toFixed(2) +
                        (video.muted ? ' (muted)' : '');

                    // Frame stats (getVideoPlaybackQuality)
                    if (video.getVideoPlaybackQuality) {
                        var q = video.getVideoPlaybackQuality();
                        document.getElementById('pi-decoded').textContent = q.totalVideoFrames || 0;
                        document.getElementById('pi-dropped').textContent = q.droppedVideoFrames || 0;
                    } else if (video.webkitDecodedFrameCount !== undefined) {
                        document.getElementById('pi-decoded').textContent = video.webkitDecodedFrameCount;
                        document.getElementById('pi-dropped').textContent = video.webkitDroppedFrameCount;
                    }

                    // Platform capabilities probe (one-time).
                    // Uses application/x-mpegURL container to test what the URL
                    // player (AVPlayer) actually supports. On Cobalt, canPlayType
                    // routes through SbMediaCanPlayMimeAndKeySystem which checks
                    // IsVideoCodecSupportedByUrlPlayer / IsAudioCodecSupportedByUrlPlayer.
                    if (document.getElementById('cap-video') && document.getElementById('cap-video').textContent === '--') {
                        var codecs = [
                            {label: 'H.264', mime: 'application/x-mpegURL; codecs="avc1.640028"'},
                            {label: 'HEVC', mime: 'application/x-mpegURL; codecs="hvc1.1.6.L93.B0"'},
                            {label: 'VP9', mime: 'application/x-mpegURL; codecs="vp09.00.51.08"'}
                        ];
                        var supported = [];
                        codecs.forEach(function(c) {
                            if (video.canPlayType(c.mime)) supported.push(c.label);
                        });
                        document.getElementById('cap-video').textContent = supported.join(', ') || 'none';

                        var acodecs = [
                            {label: 'AAC', mime: 'application/x-mpegURL; codecs="mp4a.40.2"'},
                            {label: 'AC-3', mime: 'application/x-mpegURL; codecs="ac-3"'},
                            {label: 'E-AC-3', mime: 'application/x-mpegURL; codecs="ec-3"'}
                        ];
                        var asupported = [];
                        acodecs.forEach(function(c) {
                            if (video.canPlayType(c.mime)) asupported.push(c.label);
                        });
                        document.getElementById('cap-audio').textContent = asupported.join(', ') || 'none';

                        document.getElementById('cap-hls').textContent = video.canPlayType('application/x-mpegURL') || 'no';
                    }
                }
            }
        }, 250);
    }

    // --- Video event monitoring ---

    function monitorEvents(video) {
        var events = [
            ['loadstart', ''],
            ['loadedmetadata', 'ok'],
            ['canplay', 'ok'],
            ['playing', 'ok'],
            ['stalled', 'warn'],
            ['waiting', ''],
            ['ended', 'warn'],
            ['pause', ''],
            ['play', 'ok']
        ];
        events.forEach(function(ev) {
            video.addEventListener(ev[0], function() {
                var extra = '';
                if (ev[0] === 'loadedmetadata') {
                    extra = ' ' + video.videoWidth + 'x' + video.videoHeight;
                }
                log('EVENT: ' + ev[0] + extra, ev[1]);
            });
        });
        video.addEventListener('error', function() {
            var e = video.error;
            log('EVENT: error code=' + (e ? e.code : '?') +
                ' message=' + (e ? e.message : '?'), 'error');
        });
        video.addEventListener('seeking', function() {
            log('EVENT: seeking (t=' + video.currentTime.toFixed(1) + ')');
        });
        video.addEventListener('seeked', function() {
            log('EVENT: seeked (t=' + video.currentTime.toFixed(1) + ')', 'ok');
        });
        video.addEventListener('ratechange', function() {
            log('EVENT: ratechange (rate=' + video.playbackRate + ')', 'ok');
        });
        video.addEventListener('volumechange', function() {
            log('EVENT: volumechange (muted=' + video.muted +
                ' vol=' + video.volume.toFixed(2) + ')');
        });

    }


    // --- Stream info and HLS manifest parsing ---

    function updateStreamInfo(config) {

        if (document.getElementById('si-type')) {
            // Set type as HLS; encryption status will be updated by EME events
            document.getElementById('si-type').textContent = 'HLS';
            document.getElementById('si-url').textContent = config.url;
            document.getElementById('di-encrypted').textContent = 'No';
        }
        parseHlsManifest(config.url);
    }

    // Uses m3u8-parser (loaded via CDN) for robust HLS manifest parsing.
    function parseHlsManifest(url) {
        fetch(url).then(function(response) {
            if (!response.ok) {
                throw new Error(response.status + ' ' + response.statusText);
            }
            return response.text();
        }).then(function(text) {
            if (!window.m3u8Parser || !window.m3u8Parser.Parser) {
                log('m3u8Parser is not available', 'error');
                return;
            }
            var parser = new window.m3u8Parser.Parser();
            parser.push(text);
            parser.end();
            var manifest = parser.manifest;

            var playlists = manifest.playlists || [];
            if (playlists.length === 0) return;

            // Collect codecs from parsed playlists
            var vcodecs = {};
            var acodecs = {};
            for (var i = 0; i < playlists.length; i++) {
                var attrs = playlists[i].attributes || {};
                var codecs = attrs.CODECS || '';
                codecs.split(',').forEach(function(c) {
                    c = c.trim();
                    if (!c) return;
                    // Video codecs start with avc, hvc, hev, vp0, av01
                    if (c.indexOf('avc') === 0 || c.indexOf('hvc') === 0 ||
                        c.indexOf('hev') === 0 || c.indexOf('vp0') === 0 ||
                        c.indexOf('av01') === 0) {
                        vcodecs[c] = true;
                    } else {
                        acodecs[c] = true;
                    }
                });
            }

            document.getElementById('si-vcodec').textContent = Object.keys(vcodecs).join(', ') || 'unknown';
            document.getElementById('si-acodec').textContent = Object.keys(acodecs).join(', ') || 'unknown';

            // Log all variants with frame rate
            log('--- HLS Manifest (' + playlists.length + ' variants) ---');
            for (var j = 0; j < playlists.length; j++) {
                var a = playlists[j].attributes || {};
                var res = a.RESOLUTION ? a.RESOLUTION.width + 'x' + a.RESOLUTION.height : '?';
                var bw = a.BANDWIDTH ? Math.round(a.BANDWIDTH / 1000) + ' kbps' : '?';
                var fps = a['FRAME-RATE'] ? a['FRAME-RATE'] + 'fps' : '';
                log('  ' + res + ' @ ' + bw + (fps ? ' ' + fps : '') + ' [' + (a.CODECS || '?') + ']');
            }

            log('---');

            // Update variants summary
            var resolutions = playlists.map(function(p) {
                var r = p.attributes && p.attributes.RESOLUTION;
                return r ? r.width + 'x' + r.height : '?';
            });
            document.getElementById('si-variants').textContent =
                resolutions.join(', ') + ' (' + playlists.length + ' variants)';
        }).catch(function(err) {
            log('Manifest fetch failed: ' + err.message, 'warn');
        });
    }

    return {
        log: log,
        initControls: initControls,
        monitorEvents: monitorEvents,
        updateStreamInfo: updateStreamInfo
    };
})();
