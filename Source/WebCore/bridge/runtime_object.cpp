/*
 * Copyright (C) 2003, 2008, 2009 Apple Inc. All rights reserved.
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
#include "runtime_object.h"

#include "JSDOMBinding.h"
#include "runtime_method.h"
#include <runtime/Error.h>

using namespace WebCore;

namespace JSC {
namespace Bindings {

WEBCORE_EXPORT const ClassInfo RuntimeObject::s_info = { "RuntimeObject", &Base::s_info, 0, CREATE_METHOD_TABLE(RuntimeObject) };

RuntimeObject::RuntimeObject(VM& vm, Structure* structure, RefPtr<Instance>&& instance)
    : JSDestructibleObject(vm, structure)
    , m_instance(WTFMove(instance))
{
}

void RuntimeObject::finishCreation(VM& vm)
{
    Base::finishCreation(vm);
    ASSERT(inherits(info()));
}

void RuntimeObject::destroy(JSCell* cell)
{
    static_cast<RuntimeObject*>(cell)->RuntimeObject::~RuntimeObject();
}

void RuntimeObject::invalidate()
{
    ASSERT(m_instance);
    if (m_instance)
        m_instance->willInvalidateRuntimeObject();
    m_instance = nullptr;
}

EncodedJSValue RuntimeObject::fallbackObjectGetter(ExecState* exec, JSObject* slotBase, EncodedJSValue, PropertyName propertyName)
{
    RuntimeObject* thisObj = jsCast<RuntimeObject*>(slotBase);
    RefPtr<Instance> instance = thisObj->m_instance;

    if (!instance)
        return JSValue::encode(throwInvalidAccessError(exec));
    
    instance->begin();

    Class *aClass = instance->getClass();
    JSValue result = aClass->fallbackObject(exec, instance.get(), propertyName);

    instance->end();
            
    return JSValue::encode(result);
}

EncodedJSValue RuntimeObject::fieldGetter(ExecState* exec, JSObject* slotBase, EncodedJSValue, PropertyName propertyName)
{    
    RuntimeObject* thisObj = jsCast<RuntimeObject*>(slotBase);
    RefPtr<Instance> instance = thisObj->m_instance;

    if (!instance)
        return JSValue::encode(throwInvalidAccessError(exec));
    
    instance->begin();

    Class *aClass = instance->getClass();
    Field* aField = aClass->fieldNamed(propertyName, instance.get());
    JSValue result = aField->valueFromInstance(exec, instance.get());
    
    instance->end();
            
    return JSValue::encode(result);
}

EncodedJSValue RuntimeObject::methodGetter(ExecState* exec, JSObject* slotBase, EncodedJSValue, PropertyName propertyName)
{
    RuntimeObject* thisObj = jsCast<RuntimeObject*>(slotBase);
    RefPtr<Instance> instance = thisObj->m_instance;

    if (!instance)
        return JSValue::encode(throwInvalidAccessError(exec));
    
    instance->begin();

    JSValue method = instance->getMethod(exec, propertyName);

    instance->end();
            
    return JSValue::encode(method);
}

bool RuntimeObject::getOwnPropertySlot(JSObject* object, ExecState *exec, PropertyName propertyName, PropertySlot& slot)
{
    RuntimeObject* thisObject = jsCast<RuntimeObject*>(object);
    if (!thisObject->m_instance) {
        throwInvalidAccessError(exec);
        return false;
    }
    
    RefPtr<Instance> instance = thisObject->m_instance;

    instance->begin();
    
    Class *aClass = instance->getClass();
    
    if (aClass) {
        // See if the instance has a field with the specified name.
        Field *aField = aClass->fieldNamed(propertyName, instance.get());
        if (aField) {
            slot.setCustom(thisObject, DontDelete, thisObject->fieldGetter);
            instance->end();
            return true;
        } else {
            // Now check if a method with specified name exists, if so return a function object for
            // that method.
            if (aClass->methodNamed(propertyName, instance.get())) {
                slot.setCustom(thisObject, DontDelete | ReadOnly, thisObject->methodGetter);
                
                instance->end();
                return true;
            }
        }

        // Try a fallback object.
        if (!aClass->fallbackObject(exec, instance.get(), propertyName).isUndefined()) {
            slot.setCustom(thisObject, DontDelete | ReadOnly | DontEnum, thisObject->fallbackObjectGetter);
            instance->end();
            return true;
        }
    }
        
    instance->end();
    
    return instance->getOwnPropertySlot(thisObject, exec, propertyName, slot);
}

void RuntimeObject::put(JSCell* cell, ExecState* exec, PropertyName propertyName, JSValue value, PutPropertySlot& slot)
{
    RuntimeObject* thisObject = jsCast<RuntimeObject*>(cell);
    if (!thisObject->m_instance) {
        throwInvalidAccessError(exec);
        return;
    }
    
    RefPtr<Instance> instance = thisObject->m_instance;
    instance->begin();

    // Set the value of the property.
    Field *aField = instance->getClass()->fieldNamed(propertyName, instance.get());
    if (aField)
        aField->setValueToInstance(exec, instance.get(), value);
    else if (!instance->setValueOfUndefinedField(exec, propertyName, value))
        instance->put(thisObject, exec, propertyName, value, slot);

    instance->end();
}

bool RuntimeObject::deleteProperty(JSCell*, ExecState*, PropertyName)
{
    // Can never remove a property of a RuntimeObject.
    return false;
}

JSValue RuntimeObject::defaultValue(const JSObject* object, ExecState* exec, PreferredPrimitiveType hint)
{
    const RuntimeObject* thisObject = jsCast<const RuntimeObject*>(object);
    if (!thisObject->m_instance)
        return throwInvalidAccessError(exec);
    
    RefPtr<Instance> instance = thisObject->m_instance;

    instance->begin();
    JSValue result = instance->defaultValue(exec, hint);
    instance->end();
    return result;
}

static EncodedJSValue JSC_HOST_CALL callRuntimeObject(ExecState* exec)
{
    ASSERT(exec->callee()->inherits(RuntimeObject::info()));
    RefPtr<Instance> instance(static_cast<RuntimeObject*>(exec->callee())->getInternalInstance());
    instance->begin();
    JSValue result = instance->invokeDefaultMethod(exec);
    instance->end();
    return JSValue::encode(result);
}

CallType RuntimeObject::getCallData(JSCell* cell, CallData& callData)
{
    RuntimeObject* thisObject = jsCast<RuntimeObject*>(cell);
    if (!thisObject->m_instance)
        return CallTypeNone;
    
    RefPtr<Instance> instance = thisObject->m_instance;
    if (!instance->supportsInvokeDefaultMethod())
        return CallTypeNone;
    
    callData.native.function = callRuntimeObject;
    return CallTypeHost;
}

static EncodedJSValue JSC_HOST_CALL callRuntimeConstructor(ExecState* exec)
{
    JSObject* constructor = exec->callee();
    ASSERT(constructor->inherits(RuntimeObject::info()));
    RefPtr<Instance> instance(static_cast<RuntimeObject*>(exec->callee())->getInternalInstance());
    instance->begin();
    ArgList args(exec);
    JSValue result = instance->invokeConstruct(exec, args);
    instance->end();
    
    ASSERT(result);
    return JSValue::encode(result.isObject() ? jsCast<JSObject*>(result.asCell()) : constructor);
}

ConstructType RuntimeObject::getConstructData(JSCell* cell, ConstructData& constructData)
{
    RuntimeObject* thisObject = jsCast<RuntimeObject*>(cell);
    if (!thisObject->m_instance)
        return ConstructTypeNone;
    
    RefPtr<Instance> instance = thisObject->m_instance;
    if (!instance->supportsConstruct())
        return ConstructTypeNone;
    
    constructData.native.function = callRuntimeConstructor;
    return ConstructTypeHost;
}

void RuntimeObject::getOwnPropertyNames(JSObject* object, ExecState* exec, PropertyNameArray& propertyNames, EnumerationMode)
{
    RuntimeObject* thisObject = jsCast<RuntimeObject*>(object);
    if (!thisObject->m_instance) {
        throwInvalidAccessError(exec);
        return;
    }

    RefPtr<Instance> instance = thisObject->m_instance;
    
    instance->begin();
    instance->getPropertyNames(exec, propertyNames);
    instance->end();
}

JSObject* RuntimeObject::throwInvalidAccessError(ExecState* exec)
{
    return exec->vm().throwException(exec, createReferenceError(exec, "Trying to access object from destroyed plug-in."));
}

}
}
