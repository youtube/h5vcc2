// Copyright 2026 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

const video = document.getElementById('video');
const togglePipButton = document.getElementById('togglePipButton');
const logDiv = document.getElementById('log');

function log(msg) {
  console.log('JS: ' + msg);
  logDiv.textContent += msg + '\n';
}

log('Page loaded');
log('pictureInPictureEnabled in document: ' + ('pictureInPictureEnabled' in document));
log('document.pictureInPictureEnabled: ' + document.pictureInPictureEnabled);

function loadVideoViaMSE() {
  log('Starting MSE video load...');
  const mediaSource = new MediaSource();
  mediaSource.addEventListener('sourceopen', async () => {
    log('MediaSource open, adding SourceBuffer (vp9)');
    const sourceBuffer = mediaSource.addSourceBuffer('video/webm; codecs="vp9"');

    try {
      const fetchUrl = 'https://storage.googleapis.com/ytlr-cert.appspot.com/test-materials/media/big-buck-bunny-vp9-480p-30fps.webm';
      log('Fetching: ' + fetchUrl);
      const response = await fetch(fetchUrl);
      if (!response.ok) {
        throw new Error('Network response was not ok');
      }
      const data = await response.arrayBuffer();
      log('Fetched ' + data.byteLength + ' bytes, appending to SourceBuffer');

      sourceBuffer.addEventListener('updateend', () => {
        if (!sourceBuffer.updating && mediaSource.readyState === 'open') {
          mediaSource.endOfStream();
          log('MSE Stream ready. Attempting play...');
          video.play().catch(e => log('Play prevented: ' + e));
        }
      }, {once: true});
      sourceBuffer.appendBuffer(data);
    } catch (error) {
      log('Failed to fetch splash video: ' + error);
      console.error(error);
    }
  }, {once: true});
  video.src = URL.createObjectURL(mediaSource);
}

// Initialize the MSE video load
loadVideoViaMSE();

// --- PiP and Video Event Logic from test.html ---
video.addEventListener('play', () => log('Video started playing'));
video.addEventListener('pause', () => log('Video paused'));

// MSE videos do not automatically respect the 'loop' attribute.
video.addEventListener('ended', () => {
  log('Video ended. Looping back to start.');
  video.currentTime = 0;
  video.play();
});
video.addEventListener('enterpictureinpicture', (event) => {
  log('Entered Picture-in-Picture');
  log('pipWindow: ' + event.pictureInPictureWindow);
});
video.addEventListener('leavepictureinpicture', () => {
  log('Left Picture-in-Picture');
});

if ('pictureInPictureEnabled' in document) {
  togglePipButton.addEventListener('click', async function(event) {
    log('Button clicked');
    togglePipButton.disabled = true;
    try {
      if (video !== document.pictureInPictureElement) {
        log('Requesting Picture-in-Picture...');
        await video.requestPictureInPicture();
      } else {
        log('Exiting Picture-in-Picture...');
        await document.exitPictureInPicture();
      }
    } catch(error) {
      log('Error: ' + error.message);
      console.error(error);
    } finally {
      togglePipButton.disabled = false;
      // Refocus button after click
      togglePipButton.focus();
    }
  });
} else {
  log('Picture-in-Picture API is not supported in this browser.');
  togglePipButton.style.backgroundColor = 'grey';
  togglePipButton.textContent = 'PiP Not Supported';
}
