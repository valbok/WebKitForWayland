/* GStreamer ClearKey common encryption decryptor
 *
 * Copyright (C) 2015 Igalia S.L
 * Copyright (C) 2015 Metrological
 * Copyright (C) 2013 YouView TV Ltd. <alex.ashley@youview.com>
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
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef WebKitCommonEncryptionDecryptorGStreamer_h
#define WebKitCommonEncryptionDecryptorGStreamer_h

#if (ENABLE(ENCRYPTED_MEDIA) || ENABLE(ENCRYPTED_MEDIA_V2)) && USE(GSTREAMER)

#include <gst/gst.h>

G_BEGIN_DECLS

#define WEBKIT_TYPE_MEDIA_CENC_DECRYPT          (webkit_media_common_encryption_decrypt_get_type())
#define WEBKIT_MEDIA_CENC_DECRYPT(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj), WEBKIT_TYPE_MEDIA_CENC_DECRYPT, WebKitMediaCommonEncryptionDecrypt))
#define WEBKIT_MEDIA_CENC_DECRYPT_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST((klass), WEBKIT_TYPE_MEDIA_CENC_DECRYPT, WebKitMediaCommonEncryptionDecryptClass))
#define WEBKIT_IS_MEDIA_CENC_DECRYPT(obj)       (G_TYPE_CHECK_INSTANCE_TYPE((obj), WEBKIT_TYPE_MEDIA_CENC_DECRYPT))
#define WEBKIT_IS_MEDIA_CENC_DECRYPT_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), WEBKIT_TYPE_MEDIA_CENC_DECRYPT))

typedef struct _WebKitMediaCommonEncryptionDecrypt      WebKitMediaCommonEncryptionDecrypt;
typedef struct _WebKitMediaCommonEncryptionDecryptClass WebKitMediaCommonEncryptionDecryptClass;

GType webkit_media_common_encryption_decrypt_get_type(void);

G_END_DECLS

#endif
#endif
