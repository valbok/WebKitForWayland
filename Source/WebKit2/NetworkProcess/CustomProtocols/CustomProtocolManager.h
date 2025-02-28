/*
 * Copyright (C) 2012, 2013 Apple Inc. All rights reserved.
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

#ifndef CustomProtocolManager_h
#define CustomProtocolManager_h

#include "Connection.h"
#include "NetworkProcessSupplement.h"
#include <wtf/WorkQueue.h>
#include <wtf/text/WTFString.h>

#if PLATFORM(COCOA)
#include <wtf/HashMap.h>
#include <wtf/HashSet.h>
#include <wtf/RetainPtr.h>
#include <wtf/Threading.h>
OBJC_CLASS WKCustomProtocol;
#else
#include "CustomProtocolManagerImpl.h"
#endif


namespace IPC {
class DataReference;
} // namespace IPC

namespace WebCore {
class ResourceError;
class ResourceRequest;
class ResourceResponse;
} // namespace WebCore

namespace WebKit {

class ChildProcess;
struct NetworkProcessCreationParameters;

class CustomProtocolManager : public NetworkProcessSupplement, public IPC::Connection::WorkQueueMessageReceiver {
    WTF_MAKE_NONCOPYABLE(CustomProtocolManager);
public:
    explicit CustomProtocolManager(ChildProcess*);

    static const char* supplementName();

    ChildProcess* childProcess() const { return m_childProcess; }

    void registerScheme(const String&);
    void unregisterScheme(const String&);
    bool supportsScheme(const String&);
    
#if PLATFORM(COCOA)
    void addCustomProtocol(WKCustomProtocol *);
    void removeCustomProtocol(WKCustomProtocol *);
#endif

private:
    // ChildProcessSupplement
    void initializeConnection(IPC::Connection*) override;

    // NetworkProcessSupplement
    void initialize(const NetworkProcessCreationParameters&) override;

    // IPC::MessageReceiver
    virtual void didReceiveMessage(IPC::Connection&, IPC::MessageDecoder&) override;

    void didFailWithError(uint64_t customProtocolID, const WebCore::ResourceError&);
    void didLoadData(uint64_t customProtocolID, const IPC::DataReference&);
    void didReceiveResponse(uint64_t customProtocolID, const WebCore::ResourceResponse&, uint32_t cacheStoragePolicy);
    void didFinishLoading(uint64_t customProtocolID);
    void wasRedirectedToRequest(uint64_t customProtocolID, const WebCore::ResourceRequest&, const WebCore::ResourceResponse& redirectResponse);

    ChildProcess* m_childProcess;
    RefPtr<WorkQueue> m_messageQueue;

#if PLATFORM(COCOA)
    HashSet<String> m_registeredSchemes;
    Lock m_registeredSchemesMutex;

    typedef HashMap<uint64_t, RetainPtr<WKCustomProtocol>> CustomProtocolMap;
    CustomProtocolMap m_customProtocolMap;
    Lock m_customProtocolMapMutex;
    
    // WKCustomProtocol objects can be removed from the m_customProtocolMap from multiple threads.
    // We return a RetainPtr here because it is unsafe to return a raw pointer since the object might immediately be destroyed from a different thread.
    RetainPtr<WKCustomProtocol> protocolForID(uint64_t customProtocolID);
#else
    // FIXME: Move mac specific code to CustomProtocolManagerImpl.
    std::unique_ptr<CustomProtocolManagerImpl> m_impl;
#endif
};

} // namespace WebKit

#endif // CustomProtocolManager_h
