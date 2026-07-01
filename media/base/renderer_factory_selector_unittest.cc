// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/renderer_factory_selector.h"

#include <memory>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "media/base/overlay_info.h"
#include "media/media_buildflags.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(USE_STARBOARD_URL_PLAYER)
#include "media/starboard/url_player_demuxer.h"
#include "url/gurl.h"
#endif  // BUILDFLAG(USE_STARBOARD_URL_PLAYER)

namespace media {

class RendererFactorySelectorTest : public testing::Test {
 public:
  class FakeFactory : public RendererFactory {
   public:
    explicit FakeFactory(RendererType type) : type_(type) {}

    std::unique_ptr<Renderer> CreateRenderer(
        const scoped_refptr<base::SequencedTaskRunner>& media_task_runner,
        const scoped_refptr<base::TaskRunner>& worker_task_runner,
        AudioRendererSink* audio_renderer_sink,
        VideoRendererSink* video_renderer_sink,
        RequestOverlayInfoCB request_overlay_info_cb,
        const gfx::ColorSpace& target_color_space) override {
      return nullptr;
    }

    RendererType factory_type() { return type_; }

   private:
    RendererType type_;
  };

  RendererFactorySelectorTest() = default;

  RendererFactorySelectorTest(const RendererFactorySelectorTest&) = delete;
  RendererFactorySelectorTest& operator=(const RendererFactorySelectorTest&) =
      delete;

  void AddBaseFactory(RendererType type) {
    selector_.AddBaseFactory(type, std::make_unique<FakeFactory>(type));
  }

  void AddFactory(RendererType type) {
    selector_.AddFactory(type, std::make_unique<FakeFactory>(type));
  }

  void AddConditionalFactory(RendererType type) {
    condition_met_map_[type] = false;
    selector_.AddConditionalFactory(
        type, std::make_unique<FakeFactory>(type),
        base::BindRepeating(&RendererFactorySelectorTest::IsConditionMet,
                            base::Unretained(this), type));
  }

#if BUILDFLAG(USE_STARBOARD_URL_PLAYER)
  void AddUrlPlayerConditionalFactory() {
    selector_.AddConditionalFactory(
        RendererType::kUrlPlayer,
        std::make_unique<FakeFactory>(RendererType::kUrlPlayer),
        base::BindRepeating(
            [](const GURL* url) { return media::IsHlsUrl(*url); },
            &current_url_));
  }
#endif  // BUILDFLAG(USE_STARBOARD_URL_PLAYER)

  RendererType GetCurrentlySelectedRendererType() {
    return reinterpret_cast<FakeFactory*>(selector_.GetCurrentFactory())
        ->factory_type();
  }

  bool IsConditionMet(RendererType type) {
    DCHECK(condition_met_map_.count(type));
    return condition_met_map_[type];
  }

 protected:
  RendererFactorySelector selector_;
  std::map<RendererType, bool> condition_met_map_;
#if BUILDFLAG(USE_STARBOARD_URL_PLAYER)
  GURL current_url_;
#endif  // BUILDFLAG(USE_STARBOARD_URL_PLAYER)
};

TEST_F(RendererFactorySelectorTest, SingleFactory) {
  AddBaseFactory(RendererType::kRendererImpl);
  EXPECT_EQ(RendererType::kRendererImpl, GetCurrentlySelectedRendererType());
}

TEST_F(RendererFactorySelectorTest, MultipleFactory) {
  AddBaseFactory(RendererType::kRendererImpl);
  AddFactory(RendererType::kMojo);

  EXPECT_EQ(RendererType::kRendererImpl, GetCurrentlySelectedRendererType());

  selector_.SetBaseRendererType(RendererType::kMojo);
  EXPECT_EQ(RendererType::kMojo, GetCurrentlySelectedRendererType());
}

TEST_F(RendererFactorySelectorTest, ConditionalFactory) {
  AddBaseFactory(RendererType::kRendererImpl);
  AddFactory(RendererType::kMojo);
  AddConditionalFactory(RendererType::kCourier);

  EXPECT_EQ(RendererType::kRendererImpl, GetCurrentlySelectedRendererType());

  condition_met_map_[RendererType::kCourier] = true;
  EXPECT_EQ(RendererType::kCourier, GetCurrentlySelectedRendererType());

  selector_.SetBaseRendererType(RendererType::kMojo);
  EXPECT_EQ(RendererType::kCourier, GetCurrentlySelectedRendererType());

  condition_met_map_[RendererType::kCourier] = false;
  EXPECT_EQ(RendererType::kMojo, GetCurrentlySelectedRendererType());
}

TEST_F(RendererFactorySelectorTest, MultipleConditionalFactories) {
  AddBaseFactory(RendererType::kRendererImpl);
  AddConditionalFactory(RendererType::kFlinging);
  AddConditionalFactory(RendererType::kCourier);

  EXPECT_EQ(RendererType::kRendererImpl, GetCurrentlySelectedRendererType());

  condition_met_map_[RendererType::kFlinging] = false;
  condition_met_map_[RendererType::kCourier] = true;
  EXPECT_EQ(RendererType::kCourier, GetCurrentlySelectedRendererType());

  condition_met_map_[RendererType::kFlinging] = true;
  condition_met_map_[RendererType::kCourier] = false;
  EXPECT_EQ(RendererType::kFlinging, GetCurrentlySelectedRendererType());

  // It's up to the implementation detail to decide which one to use.
  condition_met_map_[RendererType::kFlinging] = true;
  condition_met_map_[RendererType::kCourier] = true;
  EXPECT_TRUE(GetCurrentlySelectedRendererType() == RendererType::kFlinging ||
              GetCurrentlySelectedRendererType() == RendererType::kCourier);
}

#if BUILDFLAG(USE_STARBOARD_URL_PLAYER)
TEST_F(RendererFactorySelectorTest, UrlPlayerConditionalFactorySelectsM3u8) {
  AddBaseFactory(RendererType::kStarboard);
  AddUrlPlayerConditionalFactory();

  current_url_ = GURL("https://example.test/master.m3u8");

  EXPECT_EQ(RendererType::kUrlPlayer, GetCurrentlySelectedRendererType());
}

TEST_F(RendererFactorySelectorTest,
       UrlPlayerConditionalFactorySelectsHlsVariant) {
  AddBaseFactory(RendererType::kStarboard);
  AddUrlPlayerConditionalFactory();

  current_url_ = GURL("https://example.test/hls_variant/video");

  EXPECT_EQ(RendererType::kUrlPlayer, GetCurrentlySelectedRendererType());
}

TEST_F(RendererFactorySelectorTest,
       UrlPlayerConditionalFactorySkipsOrdinaryUrl) {
  AddBaseFactory(RendererType::kStarboard);
  AddUrlPlayerConditionalFactory();

  current_url_ = GURL("https://example.test/video.mp4");

  EXPECT_EQ(RendererType::kStarboard, GetCurrentlySelectedRendererType());
}
#endif  // BUILDFLAG(USE_STARBOARD_URL_PLAYER)

}  // namespace media
