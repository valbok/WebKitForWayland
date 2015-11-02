/*
 * Copyright (C) 2007, 2009 Apple Inc.  All rights reserved.
 * Copyright (C) 2007 Collabora Ltd.  All rights reserved.
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2009 Gustavo Noronha Silva <gns@gnome.org>
 * Copyright (C) 2009, 2010 Igalia S.L
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

#include "config.h"
#include "MediaPlayerPrivateGStreamerBase.h"

#if ENABLE(VIDEO) && USE(GSTREAMER)

#include "ColorSpace.h"
#include "GStreamerUtilities.h"
#include "GraphicsContext.h"
#include "GraphicsTypes.h"
#include "ImageGStreamer.h"
#include "ImageOrientation.h"
#include "IntRect.h"
#include "MediaPlayer.h"
#include "NotImplemented.h"
#include "UUID.h"
#include "VideoSinkGStreamer.h"
#include "WebKitWebSourceGStreamer.h"
#include <gst/gst.h>
#include <wtf/glib/GMutexLocker.h>
#include <wtf/text/CString.h>

#include <gst/audio/streamvolume.h>
#include <gst/video/gstvideometa.h>

#if USE(GSTREAMER_GL)
#define GST_USE_UNSTABLE_API
#include <gst/gl/gstglmemory.h>
#undef GST_USE_UNSTABLE_API
#endif

#if GST_CHECK_VERSION(1, 1, 0) && USE(TEXTURE_MAPPER_GL)
#include "BitmapTextureGL.h"
#include "BitmapTexturePool.h"
#include "TextureMapperGL.h"
#endif
#if USE(COORDINATED_GRAPHICS_THREADED)
#include "TextureMapperPlatformLayerBuffer.h"
#endif


#define WL_EGL_PLATFORM

#if USE(OPENGL_ES_2)
#if GST_CHECK_VERSION(1, 3, 0)
#if !USE(HOLE_PUNCH_GSTREAMER)
#define GST_USE_UNSTABLE_API
#include <gst/gl/egl/gsteglimagememory.h>
#undef GST_USE_UNSTABLE_API
#endif
#endif
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

#include <EGL/egl.h>

struct _EGLDetails {
    EGLDisplay display;
    EGLContext context;
    EGLSurface draw;
    EGLSurface read;
};

#if USE(GSTREAMER_GL)
#include "GLContext.h"

#define GST_USE_UNSTABLE_API
#include <gst/gl/gl.h>
#undef GST_USE_UNSTABLE_API

#if USE(GLX)
#include "GLContextGLX.h"
#include <gst/gl/x11/gstgldisplay_x11.h>
#elif USE(EGL)
#include "GLContextEGL.h"
#include <gst/gl/egl/gstgldisplay_egl.h>
#endif

#if PLATFORM(X11)
#include "PlatformDisplayX11.h"
#elif PLATFORM(WAYLAND)
#include "PlatformDisplayWayland.h"
#elif PLATFORM(BCM_RPI)
#include "PlatformDisplayBCMRPi.h"
#endif

// gstglapi.h may include eglplatform.h and it includes X.h, which
// defines None, breaking MediaPlayer::None enum
#if PLATFORM(X11) && GST_GL_HAVE_PLATFORM_EGL
#undef None
#endif
#endif // USE(GSTREAMER_GL)

#if ENABLE(ENCRYPTED_MEDIA)
#include "WebKitCommonEncryptionDecryptorGStreamer.h"
#endif

#if ENABLE(ENCRYPTED_MEDIA) || ENABLE(ENCRYPTED_MEDIA_V2)
#include <runtime/JSCInlines.h>
#include <runtime/TypedArrayInlines.h>
#include <runtime/Uint8Array.h>
#endif

#if USE(DXDRM) && (ENABLE(ENCRYPTED_MEDIA) || ENABLE(ENCRYPTED_MEDIA_V2))
#if ENABLE(ENCRYPTED_MEDIA_V2)
#include "CDMPRSessionGStreamer.h"
#endif
#include "DiscretixSession.h"
#include "WebKitPlayReadyDecryptorGStreamer.h"
#endif

GST_DEBUG_CATEGORY(webkit_media_player_debug);
#define GST_CAT_DEFAULT webkit_media_player_debug

using namespace std;

namespace WebCore {

void registerWebKitGStreamerElements()
{
#if ENABLE(ENCRYPTED_MEDIA)
    GRefPtr<GstElementFactory> cencDecryptorFactory = gst_element_factory_find("webkitcencdec");
    if (!cencDecryptorFactory)
        gst_element_register(0, "webkitcencdec", GST_RANK_PRIMARY + 100, WEBKIT_TYPE_MEDIA_CENC_DECRYPT);
#endif

#if USE(DXDRM)
    GRefPtr<GstElementFactory> playReadyDecryptorFactory = gst_element_factory_find("webkitplayreadydec");
    if (!playReadyDecryptorFactory)
        gst_element_register(0, "webkitplayreadydec", GST_RANK_PRIMARY + 100, WEBKIT_TYPE_MEDIA_PLAYREADY_DECRYPT);
#endif
}

static int greatestCommonDivisor(int a, int b)
{
    while (b) {
        int temp = a;
        a = b;
        b = temp % b;
    }

    return ABS(a);
}

static void mediaPlayerPrivateVolumeChangedCallback(GObject*, GParamSpec*, MediaPlayerPrivateGStreamerBase* player)
{
    // This is called when m_volumeElement receives the notify::volume signal.
    LOG_MEDIA_MESSAGE("Volume changed to: %f", player->volume());
    player->volumeChanged();
}

static void mediaPlayerPrivateMuteChangedCallback(GObject*, GParamSpec*, MediaPlayerPrivateGStreamerBase* player)
{
    // This is called when m_volumeElement receives the notify::mute signal.
    player->muteChanged();
}

#if !USE(HOLE_PUNCH_GSTREAMER)
static void mediaPlayerPrivateRepaintCallback(WebKitVideoSink*, GstSample* sample, MediaPlayerPrivateGStreamerBase* playerPrivate)
{
    playerPrivate->triggerRepaint(sample);
}

static void mediaPlayerPrivateDrainCallback(WebKitVideoSink*, MediaPlayerPrivateGStreamerBase* playerPrivate)
{
    playerPrivate->triggerDrain();
}
#endif

#if USE(GSTREAMER_GL)
static gboolean mediaPlayerPrivateDrawCallback(GstElement*, GstContext*, GstSample* sample, MediaPlayerPrivateGStreamerBase* playerPrivate)
{
    playerPrivate->triggerRepaint(sample);
    return TRUE;
}
#endif

static void mediaPlayerPrivateNeedContextMessageCallback(GstBus*, GstMessage* message, MediaPlayerPrivateGStreamerBase* player)
{
    player->handleNeedContextMessage(message);
}

static void mediaPlayerPrivateElementMessageCallback(GstBus*, GstMessage* message, MediaPlayerPrivateGStreamerBase* player)
{
    player->handleElementMessage(message);
}

#if USE(COORDINATED_GRAPHICS_THREADED) && USE(GSTREAMER_GL)
class GstVideoFrameHolder : public TextureMapperPlatformLayerBuffer::UnmanagedBufferDataHolder {
public:
    GstVideoFrameHolder(GstSample& sample)
    {
        GstVideoInfo videoInfo;
        gst_video_info_init(&videoInfo);
        GstCaps *caps = gst_sample_get_caps(&sample);
        if (UNLIKELY(!gst_video_info_from_caps(&videoInfo, caps)))
            return;

        m_size = IntSize(GST_VIDEO_INFO_WIDTH(&videoInfo), GST_VIDEO_INFO_HEIGHT(&videoInfo));
        m_flags = GST_VIDEO_INFO_HAS_ALPHA(&videoInfo) ? TextureMapperGL::ShouldBlend : 0;

        GstBuffer* buffer = gst_sample_get_buffer(&sample);
        m_videoFrame = new GstVideoFrame();
        if (UNLIKELY(!gst_video_frame_map(m_videoFrame, &videoInfo, buffer, static_cast<GstMapFlags>(GST_MAP_READ | GST_MAP_GL))))
            return;

        m_textureID = *reinterpret_cast<GLuint*>(m_videoFrame->data[0]);
        m_isValid = true;
    }
    virtual ~GstVideoFrameHolder()
    {
        if (UNLIKELY(!m_isValid))
            return;

        GstMapInfo* info = &m_videoFrame->map[0];
        GstGLBaseBuffer* mem = (GstGLBaseBuffer*)info->memory;
        GstGLWindow* gstWindow = gst_gl_context_get_window(mem->context);

        gst_gl_window_send_message_async(gstWindow, (GstGLWindowCB) unmapVideoFrameCallback, m_videoFrame, (GDestroyNotify) freeVideoFrameCallback);
    }

    static void unmapVideoFrameCallback(GstVideoFrame* videoFrame)
    {
        gst_video_frame_unmap(videoFrame);
    }
    static void freeVideoFrameCallback(GstVideoFrame* videoFrame)
    {
        delete videoFrame;
    }

    IntSize size() const { return m_size; }
    TextureMapperGL::Flags flags() const { return m_flags; }
    GLuint textureID() const { return m_textureID; }
    bool isValid() const { return m_isValid; }

private:
    GstVideoFrame* m_videoFrame;
    IntSize m_size;
    TextureMapperGL::Flags m_flags;
    GLuint m_textureID;
    bool m_isValid { false };
};
#endif

MediaPlayerPrivateGStreamerBase::MediaPlayerPrivateGStreamerBase(MediaPlayer* player)
    : m_player(player)
    , m_fpsSink(0)
    , m_readyState(MediaPlayer::HaveNothing)
    , m_networkState(MediaPlayer::Empty)
    , m_isEndReached(false)
    , m_sample(0)
    , m_volumeTimerHandler("[WebKit] MediaPlayerPrivateGStreamerBase::volumeChanged", std::function<void()>(std::bind(&MediaPlayerPrivateGStreamerBase::notifyPlayerOfVolumeChange, this)))
    , m_muteTimerHandler("[WebKit] MediaPlayerPrivateGStreamerBase::muteChanged", std::function<void()>(std::bind(&MediaPlayerPrivateGStreamerBase::notifyPlayerOfMute, this)))
    , m_repaintHandler(0)
    , m_volumeSignalHandler(0)
    , m_muteSignalHandler(0)
    , m_usingFallbackVideoSink(false)
#if ENABLE(ENCRYPTED_MEDIA) && USE(DXDRM)
    , m_dxdrmSession(0)
#endif
#if ENABLE(ENCRYPTED_MEDIA_V2)
    , m_cdmSession(0)
#endif
    , m_videoSourceRotation(NoVideoSourceRotation)
#if USE(TEXTURE_MAPPER_GL)
    , m_textureMapperRotationFlag(0)
#endif
{
    g_mutex_init(&m_sampleMutex);
#if USE(GSTREAMER_GL)
    g_cond_init(&m_drawCondition);
    g_mutex_init(&m_drawMutex);
#endif
#if USE(COORDINATED_GRAPHICS_THREADED)
    m_platformLayerProxy = adoptRef(new TextureMapperPlatformLayerProxy());
    g_cond_init(&m_updateCondition);
    g_mutex_init(&m_updateMutex);
#endif

#if USE(HOLE_PUNCH_GSTREAMER)
#if USE(COORDINATED_GRAPHICS_THREADED)
    LockHolder locker(m_platformLayerProxy->mutex());
    m_platformLayerProxy->pushNextBuffer(locker, std::make_unique<TextureMapperPlatformLayerBuffer>(0, m_size, TextureMapperGL::ShouldOverwriteRect));
#endif
#endif

}

MediaPlayerPrivateGStreamerBase::~MediaPlayerPrivateGStreamerBase()
{
    g_mutex_clear(&m_sampleMutex);

    m_player = 0;

    if (m_volumeSignalHandler) {
        g_signal_handler_disconnect(m_volumeElement.get(), m_volumeSignalHandler);
        m_volumeSignalHandler = 0;
    }

    if (m_muteSignalHandler) {
        g_signal_handler_disconnect(m_volumeElement.get(), m_muteSignalHandler);
        m_muteSignalHandler = 0;
    }

#if USE(GSTREAMER_GL)
    g_cond_clear(&m_drawCondition);
    g_mutex_clear(&m_drawMutex);
#endif

#if USE(COORDINATED_GRAPHICS_THREADED)
    g_cond_clear(&m_updateCondition);
    g_mutex_clear(&m_updateMutex);
#endif

    if (m_pipeline) {
        GRefPtr<GstBus> bus = adoptGRef(gst_pipeline_get_bus(GST_PIPELINE(m_pipeline.get())));
        ASSERT(bus);
        g_signal_handlers_disconnect_by_func(bus.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateNeedContextMessageCallback), this);
        g_signal_handlers_disconnect_by_func(bus.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateElementMessageCallback), this);
        gst_bus_disable_sync_message_emission(bus.get());
        m_pipeline.clear();
    }

#if USE(DXDRM)
#if ENABLE(ENCRYPTED_MEDIA)
    if (m_dxdrmSession != NULL) {
        delete m_dxdrmSession;
    }
#elif ENABLE(ENCRYPTED_MEDIA_V2)
    if (m_cdmSession != NULL) {
        delete m_cdmSession;
    }
#endif
#endif

#if USE(TEXTURE_MAPPER_GL) && !USE(COORDINATED_GRAPHICS)
    if (client())
        client()->platformLayerWillBeDestroyed();
#endif
}

void MediaPlayerPrivateGStreamerBase::setPipeline(GstElement* pipeline)
{
    m_pipeline = pipeline;

    GRefPtr<GstBus> bus = adoptGRef(gst_pipeline_get_bus(GST_PIPELINE(m_pipeline.get())));
    gst_bus_enable_sync_message_emission(bus.get());
    g_signal_connect(bus.get(), "sync-message::need-context", G_CALLBACK(mediaPlayerPrivateNeedContextMessageCallback), this);
    g_signal_connect(bus.get(), "sync-message::element", G_CALLBACK(mediaPlayerPrivateElementMessageCallback), this);
}

void MediaPlayerPrivateGStreamerBase::clearSamples()
{
#if USE(COORDINATED_GRAPHICS_THREADED)
    // Disconnect handlers to ensure that new samples aren't going to arrive
    // before the pipeline destruction
    if (m_repaintHandler) {
        g_signal_handler_disconnect(m_videoSink.get(), m_repaintHandler);
        m_repaintHandler = 0;
    }

    if (m_drainHandler) {
        g_signal_handler_disconnect(m_videoSink.get(), m_drainHandler);
        m_drainHandler = 0;
    }
#endif

    WTF::GMutexLocker<GMutex> lock(m_sampleMutex);
    m_sample = nullptr;
}

void MediaPlayerPrivateGStreamerBase::handleNeedContextMessage(GstMessage* message)
{
#if USE(GSTREAMER_GL)
    const gchar* contextType;
    gst_message_parse_context_type(message, &contextType);

    if (!ensureGstGLContext())
        return;

    if (!g_strcmp0(contextType, GST_GL_DISPLAY_CONTEXT_TYPE)) {
        GstContext* displayContext = gst_context_new(GST_GL_DISPLAY_CONTEXT_TYPE, TRUE);
        gst_context_set_gl_display(displayContext, m_glDisplay.get());
        gst_element_set_context(GST_ELEMENT(message->src), displayContext);
        return;
    }

    if (!g_strcmp0(contextType, "gst.gl.app_context")) {
        GstContext* appContext = gst_context_new("gst.gl.app_context", TRUE);
        GstStructure* structure = gst_context_writable_structure(appContext);
        gst_structure_set(structure, "context", GST_GL_TYPE_CONTEXT, m_glContext.get(), nullptr);
        gst_element_set_context(GST_ELEMENT(message->src), appContext);
        return;
    }
#else
    UNUSED_PARAM(message);
#endif // USE(GSTREAMER_GL)
}

#if USE(GSTREAMER_GL)
bool MediaPlayerPrivateGStreamerBase::ensureGstGLContext()
{
    if (m_glContext)
        return true;

    if (!m_glDisplay) {
        const auto& sharedDisplay = PlatformDisplay::sharedDisplay();
#if PLATFORM(X11)
        m_glDisplay = GST_GL_DISPLAY(gst_gl_display_x11_new_with_display(downcast<PlatformDisplayX11>(sharedDisplay).native()));
#elif PLATFORM(WAYLAND)
        m_glDisplay = GST_GL_DISPLAY(gst_gl_display_egl_new_with_egl_display(downcast<PlatformDisplayWayland>(sharedDisplay).eglDisplay()));
#elif PLATFORM(BCM_RPI)
        m_glDisplay = GST_GL_DISPLAY(gst_gl_display_egl_new_with_egl_display(downcast<PlatformDisplayBCMRPi>(sharedDisplay).eglDisplay()));
#endif
    }

    GLContext* webkitContext = GLContext::sharingContext();
    // EGL and GLX are mutually exclusive, no need for ifdefs here.
    GstGLPlatform glPlatform = webkitContext->isEGLContext() ? GST_GL_PLATFORM_EGL : GST_GL_PLATFORM_GLX;

#if USE(OPENGL_ES_2)
    GstGLAPI glAPI = GST_GL_API_GLES2;
#elif USE(OPENGL)
    GstGLAPI glAPI = GST_GL_API_OPENGL;
#else
    ASSERT_NOT_REACHED();
#endif

    PlatformGraphicsContext3D contextHandle = webkitContext->platformContext();
    if (!contextHandle)
        return false;

    m_glContext = gst_gl_context_new_wrapped(m_glDisplay.get(), reinterpret_cast<guintptr>(contextHandle), glPlatform, glAPI);
    return true;
}
#endif // USE(GSTREAMER_GL)

// Returns the size of the video
FloatSize MediaPlayerPrivateGStreamerBase::naturalSize() const
{
#if USE(HOLE_PUNCH_GSTREAMER)
    // We don't care about the natural size of the video, the external sink will deal with it.
    // This means that the video will always have the size of the <video> component or the default 300x150
    return FloatSize();
#endif

    if (!hasVideo())
        return FloatSize();

    if (!m_videoSize.isEmpty())
        return m_videoSize;

    WTF::GMutexLocker<GMutex> lock(m_sampleMutex);

    GRefPtr<GstCaps> caps;
    // We may not have enough data available for the video sink yet,
    // but the demuxer might haver it already.
    if (!GST_IS_SAMPLE(m_sample.get())) {
#if ENABLE(MEDIA_SOURCE)
        caps = currentDemuxerCaps();
#else
        return FloatSize();
#endif
    }

    if (GST_IS_SAMPLE(m_sample.get()) && !caps)
        caps = gst_sample_get_caps(m_sample.get());

    if (!caps) {
        GRefPtr<GstPad> videoSinkPad = adoptGRef(gst_element_get_static_pad(m_videoSink.get(), "sink"));
        if (videoSinkPad)
            caps = gst_pad_get_current_caps(videoSinkPad.get());
    }

    if (!caps)
        return FloatSize();

    // TODO: handle possible clean aperture data. See
    // https://bugzilla.gnome.org/show_bug.cgi?id=596571
    // TODO: handle possible transformation matrix. See
    // https://bugzilla.gnome.org/show_bug.cgi?id=596326

    // Get the video PAR and original size, if this fails the
    // video-sink has likely not yet negotiated its caps.
    int pixelAspectRatioNumerator, pixelAspectRatioDenominator, stride;
    IntSize originalSize;
    GstVideoFormat format;
    if (!getVideoSizeAndFormatFromCaps(caps.get(), originalSize, format, pixelAspectRatioNumerator, pixelAspectRatioDenominator, stride))
        return FloatSize();

    LOG_MEDIA_MESSAGE("Original video size: %dx%d", originalSize.width(), originalSize.height());
    LOG_MEDIA_MESSAGE("Pixel aspect ratio: %d/%d", pixelAspectRatioNumerator, pixelAspectRatioDenominator);

    // If the video is tagged as rotated 90 or 270 degrees, swap width and height
    if (m_videoSourceRotation == VideoSourceRotation90 || m_videoSourceRotation == VideoSourceRotation270)
        originalSize = originalSize.transposedSize();

    // Calculate DAR based on PAR and video size.
    int displayWidth = originalSize.width() * pixelAspectRatioNumerator;
    int displayHeight = originalSize.height() * pixelAspectRatioDenominator;

    // Divide display width and height by their GCD to avoid possible overflows.
    int displayAspectRatioGCD = greatestCommonDivisor(displayWidth, displayHeight);
    displayWidth /= displayAspectRatioGCD;
    displayHeight /= displayAspectRatioGCD;

    // Apply DAR to original video size. This is the same behavior as in xvimagesink's setcaps function.
    guint64 width = 0, height = 0;
    if (!(originalSize.height() % displayHeight)) {
        LOG_MEDIA_MESSAGE("Keeping video original height");
        width = gst_util_uint64_scale_int(originalSize.height(), displayWidth, displayHeight);
        height = static_cast<guint64>(originalSize.height());
    } else if (!(originalSize.width() % displayWidth)) {
        LOG_MEDIA_MESSAGE("Keeping video original width");
        height = gst_util_uint64_scale_int(originalSize.width(), displayHeight, displayWidth);
        width = static_cast<guint64>(originalSize.width());
    } else {
        LOG_MEDIA_MESSAGE("Approximating while keeping original video height");
        width = gst_util_uint64_scale_int(originalSize.height(), displayWidth, displayHeight);
        height = static_cast<guint64>(originalSize.height());
    }

    LOG_MEDIA_MESSAGE("Natural size: %" G_GUINT64_FORMAT "x%" G_GUINT64_FORMAT, width, height);
    m_videoSize = FloatSize(static_cast<int>(width), static_cast<int>(height));
    return m_videoSize;
}

void MediaPlayerPrivateGStreamerBase::setVolume(float volume)
{
    if (!m_volumeElement)
        return;

    LOG_MEDIA_MESSAGE("Setting volume: %f", volume);
    gst_stream_volume_set_volume(m_volumeElement.get(), GST_STREAM_VOLUME_FORMAT_CUBIC, static_cast<double>(volume));
}

float MediaPlayerPrivateGStreamerBase::volume() const
{
    if (!m_volumeElement)
        return 0;

    return gst_stream_volume_get_volume(m_volumeElement.get(), GST_STREAM_VOLUME_FORMAT_CUBIC);
}


void MediaPlayerPrivateGStreamerBase::notifyPlayerOfVolumeChange()
{
    if (!m_player || !m_volumeElement)
        return;
    double volume;
    volume = gst_stream_volume_get_volume(m_volumeElement.get(), GST_STREAM_VOLUME_FORMAT_CUBIC);
    // get_volume() can return values superior to 1.0 if the user
    // applies software user gain via third party application (GNOME
    // volume control for instance).
    volume = CLAMP(volume, 0.0, 1.0);
    m_player->volumeChanged(static_cast<float>(volume));
}

void MediaPlayerPrivateGStreamerBase::volumeChanged()
{
    m_volumeTimerHandler.schedule();
}

MediaPlayer::NetworkState MediaPlayerPrivateGStreamerBase::networkState() const
{
    return m_networkState;
}

MediaPlayer::ReadyState MediaPlayerPrivateGStreamerBase::readyState() const
{
    return m_readyState;
}

void MediaPlayerPrivateGStreamerBase::sizeChanged()
{
    notImplemented();
}

void MediaPlayerPrivateGStreamerBase::setMuted(bool muted)
{
    if (!m_volumeElement)
        return;

    g_object_set(m_volumeElement.get(), "mute", muted, NULL);
}

bool MediaPlayerPrivateGStreamerBase::muted() const
{
    if (!m_volumeElement)
        return false;

    bool muted;
    g_object_get(m_volumeElement.get(), "mute", &muted, NULL);
    return muted;
}

void MediaPlayerPrivateGStreamerBase::notifyPlayerOfMute()
{
    if (!m_player || !m_volumeElement)
        return;

    gboolean muted;
    g_object_get(m_volumeElement.get(), "mute", &muted, NULL);
    m_player->muteChanged(static_cast<bool>(muted));
}

void MediaPlayerPrivateGStreamerBase::muteChanged()
{
    m_muteTimerHandler.schedule();
}

#if USE(TEXTURE_MAPPER_GL) && !USE(COORDINATED_GRAPHICS_MULTIPROCESS)
void MediaPlayerPrivateGStreamerBase::updateTexture(BitmapTextureGL& texture, GstVideoInfo& videoInfo)
{
    IntSize size = IntSize(GST_VIDEO_INFO_WIDTH(&videoInfo), GST_VIDEO_INFO_HEIGHT(&videoInfo));
    GstBuffer* buffer = gst_sample_get_buffer(m_sample.get());

#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 1, 2) && !USE(HOLE_PUNCH_GSTREAMER)
    GstMemory *mem;
    if (gst_buffer_n_memory (buffer) >= 1) {
        if ((mem = gst_buffer_peek_memory (buffer, 0)) && gst_is_egl_image_memory (mem)) {
            guint n, i;

            n = gst_buffer_n_memory (buffer);

            n = 1; // FIXME

            for (i = 0; i < n; i++) {
                mem = gst_buffer_peek_memory (buffer, i);

                g_assert (gst_is_egl_image_memory (mem));

                if (i == 0)
                    glActiveTexture (GL_TEXTURE0);
                else if (i == 1)
                    glActiveTexture (GL_TEXTURE1);
                else if (i == 2)
                    glActiveTexture (GL_TEXTURE2);

                glBindTexture (GL_TEXTURE_2D, texture.id());
                glEGLImageTargetTexture2DOES (GL_TEXTURE_2D,
                    gst_egl_image_memory_get_image (mem));

                m_orientation = gst_egl_image_memory_get_orientation (mem);
                if (m_orientation != GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL
                    && m_orientation != GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_FLIP) {
                    LOG_ERROR("MediaPlayerPrivateGStreamerBase::updateTexture: invalid GstEGLImage orientation");
                }
            }

            return;
        }
    }

    return;
#endif
#if GST_CHECK_VERSION(1, 1, 0)
    GstVideoGLTextureUploadMeta* meta;
    if ((meta = gst_buffer_get_video_gl_texture_upload_meta(buffer))) {
        if (meta->n_textures == 1) { // BRGx & BGRA formats use only one texture.
            guint ids[4] = { texture.id(), 0, 0, 0 };

            if (gst_video_gl_texture_upload_meta_upload(meta, ids))
                return;
        }
    }
#endif

    // Right now the TextureMapper only supports chromas with one plane
    ASSERT(GST_VIDEO_INFO_N_PLANES(&videoInfo) == 1);

    GstVideoFrame videoFrame;
    if (!gst_video_frame_map(&videoFrame, &videoInfo, buffer, GST_MAP_READ))
        return;

    int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&videoFrame, 0);
    const void* srcData = GST_VIDEO_FRAME_PLANE_DATA(&videoFrame, 0);
    texture.updateContents(srcData, WebCore::IntRect(WebCore::IntPoint(0, 0), size), WebCore::IntPoint(0, 0), stride, BitmapTexture::UpdateCannotModifyOriginalImageData);
    gst_video_frame_unmap(&videoFrame);
}
#endif

#if USE(COORDINATED_GRAPHICS_THREADED)
void MediaPlayerPrivateGStreamerBase::updateOnCompositorThread()
{
    WTF::GMutexLocker<GMutex> lock(m_sampleMutex);

#if USE(GSTREAMER_GL)
    if (!GST_IS_SAMPLE(m_sample.get()))
        return;

    std::unique_ptr<GstVideoFrameHolder> frameHolder = std::make_unique<GstVideoFrameHolder>(*m_sample);
    if (UNLIKELY(!frameHolder->isValid()))
        return;

    TextureMapperGL::Flags flags = m_textureMapperRotationFlag | frameHolder->flags();
    std::unique_ptr<TextureMapperPlatformLayerBuffer> layerBuffer = std::make_unique<TextureMapperPlatformLayerBuffer>(frameHolder->textureID(), frameHolder->size(), flags);
    layerBuffer->setUnmanagedBufferDataHolder(WTF::move(frameHolder));
    m_platformLayerProxy->pushNextBuffer(WTF::move(layerBuffer));
    return;
#else
    class ConditionNotifier {
    public:
        ConditionNotifier(GMutex& mutex, GCond& condition)
            : locker(mutex), m_condition(condition)
        {
        }
        ~ ConditionNotifier()
        {
            g_cond_signal(&m_condition);
        }
    private:
        WTF::GMutexLocker<GMutex> locker;
        GCond& m_condition;
    };
    ConditionNotifier notifier(m_updateMutex, m_updateCondition);

    GRefPtr<GstCaps> caps;
    if (!GST_IS_SAMPLE(m_sample.get())) {
#if ENABLE(MEDIA_SOURCE)
        caps = currentDemuxerCaps();
#else
        return;
#endif
    }

    if (!caps && m_sample)
        caps = gst_sample_get_caps(m_sample.get());

    if (UNLIKELY(!caps))
        return;

    GstVideoInfo videoInfo;
    gst_video_info_init(&videoInfo);
    if (UNLIKELY(!gst_video_info_from_caps(&videoInfo, caps.get())))
        return;


    IntSize size = IntSize(GST_VIDEO_INFO_WIDTH(&videoInfo), GST_VIDEO_INFO_HEIGHT(&videoInfo));

    if (!m_platformLayerProxy->isActive())
        return;

    unique_ptr<TextureMapperPlatformLayerBuffer> buffer = m_platformLayerProxy->getAvailableBuffer(size, GraphicsContext3D::DONT_CARE);
    if (UNLIKELY(!buffer)) {
        if (UNLIKELY(!m_context3D))
            m_context3D = GraphicsContext3D::create(GraphicsContext3D::Attributes(), nullptr, GraphicsContext3D::RenderToCurrentGLContext);

        RefPtr<BitmapTexture> texture = adoptRef(new BitmapTextureGL(m_context3D));
        texture->reset(size, GST_VIDEO_INFO_HAS_ALPHA(&videoInfo));
        buffer = std::make_unique<TextureMapperPlatformLayerBuffer>(WTF::move(texture), m_textureMapperRotationFlag);
    }
    updateTexture(buffer->textureGL(), videoInfo);
    m_platformLayerProxy->pushNextBuffer(WTF::move(buffer));
#endif
}
#endif

void MediaPlayerPrivateGStreamerBase::triggerRepaint(GstSample* sample)
{
    {
        WTF::GMutexLocker<GMutex> lock(m_sampleMutex);
        m_sample = sample;
    }

#if USE(COORDINATED_GRAPHICS_THREADED)
#if USE(GSTREAMER_GL)
    if (!m_platformLayerProxy->isActive())
        return;

    updateOnCompositorThread();
    return;
#else
    {
        WTF::GMutexLocker<GMutex> lock(m_updateMutex);
        if (!m_platformLayerProxy->scheduleUpdateOnCompositorThread([this] { this->updateOnCompositorThread(); }))
            return;
        g_cond_wait(&m_updateCondition, &m_updateMutex);
    }
    return;
#endif
#endif

#if USE(TEXTURE_MAPPER_GL) && !USE(COORDINATED_GRAPHICS)
#if USE(GSTREAMER_GL)
    {
        WTF::GMutexLocker<GMutex> lock(m_drawMutex);
        callOnMainThread([this] {
                WTF::GMutexLocker<GMutex> lock(m_drawMutex);
                if (supportsAcceleratedRendering() && m_player->client().mediaPlayerRenderingCanBeAccelerated(m_player) && client())
                    client()->setPlatformLayerNeedsDisplay();
                g_cond_signal(&m_drawCondition);
            });
        g_cond_wait(&m_drawCondition, &m_drawMutex);
    }
#else
    if (supportsAcceleratedRendering() && m_player->client().mediaPlayerRenderingCanBeAccelerated(m_player) && client()) {
        client()->setPlatformLayerNeedsDisplay();
    }
#endif
    return;
#endif

    m_player->repaint();
}

void MediaPlayerPrivateGStreamerBase::triggerDrain()
{
    WTF::GMutexLocker<GMutex> lock(m_sampleMutex);
    m_videoSize.setWidth(0);
    m_videoSize.setHeight(0);
    m_sample = nullptr;
}

void MediaPlayerPrivateGStreamerBase::setSize(const IntSize& size)
{
    INFO_MEDIA_MESSAGE("Setting size to %dx%d", size.width(), size.height());
    m_size = size;
}

void MediaPlayerPrivateGStreamerBase::paint(GraphicsContext& context, const FloatRect& rect)
{
#if USE(COORDINATED_GRAPHICS_THREADED)
        return;
#elif USE(TEXTURE_MAPPER_GL) && !USE(COORDINATED_GRAPHICS)
    if (client())
        return;
#endif

    if (context.paintingDisabled())
        return;

    if (!m_player->visible())
        return;

    WTF::GMutexLocker<GMutex> lock(m_sampleMutex);
    if (!GST_IS_SAMPLE(m_sample.get()))
        return;

    RefPtr<ImageGStreamer> gstImage = ImageGStreamer::createImage(m_sample.get());
    if (!gstImage)
        return;

    if (Image* image = reinterpret_cast<Image*>(gstImage->image().get()))
        context.drawImage(*image, ColorSpaceSRGB, rect, gstImage->rect(), CompositeCopy);
}

#if USE(TEXTURE_MAPPER_GL) && !USE(COORDINATED_GRAPHICS)
void MediaPlayerPrivateGStreamerBase::paintToTextureMapper(TextureMapper* textureMapper, const FloatRect& targetRect, const TransformationMatrix& modelViewMatrix, float opacity)
{
    if (!m_player->visible())
        return;

    if (m_usingFallbackVideoSink) {
        RefPtr<BitmapTexture> texture;
        {
            WTF::GMutexLocker<GMutex> lock(m_sampleMutex);

            if (!m_sample)
                return;

            GstCaps* caps = gst_sample_get_caps(m_sample.get());
            if (UNLIKELY(!caps))
                return;

            GstVideoInfo videoInfo;
            gst_video_info_init(&videoInfo);
            if (UNLIKELY(!gst_video_info_from_caps(&videoInfo, caps)))
                return;

            IntSize size = IntSize(GST_VIDEO_INFO_WIDTH(&videoInfo), GST_VIDEO_INFO_HEIGHT(&videoInfo));
            texture = textureMapper->acquireTextureFromPool(size, GST_VIDEO_INFO_HAS_ALPHA(&videoInfo));
            updateTexture(static_cast<BitmapTextureGL&>(*texture), videoInfo);
        }
        textureMapper->drawTexture(*texture.get(), targetRect, matrix, opacity);
        return;
    }

#if USE(GSTREAMER_GL)
    {
        WTF::GMutexLocker<GMutex> lock(m_sampleMutex);
        if (!GST_IS_SAMPLE(m_sample.get()))
            return;

        unsigned textureID = *reinterpret_cast<unsigned*>(m_videoFrame->data[0]);
        TextureMapperGL::Flags flags = m_textureMapperRotationFlag | (GST_VIDEO_INFO_HAS_ALPHA(m_videoInfo) ? TextureMapperGL::ShouldBlend : 0);

        IntSize size = IntSize(GST_VIDEO_INFO_WIDTH(m_videoInfo), GST_VIDEO_INFO_HEIGHT(m_videoInfo));
        TextureMapperGL* textureMapperGL = reinterpret_cast<TextureMapperGL*>(textureMapper);
        textureMapperGL->drawTexture(textureID, flags, size, targetRect, modelViewMatrix, opacity);
    }
#endif
}
#endif

bool MediaPlayerPrivateGStreamerBase::supportsFullscreen() const
{
    return true;
}

PlatformMedia MediaPlayerPrivateGStreamerBase::platformMedia() const
{
    return NoPlatformMedia;
}

MediaPlayer::MovieLoadType MediaPlayerPrivateGStreamerBase::movieLoadType() const
{
    if (m_readyState == MediaPlayer::HaveNothing)
        return MediaPlayer::Unknown;

    if (isLiveStream())
        return MediaPlayer::LiveStream;

    return MediaPlayer::Download;
}

#if !USE(HOLE_PUNCH_GSTREAMER)
GstElement* MediaPlayerPrivateGStreamerBase::createVideoSink()
{
    GstElement* videoSink = nullptr;
#if USE(GSTREAMER_GL)
    if (webkitGstCheckVersion(1, 5, 0)) {
        m_videoSink = gst_element_factory_make("glimagesink", nullptr);
        if (m_videoSink) {
            m_repaintHandler = g_signal_connect(m_videoSink.get(), "client-draw", G_CALLBACK(mediaPlayerPrivateDrawCallback), this);
            videoSink = m_videoSink.get();
        }
    }
#endif

    if (!m_videoSink) {
        m_usingFallbackVideoSink = true;
        m_videoSink = webkitVideoSinkNew();
        m_repaintHandler = g_signal_connect(m_videoSink.get(), "repaint-requested", G_CALLBACK(mediaPlayerPrivateRepaintCallback), this);
        m_drainHandler = g_signal_connect(m_videoSink.get(), "drain", G_CALLBACK(mediaPlayerPrivateDrainCallback), this);
    }

    m_fpsSink = gst_element_factory_make("fpsdisplaysink", "sink");
    if (m_fpsSink) {
        g_object_set(m_fpsSink.get(), "silent", TRUE , nullptr);

        // Turn off text overlay unless logging is enabled.
#if LOG_DISABLED
        g_object_set(m_fpsSink.get(), "text-overlay", FALSE , nullptr);
#else
        if (!isLogChannelEnabled("Media"))
            g_object_set(m_fpsSink.get(), "text-overlay", FALSE , nullptr);
#endif // LOG_DISABLED

        if (g_object_class_find_property(G_OBJECT_GET_CLASS(m_fpsSink.get()), "video-sink")) {
            g_object_set(m_fpsSink.get(), "video-sink", m_videoSink.get(), nullptr);
            videoSink = m_fpsSink.get();
        } else
            m_fpsSink = nullptr;
    }

    if (!m_fpsSink)
        videoSink = m_videoSink.get();

    ASSERT(videoSink);

    return videoSink;
}
#endif

void MediaPlayerPrivateGStreamerBase::setStreamVolumeElement(GstStreamVolume* volume)
{
    ASSERT(!m_volumeElement);
    m_volumeElement = volume;

    // We don't set the initial volume because we trust the sink to keep it for us. See
    // https://bugs.webkit.org/show_bug.cgi?id=118974 for more information.
    if (!m_player->platformVolumeConfigurationRequired()) {
        LOG_MEDIA_MESSAGE("Setting stream volume to %f", m_player->volume());
        g_object_set(m_volumeElement.get(), "volume", m_player->volume(), NULL);
    } else
        LOG_MEDIA_MESSAGE("Not setting stream volume, trusting system one");

    LOG_MEDIA_MESSAGE("Setting stream muted %d",  m_player->muted());
    g_object_set(m_volumeElement.get(), "mute", m_player->muted(), NULL);

    m_volumeSignalHandler = g_signal_connect(m_volumeElement.get(), "notify::volume", G_CALLBACK(mediaPlayerPrivateVolumeChangedCallback), this);
    m_muteSignalHandler = g_signal_connect(m_volumeElement.get(), "notify::mute", G_CALLBACK(mediaPlayerPrivateMuteChangedCallback), this);
}

unsigned MediaPlayerPrivateGStreamerBase::decodedFrameCount() const
{
    guint64 decodedFrames = 0;
    if (m_fpsSink)
        g_object_get(m_fpsSink.get(), "frames-rendered", &decodedFrames, NULL);
    return static_cast<unsigned>(decodedFrames);
}

unsigned MediaPlayerPrivateGStreamerBase::droppedFrameCount() const
{
    guint64 framesDropped = 0;
    if (m_fpsSink)
        g_object_get(m_fpsSink.get(), "frames-dropped", &framesDropped, NULL);
    return static_cast<unsigned>(framesDropped);
}

unsigned MediaPlayerPrivateGStreamerBase::audioDecodedByteCount() const
{
    GstQuery* query = gst_query_new_position(GST_FORMAT_BYTES);
    gint64 position = 0;

    if (audioSink() && gst_element_query(audioSink(), query))
        gst_query_parse_position(query, 0, &position);

    gst_query_unref(query);
    return static_cast<unsigned>(position);
}

unsigned MediaPlayerPrivateGStreamerBase::videoDecodedByteCount() const
{
    GstQuery* query = gst_query_new_position(GST_FORMAT_BYTES);
    gint64 position = 0;

    if (gst_element_query(m_videoSink.get(), query))
        gst_query_parse_position(query, 0, &position);

    gst_query_unref(query);
    return static_cast<unsigned>(position);
}

bool MediaPlayerPrivateGStreamerBase::supportsKeySystem(const String& keySystem, const String& mimeType)
{
    LOG_MEDIA_MESSAGE("Checking for KeySystem support with %s and type %s", keySystem.utf8().data(), mimeType.utf8().data());

#if ENABLE(ENCRYPTED_MEDIA)
    if (equalIgnoringCase(keySystem, "org.w3.clearkey"))
        return true;
#endif

#if USE(DXDRM) && (ENABLE(ENCRYPTED_MEDIA) || ENABLE(ENCRYPTED_MEDIA_V2))
    if (equalIgnoringCase(keySystem, "com.microsoft.playready")
        || equalIgnoringCase(keySystem, "com.youtube.playready"))
        return true;
#endif

    return false;
}

#if USE(DXDRM)
DiscretixSession* MediaPlayerPrivateGStreamerBase::dxdrmSession() const
{
    DiscretixSession* session = nullptr;
#if ENABLE(ENCRYPTED_MEDIA)
    session = m_dxdrmSession;
#elif ENABLE(ENCRYPTED_MEDIA_V2)
    if (m_cdmSession) {
        CDMPRSessionGStreamer* cdmSession = static_cast<CDMPRSessionGStreamer*>(m_cdmSession);
        session = static_cast<DiscretixSession*>(cdmSession);
    }
#endif
    return session;
}

void MediaPlayerPrivateGStreamerBase::emitSession()
{
    DiscretixSession* session = dxdrmSession();
    if (!session->ready())
        return;

    gst_element_send_event(m_pipeline.get(), gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_OOB,
        gst_structure_new("dxdrm-session", "session", G_TYPE_POINTER, session, nullptr)));
}
#endif

void MediaPlayerPrivateGStreamerBase::handleElementMessage(GstMessage* message)
{
#if ENABLE(ENCRYPTED_MEDIA) || ENABLE(ENCRYPTED_MEDIA_V2)
    const GstStructure* structure = gst_message_get_structure(message);
    if (!gst_structure_has_name(structure, "drm-key-needed"))
        return;

    LOG_MEDIA_MESSAGE("handling drm-key-needed message");
#if USE(DXDRM)
    DiscretixSession* session = dxdrmSession();
    if (session && session->keyRequested()) {
        LOG_MEDIA_MESSAGE("key requested already");
        if (session->ready()) {
            LOG_MEDIA_MESSAGE("key already negotiated");
            emitSession();
        }
        return;
    }
#endif
    // Here we receive the DRM init data from the pipeline: we will emit
    // the needkey event with that data and the browser might create a
    // CDMSession from this event handler. If such a session was created
    // We will emit the message event from the session to provide the
    // DRM challenge to the browser and wait for an update. If on the
    // contrary no session was created we won't wait and let the pipeline
    // error out by itself.
    GstBuffer* data;
    const char* keySystemId;
    gboolean valid = gst_structure_get(structure, "data", GST_TYPE_BUFFER, &data,
                                       "key-system-id", G_TYPE_STRING, &keySystemId, nullptr);
    GstMapInfo mapInfo;
    if (!valid || !gst_buffer_map(data, &mapInfo, GST_MAP_READ))
        return;

    GST_DEBUG("scheduling keyNeeded event");
    // FIXME: Provide a somehow valid sessionId.
#if ENABLE(ENCRYPTED_MEDIA)
    needKey(keySystemId, "sessionId", reinterpret_cast<const unsigned char *>(mapInfo.data), mapInfo.size);
#elif ENABLE(ENCRYPTED_MEDIA_V2)
    RefPtr<Uint8Array> initData = Uint8Array::create(reinterpret_cast<const unsigned char *>(mapInfo.data), mapInfo.size);
    needKey(initData);
#else
    ASSERT_NOT_REACHED();
#endif
    gst_buffer_unmap(data, &mapInfo);
#else
    UNUSED_PARAM(message);
#endif
}


#if ENABLE(ENCRYPTED_MEDIA) || ENABLE(ENCRYPTED_MEDIA_V2)
void MediaPlayerPrivateGStreamerBase::dispatchDecryptionKey(GstBuffer* buffer)
{
    gst_element_send_event(m_pipeline.get(), gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_OOB,
        gst_structure_new("drm-cipher", "key", GST_TYPE_BUFFER, buffer, nullptr)));
}
#endif

#if ENABLE(ENCRYPTED_MEDIA)
MediaPlayer::MediaKeyException MediaPlayerPrivateGStreamerBase::addKey(const String& keySystem, const unsigned char* keyData, unsigned keyLength, const unsigned char* /* initData */, unsigned /* initDataLength */ , const String& sessionID)
{
    LOG_MEDIA_MESSAGE("addKey system: %s, length: %u, session: %s", keySystem.utf8().data(), keyLength, sessionID.utf8().data());

#if USE(DXDRM)
    if (equalIgnoringCase(keySystem, "com.microsoft.playready")
        || equalIgnoringCase(keySystem, "com.youtube.playready")) {
        RefPtr<Uint8Array> key = Uint8Array::create(keyData, keyLength);
        RefPtr<Uint8Array> nextMessage;
        unsigned short errorCode;
        unsigned long systemCode;

        bool result = m_dxdrmSession->dxdrmProcessKey(key.get(), nextMessage, errorCode, systemCode);
        if (errorCode || !result) {
            LOG_MEDIA_MESSAGE("Error processing key: errorCode: %u, result: %d", errorCode, result);
            return MediaPlayer::InvalidPlayerState;
        }

        // XXX: use nextMessage here and send a new keyMessage is ack is needed?
        emitSession();

        return MediaPlayer::NoError;
    }
#endif

    if (!equalIgnoringCase(keySystem, "org.w3.clearkey"))
        return MediaPlayer::KeySystemNotSupported;

    GstBuffer* buffer = gst_buffer_new_wrapped(g_memdup(keyData, keyLength), keyLength);
    dispatchDecryptionKey(buffer);
    gst_buffer_unref(buffer);
    return MediaPlayer::NoError;
}

MediaPlayer::MediaKeyException MediaPlayerPrivateGStreamerBase::generateKeyRequest(const String& keySystem, const unsigned char* initDataPtr, unsigned initDataLength)
{
    LOG_MEDIA_MESSAGE("generating key request for system: %s", keySystem.utf8().data());
#if USE(DXDRM)
    if (equalIgnoringCase(keySystem, "com.microsoft.playready")
        || equalIgnoringCase(keySystem, "com.youtube.playready")) {
        if (!m_dxdrmSession)
            m_dxdrmSession = new DiscretixSession();
        unsigned short errorCode;
        unsigned long systemCode;
        RefPtr<Uint8Array> initData = Uint8Array::create(initDataPtr, initDataLength);
        String destinationURL;
        RefPtr<Uint8Array> result = m_dxdrmSession->dxdrmGenerateKeyRequest(initData.get(), destinationURL, errorCode, systemCode);
        if (errorCode)
            return MediaPlayer::InvalidPlayerState;

        URL url(URL(), destinationURL);
        m_player->keyMessage(keySystem, createCanonicalUUIDString(), result->data(), result->length(), url);
        return MediaPlayer::NoError;
    }
#endif

    if (!equalIgnoringCase(keySystem, "org.w3.clearkey"))
        return MediaPlayer::KeySystemNotSupported;

    m_player->keyMessage(keySystem, createCanonicalUUIDString(), initDataPtr, initDataLength, URL());
    return MediaPlayer::NoError;
}

MediaPlayer::MediaKeyException MediaPlayerPrivateGStreamerBase::cancelKeyRequest(const String& /* keySystem */ , const String& /* sessionID */)
{
    LOG_MEDIA_MESSAGE("cancelKeyRequest");
    return MediaPlayer::KeySystemNotSupported;
}

void MediaPlayerPrivateGStreamerBase::needKey(const String& keySystem, const String& sessionId, const unsigned char* initData, unsigned initDataLength)
{
    if (!m_player->keyNeeded(keySystem, sessionId, initData, initDataLength))
        GST_DEBUG("no event handler for key needed");
}
#endif

#if ENABLE(ENCRYPTED_MEDIA_V2)
void MediaPlayerPrivateGStreamerBase::needKey(RefPtr<Uint8Array> initData)
{
    if (!m_player->keyNeeded(initData.get()))
        GST_DEBUG("no event handler for key needed");
}

std::unique_ptr<CDMSession> MediaPlayerPrivateGStreamerBase::createSession(const String& keySystem)
{
    if (!supportsKeySystem(keySystem, emptyString()))
        return nullptr;

    LOG_MEDIA_MESSAGE("creating key session for %s", keySystem.utf8().data());
#if USE(DXDRM)
    if (equalIgnoringCase(keySystem, "com.microsoft.playready")
        || equalIgnoringCase(keySystem, "com.youtube.playready"))
        return std::make_unique<CDMPRSessionGStreamer>();
#endif

    return nullptr;
}

void MediaPlayerPrivateGStreamerBase::setCDMSession(CDMSession* session)
{
    LOG_MEDIA_MESSAGE("setting CDM session");
    m_cdmSession = session;
}

void MediaPlayerPrivateGStreamerBase::keyAdded()
{
#if USE(DXDRM)
    emitSession();
#endif
}
#endif

MediaPlayer::SupportsType MediaPlayerPrivateGStreamerBase::extendedSupportsType(const MediaEngineSupportParameters& parameters, MediaPlayer::SupportsType result)
{
#if ENABLE(ENCRYPTED_MEDIA)
    // From: <http://dvcs.w3.org/hg/html-media/raw-file/eme-v0.1b/encrypted-media/encrypted-media.html#dom-canplaytype>
    // In addition to the steps in the current specification, this method must run the following steps:

    // 1. Check whether the Key System is supported with the specified container and codec type(s) by following the steps for the first matching condition from the following list:
    //    If keySystem is null, continue to the next step.
    if (parameters.keySystem.isNull() || parameters.keySystem.isEmpty())
        return result;

    // If keySystem contains an unrecognized or unsupported Key System, return the empty string
    if (supportsKeySystem(parameters.keySystem, emptyString()))
        result = MediaPlayer::IsSupported;
#else
    UNUSED_PARAM(parameters);
#endif
    return result;
}

void MediaPlayerPrivateGStreamerBase::setVideoSourceRotation(VideoSourceRotation rotation)
{
    if (m_videoSourceRotation == rotation)
            return;

    m_videoSourceRotation = rotation;

    switch (m_videoSourceRotation) {
    case NoVideoSourceRotation:
#if USE(TEXTURE_MAPPER_GL)
        m_textureMapperRotationFlag = 0;
#endif
        break;
    case VideoSourceRotation90:
#if USE(TEXTURE_MAPPER_GL)
        m_textureMapperRotationFlag = TextureMapperGL::ShouldRotateTexture90;
#endif
        break;
    case VideoSourceRotation180:
#if USE(TEXTURE_MAPPER_GL)
        m_textureMapperRotationFlag = TextureMapperGL::ShouldRotateTexture180;
#endif
        break;
    case VideoSourceRotation270:
#if USE(TEXTURE_MAPPER_GL)
        m_textureMapperRotationFlag = TextureMapperGL::ShouldRotateTexture270;
#endif
        break;
    }
}

}

#endif // USE(GSTREAMER)
