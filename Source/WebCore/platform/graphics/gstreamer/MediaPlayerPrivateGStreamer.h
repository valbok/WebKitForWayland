/*
 * Copyright (C) 2007, 2009 Apple Inc.  All rights reserved.
 * Copyright (C) 2007 Collabora Ltd. All rights reserved.
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2009, 2010 Igalia S.L
 * Copyright (C) 2014 Cable Television Laboratories, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef MediaPlayerPrivateGStreamer_h
#define MediaPlayerPrivateGStreamer_h
#if ENABLE(VIDEO) && USE(GSTREAMER)

#include "GRefPtrGStreamer.h"
#include "MediaPlayerPrivateGStreamerBase.h"
#include "Timer.h"

#include <glib.h>
#include <gst/gst.h>
#include <gst/pbutils/install-plugins.h>
#include <wtf/Forward.h>
#include <wtf/RunLoop.h>
#include <wtf/WeakPtr.h>
#include <wtf/glib/GSourceWrap.h>

#if ENABLE(VIDEO_TRACK) && USE(GSTREAMER_MPEGTS)
#include <wtf/text/AtomicStringHash.h>
#endif

typedef struct _GstBuffer GstBuffer;
typedef struct _GstMessage GstMessage;
typedef struct _GstElement GstElement;
typedef struct _GstMpegtsSection GstMpegtsSection;

namespace WebCore {

#if ENABLE(WEB_AUDIO)
class AudioSourceProvider;
class AudioSourceProviderGStreamer;
#endif

class AudioTrackPrivateGStreamer;
class InbandMetadataTextTrackPrivateGStreamer;
class InbandTextTrackPrivateGStreamer;
class MediaPlayerRequestInstallMissingPluginsCallback;
class VideoTrackPrivateGStreamer;

#if ENABLE(MEDIA_SOURCE)
class MediaSourcePrivateClient;
#endif

class MediaPlayerPrivateGStreamer : public MediaPlayerPrivateGStreamerBase {
public:
    explicit MediaPlayerPrivateGStreamer(MediaPlayer*);
    ~MediaPlayerPrivateGStreamer();

    static void registerMediaEngine(MediaEngineRegistrar);
    void handleMessage(GstMessage*);
    void handlePluginInstallerResult(GstInstallPluginsReturn);

    bool hasVideo() const override { return m_hasVideo; }
    bool hasAudio() const override { return m_hasAudio; }

    void load(const String &url) override;
#if ENABLE(MEDIA_SOURCE)
    void load(const String& url, MediaSourcePrivateClient* mediaSource) override;
#endif

#if ENABLE(MEDIA_STREAM)
    void load(MediaStreamPrivate&) override;
#endif
    void commitLoad();
    void cancelLoad() override;

    void prepareToPlay() override;
    void play() override;
    void pause() override;

    bool paused() const override;
    bool seeking() const override;

    float duration() const override;
    float currentTime() const override;
    void seek(float) override;

    void setRate(float) override;
    double rate() const override;
    void setPreservesPitch(bool) override;

    void setPreload(MediaPlayer::Preload) override;
    void fillTimerFired();

    std::unique_ptr<PlatformTimeRanges> buffered() const override;
    float maxTimeSeekable() const override;
    bool didLoadingProgress() const override;
    unsigned long long totalBytes() const override;
    float maxTimeLoaded() const override;

    void loadStateChanged();
    void timeChanged();
    void didEnd();
    void notifyDurationChanged();
    virtual void durationChanged();
    void loadingFailed(MediaPlayer::NetworkState);

    virtual void sourceChanged();

    GstElement* audioSink() const override;
    void configurePlaySink();

    void simulateAudioInterruption() override;

    bool changePipelineState(GstState);

#if ENABLE(WEB_AUDIO)
    AudioSourceProvider* audioSourceProvider() override { return reinterpret_cast<AudioSourceProvider*>(m_audioSourceProvider.get()); }
#endif

    bool isLiveStream() const override { return m_isStreaming; }

    bool handleSyncMessage(GstMessage*) override;

private:
    static void getSupportedTypes(HashSet<String>&);
    static MediaPlayer::SupportsType supportsType(const MediaEngineSupportParameters&);

    static bool isAvailable();

    WeakPtr<MediaPlayerPrivateGStreamer> createWeakPtr() { return m_weakPtrFactory.createWeakPtr(); }

    GstElement* createAudioSink() override;

    float playbackPosition() const;

    virtual void updateStates();
    void asyncStateChangeDone();

    void createGSTPlayBin();

    bool loadNextLocation();
    void mediaLocationChanged(GstMessage*);

    void setDownloadBuffering();
    void processBufferingStats(GstMessage*);
#if ENABLE(VIDEO_TRACK) && USE(GSTREAMER_MPEGTS)
    void processMpegTsSection(GstMpegtsSection*);
#endif
#if ENABLE(VIDEO_TRACK)
    void processTableOfContents(GstMessage*);
    void processTableOfContentsEntry(GstTocEntry*, GstTocEntry* parent);
#endif
    virtual bool doSeek(gint64 position, float rate, GstSeekFlags seekType);

    String engineDescription() const override { return "GStreamer"; }
    bool didPassCORSAccessCheck() const override;
    bool canSaveMediaData() const override;

protected:
    void cacheDuration();
    virtual void updatePlaybackRate();

    bool m_buffering;
    int m_bufferingPercentage;
    mutable float m_cachedPosition;
    bool m_changingRate;
    bool m_downloadFinished;
    bool m_errorOccured;
    mutable bool m_isStreaming;
    mutable gfloat m_mediaDuration;
    bool m_paused;
    float m_playbackRate;
    GstState m_requestedState;
    bool m_resetPipeline;
    bool m_seeking;
    bool m_seekIsPending;
    float m_seekTime;
    bool m_volumeAndMuteInitialized;

    void readyTimerFired();

    void notifyPlayerOfVideo();
    void notifyPlayerOfVideoCaps();
    void notifyPlayerOfAudio();

#if ENABLE(VIDEO_TRACK)
    void notifyPlayerOfText();
    void newTextSample();
#endif

    void setAudioStreamProperties(GObject*);

    static void setAudioStreamPropertiesCallback(MediaPlayerPrivateGStreamer*, GObject*);

    static void sourceChangedCallback(MediaPlayerPrivateGStreamer*);
    static void videoChangedCallback(MediaPlayerPrivateGStreamer*);
    static void videoSinkCapsChangedCallback(MediaPlayerPrivateGStreamer*);
    static void audioChangedCallback(MediaPlayerPrivateGStreamer*);
#if ENABLE(VIDEO_TRACK)
    static void textChangedCallback(MediaPlayerPrivateGStreamer*);
    static GstFlowReturn newTextSampleCallback(MediaPlayerPrivateGStreamer*);
#endif
    static gboolean durationChangedCallback(MediaPlayerPrivateGStreamer*);

    WeakPtrFactory<MediaPlayerPrivateGStreamer> m_weakPtrFactory;

    GRefPtr<GstElement> m_source;
#if ENABLE(VIDEO_TRACK)
    GRefPtr<GstElement> m_textAppSink;
    GRefPtr<GstPad> m_textAppSinkPad;
#endif
    float m_endTime;
    GstStructure* m_mediaLocations;
    int m_mediaLocationCurrentIndex;
    bool m_playbackRatePause;
    float m_timeOfOverlappingSeek;
    bool m_canFallBackToLastFinishedSeekPosition;
    float m_lastPlaybackRate;
    Timer m_fillTimer;
    float m_maxTimeLoaded;
    MediaPlayer::Preload m_preload;
    bool m_delayingLoad;
    bool m_mediaDurationKnown;
    mutable float m_maxTimeLoadedAtLastDidLoadingProgress;
    bool m_hasVideo;
    bool m_hasAudio;
    GSourceWrap::Static m_readyTimerHandler;
    mutable unsigned long long m_totalBytes;
    URL m_url;
    bool m_preservesPitch;
    mutable double m_lastQuery;
#if ENABLE(WEB_AUDIO)
    std::unique_ptr<AudioSourceProviderGStreamer> m_audioSourceProvider;
#endif
    GRefPtr<GstElement> m_autoAudioSink;
    RefPtr<MediaPlayerRequestInstallMissingPluginsCallback> m_missingPluginsCallback;
#if ENABLE(VIDEO_TRACK)
    Vector<RefPtr<AudioTrackPrivateGStreamer>> m_audioTracks;
    Vector<RefPtr<InbandTextTrackPrivateGStreamer>> m_textTracks;
    Vector<RefPtr<VideoTrackPrivateGStreamer>> m_videoTracks;
    RefPtr<InbandMetadataTextTrackPrivateGStreamer> m_chaptersTrack;
#endif
#if ENABLE(VIDEO_TRACK) && USE(GSTREAMER_MPEGTS)
    HashMap<AtomicString, RefPtr<InbandMetadataTextTrackPrivateGStreamer>> m_metadataTracks;
#endif
    bool isMediaSource() const { return false; }

    Mutex m_pendingAsyncOperationsLock;
    GList* m_pendingAsyncOperations;
};
}

#endif // USE(GSTREAMER)
#endif
