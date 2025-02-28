/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"

#if ENABLE(VIDEO_TRACK)

#include "JSTrackCustom.h"

#include "JSAudioTrack.h"
#include "JSTextTrack.h"
#include "JSVideoTrack.h"

using namespace JSC;

namespace WebCore {

TrackBase* toTrack(JSValue value)
{
    if (!value.isObject())
        return 0;

    JSObject* object = asObject(value);
    if (object->inherits(JSTextTrack::info()))
        return &jsCast<JSTextTrack*>(object)->wrapped();
    if (object->inherits(JSAudioTrack::info()))
        return &jsCast<JSAudioTrack*>(object)->wrapped();
    if (object->inherits(JSVideoTrack::info()))
        return &jsCast<JSVideoTrack*>(object)->wrapped();

    return 0;
}

JSC::JSValue toJS(JSC::ExecState*, JSDOMGlobalObject* globalObject, TrackBase* track)
{
    if (!track)
        return jsNull();
    
    JSObject* wrapper = getCachedWrapper(globalObject->world(), track);
    if (wrapper)
        return wrapper;
    
    switch (track->type()) {
    case TrackBase::BaseTrack:
        // This should never happen.
        ASSERT_NOT_REACHED();
        break;
        
    case TrackBase::AudioTrack:
        return CREATE_DOM_WRAPPER(globalObject, AudioTrack, track);

    case TrackBase::VideoTrack:
        return CREATE_DOM_WRAPPER(globalObject, VideoTrack, track);

    case TrackBase::TextTrack:
        return CREATE_DOM_WRAPPER(globalObject, TextTrack, track);
    }
    
    return jsNull();
}

} // namespace WebCore

#endif
