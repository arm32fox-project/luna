/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsError.h"
#include "MediaDecoderStateMachine.h"
#include "AbstractMediaDecoder.h"
#include "MediaResource.h"
#include "GStreamerReader.h"
#include "GStreamerFormatHelper.h"
#if GST_VERSION_MAJOR >= 1
#include "GStreamerAllocator.h"
#endif
#include "VideoUtils.h"
#include "mozilla/dom/TimeRanges.h"
#include "mozilla/Preferences.h"
#include "GStreamerLoader.h"

namespace mozilla {

using namespace layers;

// Un-comment to enable logging of seek bisections.
//#define SEEK_LOGGING

#ifdef PR_LOGGING
extern PRLogModuleInfo* gMediaDecoderLog;
#define LOG(type, msg) PR_LOG(gMediaDecoderLog, type, msg)
#else
#define LOG(type, msg)
#endif

extern bool
IsYV12Format(const VideoData::YCbCrBuffer::Plane& aYPlane,
             const VideoData::YCbCrBuffer::Plane& aCbPlane,
             const VideoData::YCbCrBuffer::Plane& aCrPlane);

#if DEBUG
static const unsigned int MAX_CHANNELS = 4;
#endif
// Let the demuxer work in pull mode for short files. This used to be a micro
// optimization to have more accurate durations for ogg files in mochitests.
// Since we aren't using gstreamer to demux ogg, and having demuxers
// work in pull mode over http makes them slower (since they really assume
// near-zero latency in pull mode) set the constant to 0 for now, which
// effectively disables it.
static const int SHORT_FILE_SIZE = 0;
// The default resource->Read() size when working in push mode
static const int DEFAULT_SOURCE_READ_SIZE = 50 * 1024;

typedef enum {
  GST_PLAY_FLAG_VIDEO         = (1 << 0),
  GST_PLAY_FLAG_AUDIO         = (1 << 1),
  GST_PLAY_FLAG_TEXT          = (1 << 2),
  GST_PLAY_FLAG_VIS           = (1 << 3),
  GST_PLAY_FLAG_SOFT_VOLUME   = (1 << 4),
  GST_PLAY_FLAG_NATIVE_AUDIO  = (1 << 5),
  GST_PLAY_FLAG_NATIVE_VIDEO  = (1 << 6),
  GST_PLAY_FLAG_DOWNLOAD      = (1 << 7),
  GST_PLAY_FLAG_BUFFERING     = (1 << 8),
  GST_PLAY_FLAG_DEINTERLACE   = (1 << 9),
  GST_PLAY_FLAG_SOFT_COLORBALANCE = (1 << 10)
} PlayFlags;

GStreamerReader::GStreamerReader(AbstractMediaDecoder* aDecoder)
  : MediaDecoderReader(aDecoder),
#if GST_VERSION_MAJOR >= 1
  mAllocator(nullptr),
  mBufferPool(nullptr),
#endif
  mPlayBin(nullptr),
  mBus(nullptr),
  mSource(nullptr),
  mVideoSink(nullptr),
  mVideoAppSink(nullptr),
  mAudioSink(nullptr),
  mAudioAppSink(nullptr),
  mFormat(GST_VIDEO_FORMAT_UNKNOWN),
  mVideoSinkBufferCount(0),
  mAudioSinkBufferCount(0),
  mGstThreadsMonitor("media.gst.threads"),
  mReachedAudioEos(false),
  mReachedVideoEos(false),
  mByteOffset(0),
  mLastReportedByteOffset(0),
#if GST_VERSION_MAJOR >= 1
  mConfigureAlignment(true),
#endif
  fpsNum(0),
  fpsDen(0)
{
  MOZ_COUNT_CTOR(GStreamerReader);

  mSrcCallbacks.need_data = GStreamerReader::NeedDataCb;
  mSrcCallbacks.enough_data = GStreamerReader::EnoughDataCb;
  mSrcCallbacks.seek_data = GStreamerReader::SeekDataCb;

  mSinkCallbacks.eos = GStreamerReader::EosCb;
  mSinkCallbacks.new_preroll = GStreamerReader::NewPrerollCb;
#if GST_VERSION_MAJOR >= 1
  mSinkCallbacks.new_sample = GStreamerReader::NewBufferCb;
#else
  mSinkCallbacks.new_buffer = GStreamerReader::NewBufferCb;
  mSinkCallbacks.new_buffer_list = nullptr;
#endif

  gst_segment_init(&mVideoSegment, GST_FORMAT_UNDEFINED);
  gst_segment_init(&mAudioSegment, GST_FORMAT_UNDEFINED);
}

GStreamerReader::~GStreamerReader()
{
  MOZ_COUNT_DTOR(GStreamerReader);
  ResetDecode();

  if (mPlayBin) {
    gst_app_src_end_of_stream(mSource);
    if (mSource)
      gst_object_unref(mSource);
    gst_element_set_state(mPlayBin, GST_STATE_NULL);
    gst_object_unref(mPlayBin);
    mPlayBin = nullptr;
    mVideoSink = nullptr;
    mVideoAppSink = nullptr;
    mAudioSink = nullptr;
    mAudioAppSink = nullptr;
    gst_object_unref(mBus);
    mBus = nullptr;
#if GST_VERSION_MAJOR >= 1
    g_object_unref(mAllocator);
    g_object_unref(mBufferPool);
#endif
  }
}

nsresult GStreamerReader::Init(MediaDecoderReader* aCloneDonor)
{
  GError* error = nullptr;
  if (!gst_init_check(0, 0, &error)) {
    LOG(PR_LOG_ERROR, ("gst initialization failed: %s", error->message));
    g_error_free(error);
    return NS_ERROR_FAILURE;
  }

#if GST_VERSION_MAJOR >= 1
  mAllocator = static_cast<GstAllocator*>(g_object_new(GST_TYPE_MOZ_GFX_MEMORY_ALLOCATOR, NULL));
  moz_gfx_memory_allocator_set_reader(mAllocator, this);

  mBufferPool = static_cast<GstBufferPool*>(g_object_new(GST_TYPE_MOZ_GFX_BUFFER_POOL, NULL));
#endif

#if GST_VERSION_MAJOR >= 1
  mPlayBin = gst_element_factory_make("playbin", nullptr);
#else
  mPlayBin = gst_element_factory_make("playbin2", nullptr);
#endif
  if (!mPlayBin) {
    LOG(PR_LOG_ERROR, ("couldn't create playbin"));
    return NS_ERROR_FAILURE;
  }
  g_object_set(mPlayBin, "buffer-size", 0, nullptr);
  mBus = gst_pipeline_get_bus(GST_PIPELINE(mPlayBin));

  mVideoSink = gst_parse_bin_from_description("capsfilter name=filter ! "
      "appsink name=videosink sync=false max-buffers=1 "
#if GST_VERSION_MAJOR >= 1
      "caps=video/x-raw,format=I420"
#else
      "caps=video/x-raw-yuv,format=(fourcc)I420"
#endif
      , TRUE, nullptr);
  mVideoAppSink = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(mVideoSink),
        "videosink"));
  mAudioSink = gst_parse_bin_from_description("capsfilter name=filter ! "
        "appsink name=audiosink sync=false max-buffers=1", TRUE, nullptr);
  mAudioAppSink = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(mAudioSink),
                                                   "audiosink"));
  GstCaps* caps = BuildAudioSinkCaps();
  g_object_set(mAudioAppSink, "caps", caps, nullptr);
  gst_caps_unref(caps);

  gst_app_sink_set_callbacks(mVideoAppSink, &mSinkCallbacks,
      (gpointer) this, nullptr);
  gst_app_sink_set_callbacks(mAudioAppSink, &mSinkCallbacks,
                             (gpointer) this, nullptr);
  InstallPadCallbacks();

  g_object_set(mPlayBin, "uri", "appsrc://",
               "video-sink", mVideoSink,
               "audio-sink", mAudioSink,
               nullptr);

  g_signal_connect(G_OBJECT(mPlayBin), "notify::source",
                   G_CALLBACK(GStreamerReader::PlayBinSourceSetupCb), this);
  g_signal_connect(G_OBJECT(mPlayBin), "element-added",
                   G_CALLBACK(GStreamerReader::PlayElementAddedCb), this);

  g_signal_connect(G_OBJECT(mPlayBin), "element-added",
                   G_CALLBACK(GStreamerReader::ElementAddedCb), this);

  return NS_OK;
}

GstBusSyncReply
GStreamerReader::ErrorCb(GstBus *aBus, GstMessage *aMessage, gpointer aUserData)
{
  return static_cast<GStreamerReader*>(aUserData)->Error(aBus, aMessage);
}

GstBusSyncReply
GStreamerReader::Error(GstBus *aBus, GstMessage *aMessage)
{
  if (GST_MESSAGE_TYPE(aMessage) == GST_MESSAGE_ERROR) {
    Eos();
  }

  return GST_BUS_PASS;
}

void GStreamerReader::ElementAddedCb(GstBin *aPlayBin,
                                     GstElement *aElement,
                                     gpointer aUserData)
{
  const gchar *name =
    gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(gst_element_get_factory(aElement)));

  if (!strcmp(name, "uridecodebin")) {
    g_signal_connect(G_OBJECT(aElement), "autoplug-sort",
                     G_CALLBACK(GStreamerReader::ElementFilterCb), aUserData);
  }
}

GValueArray *GStreamerReader::ElementFilterCb(GstURIDecodeBin *aBin,
                                              GstPad *aPad,
                                              GstCaps *aCaps,
                                              GValueArray *aFactories,
                                              gpointer aUserData)
{
  return ((GStreamerReader*)aUserData)->ElementFilter(aBin, aPad, aCaps, aFactories);
}

GValueArray *GStreamerReader::ElementFilter(GstURIDecodeBin *aBin,
                                            GstPad *aPad,
                                            GstCaps *aCaps,
                                            GValueArray *aFactories)
{
  GValueArray *filtered = g_value_array_new(aFactories->n_values);

  for (unsigned int i = 0; i < aFactories->n_values; i++) {
    GValue *value = &aFactories->values[i];
    GstPluginFeature *factory = GST_PLUGIN_FEATURE(g_value_peek_pointer(value));

    if (!GStreamerFormatHelper::IsPluginFeatureBlacklisted(factory)) {
      g_value_array_append(filtered, value);
    }
  }

  return filtered;
}

void GStreamerReader::PlayBinSourceSetupCb(GstElement* aPlayBin,
                                           GParamSpec* pspec,
                                           gpointer aUserData)
{
  GstElement *source;
  GStreamerReader* reader = reinterpret_cast<GStreamerReader*>(aUserData);

  g_object_get(aPlayBin, "source", &source, NULL);
  reader->PlayBinSourceSetup(GST_APP_SRC(source));
}

void GStreamerReader::PlayBinSourceSetup(GstAppSrc* aSource)
{
  mSource = GST_APP_SRC(aSource);
  gst_app_src_set_callbacks(mSource, &mSrcCallbacks, (gpointer) this, nullptr);
  MediaResource* resource = mDecoder->GetResource();

  /* do a short read to trigger a network request so that GetLength() below
   * returns something meaningful and not -1
   */
  char buf[512];
  unsigned int size = 0;
  resource->Read(buf, sizeof(buf), &size);
  resource->Seek(SEEK_SET, 0);

  /* now we should have a length */
  int64_t resourceLength = resource->GetLength();
  gst_app_src_set_size(mSource, resourceLength);
  if (resource->IsDataCachedToEndOfResource(0) ||
      (resourceLength != -1 && resourceLength <= SHORT_FILE_SIZE)) {
    /* let the demuxer work in pull mode for local files (or very short files)
     * so that we get optimal seeking accuracy/performance
     */
    LOG(PR_LOG_DEBUG, ("configuring random access, len %lld", resourceLength));
    gst_app_src_set_stream_type(mSource, GST_APP_STREAM_TYPE_RANDOM_ACCESS);
  } else {
    /* make the demuxer work in push mode so that seeking is kept to a minimum
     */
    LOG(PR_LOG_DEBUG, ("configuring push mode, len %lld", resourceLength));
    gst_app_src_set_stream_type(mSource, GST_APP_STREAM_TYPE_SEEKABLE);
  }

  // Set the source MIME type to stop typefind trying every. single. format.
  GstCaps *caps = GStreamerFormatHelper::ConvertFormatsToCaps(mDecoder->GetResource()->GetContentType().get(),
                                                              nullptr);
  gst_app_src_set_caps(aSource, caps);
  gst_caps_unref(caps);
}

nsresult GStreamerReader::ReadMetadata(VideoInfo* aInfo,
                                       MetadataTags** aTags)
{
  NS_ASSERTION(mDecoder->OnDecodeThread(), "Should be on decode thread.");
  nsresult ret = NS_OK;

  /* We do 3 attempts here: decoding audio and video, decoding video only,
   * decoding audio only. This allows us to play streams that have one broken
   * stream but that are otherwise decodeable.
   */
  guint flags[3] = {GST_PLAY_FLAG_VIDEO|GST_PLAY_FLAG_AUDIO,
    static_cast<guint>(~GST_PLAY_FLAG_AUDIO), static_cast<guint>(~GST_PLAY_FLAG_VIDEO)};
  guint default_flags, current_flags;
  g_object_get(mPlayBin, "flags", &default_flags, nullptr);

  GstMessage* message = nullptr;
  for (unsigned int i = 0; i < G_N_ELEMENTS(flags); i++) {
    current_flags = default_flags & flags[i];
    g_object_set(G_OBJECT(mPlayBin), "flags", current_flags, nullptr);

    /* reset filter caps to ANY */
    GstCaps* caps = gst_caps_new_any();
    GstElement* filter = gst_bin_get_by_name(GST_BIN(mAudioSink), "filter");
    g_object_set(filter, "caps", caps, nullptr);
    gst_object_unref(filter);

    filter = gst_bin_get_by_name(GST_BIN(mVideoSink), "filter");
    g_object_set(filter, "caps", caps, nullptr);
    gst_object_unref(filter);
    gst_caps_unref(caps);
    filter = nullptr;

    if (!(current_flags & GST_PLAY_FLAG_AUDIO))
      filter = gst_bin_get_by_name(GST_BIN(mAudioSink), "filter");
    else if (!(current_flags & GST_PLAY_FLAG_VIDEO))
      filter = gst_bin_get_by_name(GST_BIN(mVideoSink), "filter");

    if (filter) {
      /* Little trick: set the target caps to "skip" so that playbin2 fails to
       * find a decoder for the stream we want to skip.
       */
      GstCaps* filterCaps = gst_caps_new_simple ("skip", nullptr, nullptr);
      g_object_set(filter, "caps", filterCaps, nullptr);
      gst_caps_unref(filterCaps);
      gst_object_unref(filter);
    }

    /* start the pipeline */
    LOG(PR_LOG_DEBUG, ("starting metadata pipeline"));
    if (gst_element_set_state(mPlayBin, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
      LOG(PR_LOG_DEBUG, ("metadata pipeline state change failed"));
      ret = NS_ERROR_FAILURE;
      continue;
    }

    /* Wait for ASYNC_DONE, which is emitted when the pipeline is built,
     * prerolled and ready to play. Also watch for errors.
     */
    message = gst_bus_timed_pop_filtered(mBus, GST_CLOCK_TIME_NONE,
                 (GstMessageType)(GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ASYNC_DONE) {
      LOG(PR_LOG_DEBUG, ("read metadata pipeline prerolled"));
      gst_message_unref(message);
      ret = NS_OK;
      break;
    } else {
      LOG(PR_LOG_DEBUG, ("read metadata pipeline failed to preroll: %s",
            gst_message_type_get_name (GST_MESSAGE_TYPE (message))));

      if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError* error;
        gchar* debug;
        gst_message_parse_error(message, &error, &debug);
        LOG(PR_LOG_ERROR, ("read metadata error: %s: %s", error->message,
                           debug));
        g_error_free(error);
        g_free(debug);
      }
      /* Unexpected stream close/EOS or other error. We'll give up if all
       * * streams are in error/EOS. */
      gst_element_set_state(mPlayBin, GST_STATE_NULL);
      gst_message_unref(message);
      ret = NS_ERROR_FAILURE;
    }
  }

  if (NS_SUCCEEDED(ret))
    ret = CheckSupportedFormats();

  if (NS_FAILED(ret))
    /* we couldn't get this to play */
    return ret;

  /* report the duration */
  gint64 duration;
#if GST_VERSION_MAJOR >= 1
  if (gst_element_query_duration(GST_ELEMENT(mPlayBin),
      GST_FORMAT_TIME, &duration)) {
#else
  GstFormat format = GST_FORMAT_TIME;
  if (gst_element_query_duration(GST_ELEMENT(mPlayBin),
      &format, &duration) && format == GST_FORMAT_TIME) {
#endif
    ReentrantMonitorAutoEnter mon(mDecoder->GetReentrantMonitor());
    LOG(PR_LOG_DEBUG, ("returning duration %" GST_TIME_FORMAT,
          GST_TIME_ARGS (duration)));
    duration = GST_TIME_AS_USECONDS (duration);
    mDecoder->SetMediaDuration(duration);
  } else {
    mDecoder->SetMediaSeekable(false);
  }

  int n_video = 0, n_audio = 0;
  g_object_get(mPlayBin, "n-video", &n_video, "n-audio", &n_audio, nullptr);
  mInfo.mHasVideo = n_video != 0;
  mInfo.mHasAudio = n_audio != 0;

  *aInfo = mInfo;

  *aTags = nullptr;

  // Watch the pipeline for fatal errors
#if GST_VERSION_MAJOR >= 1
  gst_bus_set_sync_handler(mBus, GStreamerReader::ErrorCb, this, nullptr);
#else
  gst_bus_set_sync_handler(mBus, GStreamerReader::ErrorCb, this);
#endif

  /* set the pipeline to PLAYING so that it starts decoding and queueing data in
   * the appsinks */
  gst_element_set_state(mPlayBin, GST_STATE_PLAYING);

  return NS_OK;
}

nsresult GStreamerReader::CheckSupportedFormats()
{
  bool done = false;
  bool unsupported = false;

  GstIterator* it = gst_bin_iterate_recurse(GST_BIN(mPlayBin));
  while (!done) {
    GstIteratorResult res;
    GstElement* element;

#if GST_VERSION_MAJOR >= 1
    GValue value = {0,};
    res = gst_iterator_next(it, &value);
#else
    res = gst_iterator_next(it, (void **) &element);
#endif
    switch(res) {
      case GST_ITERATOR_OK:
      {
#if GST_VERSION_MAJOR >= 1
        element = GST_ELEMENT (g_value_get_object (&value));
#endif
        GstElementFactory* factory = gst_element_get_factory(element);
        if (factory) {
          const char* klass = gst_element_factory_get_klass(factory);
          GstPad* pad = gst_element_get_static_pad(element, "sink");
          if (pad) {
            GstCaps* caps;

#if GST_VERSION_MAJOR >= 1
            caps = gst_pad_get_current_caps(pad);
#else
            caps = gst_pad_get_negotiated_caps(pad);
#endif

            if (caps) {
              /* check for demuxers but ignore elements like id3demux */
              if (strstr (klass, "Demuxer") && !strstr(klass, "Metadata"))
                unsupported = !GStreamerFormatHelper::Instance()->CanHandleContainerCaps(caps);
              else if (strstr (klass, "Decoder") && !strstr(klass, "Generic"))
                unsupported = !GStreamerFormatHelper::Instance()->CanHandleCodecCaps(caps);

              gst_caps_unref(caps);
            }
            gst_object_unref(pad);
          }
        }

#if GST_VERSION_MAJOR >= 1
        g_value_unset (&value);
#else
	    gst_object_unref(element);
#endif
        done = unsupported;
        break;
      }
      case GST_ITERATOR_RESYNC:
        unsupported = false;
        break;
      case GST_ITERATOR_ERROR:
        done = true;
        break;
      case GST_ITERATOR_DONE:
        done = true;
        break;
    }
  }

  gst_iterator_free(it);

  return unsupported ? NS_ERROR_FAILURE : NS_OK;
}

nsresult GStreamerReader::ResetDecode()
{
  nsresult res = NS_OK;

  LOG(PR_LOG_DEBUG, ("reset decode"));

  if (NS_FAILED(MediaDecoderReader::ResetDecode())) {
    res = NS_ERROR_FAILURE;
  }

  mVideoQueue.Reset();
  mAudioQueue.Reset();

  mVideoSinkBufferCount = 0;
  mAudioSinkBufferCount = 0;
  mReachedAudioEos = false;
  mReachedVideoEos = false;
#if GST_VERSION_MAJOR >= 1
  mConfigureAlignment = true;
#endif
  mLastReportedByteOffset = 0;
  mByteOffset = 0;

  LOG(PR_LOG_DEBUG, ("reset decode done"));

  return res;
}

void GStreamerReader::NotifyBytesConsumed()
{
  NS_ASSERTION(mByteOffset >= mLastReportedByteOffset,
      "current byte offset less than prev offset");
  mDecoder->NotifyBytesConsumed(mByteOffset - mLastReportedByteOffset);
  mLastReportedByteOffset = mByteOffset;
}

bool GStreamerReader::DecodeAudioData()
{
  NS_ASSERTION(mDecoder->OnDecodeThread(), "Should be on decode thread.");

  GstBuffer *buffer = nullptr;

  {
    ReentrantMonitorAutoEnter mon(mGstThreadsMonitor);

    if (mReachedAudioEos && !mAudioSinkBufferCount) {
      mAudioQueue.Finish();
      return false;
    }

    /* Wait something to be decoded before return or continue */
    if (!mAudioSinkBufferCount) {
      if(!mVideoSinkBufferCount) {
        /* We have nothing decoded so it makes no sense to return to the state machine
         * as it will call us back immediately, we'll return again and so on, wasting
         * CPU cycles for no job done. So, block here until there is either video or
         * audio data available
        */
        mon.Wait();
        if (!mAudioSinkBufferCount) {
          /* There is still no audio data available, so either there is video data or
           * something else has happened (Eos, etc...). Return to the state machine
           * to process it.
           */
          return true;
        }
      }
      else {
        return true;
      }
    }

    NotifyBytesConsumed();
#if GST_VERSION_MAJOR >= 1
    GstSample *sample = gst_app_sink_pull_sample(mAudioAppSink);
    buffer = gst_buffer_ref(gst_sample_get_buffer(sample));
    gst_sample_unref(sample);
#else
    buffer = gst_app_sink_pull_buffer(mAudioAppSink);
#endif
    mAudioSinkBufferCount--;
  }

  int64_t timestamp = GST_BUFFER_TIMESTAMP(buffer);
  timestamp = gst_segment_to_stream_time(&mAudioSegment,
      GST_FORMAT_TIME, timestamp);

  LOG(PR_LOG_DEBUG, ("audio %" GST_TIME_FORMAT,
        GST_TIME_ARGS(timestamp)));
  timestamp = GST_TIME_AS_USECONDS(timestamp);
  int64_t duration = 0;
  if (GST_CLOCK_TIME_IS_VALID(GST_BUFFER_DURATION(buffer)))
    duration = GST_TIME_AS_USECONDS(GST_BUFFER_DURATION(buffer));

  int64_t offset = GST_BUFFER_OFFSET(buffer);
#if GST_VERSION_MAJOR >= 1
  GstMapInfo info;
  gst_buffer_map(buffer, &info, GST_MAP_READ);
  unsigned int size = info.size;
#else
  unsigned int size = GST_BUFFER_SIZE(buffer);
#endif
  int32_t frames = (size / sizeof(AudioDataValue)) / mInfo.mAudioChannels;
  ssize_t outSize = static_cast<size_t>(size / sizeof(AudioDataValue));
  nsAutoArrayPtr<AudioDataValue> data(new AudioDataValue[outSize]);
#if GST_VERSION_MAJOR >= 1
  memcpy(data, info.data, info.size);
  gst_buffer_unmap(buffer, &info);
#else
  memcpy(data, GST_BUFFER_DATA(buffer), GST_BUFFER_SIZE(buffer));
#endif
  AudioData* audio = new AudioData(offset, timestamp, duration,
      frames, data.forget(), mInfo.mAudioChannels);

  mAudioQueue.Push(audio);
  gst_buffer_unref(buffer);

  return true;
}

bool GStreamerReader::DecodeVideoFrame(bool &aKeyFrameSkip,
                                       int64_t aTimeThreshold)
{
  NS_ASSERTION(mDecoder->OnDecodeThread(), "Should be on decode thread.");

  GstBuffer *buffer = nullptr;

  {
    ReentrantMonitorAutoEnter mon(mGstThreadsMonitor);

    if (mReachedVideoEos && !mVideoSinkBufferCount) {
      mVideoQueue.Finish();
      return false;
    }

    /* Wait something to be decoded before return or continue */
    if (!mVideoSinkBufferCount) {
      if (!mAudioSinkBufferCount) {
        /* We have nothing decoded so it makes no sense to return to the state machine
         * as it will call us back immediately, we'll return again and so on, wasting
         * CPU cycles for no job done. So, block here until there is either video or
         * audio data available
        */
        mon.Wait();
        if (!mVideoSinkBufferCount) {
          /* There is still no video data available, so either there is audio data or
           * something else has happened (Eos, etc...). Return to the state machine
           * to process it
           */
          return true;
        }
      }
      else {
        return true;
      }
    }

    NotifyBytesConsumed();
    mDecoder->NotifyDecodedFrames(0, 1);

#if GST_VERSION_MAJOR >= 1
    GstSample *sample = gst_app_sink_pull_sample(mVideoAppSink);
    buffer = gst_buffer_ref(gst_sample_get_buffer(sample));
    gst_sample_unref(sample);
#else
    buffer = gst_app_sink_pull_buffer(mVideoAppSink);
#endif
    mVideoSinkBufferCount--;
  }

  bool isKeyframe = !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  if ((aKeyFrameSkip && !isKeyframe)) {
    gst_buffer_unref(buffer);
    return true;
  }

  int64_t timestamp = GST_BUFFER_TIMESTAMP(buffer);
  {
    ReentrantMonitorAutoEnter mon(mGstThreadsMonitor);
    timestamp = gst_segment_to_stream_time(&mVideoSegment,
                                           GST_FORMAT_TIME, timestamp);
  }
  NS_ASSERTION(GST_CLOCK_TIME_IS_VALID(timestamp),
               "frame has invalid timestamp");
  LOG(PR_LOG_DEBUG, ("num queued frames %d, current video ts %" GST_TIME_FORMAT,
        mVideoSinkBufferCount, GST_TIME_ARGS(timestamp)));
  int64_t nextTimestamp = timestamp = GST_TIME_AS_USECONDS(timestamp);
  if (GST_CLOCK_TIME_IS_VALID(GST_BUFFER_DURATION(buffer)))
    nextTimestamp += GST_TIME_AS_USECONDS(GST_BUFFER_DURATION(buffer));
  else if (fpsNum && fpsDen)
    /* add 1-frame duration */
    nextTimestamp += gst_util_uint64_scale(GST_USECOND, fpsNum, fpsDen);

  if (timestamp < aTimeThreshold) {
    LOG(PR_LOG_DEBUG, ("skipping frame %" GST_TIME_FORMAT
                       " threshold %" GST_TIME_FORMAT,
                       GST_TIME_ARGS(timestamp), GST_TIME_ARGS(aTimeThreshold)));
    gst_buffer_unref(buffer);
    return true;
  }

  if (!buffer)
    /* no more frames */
    return true;

#if GST_VERSION_MAJOR >= 1
  if (mConfigureAlignment && buffer->pool) {
    GstStructure *config = gst_buffer_pool_get_config(buffer->pool);
    GstVideoAlignment align;
    if (gst_buffer_pool_config_get_video_alignment(config, &align))
      gst_video_info_align(&mVideoInfo, &align);
    gst_structure_free(config);
	mConfigureAlignment = false;
  }
#endif

  nsRefPtr<PlanarYCbCrImage> image = GetImageFromBuffer(buffer);

  if (!image) {
    /* Ugh, upstream is not calling gst_pad_alloc_buffer(). Fallback to
     * allocating a PlanarYCbCrImage backed GstBuffer here and memcpy.
     */
    GstBuffer* tmp = nullptr;
    CopyIntoImageBuffer(buffer, &tmp, image);
    gst_buffer_unref(buffer);
    buffer = tmp;
  }

#if GST_VERSION_MAJOR == 0
  VideoData::YCbCrBuffer buf;
  FillYCbCrBuffer(buffer, &buf);
#endif
  int64_t offset = 0;
#if GST_VERSION_MAJOR >= 1
  VideoData* video = VideoData::CreateFromImage(mInfo,
                                       GetImageContainer(),
                                       offset, timestamp, nextTimestamp,
                                       static_cast<Image*>(image.get()),
                                       isKeyframe, -1, mPicture);
#else
  VideoData* video = VideoData::Create(mInfo, image, offset,
                                       timestamp, nextTimestamp, buf,
                                       isKeyframe, -1, mPicture);
#endif
  mVideoQueue.Push(video);

  gst_buffer_unref(buffer);
  return true;
}

nsresult GStreamerReader::Seek(int64_t aTarget,
                                 int64_t aStartTime,
                                 int64_t aEndTime,
                                 int64_t aCurrentTime)
{
  NS_ASSERTION(mDecoder->OnDecodeThread(), "Should be on decode thread.");

  ResetDecode();

  gint64 seekPos = aTarget * GST_USECOND;
  LOG(PR_LOG_DEBUG, ("%p About to seek to %" GST_TIME_FORMAT,
        mDecoder, GST_TIME_ARGS(seekPos)));

  if (!gst_element_seek_simple(mPlayBin, GST_FORMAT_TIME,
    static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE), seekPos)) {
    LOG(PR_LOG_ERROR, ("seek failed"));
    return NS_ERROR_FAILURE;
  }
  LOG(PR_LOG_DEBUG, ("seek succeeded"));
  GstMessage* message = gst_bus_timed_pop_filtered(mBus, GST_CLOCK_TIME_NONE,
               (GstMessageType)(GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_ERROR));
  gst_message_unref(message);
  LOG(PR_LOG_DEBUG, ("seek completed"));

  return DecodeToTarget(aTarget);
}

nsresult GStreamerReader::GetBuffered(TimeRanges* aBuffered,
                                      int64_t aStartTime)
{
  if (!mInfo.mHasVideo && !mInfo.mHasAudio) {
    return NS_OK;
  }

#if GST_VERSION_MAJOR == 0
  GstFormat format = GST_FORMAT_TIME;
#endif
  MediaResource* resource = mDecoder->GetResource();
  nsTArray<MediaByteRange> ranges;
  resource->GetCachedRanges(ranges);

  if (mDecoder->OnStateMachineThread())
    /* Report the position from here while buffering as we can't report it from
     * the gstreamer threads that are actually reading from the resource
     */
    NotifyBytesConsumed();

  if (resource->IsDataCachedToEndOfResource(0)) {
    /* fast path for local or completely cached files */
    gint64 duration = 0;

    duration = QueryDuration();
    double end = (double) duration / GST_MSECOND;
    LOG(PR_LOG_DEBUG, ("complete range [0, %f] for [0, %li]",
          end, resource->GetLength()));
    aBuffered->Add(0, end);
    return NS_OK;
  }

  for(uint32_t index = 0; index < ranges.Length(); index++) {
    int64_t startOffset = ranges[index].mStart;
    int64_t endOffset = ranges[index].mEnd;
    gint64 startTime, endTime;

#if GST_VERSION_MAJOR >= 1
    if (!gst_element_query_convert(GST_ELEMENT(mPlayBin), GST_FORMAT_BYTES,
      startOffset, GST_FORMAT_TIME, &startTime))
      continue;
    if (!gst_element_query_convert(GST_ELEMENT(mPlayBin), GST_FORMAT_BYTES,
      endOffset, GST_FORMAT_TIME, &endTime))
      continue;
#else
    if (!gst_element_query_convert(GST_ELEMENT(mPlayBin), GST_FORMAT_BYTES,
      startOffset, &format, &startTime) || format != GST_FORMAT_TIME)
      continue;
    if (!gst_element_query_convert(GST_ELEMENT(mPlayBin), GST_FORMAT_BYTES,
      endOffset, &format, &endTime) || format != GST_FORMAT_TIME)
      continue;
#endif

    double start = (double) GST_TIME_AS_USECONDS (startTime) / GST_MSECOND;
    double end = (double) GST_TIME_AS_USECONDS (endTime) / GST_MSECOND;
    LOG(PR_LOG_DEBUG, ("adding range [%f, %f] for [%li %li] size %li",
          start, end, startOffset, endOffset, resource->GetLength()));
    aBuffered->Add(start, end);
  }

  return NS_OK;
}

void GStreamerReader::ReadAndPushData(guint aLength)
{
  MediaResource* resource = mDecoder->GetResource();
  NS_ASSERTION(resource, "Decoder has no media resource");
  nsresult rv = NS_OK;

  GstBuffer* buffer = gst_buffer_new_and_alloc(aLength);
#if GST_VERSION_MAJOR >= 1
  GstMapInfo info;
  gst_buffer_map(buffer, &info, GST_MAP_WRITE);
  guint8 *data = info.data;
#else
  guint8* data = GST_BUFFER_DATA(buffer);
#endif
  uint32_t size = 0, bytesRead = 0;
  while(bytesRead < aLength) {
    rv = resource->Read(reinterpret_cast<char*>(data + bytesRead),
        aLength - bytesRead, &size);
    if (NS_FAILED(rv) || size == 0)
      break;

    bytesRead += size;
  }

#if GST_VERSION_MAJOR >= 1
  info.size = bytesRead;
  gst_buffer_unmap(buffer, &info);
#else
  GST_BUFFER_SIZE(buffer) = bytesRead;
#endif
  mByteOffset += bytesRead;

  GstFlowReturn ret = gst_app_src_push_buffer(mSource, gst_buffer_ref(buffer));
  if (ret != GST_FLOW_OK) {
    LOG(PR_LOG_ERROR, ("ReadAndPushData push ret %s", gst_flow_get_name(ret)));
  }

  if (bytesRead < aLength) {
    /* If we read less than what we wanted, we reached the end */
    gst_app_src_end_of_stream(mSource);
  }

  gst_buffer_unref(buffer);
}

int64_t GStreamerReader::QueryDuration()
{
  gint64 duration = 0;
  GstFormat format = GST_FORMAT_TIME;

#if GST_VERSION_MAJOR >= 1
  if (gst_element_query_duration(GST_ELEMENT(mPlayBin),
      format, &duration)) {
#else
  if (gst_element_query_duration(GST_ELEMENT(mPlayBin),
      &format, &duration)) {
#endif
    if (format == GST_FORMAT_TIME) {
      LOG(PR_LOG_DEBUG, ("pipeline duration %" GST_TIME_FORMAT,
            GST_TIME_ARGS (duration)));
      duration = GST_TIME_AS_USECONDS (duration);
    }
  }

  {
    ReentrantMonitorAutoEnter mon(mDecoder->GetReentrantMonitor());
    int64_t media_duration = mDecoder->GetMediaDuration();
    if (media_duration != -1 && media_duration > duration) {
      // We decoded more than the reported duration (which could be estimated)
      LOG(PR_LOG_DEBUG, ("decoded duration > estimated duration"));
      duration = media_duration;
    }
  }

  return duration;
}

void GStreamerReader::NeedDataCb(GstAppSrc* aSrc,
                                 guint aLength,
                                 gpointer aUserData)
{
  GStreamerReader* reader = reinterpret_cast<GStreamerReader*>(aUserData);
  reader->NeedData(aSrc, aLength);
}

void GStreamerReader::NeedData(GstAppSrc* aSrc, guint aLength)
{
  if (aLength == static_cast<guint>(-1))
    aLength = DEFAULT_SOURCE_READ_SIZE;
  ReadAndPushData(aLength);
}

void GStreamerReader::EnoughDataCb(GstAppSrc* aSrc, gpointer aUserData)
{
  GStreamerReader* reader = reinterpret_cast<GStreamerReader*>(aUserData);
  reader->EnoughData(aSrc);
}

void GStreamerReader::EnoughData(GstAppSrc* aSrc)
{
}

gboolean GStreamerReader::SeekDataCb(GstAppSrc* aSrc,
                                     guint64 aOffset,
                                     gpointer aUserData)
{
  GStreamerReader* reader = reinterpret_cast<GStreamerReader*>(aUserData);
  return reader->SeekData(aSrc, aOffset);
}

gboolean GStreamerReader::SeekData(GstAppSrc* aSrc, guint64 aOffset)
{
  ReentrantMonitorAutoEnter mon(mGstThreadsMonitor);
  MediaResource* resource = mDecoder->GetResource();
  int64_t resourceLength = resource->GetLength();

  if (gst_app_src_get_size(mSource) == -1) {
    /* It's possible that we didn't know the length when we initialized mSource
     * but maybe we do now
     */
    gst_app_src_set_size(mSource, resourceLength);
  }

  nsresult rv = NS_ERROR_FAILURE;
  if (aOffset < static_cast<guint64>(resourceLength)) {
    rv = resource->Seek(SEEK_SET, aOffset);
  }

  if (NS_SUCCEEDED(rv)) {
    mByteOffset = mLastReportedByteOffset = aOffset;
  } else {
    LOG(PR_LOG_ERROR, ("seek at %lu failed", aOffset));
  }

  return NS_SUCCEEDED(rv);
}

GstFlowReturn GStreamerReader::NewPrerollCb(GstAppSink* aSink,
                                              gpointer aUserData)
{
  GStreamerReader* reader = reinterpret_cast<GStreamerReader*>(aUserData);

  if (aSink == reader->mVideoAppSink)
    reader->VideoPreroll();
  else
    reader->AudioPreroll();
  return GST_FLOW_OK;
}

void GStreamerReader::AudioPreroll()
{
  /* The first audio buffer has reached the audio sink. Get rate and channels */
  LOG(PR_LOG_DEBUG, ("Audio preroll"));
  GstPad* sinkpad = gst_element_get_static_pad(GST_ELEMENT(mAudioAppSink), "sink");
#if GST_VERSION_MAJOR >= 1
  GstCaps *caps = gst_pad_get_current_caps(sinkpad);
#else
  GstCaps* caps = gst_pad_get_negotiated_caps(sinkpad);
#endif
  GstStructure* s = gst_caps_get_structure(caps, 0);
  mInfo.mAudioRate = mInfo.mAudioChannels = 0;
  gst_structure_get_int(s, "rate", (gint*) &mInfo.mAudioRate);
  gst_structure_get_int(s, "channels", (gint*) &mInfo.mAudioChannels);
  NS_ASSERTION(mInfo.mAudioRate != 0, ("audio rate is zero"));
  NS_ASSERTION(mInfo.mAudioChannels != 0, ("audio channels is zero"));
  NS_ASSERTION(mInfo.mAudioChannels > 0 && mInfo.mAudioChannels <= MAX_CHANNELS,
      "invalid audio channels number");
  mInfo.mHasAudio = true;
  gst_caps_unref(caps);
  gst_object_unref(sinkpad);
}

void GStreamerReader::VideoPreroll()
{
  /* The first video buffer has reached the video sink. Get width and height */
  LOG(PR_LOG_DEBUG, ("Video preroll"));
  GstPad* sinkpad = gst_element_get_static_pad(GST_ELEMENT(mVideoAppSink), "sink");
#if GST_VERSION_MAJOR >= 1
  GstCaps* caps = gst_pad_get_current_caps(sinkpad);
  memset (&mVideoInfo, 0, sizeof (mVideoInfo));
  gst_video_info_from_caps(&mVideoInfo, caps);
  mFormat = mVideoInfo.finfo->format;
  mPicture.width = mVideoInfo.width;
  mPicture.height = mVideoInfo.height;
#else
  GstCaps* caps = gst_pad_get_negotiated_caps(sinkpad);
  gst_video_format_parse_caps(caps, &mFormat, &mPicture.width, &mPicture.height);
#endif
  GstStructure* structure = gst_caps_get_structure(caps, 0);
  gst_structure_get_fraction(structure, "framerate", &fpsNum, &fpsDen);
  NS_ASSERTION(mPicture.width && mPicture.height, "invalid video resolution");
  mInfo.mDisplay = nsIntSize(mPicture.width, mPicture.height);
  mInfo.mHasVideo = true;
  gst_caps_unref(caps);
  gst_object_unref(sinkpad);
}

GstFlowReturn GStreamerReader::NewBufferCb(GstAppSink* aSink,
                                           gpointer aUserData)
{
  GStreamerReader* reader = reinterpret_cast<GStreamerReader*>(aUserData);

  if (aSink == reader->mVideoAppSink)
    reader->NewVideoBuffer();
  else
    reader->NewAudioBuffer();

  return GST_FLOW_OK;
}

void GStreamerReader::NewVideoBuffer()
{
  ReentrantMonitorAutoEnter mon(mGstThreadsMonitor);
  /* We have a new video buffer queued in the video sink. Increment the counter
   * and notify the decode thread potentially blocked in DecodeVideoFrame
   */
  mDecoder->NotifyDecodedFrames(1, 0);
  mVideoSinkBufferCount++;
  mon.NotifyAll();
}

void GStreamerReader::NewAudioBuffer()
{
  ReentrantMonitorAutoEnter mon(mGstThreadsMonitor);
  /* We have a new audio buffer queued in the audio sink. Increment the counter
   * and notify the decode thread potentially blocked in DecodeAudioData
   */
  mAudioSinkBufferCount++;
  mon.NotifyAll();
}

void GStreamerReader::EosCb(GstAppSink* aSink, gpointer aUserData)
{
  GStreamerReader* reader = reinterpret_cast<GStreamerReader*>(aUserData);
  reader->Eos(aSink);
}

void GStreamerReader::Eos(GstAppSink* aSink)
{
  /* We reached the end of the stream */
  {
    ReentrantMonitorAutoEnter mon(mGstThreadsMonitor);
    /* Potentially unblock DecodeVideoFrame and DecodeAudioData */
    if (aSink == mVideoAppSink) {
      mReachedVideoEos = true;
    } else if (aSink == mAudioAppSink) {
      mReachedAudioEos = true;
    } else {
      // Assume this is an error causing an EOS.
      mReachedAudioEos = true;
      mReachedVideoEos = true;
    }
    mon.NotifyAll();
  }

  {
    ReentrantMonitorAutoEnter mon(mDecoder->GetReentrantMonitor());
    /* Potentially unblock the decode thread in ::DecodeLoop */
    mVideoQueue.Finish();
    mAudioQueue.Finish();
    mon.NotifyAll();
  }
}

/**
 * This callback is called while the pipeline is automatically built, after a
 * new element has been added to the pipeline. We use it to find the
 * uridecodebin instance used by playbin and connect to it to apply our
 * blacklist.
 */
void
GStreamerReader::PlayElementAddedCb(GstBin *aBin, GstElement *aElement,
                                    gpointer *aUserData)
{
  const static char sUriDecodeBinPrefix[] = "uridecodebin";
  gchar *name = gst_element_get_name(aElement);

  // Attach this callback to uridecodebin, child of playbin.
  if (!strncmp(name, sUriDecodeBinPrefix, sizeof(sUriDecodeBinPrefix) - 1)) {
  #if GST_CHECK_VERSION(1,2,3)
    g_signal_connect(G_OBJECT(aElement), "autoplug-select",
                     G_CALLBACK(GStreamerReader::AutoplugSelectCb), aUserData);
  #else
    /* autoplug-select is broken in older GStreamer versions. We can (ab)use
     * autoplug-sort to achieve the same effect.
     */
    g_signal_connect(G_OBJECT(aElement), "autoplug-sort",
                     G_CALLBACK(GStreamerReader::AutoplugSortCb), aUserData);
  #endif
  }

  g_free(name);
}

bool
GStreamerReader::ShouldAutoplugFactory(GstElementFactory* aFactory, GstCaps* aCaps)
{
  bool autoplug;
  const gchar *klass = gst_element_factory_get_klass(aFactory);
  if (strstr(klass, "Demuxer") && !strstr(klass, "Metadata")) {
    autoplug = GStreamerFormatHelper::Instance()->CanHandleContainerCaps(aCaps);
  } else if (strstr(klass, "Decoder") && !strstr(klass, "Generic")) {
    autoplug = GStreamerFormatHelper::Instance()->CanHandleCodecCaps(aCaps);
  } else {
    /* we only filter demuxers and decoders, let everything else be autoplugged */
    autoplug = true;
  }

  return autoplug;
}

/**
 * This is called by uridecodebin (running inside playbin), after it has found
 * candidate factories to continue decoding the stream. We apply the blacklist
 * here, disallowing known-crashy plugins.
 */
#if GST_CHECK_VERSION(1,2,3)
GstAutoplugSelectResult
GStreamerReader::AutoplugSelectCb(GstElement* aDecodeBin, GstPad* aPad,
                                  GstCaps* aCaps, GstElementFactory* aFactory,
                                  void* aGroup)
{
  if (!ShouldAutoplugFactory(aFactory, aCaps)) {
    /* We don't support this factory. Skip it to stop decoding this (substream) */
  return GST_AUTOPLUG_SELECT_SKIP;
  }

  return GST_AUTOPLUG_SELECT_TRY;
}
#else
GValueArray*
GStreamerReader::AutoplugSortCb(GstElement* aElement, GstPad* aPad,
                                GstCaps* aCaps, GValueArray* aFactories)
{
  if (!aFactories->n_values) {
    return nullptr;
  }

  /* aFactories[0] is the element factory that is going to be used to
   * create the next element needed to demux or decode the stream.
   */
  GstElementFactory *factory = (GstElementFactory*) g_value_get_object(g_value_array_get_nth(aFactories, 0));
  if (!ShouldAutoplugFactory(factory, aCaps)) {
    /* We don't support this factory. Return an empty array to signal that we
     * don't want to continue decoding this (sub)stream.
     */
    return g_value_array_new(0);
  }

  /* nullptr means that we're ok with the candidates and don't need to apply any
   * sorting/filtering.
   */
  return nullptr;
}
#endif

#if GST_VERSION_MAJOR >= 1
GstCaps* GStreamerReader::BuildAudioSinkCaps()
{
  GstCaps* caps = gst_caps_from_string("audio/x-raw, channels={1,2}");
  const char* format;
#ifdef MOZ_SAMPLE_TYPE_FLOAT32
#ifdef IS_LITTLE_ENDIAN
  format = "F32LE";
#else
  format = "F32BE";
#endif
#else /* !MOZ_SAMPLE_TYPE_FLOAT32 */
#ifdef IS_LITTLE_ENDIAN
  format = "S16LE";
#else
  format = "S16BE";
#endif
#endif
  gst_caps_set_simple(caps, "format", G_TYPE_STRING, format, NULL);

  return caps;
}

void GStreamerReader::InstallPadCallbacks()
{
  GstPad* sinkpad = gst_element_get_static_pad(GST_ELEMENT(mVideoAppSink), "sink");

  gst_pad_add_probe(sinkpad,
      (GstPadProbeType) (GST_PAD_PROBE_TYPE_SCHEDULING |
        GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM |
        GST_PAD_PROBE_TYPE_EVENT_UPSTREAM |
        GST_PAD_PROBE_TYPE_EVENT_FLUSH),
      &GStreamerReader::EventProbeCb, this, nullptr);
  gst_pad_add_probe(sinkpad, GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM,
      GStreamerReader::QueryProbeCb, nullptr, nullptr);

  gst_pad_set_element_private(sinkpad, this);
  gst_object_unref(sinkpad);

  sinkpad = gst_element_get_static_pad(GST_ELEMENT(mAudioAppSink), "sink");
  gst_pad_add_probe(sinkpad,
      (GstPadProbeType) (GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_EVENT_UPSTREAM),
      &GStreamerReader::EventProbeCb, this, nullptr);
  gst_object_unref(sinkpad);
}

GstPadProbeReturn GStreamerReader::EventProbeCb(GstPad *aPad,
                                                GstPadProbeInfo *aInfo,
                                                gpointer aUserData)
{
  GStreamerReader *reader = (GStreamerReader *) aUserData;
  GstEvent *aEvent = (GstEvent *)aInfo->data;
  return reader->EventProbe(aPad, aEvent);
}

GstPadProbeReturn GStreamerReader::EventProbe(GstPad *aPad, GstEvent *aEvent)
{
  GstElement* parent = GST_ELEMENT(gst_pad_get_parent(aPad));
  switch(GST_EVENT_TYPE(aEvent)) {
    case GST_EVENT_SEGMENT:
    {
      const GstSegment *newSegment;
      GstSegment* segment;

      /* Store the segments so we can convert timestamps to stream time, which
       * is what the upper layers sync on.
       */
      ReentrantMonitorAutoEnter mon(mGstThreadsMonitor);
#if GST_VERSION_MINOR <= 1 && GST_VERSION_MICRO < 1
      ResetDecode();
#endif
      gst_event_parse_segment(aEvent, &newSegment);
      if (parent == GST_ELEMENT(mVideoAppSink))
        segment = &mVideoSegment;
      else
        segment = &mAudioSegment;
      gst_segment_copy_into (newSegment, segment);
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      /* Reset on seeks */
      ResetDecode();
      break;
    default:
      break;
  }
  gst_object_unref(parent);

  return GST_PAD_PROBE_OK;
}

GstPadProbeReturn GStreamerReader::QueryProbeCb(GstPad* aPad, GstPadProbeInfo* aInfo, gpointer aUserData)
{
  GStreamerReader* reader = reinterpret_cast<GStreamerReader*>(gst_pad_get_element_private(aPad));
  return reader->QueryProbe(aPad, aInfo, aUserData);
}

GstPadProbeReturn GStreamerReader::QueryProbe(GstPad* aPad, GstPadProbeInfo* aInfo, gpointer aUserData)
{
  GstQuery *query = gst_pad_probe_info_get_query(aInfo);
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:
      GstCaps *caps;
      GstVideoInfo info;
      gboolean need_pool;

      gst_query_parse_allocation(query, &caps, &need_pool);
      gst_video_info_init(&info);
      gst_video_info_from_caps(&info, caps);
      gst_query_add_allocation_param(query, mAllocator, NULL);
      gst_query_add_allocation_pool(query, mBufferPool, info.size, 0, 0);
      gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);
      break;
    default:
      break;
  }

  return ret;
}

void
GStreamerReader::ImageDataFromVideoFrame(GstVideoFrame *aFrame,
                                         PlanarYCbCrImage::Data *aData)
{
  GstVideoInfo *info = &mVideoInfo;
  NS_ASSERTION(GST_VIDEO_INFO_IS_YUV(info),
               "Non-YUV video frame formats not supported");
  NS_ASSERTION(GST_VIDEO_FRAME_N_COMPONENTS(aFrame) == 3,
                "Unsupported number of components in video frame");

  aData->mPicX = aData->mPicY = 0;
  aData->mPicSize = gfxIntSize(mPicture.width, mPicture.height);

  aData->mYChannel = GST_VIDEO_FRAME_COMP_DATA(aFrame, 0);
  aData->mYStride = GST_VIDEO_FRAME_COMP_STRIDE(aFrame, 0);
  aData->mYSize = nsIntSize(GST_VIDEO_FRAME_COMP_WIDTH(aFrame, 0),
                          GST_VIDEO_FRAME_COMP_HEIGHT(aFrame, 0));
  aData->mYSkip = GST_VIDEO_FRAME_COMP_PSTRIDE(aFrame, 0) - 1;
  aData->mCbCrSize = nsIntSize(GST_VIDEO_FRAME_COMP_WIDTH(aFrame, 1),
                             GST_VIDEO_FRAME_COMP_HEIGHT(aFrame, 1));

  aData->mCbChannel = GST_VIDEO_FRAME_COMP_DATA(aFrame, 1);
  aData->mCrChannel = GST_VIDEO_FRAME_COMP_DATA(aFrame, 2);
  aData->mCbCrStride = GST_VIDEO_FRAME_COMP_STRIDE(aFrame, 1);
  aData->mCbSkip = GST_VIDEO_FRAME_COMP_PSTRIDE(aFrame, 1) - 1;
  aData->mCrSkip = GST_VIDEO_FRAME_COMP_PSTRIDE(aFrame, 2) - 1;
}

nsRefPtr<PlanarYCbCrImage> GStreamerReader::GetImageFromBuffer(GstBuffer* aBuffer)
{
  nsRefPtr<PlanarYCbCrImage> image = nullptr;

  if (gst_buffer_n_memory(aBuffer) == 1) {
    GstMemory* mem = gst_buffer_peek_memory(aBuffer, 0);
    if (GST_IS_MOZ_GFX_MEMORY_ALLOCATOR(mem->allocator)) {
      image = moz_gfx_memory_get_image(mem);

	    GstVideoFrame frame;
	    gst_video_frame_map(&frame, &mVideoInfo, aBuffer, GST_MAP_READ);
	    PlanarYCbCrImage::Data data;
      ImageDataFromVideoFrame(&frame, &data);
      image->SetDataNoCopy(data);
      gst_video_frame_unmap(&frame);
	  }
  }

  return image;
}

void GStreamerReader::CopyIntoImageBuffer(GstBuffer* aBuffer,
	                                      GstBuffer** aOutBuffer,
	                                      nsRefPtr<PlanarYCbCrImage> &image)
{
  *aOutBuffer = gst_buffer_new_allocate(mAllocator, gst_buffer_get_size(aBuffer), nullptr);
  GstMemory *mem = gst_buffer_peek_memory(*aOutBuffer, 0);
  GstMapInfo map_info;
  gst_memory_map(mem, &map_info, GST_MAP_WRITE);
  gst_buffer_extract(aBuffer, 0, map_info.data, gst_buffer_get_size(aBuffer));
  gst_memory_unmap(mem, &map_info);

  /* create a new gst buffer with the newly created memory and copy the
   * metadata over from the incoming buffer */
  gst_buffer_copy_into(*aOutBuffer, aBuffer,
      (GstBufferCopyFlags)(GST_BUFFER_COPY_METADATA), 0, -1);
  image = GetImageFromBuffer(*aOutBuffer);
}
#endif

} // namespace mozilla
