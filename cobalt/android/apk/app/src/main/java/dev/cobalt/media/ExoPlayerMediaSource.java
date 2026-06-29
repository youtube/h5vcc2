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

import androidx.annotation.Nullable;
import androidx.media3.common.C;
import androidx.media3.common.Format;
import androidx.media3.common.MediaItem;
import androidx.media3.common.MediaMetadata;
import androidx.media3.datasource.TransferListener;
import androidx.media3.exoplayer.source.BaseMediaSource;
import androidx.media3.exoplayer.source.MediaPeriod;
import androidx.media3.exoplayer.source.SinglePeriodTimeline;
import androidx.media3.exoplayer.upstream.Allocator;
import java.io.IOException;
import javax.annotation.concurrent.GuardedBy;

/**
 * A custom {@link BaseMediaSource} that receives encoded media data from the native Starboard layer
 * and provides it to ExoPlayer.
 *
 * This source is designed for a single-period lifecycle, mapping to a single audio or video
 * stream provided by the native application.
 */
public final class ExoPlayerMediaSource extends BaseMediaSource {
  private final Format mFormat;
  private final Object mLock = new Object();
  private final ExoPlayerBridge mBridge;

  @GuardedBy("mLock")
  private ExoPlayerMediaPeriod mMediaPeriod;

  private final MediaItem mMediaItem;

  ExoPlayerMediaSource(Format format, ExoPlayerBridge bridge) {
    mFormat = format;
    mBridge = bridge;
    mMediaItem = new MediaItem.Builder().setMediaMetadata(MediaMetadata.EMPTY).build();
  }

  public Format getFormat() {
    return mFormat;
  }

  @Override
  protected void prepareSourceInternal(@Nullable TransferListener mediaTransferListener) {
    refreshSourceInfo(
        new SinglePeriodTimeline(
            /* durationUs= */ C.TIME_UNSET,
            /* isSeekable= */ true,
            /* isDynamic= */ false,
            /* useLiveConfiguration= */ false,
            /* manifest= */ null,
            getMediaItem()));
  }

  @Override
  protected void releaseSourceInternal() {}

  @Override
  public MediaItem getMediaItem() {
    return mMediaItem;
  }

  @Override
  public void maybeThrowSourceInfoRefreshError() throws IOException {}

  @Override
  public MediaPeriod createPeriod(MediaPeriodId id, Allocator allocator, long startPositionUs) {
    synchronized (mLock) {
      if (mMediaPeriod == null) {
        mMediaPeriod = new ExoPlayerMediaPeriod(mFormat, mBridge);
        return mMediaPeriod;
      }
    }
    throw new IllegalStateException(
        "Called MediaSource.createPeriod when the MediaPeriod already exists");
  }

  @Override
  public void releasePeriod(MediaPeriod mediaPeriod) {
    synchronized (mLock) {
      if (mMediaPeriod != null) {
        if (mediaPeriod != mMediaPeriod) {
            throw new IllegalStateException(
                "Called MediaSource.releasePeriod on an unknown MediaPeriod");
        }
        mMediaPeriod = null;
        return;
      }
    }
    throw new IllegalStateException(
        "Called MediaSource.releasePeriod() after period was already released");
  }
}
