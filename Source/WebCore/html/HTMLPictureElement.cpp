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
#include "HTMLPictureElement.h"

#include "ElementChildIterator.h"
#include "HTMLImageElement.h"
#include "HTMLNames.h"

namespace WebCore {

HTMLPictureElement::HTMLPictureElement(const QualifiedName& tagName, Document& document)
    : HTMLElement(tagName, document)
{
}

HTMLPictureElement::~HTMLPictureElement()
{
    if (hasViewportDependentResults())
        document().removeViewportDependentPicture(*this);
}

void HTMLPictureElement::didMoveToNewDocument(Document* oldDocument)
{
    if (hasViewportDependentResults() && oldDocument)
        oldDocument->removeViewportDependentPicture(*this);
    HTMLElement::didMoveToNewDocument(oldDocument);
    sourcesChanged();
}

Ref<HTMLPictureElement> HTMLPictureElement::create(const QualifiedName& tagName, Document& document)
{
    return adoptRef(*new HTMLPictureElement(tagName, document));
}

void HTMLPictureElement::sourcesChanged()
{
    for (auto& imageElement : childrenOfType<HTMLImageElement>(*this))
        imageElement.selectImageSource();
}

bool HTMLPictureElement::viewportChangeAffectedPicture()
{
    MediaQueryEvaluator evaluator(document().printing() ? "print" : "screen", document().frame(), computedStyle());
    unsigned numResults = m_viewportDependentMediaQueryResults.size();
    for (unsigned i = 0; i < numResults; i++) {
        if (evaluator.eval(&m_viewportDependentMediaQueryResults[i]->m_expression) != m_viewportDependentMediaQueryResults[i]->m_result)
            return true;
    }
    return false;
}

}

