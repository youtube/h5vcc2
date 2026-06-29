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

package dev.cobalt.media;

import androidx.annotation.NonNull;
import androidx.media3.common.C;
import androidx.media3.common.Format;
import androidx.media3.common.TrackGroup;
import androidx.media3.exoplayer.LoadingInfo;
import androidx.media3.exoplayer.SeekParameters;
import androidx.media3.exoplayer.source.MediaPeriod;
import androidx.media3.exoplayer.source.SampleStream;
import androidx.media3.exoplayer.source.TrackGroupArray;
import androidx.media3.exoplayer.trackselection.ExoTrackSelection;
import java.io.IOException;

/** Implements the ExoPlayer MediaPeriod interface to write samples to the ExoPlayerSampleStream. */
public class ExoPlayerMediaPeriod implements MediaPeriod {
  private final Format mFormat;
  private final ExoPlayerBridge mBridge;
  private ExoPlayerSampleStream mStream;

  ExoPlayerMediaPeriod(Format format, ExoPlayerBridge bridge) {
    mFormat = format;
    mBridge = bridge;
  }

  @Override
  public void prepare(Callback callback, long positionUs) {
    callback.onPrepared(this);
  }

  @Override
  public void maybeThrowPrepareError() throws IOException {}

  @Override
  public TrackGroupArray getTrackGroups() {
    return new TrackGroupArray(new TrackGroup(mFormat));
  }

  @Override
  public long selectTracks(
      ExoTrackSelection[] selections,
      boolean[] mayRetainStreamFlags,
      SampleStream[] streams,
      boolean[] streamResetFlags,
      long positionUs) {

    // First pass: Release and null out existing streams if their track is deselected or if
    // ExoPlayer indicates the stream cannot be retained. This prevents using stale stream
    // references and ensures the second pass creates a fresh SampleStream and sets streamResetFlags
    // appropriately.
    for (int i = 0; i < selections.length; ++i) {
      if (streams[i] != null && (selections[i] == null || !mayRetainStreamFlags[i])) {
        if (streams[i] == mStream) {
            mStream = null;
        }
        streams[i] = null;
      }
    }

    for (int i = 0; i < selections.length; ++i) {
      if (streams[i] == null && selections[i] != null) {
        mStream = new ExoPlayerSampleStream(selections[i].getSelectedFormat(), mBridge);
        streams[i] = mStream;
        streamResetFlags[i] = true;
      }
    }
    return positionUs;
  }

  @Override
  public void discardBuffer(long positionUs, boolean toKeyframe) {}

  @Override
  public long readDiscontinuity() {
    return C.TIME_UNSET;
  }

  @Override
  public long seekToUs(long positionUs) {
    return positionUs;
  }

  @Override
  public long getAdjustedSeekPositionUs(long positionUs, SeekParameters seekParameters) {
    return positionUs;
  }

  @Override
  public long getBufferedPositionUs() {
    return mStream != null ? mStream.getBufferedPositionUs() : 0L;
  }

  @Override
  public long getNextLoadPositionUs() {
    return getBufferedPositionUs();
  }

  @Override
  public boolean continueLoading(@NonNull LoadingInfo loadingInfo) {
    return true;
  }

  @Override
  public boolean isLoading() {
    return getBufferedPositionUs() != C.TIME_END_OF_SOURCE;
  }

  @Override
  public void reevaluateBuffer(long positionUs) {}
}
