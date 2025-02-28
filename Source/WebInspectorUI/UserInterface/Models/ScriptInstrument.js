/*
 * Copyright (C) 2015 Apple Inc. All rights reserved.
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

WebInspector.ScriptInstrument = class ScriptInstrument extends WebInspector.Instrument
{
    // Protected

    get timelineRecordType()
    {
        return WebInspector.TimelineRecord.Type.Script;
    }

    startInstrumentation()
    {
        // COMPATIBILITY (iOS 9): Legacy backends did not have ScriptProfilerAgent. They use TimelineAgent.
        // FIXME: ScriptProfilerAgent is only enabled for JavaScript debuggables until we transition TimelineAgent for Web debuggables.
        if (!window.ScriptProfilerAgent || WebInspector.debuggableType !== WebInspector.DebuggableType.JavaScript) {
            super.startInstrumentation();
            return;
        }

        // FIXME: Make this some UI visible option.
        const includeProfiles = true;

        ScriptProfilerAgent.startTracking(includeProfiles);
    }

    stopInstrumentation()
    {
        // COMPATIBILITY (iOS 9): Legacy backends did not have ScriptProfilerAgent. They use TimelineAgent.
        // FIXME: ScriptProfilerAgent is only enabled for JavaScript debuggables until we transition TimelineAgent for Web debuggables.
        if (!window.ScriptProfilerAgent || WebInspector.debuggableType !== WebInspector.DebuggableType.JavaScript) {
            super.stopInstrumentation();
            return;
        }

        ScriptProfilerAgent.stopTracking();
    }

};
