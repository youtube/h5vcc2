// Copyright 2017 The Cobalt Authors. All Rights Reserved.
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

package dev.cobalt.coat;

import static dev.cobalt.util.Log.TAG;

import android.app.Activity;
import android.content.Context;
import android.graphics.Bitmap;
import android.os.Handler;
import android.os.Looper;
import android.support.v4.media.MediaMetadataCompat;
import android.support.v4.media.session.MediaSessionCompat;
import android.support.v4.media.session.PlaybackStateCompat;
import androidx.annotation.Nullable;
import dev.cobalt.util.Holder;
import dev.cobalt.util.Log;
import java.util.List;
import java.util.Set;
import org.chromium.content_public.browser.MediaSession;
import org.chromium.content_public.browser.MediaSessionObserver;
import org.chromium.content_public.browser.WebContents;
import org.chromium.content_public.browser.WebContentsObserver;
import org.chromium.media_session.mojom.MediaSessionAction;
import org.chromium.services.media_session.MediaImage;
import org.chromium.services.media_session.MediaMetadata;
import org.chromium.services.media_session.MediaPosition;

/**
 * Cobalt MediaSession glue, as well as collection of state and logic to switch on/off Android OS
 * features used in media playback, such as audio focus, "KEEP_SCREEN_ON" mode, and "visible
 * behind".
 */
public class CobaltMediaSession implements ArtworkLoader.Callback {
  // We do handle transport controls and set this flag on all API levels, even though it's
  // deprecated and unnecessary on API 26+.
  @SuppressWarnings("deprecation")
  private static final int MEDIA_SESSION_FLAG_HANDLES_TRANSPORT_CONTROLS =
      MediaSessionCompat.FLAG_HANDLES_TRANSPORT_CONTROLS;

  private final Handler mMainHandler = new Handler(Looper.getMainLooper());
  private final Context mContext;
  private final ArtworkLoader mArtworkLoader;
  private final Holder<Activity> mActivityHolder;
  private WebContents mWebContents;
  private WebContentsObserver mWebContentsObserver;
  private MediaSessionCompat mMediaSession;
  private MediaSessionObserver mMediaSessionObserver;
  private boolean mIsControllable;
  private boolean mIsPaused;
  private MediaMetadata mMetadata;
  private Set<Integer> mActions;
  private MediaPosition mPosition;
  private Bitmap mArtworkImage;
  private final MediaSessionCompat.Callback mMediaSessionCallback;
  private LifecycleCallback mLifecycleCallback = null;

  // TODO: decouple LifecycleCallback and CobaltMediaSession implementation.
  /** LifecycleCallback to notify listeners when |mediaSession| becomes active or inactive. */
  public interface LifecycleCallback {
    void onMediaSessionLifecycle(boolean isActive, MediaSessionCompat.Token token);
  }

  public void setLifecycleCallback(LifecycleCallback lifecycleCallback) {
    mLifecycleCallback = lifecycleCallback;
    if (mLifecycleCallback != null && mMediaSession != null) {
      mLifecycleCallback.onMediaSessionLifecycle(true, mMediaSession.getSessionToken());
    }
  }

  public CobaltMediaSession(
      Context context, Holder<Activity> activityHolder, ArtworkDownloader artworkDownloader) {
    mContext = context;
    mActivityHolder = activityHolder;
    mArtworkLoader = new ArtworkLoader(this, artworkDownloader);
    mMediaSessionCallback =
        new MediaSessionCompat.Callback() {
          private void onReceiveAction(int action) {
            // To be cautious, explicitly run the code on main loop .
            mMainHandler.post(
                new Runnable() {
                  @Override
                  public void run() {
                    if (mMediaSessionObserver == null) {
                      return;
                    }
                    Log.i(TAG, "MediaSession didReceiveAction: %d", action);
                    mMediaSessionObserver.getMediaSession().didReceiveAction(action);
                  }
                });
          }

          @Override
          public void onPlay() {
            onReceiveAction(MediaSessionAction.PLAY);
          }

          @Override
          public void onPause() {
            onReceiveAction(MediaSessionAction.PAUSE);
          }

          @Override
          public void onSkipToPrevious() {
            onReceiveAction(MediaSessionAction.PREVIOUS_TRACK);
          }

          @Override
          public void onSkipToNext() {
            onReceiveAction(MediaSessionAction.NEXT_TRACK);
          }

          @Override
          public void onFastForward() {
            onReceiveAction(MediaSessionAction.SEEK_FORWARD);
          }

          @Override
          public void onRewind() {
            onReceiveAction(MediaSessionAction.SEEK_BACKWARD);
          }

          @Override
          public void onSeekTo(long pos) {
            // To be cautious, explicitly run the code on main loop .
            mMainHandler.post(
                new Runnable() {
                  @Override
                  public void run() {
                    if (mMediaSessionObserver == null) {
                      return;
                    }
                    mMediaSessionObserver.getMediaSession().seekTo(pos);
                  }
                });
          }

          @Override
          public void onStop() {
            onReceiveAction(MediaSessionAction.STOP);
          }
        };
  }

  public void setWebContents(WebContents webContents) {
    if (mWebContents == webContents) {
      return;
    }

    if (mWebContentsObserver != null) {
      mWebContentsObserver.observe(null);
      mWebContentsObserver = null;
    }

    if (webContents == null) {
      cleanupMediaSessionObserver();
      mWebContents = null;
      return;
    }

    mWebContents = webContents;
    mWebContentsObserver =
        new WebContentsObserver(mWebContents) {
          @Override
          public void mediaSessionCreated(MediaSession mediaSession) {
            setupMediaSessionObserver(mediaSession);
          }

          @Override
          public void webContentsDestroyed() {
            setWebContents(null);
          }
        };
    setupMediaSessionObserver(MediaSession.fromWebContents(mWebContents));
  }

  public void onActivityStop() {
    // A workaround to deactivate media session before the app enters background to avoid
    // b/424538093.
    deactivateMediaSession();
  }

  private void activateMediaSession() {
    mMediaSession = new MediaSessionCompat(mContext, TAG);
    mMediaSession.setFlags(MEDIA_SESSION_FLAG_HANDLES_TRANSPORT_CONTROLS);
    mMediaSession.setCallback(mMediaSessionCallback);
    mMediaSession.setActive(true);

    if (mLifecycleCallback != null) {
      mLifecycleCallback.onMediaSessionLifecycle(true, mMediaSession.getSessionToken());
    }

    Log.i(TAG, "MediaSession is activated.");
  }

  private void deactivateMediaSession() {
    if (mMediaSession == null) {
      return;
    }

    if (mLifecycleCallback != null) {
      mLifecycleCallback.onMediaSessionLifecycle(false, null);
    }

    mMediaSession.setCallback(null);
    mMediaSession.setActive(false);
    mMediaSession.release();
    mMediaSession = null;

    Log.i(TAG, "MediaSession has been deactivated.");
  }

  private MediaSessionObserver createMediaSessionObserver(MediaSession mediaSession) {
    return new MediaSessionObserver(mediaSession) {
      @Override
      public void mediaSessionDestroyed() {
        // We always keep a MediaSession, so nothing to destroy here.
        mMetadata = null;
        mActions = null;
        mPosition = null;
        mArtworkImage = null;
      }

      @Override
      public void mediaSessionStateChanged(boolean isControllable, boolean isPaused) {
        mIsControllable = isControllable;
        mIsPaused = isPaused;
        updatePlaybackState();
      }

      @Override
      public void mediaSessionMetadataChanged(MediaMetadata metadata) {
        mMetadata = metadata;
        updateMetadata();
      }

      @Override
      public void mediaSessionActionsChanged(Set<Integer> actions) {
        mActions = actions;
        updatePlaybackState();
      }

      @Override
      public void mediaSessionArtworkChanged(List<MediaImage> images) {
        if (!images.isEmpty()) {
          Bitmap bitmap = mArtworkLoader.getOrLoadArtwork(images);
          if (bitmap != null) {
            onArtworkLoaded(bitmap);
          }
        }
      }

      @Override
      public void mediaSessionPositionChanged(@Nullable MediaPosition position) {
        mPosition = position;
        updateMetadata();
        updatePlaybackState();
      }
    };
  }

  private void cleanupMediaSessionObserver() {
    if (mMediaSessionObserver == null) {
      return;
    }
    mMediaSessionObserver.stopObserving();
    mMediaSessionObserver = null;
  }

  private void setupMediaSessionObserver(MediaSession mediaSession) {
    if (mMediaSessionObserver != null && mMediaSessionObserver.getMediaSession() == mediaSession) {
      return;
    }

    cleanupMediaSessionObserver();
    if (mediaSession != null) {
      mMediaSessionObserver = createMediaSessionObserver(mediaSession);
    }
  }

  private void toggleKeepScreenOn(boolean keepScreenOn) {
    CobaltActivity activity = (CobaltActivity) mActivityHolder.get();
    if (activity != null) {
      activity.toggleKeepScreenOn(keepScreenOn);
    }
  }

  private void updatePlaybackState() {
    if (!mIsControllable) {
      deactivateMediaSession();
      toggleKeepScreenOn(false);
      return;
    }

    if (mIsControllable && mMediaSession == null) {
      activateMediaSession();
    }

    PlaybackStateCompat.Builder playbackStateBuilder =
        new PlaybackStateCompat.Builder().setActions(computeMediaSessionActions());

    int state = PlaybackStateCompat.STATE_NONE;
    if (mMetadata != null) {
      state = mIsPaused ? PlaybackStateCompat.STATE_PAUSED : PlaybackStateCompat.STATE_PLAYING;
    }

    toggleKeepScreenOn(state == PlaybackStateCompat.STATE_PLAYING);

    if (mPosition != null) {
      playbackStateBuilder.setState(
          state,
          mPosition.getPosition(),
          mPosition.getPlaybackRate(),
          mPosition.getLastUpdatedTime());
    } else {
      playbackStateBuilder.setState(state, PlaybackStateCompat.PLAYBACK_POSITION_UNKNOWN, 1.0f);
    }

    PlaybackStateCompat playbackState = playbackStateBuilder.build();
    mMediaSession.setPlaybackState(playbackState);

    Log.i(
        TAG,
        "MediaSession setPlaybackState: %d, actions: %d, position: %d ms, speed: %f x, last"
            + " updated: %d.",
        playbackState.getState(),
        playbackState.getActions(),
        playbackState.getPosition(),
        playbackState.getPlaybackSpeed(),
        playbackState.getLastPositionUpdateTime());
  }

  private void resetMetaData() {
    if (mMediaSession == null) {
      return;
    }

    MediaMetadataCompat.Builder metadataBuilder = new MediaMetadataCompat.Builder();
    mMediaSession.setMetadata(metadataBuilder.build());
  }

  private void updateMetadata() {
    if (mMediaSession == null) {
      return;
    }

    if (mMetadata == null) {
      resetMetaData();
      return;
    }

    MediaMetadataCompat.Builder metadataBuilder = new MediaMetadataCompat.Builder();
    metadataBuilder
        .putString(MediaMetadataCompat.METADATA_KEY_TITLE, mMetadata.getTitle())
        .putString(MediaMetadataCompat.METADATA_KEY_ARTIST, mMetadata.getArtist())
        .putString(MediaMetadataCompat.METADATA_KEY_ALBUM, mMetadata.getAlbum());
    if (mArtworkImage != null) {
      metadataBuilder.putBitmap(MediaMetadataCompat.METADATA_KEY_ART, mArtworkImage);
    }
    if (mPosition != null) {
      metadataBuilder.putLong(MediaMetadataCompat.METADATA_KEY_DURATION, mPosition.getDuration());
    }
    // Set mediaSession's metadata to update the "Now Playing Card".
    MediaMetadataCompat metadata = metadataBuilder.build();
    mMediaSession.setMetadata(metadata);

    Log.i(
        TAG,
        "MediaSession setMetadata title: %s, artist %s, album %s, duration %d.",
        metadata.getString(MediaMetadataCompat.METADATA_KEY_TITLE),
        metadata.getString(MediaMetadataCompat.METADATA_KEY_ARTIST),
        metadata.getString(MediaMetadataCompat.METADATA_KEY_ALBUM),
        metadata.getLong(MediaMetadataCompat.METADATA_KEY_DURATION));
  }

  private long computeMediaSessionActions() {
    long actions = PlaybackStateCompat.ACTION_PLAY | PlaybackStateCompat.ACTION_PAUSE;
    if (mActions != null) {
      if (mActions.contains(MediaSessionAction.PREVIOUS_TRACK)) {
        actions |= PlaybackStateCompat.ACTION_SKIP_TO_PREVIOUS;
      }
      if (mActions.contains(MediaSessionAction.NEXT_TRACK)) {
        actions |= PlaybackStateCompat.ACTION_SKIP_TO_NEXT;
      }
      if (mActions.contains(MediaSessionAction.SEEK_FORWARD)) {
        actions |= PlaybackStateCompat.ACTION_FAST_FORWARD;
      }
      if (mActions.contains(MediaSessionAction.SEEK_BACKWARD)) {
        actions |= PlaybackStateCompat.ACTION_REWIND;
      }
      if (mActions.contains(MediaSessionAction.SEEK_TO)) {
        actions |= PlaybackStateCompat.ACTION_SEEK_TO;
      }
    }
    return actions;
  }

  @Override
  public void onArtworkLoaded(Bitmap bitmap) {
    mArtworkImage = bitmap;
    // Reset the metadata to make sure the artwork update correctly.
    resetMetaData();
    updateMetadata();
  }
}
