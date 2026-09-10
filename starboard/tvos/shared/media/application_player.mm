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

#import "starboard/tvos/shared/media/application_player.h"

#import <MediaPlayer/MediaPlayer.h>

#include <limits>

#include "starboard/common/log.h"
#include "starboard/media.h"
#import "starboard/tvos/shared/defines.h"
#import "starboard/tvos/shared/media/application_drm_system.h"
#import "starboard/tvos/shared/media/player_manager.h"
#import "starboard/tvos/shared/starboard_application.h"

#ifdef COBALT_INTERNAL_BUILD
#include "cobalt/internal/starboard/shared/tvos/drm_system_fairplay_constants.h"
#endif  // COBALT_INTERNAL_BUILD

/**
 *  @brief The context used to track the player item through KVO.
 */
static const NSString* kPlayerItemStatusContext = @"kPlayerItemStatusContext";

/**
 *  @brief The context used to track the player rate through KVO.
 */
static const NSString* kPlayerRateContext = @"kPlayerRateContext";

/**
 *  @brief The context used to track the HDCP status through KVO.
 */
static const NSString*
    kPlayerOutputObscuredDueToInsufficientExternalProtectionContext =
        @"kPlayerOutputObscuredDueToInsufficientExternalProtectionContext";

/**
 *  @brief The key for a AVURLAsset tracks property.
 */
static NSString* const kTracksKey = @"tracks";

/**
 *  @brief The interval at which we update QoE stats from the player item's
 *      access log.
 */
static NSTimeInterval kAccessLogTimerInterval = 1;

/**
 *  @brief A @c UIView used to display an @c AVPlayer.
 *      See
 * https://developer.apple.com/documentation/avfoundation/avplayerlayer?language=objc
 */
@interface SBDPlayerView : UIView
@property(readonly) AVPlayerLayer* playerLayer;
@property AVPlayer* player;
@end

@implementation SBDPlayerView

+ (Class)layerClass {
  return [AVPlayerLayer class];
}

- (AVPlayerLayer*)playerLayer {
  return (AVPlayerLayer*)self.layer;
}

- (AVPlayer*)player {
  return self.playerLayer.player;
}

- (void)setPlayer:(AVPlayer*)player {
  self.playerLayer.player = player;
}

@end

@implementation SBDApplicationPlayer {
  /**
   *  @brief The @c AVPlayer which is responsible for controlling video
   *      playback.
   */
  AVPlayer* _player;

  /**
   *  @brief The view that will display the @c _player.
   */
  SBDPlayerView* _playerView;

  /**
   *  @brief The current state of the player.
   */
  SbPlayerState _playerState;

  /**
   *  @brief Specifies whether player callbacks should be invoked.
   */
  bool _callbacksEnabled;

  /**
   *  @brief If an error has occurred since the last player state change.
   */
  bool _errorOccurred;

  /**
   *  @brief Specifies whether playback should be paused.
   */
  bool _playerShouldPause;

  /**
   *  @brief The URL of the video asset that should be played.
   */
  NSURL* _url;

  /**
   *  @brief Context to be passed when invoking player callbacks.
   */
  void* _playerContext;

  /**
   *  @brief Callback that should be invoked on player status changes.
   */
  SbPlayerStatusFunc _playerStatusFunc;

  /**
   *  @brief Callback that should be invoked when the player encounters
   *      encrypted media.
   */
  SbPlayerEncryptedMediaInitDataEncounteredCB _encryptedMediaFunc;

  /**
   *  @brief Callback that should be invoked when an error occurs.
   */
  SbPlayerErrorFunc _playerErrorFunc;

  /**
   *  @brief Serial number to help associate callback invocations with
   *      operations on the player.
   */
  int _ticket;

  /**
   *  @brief The @c AVContentKeySession used to be notified when encrypted
   *      media is encountered.
   */
  AVContentKeySession* _keySession;

  /**
   *  @brief A list of @c AVContentKeyRequest instances awaiting a DRM system
   *      to be set on the player.
   */
  NSMutableArray<AVContentKeyRequest*>* _pendingKeyRequests;

  /**
   *  @brief The DRM system that is responsible for authorizing protected
   *      content.
   */
  SBDApplicationDrmSystem* _drmSystem;

  /**
   *  @brief The point in time where playback should start (in microseconds).
   */
  NSInteger _playbackStartTime;

  /**
   *  @brief The index of the last-processed event in the source's network
   *      access log.
   */
  NSUInteger _currentAccessLogIndex;

  /**
   *  @brief The number of dropped frames last recorded from the event at
   *      @c _currentAccessLogIndex.
   */
  NSUInteger _lastDroppedFrames;

  /**
   *  @brief The duration in seconds last recorded from the event at
   *      @c _currentAccessLogIndex.
   */
  NSUInteger _lastDurationWatched;

  /**
   *  @brief The timer that when fired updates QoE data from the player item's
   *      access log.
   */
  NSTimer* _accessLogTimer;

  /**
   *  @brief Indicates that the player has been stopped by Starboard. Stop must
   *      be called prior to deallocation.
   */
  BOOL _destroyCalled;

  /**
   *  @brief Indicates that the tracks have already been loaded for the player.
   */
  BOOL _loadedTracks;

  /**
   *  @brief Indicates that the output doesn't have hdcp protection.
   */
  bool _insufficientExternalProtection;
}

@synthesize totalDroppedFrames = _totalDroppedFrames;
@synthesize totalFrames = _totalFrames;
@synthesize frameWidth = _frameWidth;
@synthesize frameHeight = _frameHeight;

- (instancetype)initWithUrl:(NSURL*)url
              playerContext:(void*)playerContext
           playerStatusFunc:(SbPlayerStatusFunc)statusFunc
     encryptedMediaCallback:
         (SbPlayerEncryptedMediaInitDataEncounteredCB)encryptedMediaCallback
            playerErrorFunc:(SbPlayerErrorFunc)errorFunc {
  if (self) {
    _url = url;
    _playerContext = playerContext;
    _playerStatusFunc = statusFunc;
    _encryptedMediaFunc = encryptedMediaCallback;
    _playerErrorFunc = errorFunc;
    _ticket = SB_PLAYER_INITIAL_TICKET;
    _callbacksEnabled = true;
    _errorOccurred = false;
    _playerShouldPause = false;
    _insufficientExternalProtection = false;

    CGRect frame = [UIScreen mainScreen].bounds;
    _playerView = [[SBDPlayerView alloc] initWithFrame:frame];

    _pendingKeyRequests = [NSMutableArray array];

#if TARGET_OS_EMBEDDED
    _keySession = [AVContentKeySession
        contentKeySessionWithKeySystem:AVContentKeySystemFairPlayStreaming];
    [_keySession setDelegate:self
                       queue:dispatch_queue_create("keySessionQueue", NULL)];
#endif  // TARGET_OS_EMBEDDED

    [self updatePlayerState:kSbPlayerStateInitialized];

    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(applicationWillResignActive)
               name:UIApplicationWillResignActiveNotification
             object:nil];
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(applicationDidBecomeActive)
               name:UIApplicationDidBecomeActiveNotification
             object:nil];

    // It's possible for the application to become paused just before we've
    // registered for the notifications, so check the current application state
    // too.
    if (UIApplication.sharedApplication.applicationState !=
        UIApplicationStateActive) {
      _playerShouldPause = true;
    }
  }
  return self;
}

- (void)dealloc {
  SB_DCHECK(_destroyCalled) << "Player was not shutdown prior to deallocation!";
  [[NSNotificationCenter defaultCenter]
      removeObserver:self
                name:UIApplicationWillResignActiveNotification
              object:nil];
  [[NSNotificationCenter defaultCenter]
      removeObserver:self
                name:UIApplicationDidBecomeActiveNotification
              object:nil];
}

- (void)applicationWillResignActive {
  dispatch_async(dispatch_get_main_queue(), ^{
    // It's possible that the player has not been instantiated yet, so set a
    // flag to tell the player to pause as soon as possible.
    self->_playerShouldPause = true;
    // Only pause while presenting to keep things simple.
    if (self->_playerState == kSbPlayerStatePresenting) {
      [self->_player pause];
    }
  });
}

- (void)applicationDidBecomeActive {
  _playerShouldPause = false;
}

- (UIView*)view {
  return _playerView;
}

- (void)startPlaybackAtTime:(NSInteger)startTime {
  if (_loadedTracks) {
    return;
  }
  _loadedTracks = YES;
  _playbackStartTime = startTime;
  [self updatePlayerState:kSbPlayerStatePrerolling];

  AVURLAsset* URLAsset = [[AVURLAsset alloc] initWithURL:_url options:nil];
  [_keySession addContentKeyRecipient:URLAsset];
  [URLAsset
      loadValuesAsynchronouslyForKeys:@[ kTracksKey ]
                    completionHandler:^{
                      NSError* error;
                      AVKeyValueStatus status =
                          [URLAsset statusOfValueForKey:kTracksKey
                                                  error:&error];
                      if (status == AVKeyValueStatusLoaded) {
                        dispatch_async(dispatch_get_main_queue(), ^{
                          [self assetTracksLoadedForAsset:URLAsset];
                        });
                      } else {
                        [self
                            updatePlayerError:
                                (SbPlayerError)kSbUrlPlayerErrorSrcNotSupported
                                      message:@"AV key value is not loaded."];
                      }
                    }];
}

- (void)assetTracksLoadedForAsset:(AVURLAsset*)URLAsset {
  if (_destroyCalled) {
    return;
  }
  AVPlayerItem* playerItem = [AVPlayerItem playerItemWithAsset:URLAsset];
  [playerItem addObserver:self
               forKeyPath:@"status"
                  options:0
                  context:&kPlayerItemStatusContext];

  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(playerItemDidReachEnd:)
             name:AVPlayerItemDidPlayToEndTimeNotification
           object:playerItem];

  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(playbackStalled:)
             name:AVPlayerItemPlaybackStalledNotification
           object:playerItem];

  _player = [AVPlayer playerWithPlayerItem:playerItem];

  [_player addObserver:self
            forKeyPath:@"rate"
               options:0
               context:&kPlayerRateContext];
  [_player
      addObserver:self
       forKeyPath:@"outputObscuredDueToInsufficientExternalProtection"
          options:0
          context:
              &kPlayerOutputObscuredDueToInsufficientExternalProtectionContext];

  _playerView.player = _player;
}

- (void)playbackStalled:(NSNotification*)notification {
  [self updatePlayerState:kSbPlayerStatePrerolling];
}

- (void)playerItemDidReachEnd:(NSNotification*)notification {
  [self updatePlayerState:kSbPlayerStateEndOfStream];
}

- (void)playerItemStatusDidChange {
  switch (_player.currentItem.status) {
    case AVPlayerItemStatusReadyToPlay: {
      dispatch_async(dispatch_get_main_queue(), ^{
        if (self->_playbackStartTime) {
          self.currentMediaTime = self->_playbackStartTime;
          self->_playbackStartTime = 0;
        } else {
          [self updatePlayerState:kSbPlayerStatePresenting];
        }
      });
      return;
    }
    case AVPlayerItemStatusUnknown: {
      dispatch_async(dispatch_get_main_queue(), ^{
        [self updatePlayerError:kSbPlayerErrorDecode
                        message:@"AV key item status unknown."];
      });
      return;
    }
    case AVPlayerItemStatusFailed: {
      NSError* error = _player.currentItem.error;
      NSString* errorMessage = [NSString stringWithFormat:@"%@", error];
      dispatch_async(dispatch_get_main_queue(), ^{
        [self updatePlayerError:kSbPlayerErrorDecode message:errorMessage];
      });
      return;
    }
  }
}

- (void)startAccessLogTimer {
  if (!_accessLogTimer) {
    _accessLogTimer =
        [NSTimer scheduledTimerWithTimeInterval:kAccessLogTimerInterval
                                         target:self
                                       selector:@selector(updatePlayerInfo)
                                       userInfo:nil
                                        repeats:YES];
    [_accessLogTimer fire];
  }
}

- (void)stopAccessLogTimer {
  [_accessLogTimer invalidate];
  _accessLogTimer = nil;
}

- (void)observeValueForKeyPath:(NSString*)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id>*)change
                       context:(void*)context {
  if (context == &kPlayerItemStatusContext) {
    [self playerItemStatusDidChange];
    return;
  } else if (context == &kPlayerRateContext) {
    __weak AVPlayer* player = _player;
    dispatch_async(dispatch_get_main_queue(), ^{
      if (!player) {
        return;
      }
      if (player.rate <= 0) {
        [self stopAccessLogTimer];
      } else {
        [self startAccessLogTimer];
      }
    });
    return;
  } else if (context ==
             &kPlayerOutputObscuredDueToInsufficientExternalProtectionContext) {
    _insufficientExternalProtection =
        _player.outputObscuredDueToInsufficientExternalProtection;
    if (_insufficientExternalProtection) {
      // Wait for 5s and check the hdcp status again. Apple TV may report
      // insufficient external protection right after wakes up the device and
      // recover in 1-2 seconds. Also, when Apple TV loses the connection to TV,
      // it will report insufficient external protection immediately, but the
      // app will enter inactive state in 1-2 seconds, it's not necessary to
      // report an error, as AVPlayer can recover itself after the connection is
      // resumed.
      const int64_t kInsufficientExternalProtectionTimeout = 5.0 * NSEC_PER_SEC;
      __weak SBDApplicationPlayer* application_player = self;
      __weak AVPlayer* player = _player;
      dispatch_after(
          dispatch_time(DISPATCH_TIME_NOW,
                        kInsufficientExternalProtectionTimeout),
          dispatch_get_main_queue(), ^{
            if (!application_player || !player ||
                UIApplication.sharedApplication.applicationState !=
                    UIApplicationStateActive) {
              return;
            }
            if (player.outputObscuredDueToInsufficientExternalProtection) {
              [application_player
                  updatePlayerError:kSbPlayerErrorDecode
                            message:@"Output obscured due to HDCP"];
            }
          });
    }
    return;
  }

  [super observeValueForKeyPath:keyPath
                       ofObject:object
                         change:change
                        context:context];
}

- (void)setBoundsX:(NSInteger)x
                 y:(NSInteger)y
             width:(NSInteger)width
            height:(NSInteger)height {
  dispatch_async(dispatch_get_main_queue(), ^{
    CGFloat scale = [UIScreen mainScreen].scale;
    CGRect frame =
        CGRectMake(x / scale, y / scale, width / scale, height / scale);
    [self->_playerView setFrame:frame];
  });
}

- (void)setZIndex:(NSInteger)zIndex {
  // NOOP
}

- (BOOL)paused {
  return _player.timeControlStatus == AVPlayerTimeControlStatusPaused;
}

- (void)setPaused:(BOOL)paused {
  if (paused) {
    [_player pause];
    return;
  }
  [_player play];
}

- (double)playbackRate {
  return _player.rate;
}

- (void)setPlaybackRate:(double)playbackRate {
  if (_player.rate == playbackRate) {
    return;
  }
  _player.rate = playbackRate;
}

- (NSInteger)totalDroppedFrames {
  return _totalDroppedFrames;
}

- (double)volume {
  return _player.volume;
}

- (void)setVolume:(double)volume {
  _player.volume = volume;
}

- (NSInteger)currentMediaTime {
  if (!_player) {
    return 0;
  }
  CMTime currentTime = _player.currentTime;
  if (!currentTime.timescale) {
    return -1;
  }
  float time = (float)currentTime.value / currentTime.timescale;
  return time * 1000000;
}

- (NSRange)bufferRange {
  if (!_player) {
    return NSMakeRange(0, 0);
  }

  NSValue* loadedTimeRangeValue =
      _player.currentItem.loadedTimeRanges.lastObject;
  CMTimeRange loadedTimeRange = loadedTimeRangeValue.CMTimeRangeValue;

  CMTime bufferStart = loadedTimeRange.start;
  if (bufferStart.timescale == 0) {
    return NSMakeRange(0, 0);
  }
  float startSeconds = (float)bufferStart.value / bufferStart.timescale;
  float startTimestamp = startSeconds * 1000000;

  CMTime bufferDuration = loadedTimeRange.duration;
  if (bufferDuration.timescale == 0) {
    return NSMakeRange(0, 0);
  }
  float durationSeconds =
      (float)bufferDuration.value / bufferDuration.timescale;
  float durationTimestamp = durationSeconds * 1000000;

  return NSMakeRange(startTimestamp, durationTimestamp);
}

- (void)setCurrentMediaTime:(NSInteger)currentMediaTime {
  if (!_player || _player.status != AVPlayerStatusReadyToPlay) {
    [self startPlaybackAtTime:currentMediaTime];
    return;
  }
  [self updatePlayerState:kSbPlayerStatePrerolling];
  __weak SBDApplicationPlayer* weakSelf = self;
  [_player seekToTime:CMTimeMake(currentMediaTime, 1000000)
        toleranceBefore:kCMTimeZero
         toleranceAfter:kCMTimeZero
      completionHandler:^(BOOL finished) {
        SBDApplicationPlayer* strongSelf = weakSelf;
        if (!strongSelf) {
          return;
        }
        [strongSelf updatePlayerState:kSbPlayerStatePresenting];
      }];
}

- (void)seekTo:(NSInteger)time ticket:(int)ticket {
  dispatch_async(dispatch_get_main_queue(), ^{
    self->_ticket = ticket;
    self.currentMediaTime = time;
  });
}

- (NSTimeInterval)currentDate {
  return _player.currentItem.currentDate.timeIntervalSince1970;
}

- (NSInteger)duration {
  if (!_player) {
    return SB_PLAYER_NO_DURATION;
  }
  if ((_playerState != kSbPlayerStatePresenting &&
       _playerState != kSbPlayerStateEndOfStream) ||
      _errorOccurred) {
    return SB_PLAYER_NO_DURATION;
  }
  CMTime endTime = _player.currentItem.duration;
  if (CMTIME_IS_INDEFINITE(endTime)) {
    return NSIntegerMax;
  }
  if (!endTime.timescale) {
    return 0;
  }
  float timeSeconds = (float)endTime.value / endTime.timescale;
  return timeSeconds * 1000000;
}

- (void)disableCallbacks {
  @synchronized(self) {
    _callbacksEnabled = false;
  }
}

- (void)destroy {
  _destroyCalled = YES;
  [self stopAccessLogTimer];
  [self removePlayerNotifications];
  [self shutdownPlayer];
}

- (HdcpProtectionState)hdcpState {
  if (_insufficientExternalProtection) {
    return kHdcpProtectionStateUnauthenticated;
  }
  if (_player && _drmSystem && _player.currentItem &&
      _player.currentItem.status == AVPlayerItemStatusReadyToPlay) {
    return kHdcpProtectionStateAuthenticated;
  }
  return kHdcpProtectionStateUnknown;
}

- (void)updatePlayerState:(SbPlayerState)state {
  if (_destroyCalled) {
    return;
  }
  _playerState = state;
  _errorOccurred = false;
  if (state == kSbPlayerStatePresenting) {
    CGSize naturalSize = _player.currentItem.presentationSize;
    _frameWidth = naturalSize.width;
    _frameHeight = naturalSize.height;
  }

  @synchronized(self) {
    if (_callbacksEnabled) {
      SBDPlayerManager* playerManager = SBDGetApplication().playerManager;
      SbPlayer starboardPlayer =
          [playerManager starboardPlayerForApplicationPlayer:self];
      _playerStatusFunc(starboardPlayer, _playerContext, _playerState, _ticket);
    }
  }

  // Only pause while presenting to keep things simple.
  if (_playerShouldPause && state == kSbPlayerStatePresenting) {
    [_player pause];
  }
}

- (void)updatePlayerError:(SbPlayerError)error message:(NSString*)message {
  if (_destroyCalled || _errorOccurred) {
    return;
  }
  _errorOccurred = true;

  @synchronized(self) {
    if (_callbacksEnabled) {
      SBDPlayerManager* playerManager = SBDGetApplication().playerManager;
      SbPlayer starboardPlayer =
          [playerManager starboardPlayerForApplicationPlayer:self];

      NSUInteger stringLength =
          [message lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
      char* str = static_cast<char*>(calloc(stringLength + 1, sizeof(char)));
      [message getCString:str
                maxLength:stringLength + 1
                 encoding:NSUTF8StringEncoding];

      _playerErrorFunc(starboardPlayer, _playerContext, error, str);
      free(str);
    }
  }
}

- (SBDApplicationDrmSystem*)drmSystem {
  return _drmSystem;
}

- (void)processKeyRequest:(AVContentKeyRequest*)keyRequest {
  if ([_drmSystem processKeyRequest:keyRequest]) {
    @synchronized(self) {
      if (_callbacksEnabled) {
        SBDPlayerManager* playerManager = SBDGetApplication().playerManager;
        SbPlayer starboardPlayer =
            [playerManager starboardPlayerForApplicationPlayer:self];
        NSString* urlString = keyRequest.identifier;
        NSData* urlStringData =
            [urlString dataUsingEncoding:NSUTF16LittleEndianStringEncoding];
        uint32_t urlStringDataLength = urlStringData.length;
        NSMutableData* initData =
            [NSMutableData dataWithBytes:&urlStringDataLength
                                  length:sizeof(urlStringDataLength)];
        [initData appendData:urlStringData];
        _encryptedMediaFunc(starboardPlayer, _playerContext,
                            starboard::kPlatformDrmInitDataType,
                            static_cast<const unsigned char*>(initData.bytes),
                            initData.length);
      }
    }
  }
}

- (void)setDrmSystem:(SBDApplicationDrmSystem*)drmSystem {
  _drmSystem = drmSystem;
  _drmSystem.keySession = _keySession;

  // Process any queued key requests.
  @synchronized(_pendingKeyRequests) {
    for (AVContentKeyRequest* keyRequest in _pendingKeyRequests) {
      dispatch_async(_keySession.delegateQueue, ^{
        [self processKeyRequest:keyRequest];
      });
    }
    [_pendingKeyRequests removeAllObjects];
  }
}

- (void)shutdownPlayer {
  _player.rate = 0;
  [_player replaceCurrentItemWithPlayerItem:nil];
  _player = nil;
  [_playerView removeFromSuperview];
}

- (void)removePlayerNotifications {
  [_player removeObserver:self forKeyPath:@"rate"];
  [_player removeObserver:self
               forKeyPath:@"outputObscuredDueToInsufficientExternalProtection"];
  [_player.currentItem removeObserver:self forKeyPath:@"status"];
  [[NSNotificationCenter defaultCenter]
      removeObserver:self
                name:AVPlayerItemDidPlayToEndTimeNotification
              object:_player.currentItem];
  [[NSNotificationCenter defaultCenter]
      removeObserver:self
                name:AVPlayerItemPlaybackStalledNotification
              object:_player.currentItem];
}

#pragma mark - AVContentKeySessionDelegate

- (void)contentKeySession:(AVContentKeySession*)session
    didProvideContentKeyRequest:(AVContentKeyRequest*)keyRequest {
  if (session == _keySession) {
    if (_drmSystem) {
      // Process the key request right away.
      [self processKeyRequest:keyRequest];
    } else {
      // Queue the key request until a DRM system is attached.
      @synchronized(_pendingKeyRequests) {
        [_pendingKeyRequests addObject:keyRequest];
      }
    }
  }
}

#pragma mark - QoE

- (void)updatePlayerInfo {
  AVPlayerItemAccessLog* accessLog = _player.currentItem.accessLog;
  NSArray<AVPlayerItemAccessLogEvent*>* events = accessLog.events;
  if (!events.count) {
    return;
  }

  float frameRate = 0;
  NSArray<AVAssetTrack*>* videoTracks =
      [_player.currentItem.asset tracksWithMediaType:AVMediaTypeVideo];
  for (AVAssetTrack* track in videoTracks) {
    if (track.nominalFrameRate > 0) {
      frameRate = track.nominalFrameRate;
      break;
    }
  }
  // TODO: For some reason, it is not always possible to get the frame rate. If
  // we do not receive one, default to a frame rate of 25 fps. This should be
  // fixed if possible.
  frameRate = frameRate ?: 25;

  NSInteger newDroppedFrames = 0;
  NSInteger newTotalFrames = 0;
  // The access log is a bit weird. It doesn't just append events to the log,
  // it actually modifies the last event in the log. It only appends events
  // under certain situations like format switches. Because of this, we need to
  // save values computed from the last event to compute incremental changes on
  // the next poll.
  for (NSUInteger i = _currentAccessLogIndex; i < events.count; i++) {
    AVPlayerItemAccessLogEvent* event = events[i];

    NSUInteger durationWatched = MAX(event.durationWatched, 0);
    NSUInteger droppedFrames = MAX(event.numberOfDroppedVideoFrames, 0);

    if (i == _currentAccessLogIndex) {
      // We must account for stats accumulated on the previous observation.
      if (droppedFrames >= _lastDroppedFrames ||
          durationWatched >= _lastDurationWatched) {
        droppedFrames -= _lastDroppedFrames;
        durationWatched -= _lastDurationWatched;
      } else {
        droppedFrames = 0;
        durationWatched = 0;
      }
    }
    _currentAccessLogIndex = i;

    _lastDroppedFrames = MAX(event.numberOfDroppedVideoFrames, 0);
    newDroppedFrames += droppedFrames;

    _lastDurationWatched = MAX(event.durationWatched, 0);
    newTotalFrames += durationWatched * frameRate;
  }
  _totalDroppedFrames += newDroppedFrames;
  _totalFrames += newTotalFrames;

  CGSize naturalSize = _player.currentItem.presentationSize;
  _frameWidth = naturalSize.width;
  _frameHeight = naturalSize.height;
}

@end
