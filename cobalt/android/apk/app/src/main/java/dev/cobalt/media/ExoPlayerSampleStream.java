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
import androidx.media3.common.MimeTypes;
import androidx.media3.decoder.DecoderInputBuffer;
import androidx.media3.exoplayer.FormatHolder;
import androidx.media3.exoplayer.source.SampleStream;
import java.io.IOException;

public class ExoPlayerSampleStream implements SampleStream {
  // Custom signal from native indicating the buffer requires allocation before reading data.
  private static final int RESULT_NEEDS_ALLOCATION = -6;

  private final Format mFormat;
  private final int mType;
  private final ExoPlayerBridge mBridge;
  private boolean mFormatSent = false;
  private final long[] mMetadata = new long[3];

  ExoPlayerSampleStream(Format format, ExoPlayerBridge bridge) {
    mFormat = format;
    mBridge = bridge;
    if (MimeTypes.isVideo(format.sampleMimeType)) {
      mType = ExoPlayerBridge.TYPE_VIDEO;
    } else {
      mType = ExoPlayerBridge.TYPE_AUDIO;
    }
  }

  @Override
  public boolean isReady() {
    return mBridge.isReady(mType);
  }

  @Override
  public void maybeThrowError() throws IOException {}

  @Override
  public int readData(
      @NonNull FormatHolder formatHolder, @NonNull DecoderInputBuffer buffer, int readFlags) {
    if (!mFormatSent || (readFlags & SampleStream.FLAG_REQUIRE_FORMAT) != 0) {
      formatHolder.format = mFormat;
      mFormatSent = true;
      return C.RESULT_FORMAT_READ;
    }

    int result = mBridge.readSample(mType, buffer.data, mMetadata);

    if (result == RESULT_NEEDS_ALLOCATION) {
      int size = (int) mMetadata[1];
      buffer.ensureSpaceForWrite(size);
      // Now that it is allocated, read again
      result = mBridge.readSample(mType, buffer.data, mMetadata);
    }

    if (result == C.RESULT_BUFFER_READ) {
      buffer.timeUs = mMetadata[0];
      int size = (int) mMetadata[1];
      int flags = (int) mMetadata[2];

      if (buffer.data != null) {
        // Set position to size so that when ExoPlayer calls buffer.flip(),
        // the limit becomes size and position becomes 0.
        buffer.data.position(size);
      }

      buffer.setFlags(flags);
    }

    return result;
  }

  @Override
  public int skipData(long positionUs) {
    return mBridge.skipData(mType, positionUs);
  }

  public long getBufferedPositionUs() {
    return mBridge.getBufferedPositionUs(mType);
  }
}
