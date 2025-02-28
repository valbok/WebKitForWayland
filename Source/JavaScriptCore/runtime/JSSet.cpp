/*
 * Copyright (C) 2013 Apple Inc. All rights reserved.
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
#include "JSSet.h"

#include "CopiedBlockInlines.h"
#include "JSCJSValueInlines.h"
#include "JSSetIterator.h"
#include "MapDataInlines.h"
#include "SlotVisitorInlines.h"
#include "StructureInlines.h"

namespace JSC {

const ClassInfo JSSet::s_info = { "Set", &Base::s_info, 0, CREATE_METHOD_TABLE(JSSet) };

void JSSet::destroy(JSCell* cell)
{
    JSSet* thisObject = jsCast<JSSet*>(cell);
    thisObject->JSSet::~JSSet();
}

void JSSet::visitChildren(JSCell* cell, SlotVisitor& visitor)
{
    Base::visitChildren(cell, visitor);
    jsCast<JSSet*>(cell)->m_setData.visitChildren(cell, visitor);
}

void JSSet::copyBackingStore(JSCell* cell, CopyVisitor& visitor, CopyToken token)
{
    Base::copyBackingStore(cell, visitor, token);
    jsCast<JSSet*>(cell)->m_setData.copyBackingStore(visitor, token);
}

bool JSSet::has(ExecState* exec, JSValue value)
{
    return m_setData.contains(exec, value);
}

size_t JSSet::size(ExecState* exec)
{
    return m_setData.size(exec);
}

void JSSet::add(ExecState* exec, JSValue value)
{
    m_setData.set(exec, this, value, value);
}

void JSSet::clear(ExecState*)
{
    m_setData.clear();
}

bool JSSet::remove(ExecState* exec, JSValue value)
{
    return m_setData.remove(exec, value);
}

}
