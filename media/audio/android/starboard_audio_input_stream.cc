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
//
// Starboard specific low latency path based on
// media/audio/android/opensles_input.cc

#include "media/audio/android/starboard_audio_input_stream.h"

#include "base/android/jni_android.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "cobalt/media/audio/audio_input_constants.h"
#include "media/audio/android/audio_manager_android.h"
#include "media/base/audio_bus.h"
#include "starboard/android/shared/audio_permission_requester.h"

#define LOG_ON_FAILURE_AND_RETURN(op, ...)      \
  do {                                          \
    SLresult err = (op);                        \
    if (err != SL_RESULT_SUCCESS) {             \
      DLOG(ERROR) << #op << " failed: " << err; \
      return __VA_ARGS__;                       \
    }                                           \
  } while (0)

namespace media {

// static
bool StarboardAudioInputStream::RequestRuntimePermission() {
  return starboard::RequestRecordAudioPermission(
      base::android::AttachCurrentThread());
}

StarboardAudioInputStream::StarboardAudioInputStream(
    AudioManagerAndroid* audio_manager,
    const AudioParameters& params)
    : audio_manager_(audio_manager),
      format_({
          SL_ANDROID_DATAFORMAT_PCM_EX,              // formatType
          1,                                         // numChannels
          cobalt::media::kSampleRate * 1000,         // sampleRate (milliHertz)
          SL_PCMSAMPLEFORMAT_FIXED_16,               // bitsPerSample
          SL_PCMSAMPLEFORMAT_FIXED_16,               // containerSize
          SL_SPEAKER_FRONT_CENTER,                   //  channelMask
          SL_BYTEORDER_LITTLEENDIAN,                 // endianness
          SL_ANDROID_PCM_REPRESENTATION_SIGNED_INT,  // representation
      }),
      buffer_size_bytes_([&]() {
        int input_sample_rate = params.sample_rate() > 0
                                    ? params.sample_rate()
                                    : cobalt::media::kSampleRate;
        int size = (cobalt::media::kSampleRate * params.frames_per_buffer() /
                    input_sample_rate) *
                   sizeof(int16_t);
        return size == 0 ? kDefaultBufferSizeInBytes : size;
      }()),
      audio_bus_(
          media::AudioBus::Create(1, buffer_size_bytes_ / sizeof(int16_t))),
      hardware_delay_(
          base::Seconds(audio_bus_->frames() /
                        static_cast<double>(cobalt::media::kSampleRate))) {
  DVLOG(2) << __func__;
}

StarboardAudioInputStream::~StarboardAudioInputStream() {
  DVLOG(2) << __func__;
  CHECK(thread_checker_.CalledOnValidThread());
  CHECK(!recorder_object_.Get());
  CHECK(!engine_object_.Get());
  CHECK(!recorder_);
  CHECK(!simple_buffer_queue_);
  CHECK(!audio_data_[0]);
}

AudioInputStream::OpenOutcome StarboardAudioInputStream::Open() {
  LOG(INFO) << "StarboardAudioInputStream::Open";
  CHECK(thread_checker_.CalledOnValidThread());

  if (engine_object_.Get()) {
    LOG(INFO) << "StarboardAudioInputStream already Open (Pre-started)";
    return AudioInputStream::OpenOutcome::kSuccess;
  }

  if (!CreateRecorder())
    return AudioInputStream::OpenOutcome::kFailed;

  SetupAudioBuffer();

  // PRE-START hardware immediately to bypass the Mojo Start handshake.
  // This allows the hardware to warm up while the Renderer thread is busy.
  SLresult err = SL_RESULT_UNKNOWN_ERROR;
  for (int i = 0; i < kMaxNumOfBuffersInQueue; ++i) {
    {
      base::AutoLock lock(lock_);
      err = (*simple_buffer_queue_)->Enqueue(
          simple_buffer_queue_, audio_data_[i].get(), buffer_size_bytes_);
    }
    if (SL_RESULT_SUCCESS != err) {
      HandleError(err);
      return AudioInputStream::OpenOutcome::kFailed;
    }
  }

  err = (*recorder_)->SetRecordState(recorder_, SL_RECORDSTATE_RECORDING);
  if (SL_RESULT_SUCCESS != err) {
    HandleError(err);
    return AudioInputStream::OpenOutcome::kFailed;
  }

  LOG(INFO) << "Cobalt: Hardware PRE-STARTED in Open()";
  return AudioInputStream::OpenOutcome::kSuccess;
}

void StarboardAudioInputStream::Start(AudioInputCallback* callback) {

  CHECK(thread_checker_.CalledOnValidThread());
  CHECK(callback);

  base::AutoLock lock(lock_);
  CHECK(recorder_);
  CHECK(simple_buffer_queue_);
  callback_ = callback;
  started_ = true;
}

void StarboardAudioInputStream::Stop() {
  CHECK(thread_checker_.CalledOnValidThread());
  if (!started_) {
    return;
  }

  SLAndroidSimpleBufferQueueItf queue = nullptr;
  SLRecordItf recorder = nullptr;
  {
    base::AutoLock lock(lock_);
    started_ = false;
    callback_ = nullptr;
    queue = simple_buffer_queue_;
    recorder = recorder_;
  }

  // Stop recording by setting the record state to SL_RECORDSTATE_STOPPED.
  if (recorder) {
    LOG_ON_FAILURE_AND_RETURN(
        (*recorder)->SetRecordState(recorder, SL_RECORDSTATE_STOPPED));
  }

  // Clear the buffer queue to get rid of old data when resuming recording.
  if (queue) {
    LOG_ON_FAILURE_AND_RETURN((*queue)->Clear(queue));
  }
}

void StarboardAudioInputStream::Close() {
  CHECK(thread_checker_.CalledOnValidThread());
  Stop();

  // Destroy OpenSLES objects outside the lock.
  // These calls are synchronous and wait for any pending callbacks to finish.
  // If we held the lock here, we would deadlock with ReadBufferQueue.
  recorder_object_.Reset();
  engine_object_.Reset();
  {
    base::AutoLock lock(lock_);
    simple_buffer_queue_ = nullptr;
    recorder_ = nullptr;
  }
  ReleaseAudioBuffer();
  audio_manager_->ReleaseInputStream(this);
}

double StarboardAudioInputStream::GetMaxVolume() { return 0.0; }
void StarboardAudioInputStream::SetVolume(double volume) {}
double StarboardAudioInputStream::GetVolume() { return 0.0; }
bool StarboardAudioInputStream::SetAutomaticGainControl(bool enabled) { return false; }
bool StarboardAudioInputStream::GetAutomaticGainControl() { return false; }
bool StarboardAudioInputStream::IsMuted() { return false; }
void StarboardAudioInputStream::SetOutputDeviceForAec(const std::string& output_device_id) {}

bool StarboardAudioInputStream::CreateRecorder() {
  SLEngineOption option[] = {
      {SL_ENGINEOPTION_THREADSAFE, static_cast<SLuint32>(SL_BOOLEAN_TRUE)}};
  LOG_ON_FAILURE_AND_RETURN(
      slCreateEngine(engine_object_.Receive(), 1, option, 0, nullptr, nullptr),
      false);

  LOG_ON_FAILURE_AND_RETURN(
      engine_object_->Realize(engine_object_.Get(), SL_BOOLEAN_FALSE), false);

  SLEngineItf engine;
  LOG_ON_FAILURE_AND_RETURN(engine_object_->GetInterface(
                                engine_object_.Get(), SL_IID_ENGINE, &engine),
                            false);

  SLDataLocator_IODevice mic_locator = {SL_DATALOCATOR_IODEVICE,
                                        SL_IODEVICE_AUDIOINPUT,
                                        SL_DEFAULTDEVICEID_AUDIOINPUT, nullptr};
  SLDataSource audio_source = {&mic_locator, nullptr};

  SLDataLocator_AndroidSimpleBufferQueue buffer_queue = {
      SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
      static_cast<SLuint32>(kMaxNumOfBuffersInQueue)};
  SLDataSink audio_sink = {
      &buffer_queue,
      static_cast<void*>(const_cast<SLAndroidDataFormat_PCM_EX*>(&format_))};

  const SLInterfaceID interface_id[] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                                        SL_IID_ANDROIDCONFIGURATION};
  const SLboolean interface_required[] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};

  LOG_ON_FAILURE_AND_RETURN(
      (*engine)->CreateAudioRecorder(
          engine, recorder_object_.Receive(), &audio_source, &audio_sink,
          std::size(interface_id), interface_id, interface_required),
      false);

  SLAndroidConfigurationItf recorder_config;
  LOG_ON_FAILURE_AND_RETURN(
      recorder_object_->GetInterface(recorder_object_.Get(),
                                     SL_IID_ANDROIDCONFIGURATION,
                                     &recorder_config),
      false);

  // Use VOICE_RECOGNITION preset for low-latency and minimal DSP (Starboard style)
  SLint32 stream_type = SL_ANDROID_RECORDING_PRESET_VOICE_RECOGNITION;
  LOG_ON_FAILURE_AND_RETURN(
      (*recorder_config)->SetConfiguration(recorder_config,
                                           SL_ANDROID_KEY_RECORDING_PRESET,
                                           &stream_type,
                                           sizeof(SLint32)),
      false);

  LOG_ON_FAILURE_AND_RETURN(
      recorder_object_->Realize(recorder_object_.Get(), SL_BOOLEAN_FALSE),
      false);

  LOG_ON_FAILURE_AND_RETURN(
      recorder_object_->GetInterface(
          recorder_object_.Get(), SL_IID_RECORD, &recorder_),
      false);

  SLAndroidSimpleBufferQueueItf queue = nullptr;
  LOG_ON_FAILURE_AND_RETURN(
      recorder_object_->GetInterface(recorder_object_.Get(),
                                     SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                                     &queue),
      false);

  LOG_ON_FAILURE_AND_RETURN(
      (*queue)->RegisterCallback(
          queue, SimpleBufferQueueCallback, this),
      false);

  {
    base::AutoLock lock(lock_);
    simple_buffer_queue_ = queue;
  }

  return true;
}

void StarboardAudioInputStream::SimpleBufferQueueCallback(
    SLAndroidSimpleBufferQueueItf buffer_queue,
    void* instance) {
  StarboardAudioInputStream* stream =
      reinterpret_cast<StarboardAudioInputStream*>(instance);
  stream->ReadBufferQueue();
}

void StarboardAudioInputStream::ReadBufferQueue() {
  base::AutoLock lock(lock_);
  if (!started_ || !callback_) {
    return;
  }

  // Convert from interleaved format to deinterleaved audio bus format while
  // still under the lock to protect audio_bus_ and audio_data_.
  audio_bus_->FromInterleaved<SignedInt16SampleTypeTraits>(
      reinterpret_cast<int16_t*>(audio_data_[active_buffer_index_].get()),
      audio_bus_->frames());

  callback_->OnData(audio_bus_.get(),
                    base::TimeTicks::Now() - hardware_delay_,
                    /*volume=(0 indicates no vol control)*/0.0,
                    /*audio_glitch_info=*/{});

  if (simple_buffer_queue_) {
    (*simple_buffer_queue_)->Enqueue(simple_buffer_queue_,
                                     audio_data_[active_buffer_index_].get(),
                                     buffer_size_bytes_);
    active_buffer_index_ = (active_buffer_index_ + 1) % kMaxNumOfBuffersInQueue;
  }
}
void StarboardAudioInputStream::SetupAudioBuffer() {
  base::AutoLock lock(lock_);
  CHECK(!audio_data_[0]);
  for (int i = 0; i < kMaxNumOfBuffersInQueue; ++i) {
    audio_data_[i] = std::make_unique<uint8_t[]>(buffer_size_bytes_);
  }
}

void StarboardAudioInputStream::ReleaseAudioBuffer() {
  base::AutoLock lock(lock_);
  for (int i = 0; i < kMaxNumOfBuffersInQueue; ++i) {
    audio_data_[i].reset();
  }
}

void StarboardAudioInputStream::HandleError(SLresult error) {
  DLOG(ERROR) << "Starboard Input error " << error;
  AudioInputCallback* callback = nullptr;
  {
    base::AutoLock lock(lock_);
    callback = callback_;
    callback_ = nullptr;
  }
  if (callback)
    callback->OnError();
}

}  // namespace media
