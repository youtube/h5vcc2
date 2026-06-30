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

#include "media/mojo/clients/starboard/url_player_renderer_client_factory.h"

#include <memory>
#include <optional>
#include <string>

#include "base/test/task_environment.h"
#include "media/base/media_util.h"
#include "media/base/mock_video_renderer_sink.h"
#include "media/mojo/clients/mojo_renderer_factory.h"
#include "media/mojo/mojom/interface_factory.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {
namespace {

class CapturingInterfaceFactory : public mojom::InterfaceFactory {
 public:
  void CreateAudioDecoder(
      mojo::PendingReceiver<mojom::AudioDecoder> receiver) override {}
  void CreateVideoDecoder(
      mojo::PendingReceiver<mojom::VideoDecoder> receiver,
      mojo::PendingRemote<mojom::VideoDecoder> dst_video_decoder) override {}
  void CreateAudioEncoder(
      mojo::PendingReceiver<mojom::AudioEncoder> receiver) override {}
  void CreateDefaultRenderer(
      const std::string& audio_device_id,
      mojo::PendingReceiver<mojom::Renderer> receiver) override {}
  void CreateStarboardRenderer(
      mojo::PendingRemote<mojom::MediaLog> media_log,
      const StarboardRendererConfig& config,
      mojo::PendingReceiver<mojom::Renderer> renderer,
      mojo::PendingReceiver<mojom::StarboardRendererExtension>
          renderer_extension,
      mojo::PendingRemote<mojom::StarboardRendererClientExtension>
          client_extension) override {
    ++starboard_renderer_create_count_;
  }
  void CreateUrlPlayerRenderer(
      mojo::PendingRemote<mojom::MediaLog> media_log,
      const StarboardRendererConfig& config,
      mojo::PendingReceiver<mojom::Renderer> renderer,
      mojo::PendingReceiver<mojom::StarboardRendererExtension>
          renderer_extension,
      mojo::PendingRemote<mojom::StarboardRendererClientExtension>
          client_extension) override {
    ++url_player_renderer_create_count_;
    url_player_config_ = config;
    media_log_pipe_was_valid_ = media_log.is_valid();
    renderer_receiver_was_valid_ = renderer.is_valid();
    renderer_extension_receiver_was_valid_ = renderer_extension.is_valid();
    client_extension_remote_was_valid_ = client_extension.is_valid();
  }
  void CreateCdm(const CdmConfig& cdm_config,
                 CreateCdmCallback callback) override {}

  const std::optional<StarboardRendererConfig>& url_player_config() const {
    return url_player_config_;
  }
  int starboard_renderer_create_count() const {
    return starboard_renderer_create_count_;
  }
  int url_player_renderer_create_count() const {
    return url_player_renderer_create_count_;
  }
  bool media_log_pipe_was_valid() const { return media_log_pipe_was_valid_; }
  bool renderer_receiver_was_valid() const {
    return renderer_receiver_was_valid_;
  }
  bool renderer_extension_receiver_was_valid() const {
    return renderer_extension_receiver_was_valid_;
  }
  bool client_extension_remote_was_valid() const {
    return client_extension_remote_was_valid_;
  }

 private:
  std::optional<StarboardRendererConfig> url_player_config_;
  int starboard_renderer_create_count_ = 0;
  int url_player_renderer_create_count_ = 0;
  bool media_log_pipe_was_valid_ = false;
  bool renderer_receiver_was_valid_ = false;
  bool renderer_extension_receiver_was_valid_ = false;
  bool client_extension_remote_was_valid_ = false;
};

class UrlPlayerRendererClientFactoryTest : public testing::Test {
 protected:
  std::unique_ptr<Renderer> CreateRenderer(
      const RendererFactoryTraits& traits = RendererFactoryTraits()) {
    factory_ = std::make_unique<UrlPlayerRendererClientFactory>(
        &media_log_, std::make_unique<MojoRendererFactory>(&interface_factory_),
        &traits);
    return factory_->CreateRenderer(task_environment_.GetMainThreadTaskRunner(),
                                    task_environment_.GetMainThreadTaskRunner(),
                                    nullptr, &video_renderer_sink_,
                                    base::NullCallback(), gfx::ColorSpace());
  }

  base::test::TaskEnvironment task_environment_;
  CapturingInterfaceFactory interface_factory_;
  NullMediaLog media_log_;
  MockVideoRendererSink video_renderer_sink_;
  std::unique_ptr<UrlPlayerRendererClientFactory> factory_;
};

TEST_F(UrlPlayerRendererClientFactoryTest, AlwaysDisablesBypassMojoForMedia) {
  RendererFactoryTraits traits;
  traits.experimental_features.bypass_mojo_for_media = true;
  traits.experimental_features.force_decode_to_texture = true;

  std::unique_ptr<Renderer> renderer = CreateRenderer(traits);

  ASSERT_TRUE(renderer);
  ASSERT_TRUE(interface_factory_.url_player_config());
  EXPECT_FALSE(interface_factory_.url_player_config()
                   ->experimental_features.bypass_mojo_for_media);
  EXPECT_TRUE(interface_factory_.url_player_config()
                  ->experimental_features.force_decode_to_texture);
}

TEST_F(UrlPlayerRendererClientFactoryTest, UsesUrlPlayerFactoryPathOnly) {
  std::unique_ptr<Renderer> renderer = CreateRenderer();

  ASSERT_TRUE(renderer);
  EXPECT_EQ(renderer->GetRendererType(), RendererType::kUrlPlayer);
  EXPECT_EQ(interface_factory_.url_player_renderer_create_count(), 1);
  EXPECT_EQ(interface_factory_.starboard_renderer_create_count(), 0);
}

TEST_F(UrlPlayerRendererClientFactoryTest, WiresMojoEndpoints) {
  std::unique_ptr<Renderer> renderer = CreateRenderer();

  ASSERT_TRUE(renderer);
  EXPECT_TRUE(interface_factory_.media_log_pipe_was_valid());
  EXPECT_TRUE(interface_factory_.renderer_receiver_was_valid());
  EXPECT_TRUE(interface_factory_.renderer_extension_receiver_was_valid());
  EXPECT_TRUE(interface_factory_.client_extension_remote_was_valid());
}

TEST_F(UrlPlayerRendererClientFactoryTest, PropagatesConfigForUrlPlayer) {
  RendererFactoryTraits traits;
  traits.experimental_features.bypass_mojo_for_media = true;
  traits.experimental_features.force_decode_to_texture = true;
  traits.experimental_features.enable_low_latency = true;
  traits.viewport_size = gfx::Size(1920, 1080);

  std::unique_ptr<Renderer> renderer = CreateRenderer(traits);

  ASSERT_TRUE(renderer);
  ASSERT_TRUE(interface_factory_.url_player_config());
  EXPECT_FALSE(
      interface_factory_.url_player_config()->overlay_plane_id.is_empty());
  EXPECT_TRUE(interface_factory_.url_player_config()
                  ->audio_write_duration_local.is_zero());
  EXPECT_TRUE(interface_factory_.url_player_config()
                  ->audio_write_duration_remote.is_zero());
  EXPECT_TRUE(
      interface_factory_.url_player_config()->max_video_capabilities.empty());
  EXPECT_EQ(interface_factory_.url_player_config()->viewport_size,
            traits.viewport_size);
  EXPECT_FALSE(interface_factory_.url_player_config()
                   ->experimental_features.bypass_mojo_for_media);
  EXPECT_TRUE(interface_factory_.url_player_config()
                  ->experimental_features.force_decode_to_texture);
  EXPECT_TRUE(interface_factory_.url_player_config()
                  ->experimental_features.enable_low_latency);
}

}  // namespace
}  // namespace media
