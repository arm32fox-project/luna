#include "nsError.h"
#include "MediaDecoderStateMachine.h"
#include "AbstractMediaDecoder.h"
#include "MediaResource.h"
#include "GStreamerReader.h"
#include "GStreamerMozVideoBuffer.h"
#include "GStreamerFormatHelper.h"
#include "VideoUtils.h"
#include "mozilla/dom/TimeRanges.h"
#include "mozilla/Preferences.h"

using namespace mozilla;
using mozilla::layers::PlanarYCbCrImage;
using mozilla::layers::ImageContainer;

GstFlowReturn GStreamerReader::AllocateVideoBufferCb(GstPad* aPad,
                                                     guint64 aOffset,
                                                     guint aSize,
                                                     GstCaps* aCaps,
                                                     GstBuffer** aBuf)
{
  GStreamerReader* reader = reinterpret_cast<GStreamerReader*>(gst_pad_get_element_private(aPad));
  return reader->AllocateVideoBuffer(aPad, aOffset, aSize, aCaps, aBuf);
}

GstFlowReturn GStreamerReader::AllocateVideoBuffer(GstPad* aPad,
                                                   guint64 aOffset,
                                                   guint aSize,
                                                   GstCaps* aCaps,
                                                   GstBuffer** aBuf)
{
  nsRefPtr<PlanarYCbCrImage> image;
  return AllocateVideoBufferFull(aPad, aOffset, aSize, aCaps, aBuf, image);
}

GstFlowReturn GStreamerReader::AllocateVideoBufferFull(GstPad* aPad,
                                                       guint64 aOffset,
                                                       guint aSize,
                                                       GstCaps* aCaps,
                                                       GstBuffer** aBuf,
                                                       nsRefPtr<PlanarYCbCrImage>& aImage)
{
  /* allocate an image using the container */
  ImageContainer* container = mDecoder->GetImageContainer();
  ImageFormat format = PLANAR_YCBCR;
  PlanarYCbCrImage* img = reinterpret_cast<PlanarYCbCrImage*>(container->CreateImage(&format, 1).get());
  nsRefPtr<PlanarYCbCrImage> image = dont_AddRef(img);

  /* prepare a GstBuffer pointing to the underlying PlanarYCbCrImage buffer */
  GstBuffer* buf = GST_BUFFER(gst_moz_video_buffer_new());
  GST_BUFFER_SIZE(buf) = aSize;
  /* allocate the actual YUV buffer */
  GST_BUFFER_DATA(buf) = image->AllocateAndGetNewBuffer(aSize);

  aImage = image;

  /* create a GstMozVideoBufferData to hold the image */
  GstMozVideoBufferData* bufferdata = new GstMozVideoBufferData(image);

  /* Attach bufferdata to our GstMozVideoBuffer, it will take care to free it */
  gst_moz_video_buffer_set_data(GST_MOZ_VIDEO_BUFFER(buf), bufferdata);

  *aBuf = buf;
  return GST_FLOW_OK;
}

gboolean GStreamerReader::EventProbe(GstPad* aPad, GstEvent* aEvent)
{
  GstElement* parent = GST_ELEMENT(gst_pad_get_parent(aPad));
  switch(GST_EVENT_TYPE(aEvent)) {
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      gdouble rate;
      GstFormat format;
      gint64 start, stop, position;
      GstSegment* segment;

      /* Store the segments so we can convert timestamps to stream time, which
       * is what the upper layers sync on.
       */
      ReentrantMonitorAutoEnter mon(mGstThreadsMonitor);
      gst_event_parse_new_segment(aEvent, &update, &rate, &format,
          &start, &stop, &position);
      if (parent == GST_ELEMENT(mVideoAppSink))
        segment = &mVideoSegment;
      else
        segment = &mAudioSegment;
      gst_segment_set_newsegment(segment, update, rate, format,
          start, stop, position);
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

  return TRUE;
}

gboolean GStreamerReader::EventProbeCb(GstPad* aPad,
                                         GstEvent* aEvent,
                                         gpointer aUserData)
{
  GStreamerReader* reader = reinterpret_cast<GStreamerReader*>(aUserData);
  return reader->EventProbe(aPad, aEvent);
}

nsRefPtr<PlanarYCbCrImage> GStreamerReader::GetImageFromBuffer(GstBuffer* aBuffer)
{
  nsRefPtr<PlanarYCbCrImage> image = nullptr;

  if (GST_IS_MOZ_VIDEO_BUFFER (aBuffer)) {
    GstMozVideoBufferData* bufferdata = reinterpret_cast<GstMozVideoBufferData*>(gst_moz_video_buffer_get_data(GST_MOZ_VIDEO_BUFFER(aBuffer)));
    image = bufferdata->mImage;
  }

  return image;
}

void GStreamerReader::CopyIntoImageBuffer(GstBuffer* aBuffer,
                                          GstBuffer** aOutBuffer,
                                          nsRefPtr<PlanarYCbCrImage> &image)
{
  AllocateVideoBufferFull(nullptr, GST_BUFFER_OFFSET(aBuffer),
      GST_BUFFER_SIZE(aBuffer), nullptr, aOutBuffer, image);

  gst_buffer_copy_metadata(*aOutBuffer, aBuffer, (GstBufferCopyFlags)GST_BUFFER_COPY_ALL);
  memcpy(GST_BUFFER_DATA(*aOutBuffer), GST_BUFFER_DATA(aBuffer), GST_BUFFER_SIZE(*aOutBuffer));
}

void GStreamerReader::FillYCbCrBuffer(GstBuffer* aBuffer,
                                      VideoData::YCbCrBuffer* aYCbCrBuffer)
{
  int width = mPicture.width;
  int height = mPicture.height;
  guint8* data = GST_BUFFER_DATA(aBuffer);

  GstVideoFormat format = mFormat;
  for(int i = 0; i < 3; i++) {
    aYCbCrBuffer->mPlanes[i].mData = data + gst_video_format_get_component_offset(format, i,
        width, height);
    aYCbCrBuffer->mPlanes[i].mStride = gst_video_format_get_row_stride(format, i, width);
    aYCbCrBuffer->mPlanes[i].mHeight = gst_video_format_get_component_height(format,
        i, height);
    aYCbCrBuffer->mPlanes[i].mWidth = gst_video_format_get_component_width(format,
        i, width);
    aYCbCrBuffer->mPlanes[i].mOffset = 0;
    aYCbCrBuffer->mPlanes[i].mSkip = 0;
  }
}

GstCaps* GStreamerReader::BuildAudioSinkCaps()
{
  GstCaps* caps;
#ifdef IS_LITTLE_ENDIAN
  int endianness = 1234;
#else
  int endianness = 4321;
#endif
  gint width;
#ifdef MOZ_SAMPLE_TYPE_FLOAT32
  caps = gst_caps_from_string("audio/x-raw-float, channels={1,2}");
  width = 32;
#else /* !MOZ_SAMPLE_TYPE_FLOAT32 */
  caps = gst_caps_from_string("audio/x-raw-int, channels={1,2}");
  width = 16;
#endif
  gst_caps_set_simple(caps,
      "width", G_TYPE_INT, width,
      "endianness", G_TYPE_INT, endianness,
      NULL);

  return caps;
}

void GStreamerReader::InstallPadCallbacks()
{
  GstPad* sinkpad = gst_element_get_static_pad(GST_ELEMENT(mVideoAppSink), "sink");
  gst_pad_add_event_probe(sinkpad,
                          G_CALLBACK(&GStreamerReader::EventProbeCb), this);

  gst_pad_set_bufferalloc_function(sinkpad, GStreamerReader::AllocateVideoBufferCb);
  gst_pad_set_element_private(sinkpad, this);
  gst_object_unref(sinkpad);

  sinkpad = gst_element_get_static_pad(GST_ELEMENT(mAudioAppSink), "sink");
  gst_pad_add_event_probe(sinkpad,
                          G_CALLBACK(&GStreamerReader::EventProbeCb), this);
  gst_object_unref(sinkpad);
}
