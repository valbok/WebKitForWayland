/*
 * Copyright (C) 2008, 2009, 2012, 2013 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SourceProvider_h
#define SourceProvider_h

#include <wtf/RefCounted.h>
#include <wtf/text/TextPosition.h>
#include <wtf/text/WTFString.h>

namespace JSC {

    class SourceProvider : public RefCounted<SourceProvider> {
    public:
        static const intptr_t nullID = 1;
        
        JS_EXPORT_PRIVATE SourceProvider(const String& url, const TextPosition& startPosition);

        JS_EXPORT_PRIVATE virtual ~SourceProvider();

        virtual unsigned hash() const = 0;
        virtual StringView source() const = 0;
        StringView getRange(int start, int end) const
        {
            return source().substring(start, end - start);
        }

        const String& url() const { return m_url; }
        const String& sourceURL() const { return m_sourceURLDirective; }
        const String& sourceMappingURL() const { return m_sourceMappingURLDirective; }

        TextPosition startPosition() const { return m_startPosition; }
        intptr_t asID()
        {
            if (!m_id)
                getID();
            return m_id;
        }

        bool isValid() const { return m_validated; }
        void setValid() { m_validated = true; }

    private:
        template <typename T> friend class Parser;

        void setSourceURLDirective(const String& sourceURL) { m_sourceURLDirective = sourceURL; }
        void setSourceMappingURLDirective(const String& sourceMappingURL) { m_sourceMappingURLDirective = sourceMappingURL; }

        JS_EXPORT_PRIVATE void getID();

        String m_url;
        String m_sourceURLDirective;
        String m_sourceMappingURLDirective;
        TextPosition m_startPosition;
        bool m_validated : 1;
        uintptr_t m_id : sizeof(uintptr_t) * 8 - 1;
    };

    class StringSourceProvider : public SourceProvider {
    public:
        static Ref<StringSourceProvider> create(const String& source, const String& url, const TextPosition& startPosition = TextPosition::minimumPosition())
        {
            return adoptRef(*new StringSourceProvider(source, url, startPosition));
        }
        
        unsigned hash() const override
        {
            return m_source.get().hash();
        }

        virtual StringView source() const override
        {
            return m_source.get();
        }

    private:
        StringSourceProvider(const String& source, const String& url, const TextPosition& startPosition)
            : SourceProvider(url, startPosition)
            , m_source(source.isNull() ? *StringImpl::empty() : *source.impl())
        {
        }

        Ref<StringImpl> m_source;
    };
    
#if ENABLE(WEBASSEMBLY)
    class WebAssemblySourceProvider : public SourceProvider {
    public:
        static Ref<WebAssemblySourceProvider> create(const Vector<uint8_t>& data, const String& url)
        {
            return adoptRef(*new WebAssemblySourceProvider(data, url));
        }

        unsigned hash() const override
        {
            return m_source.impl()->hash();
        }

        virtual StringView source() const override
        {
            return m_source;
        }

        const Vector<uint8_t>& data() const
        {
            return m_data;
        }

    private:
        WebAssemblySourceProvider(const Vector<uint8_t>& data, const String& url)
            : SourceProvider(url, TextPosition::minimumPosition())
            , m_source("[WebAssembly source]")
            , m_data(data)
        {
        }

        String m_source;
        Vector<uint8_t> m_data;
    };
#endif

} // namespace JSC

#endif // SourceProvider_h
