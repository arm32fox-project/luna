/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdint.h>
#include <algorithm>
#include "nsIStringBundle.h"
#include "nsDebug.h"
#include "nsString.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/Logging.h"
#include "nsThreadUtils.h"
#include "CubebUtils.h"
#include "nsAutoRef.h"
#include "prdtoa.h"

#define PREF_VOLUME_SCALE "media.volume_scale"
#define PREF_CUBEB_BACKEND "media.cubeb.backend"
#define PREF_CUBEB_LATENCY_PLAYBACK "media.cubeb_latency_playback_ms"
#define PREF_CUBEB_LATENCY_MSG "media.cubeb_latency_msg_frames"

namespace mozilla {

namespace {

LazyLogModule gCubebLog("cubeb");

void CubebLogCallback(const char* aFmt, ...)
{
  char buffer[256];

  va_list arglist;
  va_start(arglist, aFmt);
  VsprintfLiteral (buffer, aFmt, arglist);
  MOZ_LOG(gCubebLog, LogLevel::Error, ("%s", buffer));
  va_end(arglist);
}

// This mutex protects the variables below.
StaticMutex sMutex;
enum class CubebState {
  Uninitialized = 0,
  Initialized,
  Shutdown
} sCubebState = CubebState::Uninitialized;
cubeb* sCubebContext;
double sVolumeScale = 1.0;
uint32_t sCubebPlaybackLatencyInMilliseconds = 100;
uint32_t sCubebMSGLatencyInFrames = 512;
bool sCubebPlaybackLatencyPrefSet = false;
bool sCubebMSGLatencyPrefSet = false;
bool sAudioStreamInitEverSucceeded = false;
StaticAutoPtr<char> sBrandName;
StaticAutoPtr<char> sCubebBackendName;

const char kBrandBundleURL[]      = "chrome://branding/locale/brand.properties";

const char* AUDIOSTREAM_BACKEND_ID_STR[] = {
  "jack",
  "pulse",
  "alsa",
  "audiounit",
  "audioqueue",
  "wasapi",
  "winmm",
  "directsound",
  "sndio",
  "opensl",
  "audiotrack",
  "kai"
};
/* Index for failures to create an audio stream the first time. */
const int CUBEB_BACKEND_INIT_FAILURE_FIRST =
  ArrayLength(AUDIOSTREAM_BACKEND_ID_STR);
/* Index for failures to create an audio stream after the first time */
const int CUBEB_BACKEND_INIT_FAILURE_OTHER = CUBEB_BACKEND_INIT_FAILURE_FIRST + 1;
/* Index for an unknown backend. */
const int CUBEB_BACKEND_UNKNOWN = CUBEB_BACKEND_INIT_FAILURE_FIRST + 2;


// Prefered samplerate, in Hz (characteristic of the hardware, mixer, platform,
// and API used).
//
// sMutex protects *initialization* of this, which must be performed from each
// thread before fetching, after which it is safe to fetch without holding the
// mutex because it is only written once per process execution (by the first
// initialization to complete).  Since the init must have been called on a
// given thread before fetching the value, it's guaranteed (via the mutex) that
// sufficient memory barriers have occurred to ensure the correct value is
// visible on the querying thread/CPU.
uint32_t sPreferredSampleRate;

} // namespace

extern LazyLogModule gAudioStreamLog;

static const uint32_t CUBEB_NORMAL_LATENCY_MS = 100;
// Consevative default that can work on all platforms.
static const uint32_t CUBEB_NORMAL_LATENCY_FRAMES = 1024;

namespace CubebUtils {

void PrefChanged(const char* aPref, void* aClosure)
{
  if (strcmp(aPref, PREF_VOLUME_SCALE) == 0) {
    nsAdoptingString value = Preferences::GetString(aPref);
    StaticMutexAutoLock lock(sMutex);
    if (value.IsEmpty()) {
      sVolumeScale = 1.0;
    } else {
      NS_ConvertUTF16toUTF8 utf8(value);
      sVolumeScale = std::max<double>(0, PR_strtod(utf8.get(), nullptr));
    }
  } else if (strcmp(aPref, PREF_CUBEB_LATENCY_PLAYBACK) == 0) {
    // Arbitrary default stream latency of 100ms.  The higher this
    // value, the longer stream volume changes will take to become
    // audible.
    sCubebPlaybackLatencyPrefSet = Preferences::HasUserValue(aPref);
    uint32_t value = Preferences::GetUint(aPref, CUBEB_NORMAL_LATENCY_MS);
    StaticMutexAutoLock lock(sMutex);
    sCubebPlaybackLatencyInMilliseconds = std::min<uint32_t>(std::max<uint32_t>(value, 1), 1000);
  } else if (strcmp(aPref, PREF_CUBEB_LATENCY_MSG) == 0) {
    sCubebMSGLatencyPrefSet = Preferences::HasUserValue(aPref);
    uint32_t value = Preferences::GetUint(aPref, CUBEB_NORMAL_LATENCY_FRAMES);
    StaticMutexAutoLock lock(sMutex);
    // 128 is the block size for the Web Audio API, which limits how low the
    // latency can be here.
    // We don't want to limit the upper limit too much, so that people can
    // experiment.
    sCubebMSGLatencyInFrames = std::min<uint32_t>(std::max<uint32_t>(value, 128), 1e6);
  } else if (strcmp(aPref, PREF_CUBEB_BACKEND) == 0) {
    nsAdoptingString value = Preferences::GetString(aPref);
    if (value.IsEmpty()) {
      sCubebBackendName = nullptr;
    } else {
      NS_LossyConvertUTF16toASCII ascii(value);
      sCubebBackendName = new char[ascii.Length() + 1];
      PodCopy(sCubebBackendName.get(), ascii.get(), ascii.Length());
      sCubebBackendName[ascii.Length()] = 0;
    }
  }
}



bool GetFirstStream()
{
  static bool sFirstStream = true;

  StaticMutexAutoLock lock(sMutex);
  bool result = sFirstStream;
  sFirstStream = false;
  return result;
}

double GetVolumeScale()
{
  StaticMutexAutoLock lock(sMutex);
  return sVolumeScale;
}

cubeb* GetCubebContext()
{
  StaticMutexAutoLock lock(sMutex);
  return GetCubebContextUnlocked();
}

bool InitPreferredSampleRate()
{
  StaticMutexAutoLock lock(sMutex);
  if (sPreferredSampleRate != 0) {
    return true;
  }
  cubeb* context = GetCubebContextUnlocked();
  if (!context) {
    return false;
  }
  if (cubeb_get_preferred_sample_rate(context,
                                      &sPreferredSampleRate) != CUBEB_OK) {

    return false;
  }
  MOZ_ASSERT(sPreferredSampleRate);
  return true;
}

uint32_t PreferredSampleRate()
{
  if (!InitPreferredSampleRate()) {
    return 44100;
  }
  MOZ_ASSERT(sPreferredSampleRate);
  return sPreferredSampleRate;
}

void InitBrandName()
{
  if (sBrandName) {
    return;
  }
  nsXPIDLString brandName;
  nsCOMPtr<nsIStringBundleService> stringBundleService =
    mozilla::services::GetStringBundleService();
  if (stringBundleService) {
    nsCOMPtr<nsIStringBundle> brandBundle;
    nsresult rv = stringBundleService->CreateBundle(kBrandBundleURL,
                                           getter_AddRefs(brandBundle));
    if (NS_SUCCEEDED(rv)) {
      rv = brandBundle->GetStringFromName(u"brandShortName",
                                          getter_Copies(brandName));
      NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv), "Could not get the program name for a cubeb stream.");
    }
  }
  NS_LossyConvertUTF16toASCII ascii(brandName);
  sBrandName = new char[ascii.Length() + 1];
  PodCopy(sBrandName.get(), ascii.get(), ascii.Length());
  sBrandName[ascii.Length()] = 0;
}

cubeb* GetCubebContextUnlocked()
{
  sMutex.AssertCurrentThreadOwns();
  if (sCubebState != CubebState::Uninitialized) {
    // If we have already passed the initialization point (below), just return
    // the current context, which may be null (e.g., after error or shutdown.)
    return sCubebContext;
  }

  if (!sBrandName && NS_IsMainThread()) {
    InitBrandName();
  } else {
    NS_WARNING_ASSERTION(
      sBrandName, "Did not initialize sbrandName, and not on the main thread?");
  }

  int rv = cubeb_init(&sCubebContext, sBrandName, sCubebBackendName.get());
  NS_WARNING_ASSERTION(rv == CUBEB_OK, "Could not get a cubeb context.");
  sCubebState = (rv == CUBEB_OK) ? CubebState::Initialized : CubebState::Uninitialized;

  if (MOZ_LOG_TEST(gCubebLog, LogLevel::Verbose)) {
    cubeb_set_log_callback(CUBEB_LOG_VERBOSE, CubebLogCallback);
  } else if (MOZ_LOG_TEST(gCubebLog, LogLevel::Error)) {
    cubeb_set_log_callback(CUBEB_LOG_NORMAL, CubebLogCallback);
  }

  return sCubebContext;
}

uint32_t GetCubebPlaybackLatencyInMilliseconds()
{
  StaticMutexAutoLock lock(sMutex);
  return sCubebPlaybackLatencyInMilliseconds;
}

bool CubebPlaybackLatencyPrefSet()
{
  StaticMutexAutoLock lock(sMutex);
  return sCubebPlaybackLatencyPrefSet;
}

bool CubebMSGLatencyPrefSet()
{
  StaticMutexAutoLock lock(sMutex);
  return sCubebMSGLatencyPrefSet;
}

uint32_t GetCubebMSGLatencyInFrames(cubeb_stream_params * params)
{
  StaticMutexAutoLock lock(sMutex);
  if (sCubebMSGLatencyPrefSet) {
    MOZ_ASSERT(sCubebMSGLatencyInFrames > 0);
    return sCubebMSGLatencyInFrames;
  }
  cubeb* context = GetCubebContextUnlocked();
  if (!context) {
    return sCubebMSGLatencyInFrames; // default 512
  }
  uint32_t latency_frames = 0;
  if (cubeb_get_min_latency(context, params, &latency_frames) != CUBEB_OK) {
    NS_WARNING("Could not get minimal latency from cubeb.");
    return sCubebMSGLatencyInFrames; // default 512
  }
  return latency_frames;
}

void InitLibrary()
{
  PrefChanged(PREF_VOLUME_SCALE, nullptr);
  Preferences::RegisterCallback(PrefChanged, PREF_VOLUME_SCALE);
  PrefChanged(PREF_CUBEB_LATENCY_PLAYBACK, nullptr);
  PrefChanged(PREF_CUBEB_LATENCY_MSG, nullptr);
  PrefChanged(PREF_CUBEB_BACKEND, nullptr);
  Preferences::RegisterCallback(PrefChanged, PREF_CUBEB_BACKEND);
  Preferences::RegisterCallback(PrefChanged, PREF_CUBEB_LATENCY_PLAYBACK);
  Preferences::RegisterCallback(PrefChanged, PREF_CUBEB_LATENCY_MSG);
  NS_DispatchToMainThread(NS_NewRunnableFunction(&InitBrandName));
}

void ShutdownLibrary()
{
  Preferences::UnregisterCallback(PrefChanged, PREF_VOLUME_SCALE);
  Preferences::UnregisterCallback(PrefChanged, PREF_CUBEB_BACKEND);
  Preferences::UnregisterCallback(PrefChanged, PREF_CUBEB_LATENCY_PLAYBACK);
  Preferences::UnregisterCallback(PrefChanged, PREF_CUBEB_LATENCY_MSG);

  StaticMutexAutoLock lock(sMutex);
  if (sCubebContext) {
    cubeb_destroy(sCubebContext);
    sCubebContext = nullptr;
  }
  sBrandName = nullptr;
  sCubebBackendName = nullptr;
  // This will ensure we don't try to re-create a context.
  sCubebState = CubebState::Shutdown;
}

uint32_t MaxNumberOfChannels()
{
  cubeb* cubebContext = GetCubebContext();
  uint32_t maxNumberOfChannels;
  if (cubebContext &&
      cubeb_get_max_channel_count(cubebContext,
                                  &maxNumberOfChannels) == CUBEB_OK) {
    return maxNumberOfChannels;
  }

  return 0;
}

void GetCurrentBackend(nsAString& aBackend)
{
  cubeb* cubebContext = GetCubebContext();
  if (cubebContext) {
    const char* backend = cubeb_get_backend_id(cubebContext);
    if (backend) {
      aBackend.AssignASCII(backend);
      return;
    }
  }
  aBackend.AssignLiteral("unknown");
}

uint16_t ConvertCubebType(cubeb_device_type aType)
{
  uint16_t map[] = {
    nsIAudioDeviceInfo::TYPE_UNKNOWN, // CUBEB_DEVICE_TYPE_UNKNOWN
    nsIAudioDeviceInfo::TYPE_INPUT,   // CUBEB_DEVICE_TYPE_INPUT,
    nsIAudioDeviceInfo::TYPE_OUTPUT   // CUBEB_DEVICE_TYPE_OUTPUT
  };
  return map[aType];
}

uint16_t ConvertCubebState(cubeb_device_state aState)
{
  uint16_t map[] = {
    nsIAudioDeviceInfo::STATE_DISABLED,   // CUBEB_DEVICE_STATE_DISABLED
    nsIAudioDeviceInfo::STATE_UNPLUGGED,  // CUBEB_DEVICE_STATE_UNPLUGGED
    nsIAudioDeviceInfo::STATE_ENABLED     // CUBEB_DEVICE_STATE_ENABLED
  };
  return map[aState];
}

uint16_t ConvertCubebPreferred(cubeb_device_pref aPreferred)
{
  if (aPreferred == CUBEB_DEVICE_PREF_NONE) {
    return nsIAudioDeviceInfo::PREF_NONE;
  } else if (aPreferred == CUBEB_DEVICE_PREF_ALL) {
    return nsIAudioDeviceInfo::PREF_ALL;
  }

  uint16_t preferred = 0;
  if (aPreferred & CUBEB_DEVICE_PREF_MULTIMEDIA) {
    preferred |= nsIAudioDeviceInfo::PREF_MULTIMEDIA;
  }
  if (aPreferred & CUBEB_DEVICE_PREF_VOICE) {
    preferred |= nsIAudioDeviceInfo::PREF_VOICE;
  }
  if (aPreferred & CUBEB_DEVICE_PREF_NOTIFICATION) {
    preferred |= nsIAudioDeviceInfo::PREF_NOTIFICATION;
  }
  return preferred;
}

uint16_t ConvertCubebFormat(cubeb_device_fmt aFormat)
{
  uint16_t format = 0;
  if (aFormat & CUBEB_DEVICE_FMT_S16LE) {
    format |= nsIAudioDeviceInfo::FMT_S16LE;
  }
  if (aFormat & CUBEB_DEVICE_FMT_S16BE) {
    format |= nsIAudioDeviceInfo::FMT_S16BE;
  }
  if (aFormat & CUBEB_DEVICE_FMT_F32LE) {
    format |= nsIAudioDeviceInfo::FMT_F32LE;
  }
  if (aFormat & CUBEB_DEVICE_FMT_F32BE) {
    format |= nsIAudioDeviceInfo::FMT_F32BE;
  }
  return format;
}

void GetDeviceCollection(nsTArray<RefPtr<AudioDeviceInfo>>& aDeviceInfos,
                         Side aSide)
{
  cubeb* context = GetCubebContext();
  if (context) {
    cubeb_device_collection collection = { nullptr, 0 };
    if (cubeb_enumerate_devices(context,
                                aSide == Input ? CUBEB_DEVICE_TYPE_INPUT :
                                                 CUBEB_DEVICE_TYPE_OUTPUT,
                                &collection) == CUBEB_OK) {
      for (unsigned int i = 0; i < collection.count; ++i) {
        auto device = collection.device[i];
        RefPtr<AudioDeviceInfo> info =
          new AudioDeviceInfo(NS_ConvertASCIItoUTF16(device.friendly_name),
                              NS_ConvertASCIItoUTF16(device.group_id),
                              NS_ConvertASCIItoUTF16(device.vendor_name),
                              ConvertCubebType(device.type),
                              ConvertCubebState(device.state),
                              ConvertCubebPreferred(device.preferred),
                              ConvertCubebFormat(device.format),
                              ConvertCubebFormat(device.default_format),
                              device.max_channels,
                              device.default_rate,
                              device.max_rate,
                              device.min_rate,
                              device.latency_hi,
                              device.latency_lo);
        aDeviceInfos.AppendElement(info);
      }
    }
    cubeb_device_collection_destroy(context, &collection);
  }
}

} // namespace CubebUtils
} // namespace mozilla
