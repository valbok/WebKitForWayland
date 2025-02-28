/*
 *  Copyright (C) 2009, 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *  Copyright (C) 2013 Collabora Ltd.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "WebKitWebSourceGStreamer.h"

#if ENABLE(VIDEO) && USE(GSTREAMER)

#include "GRefPtrGStreamer.h"
#include "GStreamerUtilities.h"
#include "GUniquePtrGStreamer.h"
#include "HTTPHeaderNames.h"
#include "MainThreadNotifier.h"
#include "MediaPlayer.h"
#include "NotImplemented.h"
#include "PlatformMediaResourceLoader.h"
#include "ResourceError.h"
#include "ResourceHandle.h"
#include "ResourceHandleClient.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "SharedBuffer.h"
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/pbutils/missing-plugins.h>
#include <wtf/MainThread.h>
#include <wtf/Noncopyable.h>
#include <wtf/glib/GSourceWrap.h>
#include <wtf/glib/GMutexLocker.h>
#include <wtf/glib/GRefPtr.h>
#include <wtf/glib/GUniquePtr.h>
#include <wtf/text/CString.h>

#include "CachedResourceLoader.h"
#include "CookieJar.h"

using namespace WebCore;

class StreamingClient {
    public:
        StreamingClient(WebKitWebSrc*);
        virtual ~StreamingClient();

    protected:
        char* createReadBuffer(size_t requestedSize, size_t& actualSize);
        void handleResponseReceived(const ResourceResponse&);
        void handleDataReceived(const char*, int);
        void handleNotifyFinished();

        GstElement* m_src;
};

class CachedResourceStreamingClient final : public PlatformMediaResourceLoaderClient, public StreamingClient {
    WTF_MAKE_NONCOPYABLE(CachedResourceStreamingClient);
    public:
        CachedResourceStreamingClient(WebKitWebSrc*);
        virtual ~CachedResourceStreamingClient();

    private:
        // PlatformMediaResourceLoaderClient virtual methods.
#if USE(SOUP)
        virtual char* getOrCreateReadBuffer(size_t requestedSize, size_t& actualSize) override;
#endif
        virtual void responseReceived(const ResourceResponse&) override;
        virtual void dataReceived(const char*, int) override;
        virtual void accessControlCheckFailed(const ResourceError&) override;
        virtual void loadFailed(const ResourceError&) override;
        virtual void loadFinished() override;
};

class ResourceHandleStreamingClient : public ResourceHandleClient, public StreamingClient {
    WTF_MAKE_NONCOPYABLE(ResourceHandleStreamingClient); WTF_MAKE_FAST_ALLOCATED;
    public:
        ResourceHandleStreamingClient(WebKitWebSrc*, const ResourceRequest&);
        virtual ~ResourceHandleStreamingClient();

        // StreamingClient virtual methods.
        bool loadFailed() const;
        void setDefersLoading(bool);

    private:
        // ResourceHandleClient virtual methods.
        virtual char* getOrCreateReadBuffer(size_t requestedSize, size_t& actualSize);
        virtual void willSendRequest(ResourceHandle*, ResourceRequest&, const ResourceResponse&);
        virtual void didReceiveResponse(ResourceHandle*, const ResourceResponse&);
        virtual void didReceiveData(ResourceHandle*, const char*, unsigned, int);
        virtual void didReceiveBuffer(ResourceHandle*, PassRefPtr<SharedBuffer>, int encodedLength);
        virtual void didFinishLoading(ResourceHandle*, double /*finishTime*/);
        virtual void didFail(ResourceHandle*, const ResourceError&);
        virtual void wasBlocked(ResourceHandle*);
        virtual void cannotShowURL(ResourceHandle*);

        RefPtr<ResourceHandle> m_resource;
};

enum MainThreadSourceNotification {
    Start = 1 << 0,
    Stop = 1 << 1,
    NeedData = 1 << 2,
    EnoughData = 1 << 3,
    Seek = 1 << 4
};

#define WEBKIT_WEB_SRC_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), WEBKIT_TYPE_WEB_SRC, WebKitWebSrcPrivate))
struct _WebKitWebSrcPrivate {
    GstAppSrc* appsrc;
    GstPad* srcpad;
    gchar* uri;
    bool keepAlive;
    GUniquePtr<GstStructure> extraHeaders;
    bool compress;
    GUniquePtr<gchar> httpMethod;

    WebCore::MediaPlayer* player;

    RefPtr<PlatformMediaResourceLoader> loader;
    ResourceHandleStreamingClient* client;

    bool didPassAccessControlCheck;

    guint64 offset;
    guint64 size;
    gboolean seekable;
    gboolean paused;
    bool isSeeking;

    guint64 requestedOffset;

    MainThreadNotifier<MainThreadSourceNotification> notifier;
    GRefPtr<GstBuffer> buffer;
};

enum {
    PROP_0,
    PROP_LOCATION,
    PROP_KEEP_ALIVE,
    PROP_EXTRA_HEADERS,
    PROP_COMPRESS,
    PROP_METHOD
};

static GstStaticPadTemplate srcTemplate = GST_STATIC_PAD_TEMPLATE("src",
                                                                  GST_PAD_SRC,
                                                                  GST_PAD_ALWAYS,
                                                                  GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC(webkit_web_src_debug);
#define GST_CAT_DEFAULT webkit_web_src_debug

static void webKitWebSrcUriHandlerInit(gpointer gIface, gpointer ifaceData);

static void webKitWebSrcDispose(GObject*);
static void webKitWebSrcFinalize(GObject*);
static void webKitWebSrcSetProperty(GObject*, guint propertyID, const GValue*, GParamSpec*);
static void webKitWebSrcGetProperty(GObject*, guint propertyID, GValue*, GParamSpec*);
static GstStateChangeReturn webKitWebSrcChangeState(GstElement*, GstStateChange);

static gboolean webKitWebSrcQueryWithParent(GstPad*, GstObject*, GstQuery*);

static void webKitWebSrcNeedData(WebKitWebSrc*);
static void webKitWebSrcEnoughData(WebKitWebSrc*);
static void webKitWebSrcSeek(WebKitWebSrc*);

static GstAppSrcCallbacks appsrcCallbacks = {
    // need_data
    [](GstAppSrc*, guint, gpointer userData) {
        WebKitWebSrc* src = WEBKIT_WEB_SRC(userData);
        WebKitWebSrcPrivate* priv = src->priv;

        {
            WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));
            if (!priv->paused)
                return;
        }

        GRefPtr<WebKitWebSrc> protector(src);
        priv->notifier.notify(MainThreadSourceNotification::NeedData, [protector] { webKitWebSrcNeedData(protector.get()); });
    },
    // enough_data
    [](GstAppSrc*, gpointer userData) {
        WebKitWebSrc* src = WEBKIT_WEB_SRC(userData);
        WebKitWebSrcPrivate* priv = src->priv;

        {
            WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));
            if (priv->paused)
                return;
        }

        GRefPtr<WebKitWebSrc> protector(src);
        priv->notifier.notify(MainThreadSourceNotification::EnoughData, [protector] { webKitWebSrcEnoughData(protector.get()); });
    },
    // seek_data
    [](GstAppSrc*, guint64 offset, gpointer userData) -> gboolean {
        WebKitWebSrc* src = WEBKIT_WEB_SRC(userData);
        WebKitWebSrcPrivate* priv = src->priv;

        {
            WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));
            if (offset == priv->offset && priv->requestedOffset == priv->offset)
                return TRUE;

            if (!priv->seekable)
                return FALSE;

            priv->isSeeking = true;
            priv->requestedOffset = offset;
        }

        GRefPtr<WebKitWebSrc> protector(src);
        priv->notifier.notify(MainThreadSourceNotification::Seek, [protector] { webKitWebSrcSeek(protector.get()); });
        return TRUE;
    },
    { nullptr }
};

#define webkit_web_src_parent_class parent_class
// We split this out into another macro to avoid a check-webkit-style error.
#define WEBKIT_WEB_SRC_CATEGORY_INIT GST_DEBUG_CATEGORY_INIT(webkit_web_src_debug, "webkitwebsrc", 0, "websrc element");
G_DEFINE_TYPE_WITH_CODE(WebKitWebSrc, webkit_web_src, GST_TYPE_BIN,
                         G_IMPLEMENT_INTERFACE(GST_TYPE_URI_HANDLER, webKitWebSrcUriHandlerInit);
                         WEBKIT_WEB_SRC_CATEGORY_INIT);

static void webkit_web_src_class_init(WebKitWebSrcClass* klass)
{
    GObjectClass* oklass = G_OBJECT_CLASS(klass);
    GstElementClass* eklass = GST_ELEMENT_CLASS(klass);

    oklass->dispose = webKitWebSrcDispose;
    oklass->finalize = webKitWebSrcFinalize;
    oklass->set_property = webKitWebSrcSetProperty;
    oklass->get_property = webKitWebSrcGetProperty;

    gst_element_class_add_pad_template(eklass,
                                       gst_static_pad_template_get(&srcTemplate));
    gst_element_class_set_metadata(eklass, "WebKit Web source element", "Source", "Handles HTTP/HTTPS uris",
                               "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

    /* Allows setting the uri using the 'location' property, which is used
     * for example by gst_element_make_from_uri() */
    g_object_class_install_property(oklass,
                                    PROP_LOCATION,
                                    g_param_spec_string("location",
                                                        "location",
                                                        "Location to read from",
                                                        0,
                                                        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(oklass, PROP_KEEP_ALIVE,
        g_param_spec_boolean("keep-alive", "keep-alive", "Use HTTP persistent connections",
            FALSE, static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(oklass, PROP_EXTRA_HEADERS,
        g_param_spec_boxed("extra-headers", "Extra Headers", "Extra headers to append to the HTTP request",
            GST_TYPE_STRUCTURE, static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(oklass, PROP_COMPRESS,
        g_param_spec_boolean("compress", "Compress", "Allow compressed content encodings",
            FALSE, static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(oklass, PROP_METHOD,
        g_param_spec_string("method", "method", "The HTTP method to use (default: GET)",
            nullptr, static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    eklass->change_state = webKitWebSrcChangeState;

    g_type_class_add_private(klass, sizeof(WebKitWebSrcPrivate));
}

static void webkit_web_src_init(WebKitWebSrc* src)
{
    WebKitWebSrcPrivate* priv = WEBKIT_WEB_SRC_GET_PRIVATE(src);

    src->priv = priv;
    new (priv) WebKitWebSrcPrivate();

    priv->appsrc = GST_APP_SRC(gst_element_factory_make("appsrc", 0));
    if (!priv->appsrc) {
        GST_ERROR_OBJECT(src, "Failed to create appsrc");
        return;
    }

    gst_bin_add(GST_BIN(src), GST_ELEMENT(priv->appsrc));


    GRefPtr<GstPad> targetPad = adoptGRef(gst_element_get_static_pad(GST_ELEMENT(priv->appsrc), "src"));
    priv->srcpad = webkitGstGhostPadFromStaticTemplate(&srcTemplate, "src", targetPad.get());

    gst_element_add_pad(GST_ELEMENT(src), priv->srcpad);

    GST_OBJECT_FLAG_SET(priv->srcpad, GST_PAD_FLAG_NEED_PARENT);
    gst_pad_set_query_function(priv->srcpad, webKitWebSrcQueryWithParent);

    gst_app_src_set_callbacks(priv->appsrc, &appsrcCallbacks, src, 0);
    gst_app_src_set_emit_signals(priv->appsrc, FALSE);
    gst_app_src_set_stream_type(priv->appsrc, GST_APP_STREAM_TYPE_SEEKABLE);

    // 512k is a abitrary number but we should choose a value
    // here to not pause/unpause the SoupMessage too often and
    // to make sure there's always some data available for
    // GStreamer to handle.
    gst_app_src_set_max_bytes(priv->appsrc, 512 * 1024);

    // Emit the need-data signal if the queue contains less
    // than 20% of data. Without this the need-data signal
    // is emitted when the queue is empty, we then dispatch
    // the soup message unpausing to the main loop and from
    // there unpause the soup message. This already takes
    // quite some time and libsoup even needs some more time
    // to actually provide data again. If we do all this
    // already if the queue is 20% empty, it's much more
    // likely that libsoup already provides new data before
    // the queue is really empty.
    // This might need tweaking for ports not using libsoup.
    g_object_set(priv->appsrc, "min-percent", 20, NULL);

    gst_app_src_set_caps(priv->appsrc, 0);
    gst_app_src_set_size(priv->appsrc, -1);
}

static void webKitWebSrcDispose(GObject* object)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(object);
    WebKitWebSrcPrivate* priv = src->priv;

    priv->player = 0;

    GST_CALL_PARENT(G_OBJECT_CLASS, dispose, (object));
}

static void webKitWebSrcFinalize(GObject* object)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(object);
    WebKitWebSrcPrivate* priv = src->priv;

    g_free(priv->uri);
    priv->~WebKitWebSrcPrivate();

    GST_CALL_PARENT(G_OBJECT_CLASS, finalize, (object));
}

static void webKitWebSrcSetProperty(GObject* object, guint propID, const GValue* value, GParamSpec* pspec)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(object);

    switch (propID) {
    case PROP_LOCATION:
        gst_uri_handler_set_uri(reinterpret_cast<GstURIHandler*>(src), g_value_get_string(value), 0);
        break;
    case PROP_KEEP_ALIVE:
        src->priv->keepAlive = g_value_get_boolean(value);
        break;
    case PROP_EXTRA_HEADERS: {
        const GstStructure* s = gst_value_get_structure(value);
        src->priv->extraHeaders.reset(s ? gst_structure_copy(s) : nullptr);
        break;
    }
    case PROP_COMPRESS:
        src->priv->compress = g_value_get_boolean(value);
        break;
    case PROP_METHOD:
        src->priv->httpMethod.reset(g_value_dup_string(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propID, pspec);
        break;
    }
}

static void webKitWebSrcGetProperty(GObject* object, guint propID, GValue* value, GParamSpec* pspec)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(object);
    WebKitWebSrcPrivate* priv = src->priv;

    WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));
    switch (propID) {
    case PROP_LOCATION:
        g_value_set_string(value, priv->uri);
        break;
    case PROP_KEEP_ALIVE:
        g_value_set_boolean(value, priv->keepAlive);
        break;
    case PROP_EXTRA_HEADERS:
        gst_value_set_structure(value, priv->extraHeaders.get());
        break;
    case PROP_COMPRESS:
        g_value_set_boolean(value, priv->compress);
        break;
    case PROP_METHOD:
        g_value_set_string(value, priv->httpMethod.get());
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propID, pspec);
        break;
    }
}

static void webKitWebSrcStop(WebKitWebSrc* src)
{
    WebKitWebSrcPrivate* priv = src->priv;

    WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));

    bool wasSeeking = std::exchange(priv->isSeeking, false);

    priv->notifier.cancelPendingNotifications(MainThreadSourceNotification::NeedData | MainThreadSourceNotification::EnoughData | MainThreadSourceNotification::Seek);

    {
        ResourceHandleStreamingClient* client = nullptr;
        if (priv->client) {
            client = priv->client;
            priv->client = nullptr;
        }

        PlatformMediaResourceLoader* loader = nullptr;
        if (!priv->keepAlive && priv->loader)
            loader = priv->loader.leakRef();

        if (client || loader)
            callOnMainThread([client, loader] {
                delete client;

                RefPtr<PlatformMediaResourceLoader> loaderRef = adoptRef(loader);
            });
    }

    if (priv->buffer) {
        unmapGstBuffer(priv->buffer.get());
        priv->buffer.clear();
    }

    priv->paused = FALSE;

    priv->offset = 0;
    priv->seekable = FALSE;

    if (!wasSeeking) {
        priv->size = 0;
        priv->requestedOffset = 0;
        priv->player = 0;
    }

    locker.unlock();

    if (priv->appsrc) {
        gst_app_src_set_caps(priv->appsrc, 0);
        if (!wasSeeking)
            gst_app_src_set_size(priv->appsrc, -1);
    }

    GST_DEBUG_OBJECT(src, "Stopped request");
}

static bool webKitWebSrcSetExtraHeader(GQuark fieldId, const GValue* value, gpointer userData)
{
    GUniquePtr<gchar> fieldContent;

    if (G_VALUE_HOLDS_STRING(value))
        fieldContent.reset(g_value_dup_string(value));
    else {
        GValue dest = G_VALUE_INIT;

        g_value_init(&dest, G_TYPE_STRING);
        if (g_value_transform(value, &dest))
            fieldContent.reset(g_value_dup_string(&dest));
    }

    const gchar* fieldName = g_quark_to_string(fieldId);
    if (!fieldContent.get()) {
        GST_ERROR("extra-headers field '%s' contains no value or can't be converted to a string", fieldName);
        return false;
    }

    GST_DEBUG("Appending extra header: \"%s: %s\"", fieldName, fieldContent.get());
    ResourceRequest* request = static_cast<ResourceRequest*>(userData);
    request->setHTTPHeaderField(fieldName, fieldContent.get());
    return true;
}

static gboolean webKitWebSrcProcessExtraHeaders(GQuark fieldId, const GValue* value, gpointer userData)
{
    if (G_VALUE_TYPE(value) == GST_TYPE_ARRAY) {
        unsigned size = gst_value_array_get_size(value);

        for (unsigned i = 0; i < size; i++) {
            if (!webKitWebSrcSetExtraHeader(fieldId, gst_value_array_get_value(value, i), userData))
                return FALSE;
        }
        return TRUE;
    }

    if (G_VALUE_TYPE(value) == GST_TYPE_LIST) {
        unsigned size = gst_value_list_get_size(value);

        for (unsigned i = 0; i < size; i++) {
            if (!webKitWebSrcSetExtraHeader(fieldId, gst_value_list_get_value(value, i), userData))
                return FALSE;
        }
        return TRUE;
    }

    return webKitWebSrcSetExtraHeader(fieldId, value, userData);
}

static void webKitWebSrcStart(WebKitWebSrc* src)
{
    WebKitWebSrcPrivate* priv = src->priv;

    ASSERT(isMainThread());

    WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));

    priv->didPassAccessControlCheck = false;

    if (!priv->uri) {
        GST_ERROR_OBJECT(src, "No URI provided");
        locker.unlock();
        webKitWebSrcStop(src);
        return;
    }

    ASSERT(!priv->client);

    GST_DEBUG_OBJECT(src, "Fetching %s", priv->uri);
    URL url = URL(URL(), priv->uri);

    ResourceRequest request(url);
    request.setAllowCookies(true);
    request.setFirstPartyForCookies(url);

    priv->size = 0;

    if (priv->player)
        request.setHTTPReferrer(priv->player->referrer());

    if (priv->httpMethod.get())
        request.setHTTPMethod(priv->httpMethod.get());

#if USE(SOUP)
    // By default, HTTP Accept-Encoding is disabled here as we don't
    // want the received response to be encoded in any way as we need
    // to rely on the proper size of the returned data on
    // didReceiveResponse.
    // If Accept-Encoding is used, the server may send the data in encoded format and
    // request.expectedContentLength() will have the "wrong" size (the size of the
    // compressed data), even though the data received in didReceiveData is uncompressed.
    // This is however useful to enable for adaptive streaming
    // scenarios, when the demuxer needs to download playlists.
    if (!priv->compress)
        request.setAcceptEncoding(false);
#endif

    // Let Apple web servers know we want to access their nice movie trailers.
    if (!g_ascii_strcasecmp("movies.apple.com", url.host().utf8().data())
        || !g_ascii_strcasecmp("trailers.apple.com", url.host().utf8().data()))
        request.setHTTPUserAgent("Quicktime/7.6.6");

    if (priv->requestedOffset) {
        GUniquePtr<gchar> val(g_strdup_printf("bytes=%" G_GUINT64_FORMAT "-", priv->requestedOffset));
        request.setHTTPHeaderField(HTTPHeaderName::Range, val.get());
    }
    priv->offset = priv->requestedOffset;

    if (!priv->keepAlive) {
        GST_DEBUG_OBJECT(src, "Persistent connection support disabled");
        request.setHTTPHeaderField(HTTPHeaderName::Connection, "close");
    }

    if (priv->extraHeaders)
        gst_structure_foreach(priv->extraHeaders.get(), webKitWebSrcProcessExtraHeaders, &request);

    // We always request Icecast/Shoutcast metadata, just in case ...
    request.setHTTPHeaderField(HTTPHeaderName::IcyMetadata, "1");

    bool loadFailed = true;
    if (priv->player && !priv->loader)
        priv->loader = priv->player->createResourceLoader(std::make_unique<CachedResourceStreamingClient>(src));

    if (priv->loader) {
        PlatformMediaResourceLoader::LoadOptions loadOptions = 0;
        if (request.url().protocolIs("blob"))
            loadOptions |= PlatformMediaResourceLoader::LoadOption::BufferData;
        loadFailed = !priv->loader->start(request, loadOptions);
    } else {
        priv->client = new ResourceHandleStreamingClient(src, request);
        loadFailed = priv->client->loadFailed();
    }

    if (loadFailed) {
        GST_ERROR_OBJECT(src, "Failed to setup streaming client");
        if (priv->client) {
            delete priv->client;
            priv->client = nullptr;
        }
        priv->loader = nullptr;
        locker.unlock();
        webKitWebSrcStop(src);
        return;
    }
    GST_DEBUG_OBJECT(src, "Started request");
}

static GstStateChangeReturn webKitWebSrcChangeState(GstElement* element, GstStateChange transition)
{
    GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
    WebKitWebSrc* src = WEBKIT_WEB_SRC(element);
    WebKitWebSrcPrivate* priv = src->priv;

    switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
        if (!priv->appsrc) {
            gst_element_post_message(element,
                                     gst_missing_element_message_new(element, "appsrc"));
            GST_ELEMENT_ERROR(src, CORE, MISSING_PLUGIN, (0), ("no appsrc"));
            return GST_STATE_CHANGE_FAILURE;
        }
        break;
    default:
        break;
    }

    ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
    if (G_UNLIKELY(ret == GST_STATE_CHANGE_FAILURE)) {
        GST_DEBUG_OBJECT(src, "State change failed");
        return ret;
    }

    switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
        GST_DEBUG_OBJECT(src, "READY->PAUSED");
        GRefPtr<WebKitWebSrc> protector(src);
        priv->notifier.notify(MainThreadSourceNotification::Start, [protector] { webKitWebSrcStart(protector.get()); });
        break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    {
        GST_DEBUG_OBJECT(src, "PAUSED->READY");
        priv->notifier.cancelPendingNotifications();
        GRefPtr<WebKitWebSrc> protector(src);
        priv->notifier.notify(MainThreadSourceNotification::Stop, [protector] { webKitWebSrcStop(protector.get()); });
        break;
    }
    default:
        break;
    }

    return ret;
}

static gboolean webKitWebSrcQueryWithParent(GstPad* pad, GstObject* parent, GstQuery* query)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(GST_ELEMENT(parent));
    gboolean result = FALSE;

    switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_DURATION: {
        GstFormat format;

        gst_query_parse_duration(query, &format, NULL);

        GST_DEBUG_OBJECT(src, "duration query in format %s", gst_format_get_name(format));
        WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));
        if (format == GST_FORMAT_BYTES && src->priv->size > 0) {
            gst_query_set_duration(query, format, src->priv->size);
            result = TRUE;
        }
        break;
    }
    case GST_QUERY_URI: {
        WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));
        gst_query_set_uri(query, src->priv->uri);
        result = TRUE;
        break;
    }
#if GST_CHECK_VERSION(1, 2, 0)
    case GST_QUERY_SCHEDULING: {
        GstSchedulingFlags flags;
        int minSize, maxSize, align;

        gst_query_parse_scheduling(query, &flags, &minSize, &maxSize, &align);
        gst_query_set_scheduling(query, static_cast<GstSchedulingFlags>(flags | GST_SCHEDULING_FLAG_BANDWIDTH_LIMITED), minSize, maxSize, align);
        result = TRUE;
        break;
    }
#endif
    case GST_QUERY_CONTEXT: {
        const gchar* contextType;
        if (gst_query_parse_context_type(query, &contextType) && !g_strcmp0(contextType, "http-headers")) {
            URL url(URL(URL(), src->priv->uri));
            String c = WebCore::cookies(src->priv->player->cachedResourceLoader()->document(), url);
            GstContext* context = gst_context_new("http-headers", FALSE);
            gst_context_make_writable(context);
            GstStructure* contextStructure = gst_context_writable_structure(context);
            const gchar* cookies[] = {c.utf8().data(), nullptr};
            gst_structure_set(contextStructure, "cookies", G_TYPE_STRV, cookies, nullptr);

            gst_query_set_context(query, context);
            result = TRUE;
            break;
        }
    }
    default: {
        GRefPtr<GstPad> target = adoptGRef(gst_ghost_pad_get_target(GST_GHOST_PAD_CAST(pad)));

        // Forward the query to the proxy target pad.
        if (target)
            result = gst_pad_query(target.get(), query);
        break;
    }
    }

    return result;
}

static bool urlHasSupportedProtocol(const URL& url)
{
    return url.isValid() && (url.protocolIsInHTTPFamily() || url.protocolIs("blob"));
}

// uri handler interface

static GstURIType webKitWebSrcUriGetType(GType)
{
    return GST_URI_SRC;
}

const gchar* const* webKitWebSrcGetProtocols(GType)
{
    static const char* protocols[] = {"webkit+http", "webkit+https", "blob", 0 };
    return protocols;
}

static gchar* webKitWebSrcGetUri(GstURIHandler* handler)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(handler);
    gchar* ret;

    WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));
    ret = g_strdup(src->priv->uri);
    return ret;
}

static gboolean webKitWebSrcSetUri(GstURIHandler* handler, const gchar* uri, GError** error)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(handler);
    WebKitWebSrcPrivate* priv = src->priv;

    if (GST_STATE(src) >= GST_STATE_PAUSED) {
        GST_ERROR_OBJECT(src, "URI can only be set in states < PAUSED");
        return FALSE;
    }

    WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));

    g_free(priv->uri);
    priv->uri = 0;

    if (!uri)
        return TRUE;

    URL url(URL(), uri);
    ASSERT(url.protocol().substring(0, 7) == "webkit+");
    url.setProtocol(url.protocol().substring(7));
    if (!urlHasSupportedProtocol(url)) {
        g_set_error(error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI, "Invalid URI '%s'", uri);
        return FALSE;
    }

    priv->uri = g_strdup(url.string().utf8().data());
    return TRUE;
}

static void webKitWebSrcUriHandlerInit(gpointer gIface, gpointer)
{
    GstURIHandlerInterface* iface = (GstURIHandlerInterface *) gIface;

    iface->get_type = webKitWebSrcUriGetType;
    iface->get_protocols = webKitWebSrcGetProtocols;
    iface->get_uri = webKitWebSrcGetUri;
    iface->set_uri = webKitWebSrcSetUri;
}

// appsrc callbacks

static void webKitWebSrcNeedData(WebKitWebSrc* src)
{
    WebKitWebSrcPrivate* priv = src->priv;

    ASSERT(isMainThread());

    GST_DEBUG_OBJECT(src, "Need more data");

    {
        WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));
        priv->paused = FALSE;
    }

    if (priv->client)
        priv->client->setDefersLoading(false);
    if (priv->loader)
        priv->loader->setDefersLoading(false);
}

static void webKitWebSrcEnoughData(WebKitWebSrc* src)
{
    WebKitWebSrcPrivate* priv = src->priv;

    ASSERT(isMainThread());

    GST_DEBUG_OBJECT(src, "Have enough data");

    {
        WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));
        priv->paused = TRUE;
    }

    if (priv->client)
        priv->client->setDefersLoading(true);
    if (priv->loader)
        priv->loader->setDefersLoading(true);
}

static void webKitWebSrcSeek(WebKitWebSrc* src)
{
    ASSERT(isMainThread());

    GST_DEBUG_OBJECT(src, "Seeking to offset: %" G_GUINT64_FORMAT, src->priv->requestedOffset);

    webKitWebSrcStop(src);
    webKitWebSrcStart(src);
}

void webKitWebSrcSetMediaPlayer(WebKitWebSrc* src, WebCore::MediaPlayer* player)
{
    ASSERT(player);
    WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));
    src->priv->player = player;
}

bool webKitSrcPassedCORSAccessCheck(WebKitWebSrc* src)
{
    return src->priv->didPassAccessControlCheck;
}

StreamingClient::StreamingClient(WebKitWebSrc* src)
    : m_src(static_cast<GstElement*>(gst_object_ref(src)))
{
}

StreamingClient::~StreamingClient()
{
    gst_object_unref(m_src);
}

char* StreamingClient::createReadBuffer(size_t requestedSize, size_t& actualSize)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(m_src);
    WebKitWebSrcPrivate* priv = src->priv;

    ASSERT(!priv->buffer);

    GstBuffer* buffer = gst_buffer_new_and_alloc(requestedSize);

    mapGstBuffer(buffer);

    WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));
    priv->buffer = adoptGRef(buffer);
    locker.unlock();

    actualSize = gst_buffer_get_size(buffer);
    return getGstBufferDataPointer(buffer);
}

void StreamingClient::handleResponseReceived(const ResourceResponse& response)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(m_src);
    WebKitWebSrcPrivate* priv = src->priv;

    GST_DEBUG_OBJECT(src, "Received response: %d", response.httpStatusCode());

    if (response.httpStatusCode() >= 400) {
        GST_ELEMENT_ERROR(src, RESOURCE, READ, ("Received %d HTTP error code", response.httpStatusCode()), (nullptr));
        gst_app_src_end_of_stream(priv->appsrc);
        webKitWebSrcStop(src);
        return;
    }

    WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));

    if (priv->isSeeking) {
        GST_DEBUG_OBJECT(src, "Seek in progress, ignoring response");
        return;
    }

    if (priv->requestedOffset) {
        // Seeking ... we expect a 206 == PARTIAL_CONTENT
        if (response.httpStatusCode() == 200) {
            // Range request didn't have a ranged response; resetting offset.
            priv->offset = 0;
        } else if (response.httpStatusCode() != 206) {
            // Range request completely failed.
            locker.unlock();
            GST_ELEMENT_ERROR(src, RESOURCE, READ, ("Received unexpected %d HTTP status code", response.httpStatusCode()), (nullptr));
            gst_app_src_end_of_stream(priv->appsrc);
            webKitWebSrcStop(src);
            return;
        }
    }

    long long length = response.expectedContentLength();
    if (length > 0 && priv->requestedOffset && response.httpStatusCode() == 206)
        length += priv->requestedOffset;

    priv->size = length >= 0 ? length : 0;
    priv->seekable = length > 0 && g_ascii_strcasecmp("none", response.httpHeaderField(HTTPHeaderName::AcceptRanges).utf8().data());

    locker.unlock();

    // notify size/duration
    if (length <= 0)
        gst_app_src_set_size(priv->appsrc, -1);

    gst_app_src_set_caps(priv->appsrc, 0);
}

void StreamingClient::handleDataReceived(const char* data, int length)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(m_src);
    WebKitWebSrcPrivate* priv = src->priv;

    WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));

    GST_LOG_OBJECT(src, "Have %lld bytes of data", priv->buffer ? static_cast<long long>(gst_buffer_get_size(priv->buffer.get())) : length);

    ASSERT(!priv->buffer || data == getGstBufferDataPointer(priv->buffer.get()));

    if (priv->buffer)
        unmapGstBuffer(priv->buffer.get());

    if (priv->isSeeking) {
        GST_DEBUG_OBJECT(src, "Seek in progress, ignoring data");
        priv->buffer.clear();
        return;
    }

    if (priv->offset < priv->requestedOffset) {
        // Range request failed; seeking manually.
        if (priv->offset + length <= priv->requestedOffset) {
            // Discard all the buffers coming before the requested seek position.
            priv->offset += length;
            priv->buffer.clear();
            return;
        }

        if (priv->offset + length > priv->requestedOffset) {
            guint64 offset = priv->requestedOffset - priv->offset;
            data += offset;
            length -= offset;
            if (priv->buffer)
                gst_buffer_resize(priv->buffer.get(), offset, -1);
            priv->offset = priv->requestedOffset;
        }

        priv->requestedOffset = 0;
    }

    // Ports using the GStreamer backend but not the soup implementation of ResourceHandle
    // won't be using buffers provided by this client, the buffer is created here in that case.
    if (!priv->buffer)
        priv->buffer = adoptGRef(createGstBufferForData(data, length));
    else
        gst_buffer_set_size(priv->buffer.get(), static_cast<gssize>(length));

    GST_BUFFER_OFFSET(priv->buffer.get()) = priv->offset;
    if (priv->requestedOffset == priv->offset)
        priv->requestedOffset += length;
    priv->offset += length;
    // priv->size == 0 if received length on didReceiveResponse < 0.
    if (priv->size > 0 && priv->offset > priv->size) {
        GST_DEBUG_OBJECT(src, "Updating internal size from %" G_GUINT64_FORMAT " to %" G_GUINT64_FORMAT, priv->size, priv->offset);
        priv->size = priv->offset;
    }
    GST_BUFFER_OFFSET_END(priv->buffer.get()) = priv->offset;

    locker.unlock();

    GstFlowReturn ret = gst_app_src_push_buffer(priv->appsrc, priv->buffer.leakRef());
    if (ret != GST_FLOW_OK && ret != GST_FLOW_EOS)
        GST_ELEMENT_ERROR(src, CORE, FAILED, (0), (0));
}

void StreamingClient::handleNotifyFinished()
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(m_src);
    WebKitWebSrcPrivate* priv = src->priv;

    GST_DEBUG_OBJECT(src, "Have EOS");

    WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));
    if (!priv->isSeeking) {
        locker.unlock();
        gst_app_src_end_of_stream(priv->appsrc);
    }
}

CachedResourceStreamingClient::CachedResourceStreamingClient(WebKitWebSrc* src)
    : StreamingClient(src)
{
}

CachedResourceStreamingClient::~CachedResourceStreamingClient()
{
}

#if USE(SOUP)
char* CachedResourceStreamingClient::getOrCreateReadBuffer(size_t requestedSize, size_t& actualSize)
{
    return createReadBuffer(requestedSize, actualSize);
}
#endif

void CachedResourceStreamingClient::responseReceived(const ResourceResponse& response)
{
    if (!m_src)
        return;

    WebKitWebSrcPrivate* priv = WEBKIT_WEB_SRC(m_src)->priv;
    if (!priv || !priv->loader)
        return;

    priv->didPassAccessControlCheck = priv->loader->didPassAccessControlCheck();
    handleResponseReceived(response);
}

void CachedResourceStreamingClient::dataReceived(const char* data, int length)
{
    handleDataReceived(data, length);
}

void CachedResourceStreamingClient::accessControlCheckFailed(const ResourceError& error)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(m_src);
    GST_ELEMENT_ERROR(src, RESOURCE, READ, ("%s", error.localizedDescription().utf8().data()), (nullptr));
    gst_app_src_end_of_stream(src->priv->appsrc);
    webKitWebSrcStop(src);
}

void CachedResourceStreamingClient::loadFailed(const ResourceError& error)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(m_src);

    if (!error.isCancellation()) {
        GST_ERROR_OBJECT(src, "Have failure: %s", error.localizedDescription().utf8().data());
        GST_ELEMENT_ERROR(src, RESOURCE, FAILED, ("%s", error.localizedDescription().utf8().data()), (nullptr));
    }

    gst_app_src_end_of_stream(src->priv->appsrc);
}

void CachedResourceStreamingClient::loadFinished()
{
    handleNotifyFinished();
}

ResourceHandleStreamingClient::ResourceHandleStreamingClient(WebKitWebSrc* src, const ResourceRequest& request)
    : StreamingClient(src)
{
    m_resource = ResourceHandle::create(0 /*context*/, request, this, false, false);
}

ResourceHandleStreamingClient::~ResourceHandleStreamingClient()
{
    if (m_resource) {
        m_resource->cancel();
        m_resource = nullptr;
    }
}

bool ResourceHandleStreamingClient::loadFailed() const
{
    return !m_resource;
}

void ResourceHandleStreamingClient::setDefersLoading(bool defers)
{
    if (m_resource)
        m_resource->setDefersLoading(defers);
}

char* ResourceHandleStreamingClient::getOrCreateReadBuffer(size_t requestedSize, size_t& actualSize)
{
    return createReadBuffer(requestedSize, actualSize);
}

void ResourceHandleStreamingClient::willSendRequest(ResourceHandle*, ResourceRequest&, const ResourceResponse&)
{
}

void ResourceHandleStreamingClient::didReceiveResponse(ResourceHandle*, const ResourceResponse& response)
{
    handleResponseReceived(response);
}

void ResourceHandleStreamingClient::didReceiveData(ResourceHandle*, const char* /* data */, unsigned /* length */, int)
{
    ASSERT_NOT_REACHED();
}

void ResourceHandleStreamingClient::didReceiveBuffer(ResourceHandle*, PassRefPtr<SharedBuffer> buffer, int /* encodedLength */)
{
    // This pattern is suggested by SharedBuffer.h.
    const char* segment;
    unsigned position = 0;
    while (unsigned length = buffer->getSomeData(segment, position)) {
        handleDataReceived(segment, length);
        position += length;
    }
}

void ResourceHandleStreamingClient::didFinishLoading(ResourceHandle*, double)
{
    handleNotifyFinished();
}

void ResourceHandleStreamingClient::didFail(ResourceHandle*, const ResourceError& error)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(m_src);

    GST_ERROR_OBJECT(src, "Have failure: %s", error.localizedDescription().utf8().data());
    GST_ELEMENT_ERROR(src, RESOURCE, FAILED, ("%s", error.localizedDescription().utf8().data()), (0));
    gst_app_src_end_of_stream(src->priv->appsrc);
}

void ResourceHandleStreamingClient::wasBlocked(ResourceHandle*)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(m_src);
    GUniquePtr<gchar> uri;

    GST_ERROR_OBJECT(src, "Request was blocked");

    WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));
    uri.reset(g_strdup(src->priv->uri));
    locker.unlock();

    GST_ELEMENT_ERROR(src, RESOURCE, OPEN_READ, ("Access to \"%s\" was blocked", uri.get()), (0));
}

void ResourceHandleStreamingClient::cannotShowURL(ResourceHandle*)
{
    WebKitWebSrc* src = WEBKIT_WEB_SRC(m_src);
    GUniquePtr<gchar> uri;

    GST_ERROR_OBJECT(src, "Cannot show URL");

    WTF::GMutexLocker<GMutex> locker(*GST_OBJECT_GET_LOCK(src));
    uri.reset(g_strdup(src->priv->uri));
    locker.unlock();

    GST_ELEMENT_ERROR(src, RESOURCE, OPEN_READ, ("Can't show \"%s\"", uri.get()), (0));
}

#endif // USE(GSTREAMER)

