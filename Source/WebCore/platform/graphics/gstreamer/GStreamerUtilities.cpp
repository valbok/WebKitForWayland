/*
 *  Copyright (C) 2012 Igalia S.L
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

#if USE(GSTREAMER)
#include "GStreamerUtilities.h"

#include "IntSize.h"

#include <gst/audio/audio-info.h>
#include <gst/gst.h>
#include <wtf/MathExtras.h>
#include <wtf/glib/GUniquePtr.h>

#if ENABLE(VIDEO_TRACK) && USE(GSTREAMER_MPEGTS)
#define GST_USE_UNSTABLE_API
#include <gst/mpegts/mpegts.h>
#undef GST_USE_UNSTABLE_API
#endif

namespace WebCore {

const char* webkitGstMapInfoQuarkString = "webkit-gst-map-info";

GstPad* webkitGstGhostPadFromStaticTemplate(GstStaticPadTemplate* staticPadTemplate, const gchar* name, GstPad* target)
{
    GstPad* pad;
    GstPadTemplate* padTemplate = gst_static_pad_template_get(staticPadTemplate);

    if (target)
        pad = gst_ghost_pad_new_from_template(name, target, padTemplate);
    else
        pad = gst_ghost_pad_new_no_target_from_template(name, padTemplate);

    gst_object_unref(padTemplate);

    return pad;
}

#if ENABLE(VIDEO)
bool getVideoSizeAndFormatFromCaps(GstCaps* caps, WebCore::IntSize& size, GstVideoFormat& format, int& pixelAspectRatioNumerator, int& pixelAspectRatioDenominator, int& stride)
{
    GstVideoInfo info;

    gst_video_info_init(&info);
    if (!gst_video_info_from_caps(&info, caps))
        return false;

    format = GST_VIDEO_INFO_FORMAT(&info);
    size.setWidth(GST_VIDEO_INFO_WIDTH(&info));
    size.setHeight(GST_VIDEO_INFO_HEIGHT(&info));
    pixelAspectRatioNumerator = GST_VIDEO_INFO_PAR_N(&info);
    pixelAspectRatioDenominator = GST_VIDEO_INFO_PAR_D(&info);
    stride = GST_VIDEO_INFO_PLANE_STRIDE(&info, 0);

    return true;
}

bool getSampleVideoInfo(GstSample* sample, GstVideoInfo& videoInfo)
{
    if (!GST_IS_SAMPLE(sample))
        return false;

    GstCaps* caps = gst_sample_get_caps(sample);
    if (!caps)
        return false;

    gst_video_info_init(&videoInfo);
    if (!gst_video_info_from_caps(&videoInfo, caps))
        return false;

    return true;
}
#endif

GstBuffer* createGstBuffer(GstBuffer* buffer)
{
    gsize bufferSize = gst_buffer_get_size(buffer);
    GstBuffer* newBuffer = gst_buffer_new_and_alloc(bufferSize);

    if (!newBuffer)
        return 0;

    gst_buffer_copy_into(newBuffer, buffer, static_cast<GstBufferCopyFlags>(GST_BUFFER_COPY_METADATA), 0, bufferSize);
    return newBuffer;
}

GstBuffer* createGstBufferForData(const char* data, int length)
{
    GstBuffer* buffer = gst_buffer_new_and_alloc(length);

    gst_buffer_fill(buffer, 0, data, length);

    return buffer;
}

char* getGstBufferDataPointer(GstBuffer* buffer)
{
    GstMiniObject* miniObject = reinterpret_cast<GstMiniObject*>(buffer);
    GstMapInfo* mapInfo = static_cast<GstMapInfo*>(gst_mini_object_get_qdata(miniObject, g_quark_from_static_string(webkitGstMapInfoQuarkString)));
    return reinterpret_cast<char*>(mapInfo->data);
}

void mapGstBuffer(GstBuffer* buffer)
{
    GstMapInfo* mapInfo = static_cast<GstMapInfo*>(fastMalloc(sizeof(GstMapInfo)));
    if (!gst_buffer_map(buffer, mapInfo, GST_MAP_WRITE)) {
        fastFree(mapInfo);
        gst_buffer_unref(buffer);
        return;
    }

    GstMiniObject* miniObject = reinterpret_cast<GstMiniObject*>(buffer);
    gst_mini_object_set_qdata(miniObject, g_quark_from_static_string(webkitGstMapInfoQuarkString), mapInfo, 0);
}

void unmapGstBuffer(GstBuffer* buffer)
{
    GstMiniObject* miniObject = reinterpret_cast<GstMiniObject*>(buffer);
    GstMapInfo* mapInfo = static_cast<GstMapInfo*>(gst_mini_object_steal_qdata(miniObject, g_quark_from_static_string(webkitGstMapInfoQuarkString)));

    if (!mapInfo)
        return;

    gst_buffer_unmap(buffer, mapInfo);
    fastFree(mapInfo);
}

bool initializeGStreamer()
{
    if (gst_is_initialized())
        return true;

    GUniqueOutPtr<GError> error;
    // FIXME: We should probably pass the arguments from the command line.
    bool gstInitialized = gst_init_check(0, 0, &error.outPtr());
    ASSERT_WITH_MESSAGE(gstInitialized, "GStreamer initialization failed: %s", error ? error->message : "unknown error occurred");

#if ENABLE(VIDEO_TRACK) && USE(GSTREAMER_MPEGTS)
    if (gstInitialized)
        gst_mpegts_initialize();
#endif

    return gstInitialized;
}

unsigned getGstPlayFlag(const char* nick)
{
    static GFlagsClass* flagsClass = static_cast<GFlagsClass*>(g_type_class_ref(g_type_from_name("GstPlayFlags")));
    ASSERT(flagsClass);

    GFlagsValue* flag = g_flags_get_value_by_nick(flagsClass, nick);
    if (!flag)
        return 0;

    return flag->value;
}

GstClockTime toGstClockTime(float time)
{
    // Extract the integer part of the time (seconds) and the fractional part (microseconds). Attempt to
    // round the microseconds so no floating point precision is lost and we can perform an accurate seek.
    float seconds;
    float microSeconds = modff(time, &seconds) * 1000000;
    GTimeVal timeValue;
    timeValue.tv_sec = static_cast<glong>(seconds);
    timeValue.tv_usec = static_cast<glong>(roundf(microSeconds / 10000) * 10000);
    return GST_TIMEVAL_TO_TIME(timeValue);
}

#if GST_CHECK_VERSION(1, 5, 3)
GstElement* createGstDecryptor(const gchar* protectionSystem)
{
    GstElement* decryptor = nullptr;
    GList *decryptors = gst_element_factory_list_get_elements(GST_ELEMENT_FACTORY_TYPE_DECRYPTOR,
                                                              GST_RANK_MARGINAL);

    for (GList* walk = decryptors; !decryptor && walk; walk = g_list_next(walk)) {
        GstElementFactory* factory = reinterpret_cast<GstElementFactory*>(walk->data);
        const GList *tmpl = gst_element_factory_get_static_pad_templates(factory);

        for (const GList* current = tmpl; current && !decryptor; current = g_list_next(current)) {
            GstStaticPadTemplate* templ = static_cast<GstStaticPadTemplate*>(current->data);
            GstCaps* caps = gst_static_pad_template_get_caps(templ);
            guint leng = gst_caps_get_size(caps);

            for (guint i = 0; !decryptor && i < leng; ++i) {
                GstStructure* st = gst_caps_get_structure(caps, i);
                if (gst_structure_has_field_typed(st, GST_PROTECTION_SYSTEM_ID_CAPS_FIELD, G_TYPE_STRING)) {
                    const gchar* sys_id = gst_structure_get_string(st, GST_PROTECTION_SYSTEM_ID_CAPS_FIELD);
                    if (!g_ascii_strcasecmp(protectionSystem, sys_id)) {
                        decryptor = gst_element_factory_create(factory, nullptr);
                        break;
                    }
                }
            }
            gst_caps_unref (caps);
        }
    }
    gst_plugin_feature_list_free(decryptors);
    return decryptor;
}
#endif

}

#endif // USE(GSTREAMER)
