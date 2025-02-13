// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/audio/public/cpp/output_device.h"

#include <utility>

#include "base/bind.h"
#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "build/build_config.h"
#include "media/audio/audio_output_device.h"
#include "media/base/audio_renderer_sink.h"
#include "media/mojo/mojom/audio_data_pipe.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/system/platform_handle.h"
#include "services/audio/public/cpp/fake_stream_factory.h"
#include "services/audio/sync_reader.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Mock;
using testing::NotNull;
using testing::StrictMock;
using testing::WithArg;

namespace audio {

namespace {

constexpr float kAudioData = 0.618;
constexpr base::TimeDelta kDelay = base::TimeDelta::FromMicroseconds(123);
constexpr char kDeviceId[] = "testdeviceid";
constexpr int kFramesSkipped = 456;
constexpr int kFrames = 789;
constexpr char kNonDefaultDeviceId[] = "valid-nondefault-device-id";
constexpr base::TimeDelta kAuthTimeout =
    base::TimeDelta::FromMilliseconds(10000);
constexpr int kBitstreamFrames = 101;
constexpr size_t kBitstreamDataSize = 512;

class MockRenderCallback : public media::AudioRendererSink::RenderCallback {
 public:
  MockRenderCallback() = default;
  ~MockRenderCallback() override = default;

  MOCK_METHOD4(Render,
               int(base::TimeDelta delay,
                   base::TimeTicks timestamp,
                   int prior_frames_skipped,
                   media::AudioBus* dest));
  void OnRenderError() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(MockRenderCallback);
};

class MockStream : public media::mojom::AudioOutputStream {
 public:
  MockStream() = default;
  ~MockStream() override = default;

  MOCK_METHOD0(Play, void());
  MOCK_METHOD0(Pause, void());
  MOCK_METHOD1(SetVolume, void(double));
  MOCK_METHOD0(Flush, void());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockStream);
};

class MockAudioOutputIPC : public media::AudioOutputIPC {
 public:
  MockAudioOutputIPC() = default;
  ~MockAudioOutputIPC() override = default;

  MOCK_METHOD3(RequestDeviceAuthorization,
               void(media::AudioOutputIPCDelegate* delegate,
                    const base::UnguessableToken& session_id,
                    const std::string& device_id));
  MOCK_METHOD3(
      CreateStream,
      void(media::AudioOutputIPCDelegate* delegate,
           const media::AudioParameters& params,
           const base::Optional<base::UnguessableToken>& processing_id));
  MOCK_METHOD0(PlayStream, void());
  MOCK_METHOD0(PauseStream, void());
  MOCK_METHOD0(FlushStream, void());
  MOCK_METHOD0(CloseStream, void());
  MOCK_METHOD1(SetVolume, void(double volume));
};

class FakeOutputStreamFactory : public audio::FakeStreamFactory {
 public:
  FakeOutputStreamFactory() : stream_(), stream_receiver_(&stream_) {}
  ~FakeOutputStreamFactory() final {}

  void CreateOutputStream(
      mojo::PendingReceiver<media::mojom::AudioOutputStream> stream_receiver,
      mojo::PendingAssociatedRemote<media::mojom::AudioOutputStreamObserver>
          observer,
      mojo::PendingRemote<media::mojom::AudioLog> log,
      const std::string& output_device_id,
      const media::AudioParameters& params,
      const base::UnguessableToken& group_id,
      const base::Optional<base::UnguessableToken>& processing_id,
      CreateOutputStreamCallback created_callback) final {
    EXPECT_FALSE(observer);
    EXPECT_FALSE(log);
    created_callback_ = std::move(created_callback);

    if (stream_receiver_.is_bound())
      stream_receiver_.reset();
    stream_receiver_.Bind(std::move(stream_receiver));
  }

  void Bind(mojo::ScopedMessagePipeHandle handle) {
    receiver_.Bind(
        mojo::PendingReceiver<audio::mojom::StreamFactory>(std::move(handle)));
  }

  StrictMock<MockStream> stream_;
  CreateOutputStreamCallback created_callback_;

 private:
  mojo::Receiver<media::mojom::AudioOutputStream> stream_receiver_;
  DISALLOW_COPY_AND_ASSIGN(FakeOutputStreamFactory);
};

struct DataFlowTestEnvironment {
  explicit DataFlowTestEnvironment(const media::AudioParameters& params) {
    const uint32_t memory_size = ComputeAudioOutputBufferSize(params);
    auto shared_memory_region =
        base::UnsafeSharedMemoryRegion::Create(memory_size);
    auto shared_memory_mapping = shared_memory_region.Map();
    CHECK(shared_memory_region.IsValid());
    CHECK(shared_memory_mapping.IsValid());
    reader = std::make_unique<audio::SyncReader>(
        /*log callback*/ base::DoNothing(), params, &client_socket);
    CHECK(reader->IsValid());
    time_stamp = base::TimeTicks::Now();

#if defined(OS_FUCHSIA)
    // TODO(https://crbug.com/838367): Fuchsia bots use nested virtualization,
    // which can result in unusually long scheduling delays, so allow a longer
    // timeout.
    reader->set_max_wait_timeout_for_test(
        base::TimeDelta::FromMilliseconds(250));
#endif
  }

  base::CancelableSyncSocket client_socket;
  StrictMock<MockRenderCallback> render_callback;
  std::unique_ptr<audio::SyncReader> reader;
  base::TimeTicks time_stamp;
};

}  // namespace

class AudioServiceOutputDeviceTest : public testing::Test {
 public:
  AudioServiceOutputDeviceTest()
      : task_env_(
            base::test::TaskEnvironment::MainThreadType::DEFAULT,
            base::test::TaskEnvironment::ThreadPoolExecutionMode::QUEUED) {
    stream_factory_ = std::make_unique<FakeOutputStreamFactory>();
  }

  ~AudioServiceOutputDeviceTest() override {
    if (!stream_factory_->created_callback_)
      return;
    std::move(stream_factory_->created_callback_).Run(nullptr);
    task_env_.RunUntilIdle();
  }

  mojo::PendingRemote<audio::mojom::StreamFactory> MakeFactoryRemote() {
    return stream_factory_->receiver_.BindNewPipeAndPassRemote();
  }

  base::test::TaskEnvironment task_env_;
  std::unique_ptr<FakeOutputStreamFactory> stream_factory_;

 private:
  DISALLOW_COPY_AND_ASSIGN(AudioServiceOutputDeviceTest);
};

TEST_F(AudioServiceOutputDeviceTest, CreatePlayPause) {
  auto params(media::AudioParameters::UnavailableDeviceParams());
  OutputDevice output_device(MakeFactoryRemote(), params, nullptr, kDeviceId);

  constexpr double volume = 0.42;
  EXPECT_CALL(stream_factory_->stream_, SetVolume(volume));
  EXPECT_CALL(stream_factory_->stream_, Play());
  EXPECT_CALL(stream_factory_->stream_, Pause());

  output_device.SetVolume(volume);
  output_device.Play();
  output_device.Pause();
  task_env_.RunUntilIdle();
}

// Flaky on Linux Chromium OS ASan LSan (https://crbug.com/889845)
#if defined(OS_CHROMEOS) && defined(ADDRESS_SANITIZER)
#define MAYBE_VerifyDataFlow DISABLED_VerifyDataFlow
#else
#define MAYBE_VerifyDataFlow VerifyDataFlow
#endif
TEST_F(AudioServiceOutputDeviceTest, MAYBE_VerifyDataFlow) {
  auto params(media::AudioParameters::UnavailableDeviceParams());
  params.set_frames_per_buffer(kFrames);
  ASSERT_EQ(2, params.channels());
  DataFlowTestEnvironment env(params);
  OutputDevice output_device(MakeFactoryRemote(), params, &env.render_callback,
                             kDeviceId);
  EXPECT_CALL(stream_factory_->stream_, Play());
  output_device.Play();
  task_env_.RunUntilIdle();

  std::move(stream_factory_->created_callback_)
      .Run({base::in_place, env.reader->TakeSharedMemoryRegion(),
            mojo::WrapPlatformFile(env.client_socket.Release())});
  task_env_.RunUntilIdle();

  // At this point, the callback thread should be running. Send some data over
  // and verify that it's propagated to |env.render_callback|. Do it a few
  // times.
  auto test_bus = media::AudioBus::Create(params);
  for (int i = 0; i < 10; ++i) {
    test_bus->Zero();
    EXPECT_CALL(env.render_callback,
                Render(kDelay, env.time_stamp, kFramesSkipped, NotNull()))
        .WillOnce(WithArg<3>(Invoke([](media::AudioBus* client_bus) -> int {
          // Place some test data in the bus so that we can check that it was
          // copied to the audio service side.
          std::fill_n(client_bus->channel(0), client_bus->frames(), kAudioData);
          std::fill_n(client_bus->channel(1), client_bus->frames(), kAudioData);
          return client_bus->frames();
        })));
    env.reader->RequestMoreData(kDelay, env.time_stamp, kFramesSkipped);
    env.reader->Read(test_bus.get());

    Mock::VerifyAndClear(&env.render_callback);
    for (int i = 0; i < kFrames; ++i) {
      EXPECT_EQ(kAudioData, test_bus->channel(0)[i]);
      EXPECT_EQ(kAudioData, test_bus->channel(1)[i]);
    }
  }
}

TEST_F(AudioServiceOutputDeviceTest, CreateBitStreamStream) {
  const int kAudioParameterFrames = 4321;
  media::AudioParameters params(media::AudioParameters::AUDIO_BITSTREAM_EAC3,
                                media::CHANNEL_LAYOUT_STEREO, 48000,
                                kAudioParameterFrames);

  DataFlowTestEnvironment env(params);
  auto* ipc = new MockAudioOutputIPC();  // owned by |audio_device|.
  auto audio_device = base::MakeRefCounted<media::AudioOutputDevice>(
      base::WrapUnique(ipc), task_env_.GetMainThreadTaskRunner(),
      media::AudioSinkParameters(base::UnguessableToken(), kNonDefaultDeviceId),
      kAuthTimeout);

  // Start a stream.
  audio_device->RequestDeviceAuthorization();
  audio_device->Initialize(params, &env.render_callback);
  audio_device->Start();
  EXPECT_CALL(*ipc, RequestDeviceAuthorization(audio_device.get(),
                                               base::UnguessableToken(),
                                               kNonDefaultDeviceId));
  EXPECT_CALL(*ipc, CreateStream(audio_device.get(), _, _));
  EXPECT_CALL(*ipc, PlayStream());
  task_env_.RunUntilIdle();
  Mock::VerifyAndClear(ipc);
  audio_device->OnDeviceAuthorized(media::OUTPUT_DEVICE_STATUS_OK, params,
                                   kNonDefaultDeviceId);
  audio_device->OnStreamCreated(env.reader->TakeSharedMemoryRegion(),
                                env.client_socket.Release(),
                                /*playing_automatically*/ false);

  task_env_.RunUntilIdle();
  // At this point, the callback thread should be running. Send some data over
  // and verify that it's propagated to |env.callback|. Do it a few times.
  auto test_bus = media::AudioBus::Create(params);
  for (int i = 0; i < 10; ++i) {
    test_bus->Zero();
    EXPECT_CALL(env.render_callback,
                Render(kDelay, env.time_stamp, kFramesSkipped, NotNull()))
        .WillOnce(WithArg<3>(Invoke([](media::AudioBus* renderer_bus) -> int {
          EXPECT_TRUE(renderer_bus->is_bitstream_format());
          // Place some test data in the bus so that we can check that it was
          // copied to the browser side.
          std::fill_n(renderer_bus->channel(0),
                      kBitstreamDataSize / sizeof(float), kAudioData);
          renderer_bus->SetBitstreamFrames(kBitstreamFrames);
          renderer_bus->SetBitstreamDataSize(kBitstreamDataSize);
          return renderer_bus->frames();
        })));
    env.reader->RequestMoreData(kDelay, env.time_stamp, kFramesSkipped);
    env.reader->Read(test_bus.get());

    Mock::VerifyAndClear(&env.render_callback);
    EXPECT_TRUE(test_bus->is_bitstream_format());
    EXPECT_EQ(kBitstreamFrames, test_bus->GetBitstreamFrames());
    EXPECT_EQ(kBitstreamDataSize, test_bus->GetBitstreamDataSize());
    for (size_t i = 0; i < kBitstreamDataSize / sizeof(float); ++i) {
      // Note: if all of these fail, the bots will behave strangely due to the
      // large amount of text output. Assert is used to avoid this.
      ASSERT_EQ(kAudioData, test_bus->channel(0)[i]);
    }
  }

  audio_device->Stop();
  EXPECT_CALL(*ipc, CloseStream());
  task_env_.RunUntilIdle();
}

TEST_F(AudioServiceOutputDeviceTest, CreateNondefaultDevice) {
  auto params = media::AudioParameters::UnavailableDeviceParams();
  params.set_frames_per_buffer(kFrames);
  ASSERT_EQ(2, params.channels());
  DataFlowTestEnvironment env(params);
  auto* ipc = new MockAudioOutputIPC();  // owned by |audio_device|.
  auto audio_device = base::MakeRefCounted<media::AudioOutputDevice>(
      base::WrapUnique(ipc), task_env_.GetMainThreadTaskRunner(),
      media::AudioSinkParameters(base::UnguessableToken(), kNonDefaultDeviceId),
      kAuthTimeout);

  audio_device->RequestDeviceAuthorization();
  audio_device->Initialize(params, &env.render_callback);
  audio_device->Start();
  EXPECT_CALL(*ipc, RequestDeviceAuthorization(audio_device.get(),
                                               base::UnguessableToken(),
                                               kNonDefaultDeviceId));
  EXPECT_CALL(*ipc, CreateStream(audio_device.get(), _, _));
  EXPECT_CALL(*ipc, PlayStream());
  task_env_.RunUntilIdle();
  Mock::VerifyAndClear(ipc);
  audio_device->OnDeviceAuthorized(media::OUTPUT_DEVICE_STATUS_OK, params,
                                   kNonDefaultDeviceId);
  audio_device->OnStreamCreated(env.reader->TakeSharedMemoryRegion(),
                                env.client_socket.Release(),
                                /*playing_automatically*/ false);

  audio_device->Stop();
  EXPECT_CALL(*ipc, CloseStream());
  task_env_.RunUntilIdle();
}

}  // namespace audio
