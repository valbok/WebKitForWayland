/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 * Portions Copyright (c) 2010 Motorola Mobility, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WebProcessMainUnix.h"

#include "ChildProcessMain.h"
#include "WebProcess.h"
#include <WebCore/SoupNetworkSession.h>
#include <glib.h>
#include <libsoup/soup.h>

#include <iostream>

using namespace WebCore;

namespace WebKit {

class WebProcessMain final: public ChildProcessMainBase {
public:
    bool platformInitialize() override
    {
        // TODO: Wrap with #ifndef NDEBUG
        if (g_getenv("WEBKIT2_PAUSE_WEB_PROCESS_ON_LAUNCH")) {
            g_printerr("WebProcess PID: %d\n", getpid());
            sleep(30);
        }

        // Despite using system CAs to validate certificates we're
        // accepting invalid certificates by default. New API will be
        // added later to let client accept/discard invalid certificates.
        SoupNetworkSession::defaultSession().setSSLPolicy(SoupNetworkSession::SSLUseSystemCAFile);

        SoupNetworkSession::defaultSession().setupHTTPProxyFromEnvironment();
        return true;
    }

    void platformFinalize() override
    {
        if (SoupCache* soupCache = SoupNetworkSession::defaultSession().cache()) {
            soup_cache_flush(soupCache);
            soup_cache_dump(soupCache);
        }
    }
};

int WebProcessMainUnix(int argc, char** argv)
{
    return ChildProcessMain<WebProcess, WebProcessMain>(argc, argv);
}

} // namespace WebKit
