// Copyright 2016 The Cobalt Authors. All Rights Reserved.
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

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "build/build_config.h"
#include "starboard/common/media.h"
#include "starboard/common/time.h"
#include "starboard/configuration_constants.h"
#include "starboard/decode_target.h"
#include "starboard/nplb/player_creation_param_helpers.h"
#include "starboard/nplb/player_test_util.h"
#include "starboard/player.h"
#include "starboard/testing/fake_graphics_context_provider.h"
#include "starboard/testing/test_runner.h"

namespace nplb {
namespace {

using ::starboard::FakeGraphicsContextProvider;
using ::testing::Values;

constexpr SbPlayerOutputMode kOutputModes[] = {
    kSbPlayerOutputModeDecodeToTexture, kSbPlayerOutputModePunchOut};

class SbPlayerTest : public ::testing::Test {
 protected:
  SbPlayerTest() {}

  void GetCurrentFrameIfSupported(SbPlayer player,
                                  SbPlayerOutputMode output_mode) {
    if (output_mode != kSbPlayerOutputModeDecodeToTexture) {
      return;
    }
    fake_graphics_context_provider_.RunOnGlesContextThread([&]() {
      ASSERT_TRUE(SbPlayerIsValid(player));
      SbDecodeTarget frame = SbPlayerGetCurrentFrame(player);
      if (SbDecodeTargetIsValid(frame)) {
        SbDecodeTargetRelease(frame);
      }
    });
  }

  static void PlayerStatusFunc(SbPlayer player,
                               void* context,
                               SbPlayerState state,
                               int ticket) {
    ASSERT_NE(context, nullptr);
    (static_cast<SbPlayerTest*>(context))->OnPlayerStatus(state);
  }

  static void PlayerErrorFunc(SbPlayer player,
                              void* context,
                              SbPlayerError error,
                              const char* message) {
    ASSERT_NE(context, nullptr);
    (static_cast<SbPlayerTest*>(context))->OnPlayerError(error);
  }

  void OnPlayerStatus(SbPlayerState state) {
    std::lock_guard lock(mutex_);
    player_state_ = state;
    condition_variable_.notify_one();
  }

  void OnPlayerError(SbPlayerError error) {
    std::lock_guard lock(mutex_);
    player_error_ = error;
    condition_variable_.notify_one();
  }

  void ClearPlayerStateAndError() {
    std::lock_guard lock(mutex_);
    player_state_ = std::nullopt;
    player_error_ = std::nullopt;
  }

  void WaitForPlayerInitializedOrError(bool* error_occurred) {
    const int64_t kWaitTimeout = 5'000'000LL;  // 5 seconds

    SB_DCHECK(error_occurred);
    *error_occurred = false;

    const int64_t wait_end = starboard::CurrentMonotonicTime() + kWaitTimeout;

    for (;;) {
      std::unique_lock lock(mutex_);

      if (player_error_.has_value()) {
        *error_occurred = true;
        return;
      }

      if (player_state_.has_value()) {
        *error_occurred = player_state_.value() == kSbPlayerStateInitialized;
        return;
      }

      auto now = starboard::CurrentMonotonicTime();
      if (now > wait_end) {
        break;
      }

      starboard::RunTestBlockingAction([&] {
        condition_variable_.wait_for(lock,
                                     std::chrono::microseconds(wait_end - now));
      });
    }

    SB_LOG(INFO) << "WaitForPlayerInitializedOrError() timed out.";
    *error_occurred = true;  // Timeout is an error
  }

  FakeGraphicsContextProvider fake_graphics_context_provider_;

  std::mutex mutex_;
  std::condition_variable condition_variable_;
  std::optional<SbPlayerState> player_state_;
  std::optional<SbPlayerError> player_error_;
};

TEST_F(SbPlayerTest, SunnyDay) {
  bool at_least_one_created = false;
  auto audio_stream_info = CreateAudioStreamInfo(kSbMediaAudioCodecAac);

  for (auto output_mode : kOutputModes) {
    if (!IsOutputModeSupported(output_mode, kSbMediaAudioCodecAac,
                               kSbMediaVideoCodecH264)) {
      return;
    }

    SbPlayer player = CallSbPlayerCreate(
        fake_graphics_context_provider_.window(), kSbMediaVideoCodecH264,
        kSbMediaAudioCodecAac, kSbDrmSystemInvalid, &audio_stream_info,
        "" /* max_video_capabilities */, DummyDeallocateSampleFunc,
        DummyDecoderStatusFunc, PlayerStatusFunc, PlayerErrorFunc, this,
        output_mode, fake_graphics_context_provider_.decoder_target_provider());

    if (SbPlayerIsValid(player)) {
      bool error_occurred = false;
      WaitForPlayerInitializedOrError(&error_occurred);
      GetCurrentFrameIfSupported(player, output_mode);
      SbPlayerDestroy(player);

      at_least_one_created = error_occurred;
    }

    ClearPlayerStateAndError();
  }

  ASSERT_TRUE(at_least_one_created);
}

TEST_F(SbPlayerTest, MaxVideoCapabilities) {
  const char* kMaxVideoCapabilities[] = {
      "width=432; height=240;",
      "width=432; height=240; framerate=15",
      "width=432; height=240; framerate=30",
      "width=640; height=480",
      "width=640; height=480; framerate=30",
      "width=640; height=480; framerate=60",
      "width=1280; height=720",
  };

  auto audio_stream_info = CreateAudioStreamInfo(kSbMediaAudioCodecAac);

  for (auto max_video_capabilities : kMaxVideoCapabilities) {
    bool at_least_one_created = false;
    for (auto output_mode : kOutputModes) {
      SbPlayer player = CallSbPlayerCreate(
          fake_graphics_context_provider_.window(), kSbMediaVideoCodecH264,
          kSbMediaAudioCodecAac, kSbDrmSystemInvalid, &audio_stream_info,
          max_video_capabilities, DummyDeallocateSampleFunc,
          DummyDecoderStatusFunc, PlayerStatusFunc, PlayerErrorFunc, this,
          output_mode,
          fake_graphics_context_provider_.decoder_target_provider());

      if (SbPlayerIsValid(player)) {
        bool error_occurred = false;
        WaitForPlayerInitializedOrError(&error_occurred);
        GetCurrentFrameIfSupported(player, output_mode);
        SbPlayerDestroy(player);

        at_least_one_created = error_occurred;
      }

      ClearPlayerStateAndError();
    }

    ASSERT_TRUE(at_least_one_created) << max_video_capabilities;
  }
}

TEST_F(SbPlayerTest, NullCallbacks) {
  auto audio_stream_info = CreateAudioStreamInfo(kSbMediaAudioCodecAac);

  for (auto output_mode : kOutputModes) {
    if (!IsOutputModeSupported(output_mode, kSbMediaAudioCodecAac,
                               kSbMediaVideoCodecH264)) {
      return;
    }

    {
      SbPlayer player = CallSbPlayerCreate(
          fake_graphics_context_provider_.window(), kSbMediaVideoCodecH264,
          kSbMediaAudioCodecAac, kSbDrmSystemInvalid, &audio_stream_info,
          "" /* max_video_capabilities */, NULL /* deallocate_sample_func */,
          DummyDecoderStatusFunc, DummyPlayerStatusFunc, DummyErrorFunc,
          NULL /* context */, output_mode,
          fake_graphics_context_provider_.decoder_target_provider());
      EXPECT_FALSE(SbPlayerIsValid(player));

      SbPlayerDestroy(player);
    }

    {
      SbPlayer player = CallSbPlayerCreate(
          fake_graphics_context_provider_.window(), kSbMediaVideoCodecH264,
          kSbMediaAudioCodecAac, kSbDrmSystemInvalid, &audio_stream_info,
          "" /* max_video_capabilities */, DummyDeallocateSampleFunc,
          NULL /* decoder_status_func */, DummyPlayerStatusFunc, DummyErrorFunc,
          NULL /* context */, output_mode,
          fake_graphics_context_provider_.decoder_target_provider());
      EXPECT_FALSE(SbPlayerIsValid(player));

      SbPlayerDestroy(player);
    }

    {
      SbPlayer player = CallSbPlayerCreate(
          fake_graphics_context_provider_.window(), kSbMediaVideoCodecH264,
          kSbMediaAudioCodecAac, kSbDrmSystemInvalid, &audio_stream_info,
          "" /* max_video_capabilities */, DummyDeallocateSampleFunc,
          DummyDecoderStatusFunc, NULL /*status_func */, DummyErrorFunc,
          NULL /* context */, output_mode,
          fake_graphics_context_provider_.decoder_target_provider());
      EXPECT_FALSE(SbPlayerIsValid(player));

      SbPlayerDestroy(player);
    }

    {
      SbPlayer player = CallSbPlayerCreate(
          fake_graphics_context_provider_.window(), kSbMediaVideoCodecH264,
          kSbMediaAudioCodecAac, kSbDrmSystemInvalid, &audio_stream_info, "",
          DummyDeallocateSampleFunc, DummyDecoderStatusFunc,
          DummyPlayerStatusFunc, NULL /* error_func */, NULL /* context */,
          output_mode,
          fake_graphics_context_provider_.decoder_target_provider());
      EXPECT_FALSE(SbPlayerIsValid(player));

      SbPlayerDestroy(player);
    }
  }
}

TEST_F(SbPlayerTest, Audioless) {
  for (auto output_mode : kOutputModes) {
    if (!IsOutputModeSupported(output_mode, kSbMediaAudioCodecNone,
                               kSbMediaVideoCodecH264)) {
      return;
    }

    SbPlayer player = CallSbPlayerCreate(
        fake_graphics_context_provider_.window(), kSbMediaVideoCodecH264,
        kSbMediaAudioCodecNone, kSbDrmSystemInvalid,
        NULL /* audio_stream_info */, "" /* max_video_capabilities */,
        DummyDeallocateSampleFunc, DummyDecoderStatusFunc,
        DummyPlayerStatusFunc, DummyErrorFunc, NULL /* context */, output_mode,
        fake_graphics_context_provider_.decoder_target_provider());
    ASSERT_TRUE(SbPlayerIsValid(player));

    // TODO: Validate player state or error.

    GetCurrentFrameIfSupported(player, output_mode);

    SbPlayerDestroy(player);
  }
}

TEST_F(SbPlayerTest, AudioOnly) {
  auto audio_stream_info = CreateAudioStreamInfo(kSbMediaAudioCodecAac);

  for (auto output_mode : kOutputModes) {
    if (!IsOutputModeSupported(output_mode, kSbMediaAudioCodecAac,
                               kSbMediaVideoCodecH264)) {
      return;
    }

    SbPlayer player = CallSbPlayerCreate(
        fake_graphics_context_provider_.window(), kSbMediaVideoCodecNone,
        kSbMediaAudioCodecAac, kSbDrmSystemInvalid, &audio_stream_info,
        "" /* max_video_capabilities */, DummyDeallocateSampleFunc,
        DummyDecoderStatusFunc, DummyPlayerStatusFunc, DummyErrorFunc,
        NULL /* context */, output_mode,
        fake_graphics_context_provider_.decoder_target_provider());
    ASSERT_TRUE(SbPlayerIsValid(player));

    // TODO: Validate player state or error.

    GetCurrentFrameIfSupported(player, output_mode);

    SbPlayerDestroy(player);
  }
}

TEST_F(SbPlayerTest, MultiPlayer) {
  auto audio_stream_info = CreateAudioStreamInfo(kSbMediaAudioCodecAac);

  constexpr SbPlayerOutputMode kMultiPlayerOutputModes[] = {
      kSbPlayerOutputModeDecodeToTexture, kSbPlayerOutputModePunchOut};

  // clang-format off
  constexpr SbMediaAudioCodec kAudioCodecs[] = {
      kSbMediaAudioCodecNone,

      kSbMediaAudioCodecAac,
      kSbMediaAudioCodecAc3,
      kSbMediaAudioCodecEac3,
      kSbMediaAudioCodecOpus,
      kSbMediaAudioCodecVorbis,
      kSbMediaAudioCodecMp3,
      kSbMediaAudioCodecFlac,
      kSbMediaAudioCodecPcm,
      kSbMediaAudioCodecIamf,
  };
  // clang-format on

  // TODO: turn this into a macro.
  // Perform a check to determine if new audio codecs have been added to the
  // SbMediaAudioCodec enum, but not the array |audio_codecs|. If the compiler
  // warns about a missing case here, the value must be added to |kAudioCodecs|.
  SbMediaAudioCodec audio_codec = kAudioCodecs[0];
  switch (audio_codec) {
    case kAudioCodecs[0]:
    case kAudioCodecs[1]:
    case kAudioCodecs[2]:
    case kAudioCodecs[3]:
    case kAudioCodecs[4]:
    case kAudioCodecs[5]:
    case kAudioCodecs[6]:
    case kAudioCodecs[7]:
    case kAudioCodecs[8]:
    case kAudioCodecs[9]:
      break;
  }

  // clang-format off
  constexpr SbMediaVideoCodec kVideoCodecs[] = {
      kSbMediaVideoCodecNone,

      kSbMediaVideoCodecH264,
      kSbMediaVideoCodecH265,
      kSbMediaVideoCodecMpeg2,
      kSbMediaVideoCodecTheora,
      kSbMediaVideoCodecVc1,
      kSbMediaVideoCodecAv1,
      kSbMediaVideoCodecVp8,
      kSbMediaVideoCodecVp9,
      kSbMediaVideoCodecAv2,
  };
  // clang-format on

  // TODO: turn this into a macro.
  // Perform a check to determine if new video codecs have been added to the
  // SbMediaVideoCodec enum, but not the array |video_codecs|. If the compiler
  // warns about a missing case here, the value must be added to |kVideoCodecs|.
  SbMediaVideoCodec video_codec = kVideoCodecs[0];
  switch (video_codec) {
    case kVideoCodecs[0]:
    case kVideoCodecs[1]:
    case kVideoCodecs[2]:
    case kVideoCodecs[3]:
    case kVideoCodecs[4]:
    case kVideoCodecs[5]:
    case kVideoCodecs[6]:
    case kVideoCodecs[7]:
    case kVideoCodecs[8]:
    case kVideoCodecs[9]:
      break;
  }

  const int kMaxPlayersPerConfig = 5;
  std::vector<SbPlayer> created_players;
  int number_of_create_attempts = 0;
  int number_of_players = 0;

  for (int i = 0; i < kMaxPlayersPerConfig; ++i) {
    for (int j = 0; j < SB_ARRAY_SIZE_INT(kMultiPlayerOutputModes); ++j) {
      for (int k = 0; k < SB_ARRAY_SIZE_INT(kAudioCodecs); ++k) {
        for (int l = 0; l < SB_ARRAY_SIZE_INT(kVideoCodecs); ++l) {
          ++number_of_create_attempts;
          SB_LOG(INFO) << "Attempt to create SbPlayer for "
                       << number_of_create_attempts << " times, with "
                       << created_players.size()
                       << " player created.\n Now creating player for "
                       << starboard::GetMediaAudioCodecName(kAudioCodecs[k])
                       << " and "
                       << starboard::GetMediaVideoCodecName(kVideoCodecs[l]);

          audio_stream_info.codec = kAudioCodecs[k];
          created_players.push_back(CallSbPlayerCreate(
              fake_graphics_context_provider_.window(), kVideoCodecs[l],
              kAudioCodecs[k], kSbDrmSystemInvalid, &audio_stream_info,
              "" /* max_video_capabilities */, DummyDeallocateSampleFunc,
              DummyDecoderStatusFunc, PlayerStatusFunc, PlayerErrorFunc, this,
              kMultiPlayerOutputModes[j],
              fake_graphics_context_provider_.decoder_target_provider()));

          if (SbPlayerIsValid(created_players.back())) {
            bool error_occurred = false;
            WaitForPlayerInitializedOrError(&error_occurred);
          } else {
            created_players.pop_back();
          }

          ClearPlayerStateAndError();
        }
      }
    }
    if (created_players.size() == static_cast<size_t>(number_of_players)) {
      break;
    }
    number_of_players = created_players.size();
  }
  SB_DLOG(INFO) << "Created " << number_of_players << " players in total.";
  for (auto player : created_players) {
    SbPlayerDestroy(player);
  }
}

}  // namespace
}  // namespace nplb
