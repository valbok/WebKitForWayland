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

#ifndef NativeWebWheelEvent_h
#define NativeWebWheelEvent_h

#include "WebEvent.h"

#if USE(APPKIT)
#include <wtf/RetainPtr.h>
OBJC_CLASS NSView;
#endif

#if PLATFORM(EFL)
#include <Evas.h>
#include <WebCore/AffineTransform.h>
#endif

#if PLATFORM(GTK)
#include <WebCore/GUniquePtrGtk.h>
typedef union _GdkEvent GdkEvent;
#endif

#if PLATFORM(WPE)
#include <WPE/Input/Events.h>
#endif

namespace WebKit {

class NativeWebWheelEvent : public WebWheelEvent {
public:
#if USE(APPKIT)
    NativeWebWheelEvent(NSEvent *, NSView *);
#elif PLATFORM(GTK)
    NativeWebWheelEvent(const NativeWebWheelEvent&);
    NativeWebWheelEvent(GdkEvent*);
#elif PLATFORM(EFL)
    NativeWebWheelEvent(const Evas_Event_Mouse_Wheel*, const WebCore::AffineTransform& toWebContent, const WebCore::AffineTransform& toDeviceScreen);
#elif PLATFORM(WPE)
    NativeWebWheelEvent(WPE::Input::AxisEvent&&);
#endif

#if USE(APPKIT)
    NSEvent* nativeEvent() const { return m_nativeEvent.get(); }
#elif PLATFORM(GTK)
    const GdkEvent* nativeEvent() const { return m_nativeEvent.get(); }
#elif PLATFORM(EFL)
    const Evas_Event_Mouse_Wheel* nativeEvent() const { return m_nativeEvent; }
#elif PLATFORM(IOS)
    const void* nativeEvent() const { return 0; }
#elif PLATFORM(WPE)
    const void* nativeEvent() const { return nullptr; }
#endif

private:
#if USE(APPKIT)
    RetainPtr<NSEvent> m_nativeEvent;
#elif PLATFORM(GTK)
    GUniquePtr<GdkEvent> m_nativeEvent;
#elif PLATFORM(EFL)
    const Evas_Event_Mouse_Wheel* m_nativeEvent;
#endif
};

} // namespace WebKit

#endif // NativeWebWheelEvent_h
