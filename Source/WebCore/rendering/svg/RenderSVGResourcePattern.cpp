/*
 * Copyright (C) 2006 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
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
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "RenderSVGResourcePattern.h"

#include "ElementIterator.h"
#include "FrameView.h"
#include "GraphicsContext.h"
#include "RenderSVGRoot.h"
#include "SVGFitToViewBox.h"
#include "SVGRenderingContext.h"
#include "SVGResources.h"
#include "SVGResourcesCache.h"

namespace WebCore {

RenderSVGResourcePattern::RenderSVGResourcePattern(SVGPatternElement& element, Ref<RenderStyle>&& style)
    : RenderSVGResourceContainer(element, WTFMove(style))
    , m_shouldCollectPatternAttributes(true)
{
}

SVGPatternElement& RenderSVGResourcePattern::patternElement() const
{
    return downcast<SVGPatternElement>(RenderSVGResourceContainer::element());
}

void RenderSVGResourcePattern::removeAllClientsFromCache(bool markForInvalidation)
{
    m_patternMap.clear();
    m_shouldCollectPatternAttributes = true;
    markAllClientsForInvalidation(markForInvalidation ? RepaintInvalidation : ParentOnlyInvalidation);
}

void RenderSVGResourcePattern::removeClientFromCache(RenderElement& client, bool markForInvalidation)
{
    m_patternMap.remove(&client);
    markClientForInvalidation(client, markForInvalidation ? RepaintInvalidation : ParentOnlyInvalidation);
}

void RenderSVGResourcePattern::collectPatternAttributes(PatternAttributes& attributes) const
{
    const RenderSVGResourcePattern* current = this;

    while (current) {
        const SVGPatternElement& pattern = current->patternElement();
        pattern.collectPatternAttributes(attributes);

        auto* resources = SVGResourcesCache::cachedResourcesForRenderer(*current);
        current = resources ? downcast<RenderSVGResourcePattern>(resources->linkedResource()) : nullptr;
    }
}

PatternData* RenderSVGResourcePattern::buildPattern(RenderElement& renderer, unsigned short resourceMode, GraphicsContext& context)
{
    PatternData* currentData = m_patternMap.get(&renderer);
    if (currentData && currentData->pattern)
        return currentData;

    if (m_shouldCollectPatternAttributes) {
        patternElement().synchronizeAnimatedSVGAttribute(anyQName());

        m_attributes = PatternAttributes();
        collectPatternAttributes(m_attributes);
        m_shouldCollectPatternAttributes = false;
    }

    // If we couldn't determine the pattern content element root, stop here.
    if (!m_attributes.patternContentElement())
        return nullptr;

    // An empty viewBox disables rendering.
    if (m_attributes.hasViewBox() && m_attributes.viewBox().isEmpty())
        return nullptr;

    // Compute all necessary transformations to build the tile image & the pattern.
    FloatRect tileBoundaries;
    AffineTransform tileImageTransform;
    if (!buildTileImageTransform(renderer, m_attributes, patternElement(), tileBoundaries, tileImageTransform))
        return nullptr;

    AffineTransform absoluteTransformIgnoringRotation = SVGRenderingContext::calculateTransformationToOutermostCoordinateSystem(renderer);

    // Ignore 2D rotation, as it doesn't affect the size of the tile.
    SVGRenderingContext::clear2DRotation(absoluteTransformIgnoringRotation);
    FloatRect absoluteTileBoundaries = absoluteTransformIgnoringRotation.mapRect(tileBoundaries);
    FloatRect clampedAbsoluteTileBoundaries;

    // Scale the tile size to match the scale level of the patternTransform.
    absoluteTileBoundaries.scale(static_cast<float>(m_attributes.patternTransform().xScale()),
        static_cast<float>(m_attributes.patternTransform().yScale()));

    // Build tile image.
    auto tileImage = createTileImage(m_attributes, tileBoundaries, absoluteTileBoundaries, tileImageTransform, clampedAbsoluteTileBoundaries, context.renderingMode());
    if (!tileImage)
        return nullptr;

    const IntSize tileImageSize = tileImage->logicalSize();

    RefPtr<Image> copiedImage = ImageBuffer::sinkIntoImage(WTFMove(tileImage));
    if (!copiedImage)
        return nullptr;

    // Build pattern.
    auto patternData = std::make_unique<PatternData>();
    patternData->pattern = Pattern::create(copiedImage, true, true);

    // Compute pattern space transformation.

    patternData->transform.translate(tileBoundaries.x(), tileBoundaries.y());
    patternData->transform.scale(tileBoundaries.width() / tileImageSize.width(), tileBoundaries.height() / tileImageSize.height());

    AffineTransform patternTransform = m_attributes.patternTransform();
    if (!patternTransform.isIdentity())
        patternData->transform = patternTransform * patternData->transform;

    // Account for text drawing resetting the context to non-scaled, see SVGInlineTextBox::paintTextWithShadows.
    if (resourceMode & ApplyToTextMode) {
        AffineTransform additionalTextTransformation;
        if (shouldTransformOnTextPainting(renderer, additionalTextTransformation))
            patternData->transform *= additionalTextTransformation;
    }
    patternData->pattern->setPatternSpaceTransform(patternData->transform);

    // Various calls above may trigger invalidations in some fringe cases (ImageBuffer allocation
    // failures in the SVG image cache for example). To avoid having our PatternData deleted by
    // removeAllClientsFromCache(), we only make it visible in the cache at the very end.
    return m_patternMap.set(&renderer, WTFMove(patternData)).iterator->value.get();
}

bool RenderSVGResourcePattern::applyResource(RenderElement& renderer, const RenderStyle& style, GraphicsContext*& context, unsigned short resourceMode)
{
    ASSERT(context);
    ASSERT(resourceMode != ApplyToDefaultMode);

    // Spec: When the geometry of the applicable element has no width or height and objectBoundingBox is specified,
    // then the given effect (e.g. a gradient or a filter) will be ignored.
    FloatRect objectBoundingBox = renderer.objectBoundingBox();
    if (m_attributes.patternUnits() == SVGUnitTypes::SVG_UNIT_TYPE_OBJECTBOUNDINGBOX && objectBoundingBox.isEmpty())
        return false;

    PatternData* patternData = buildPattern(renderer, resourceMode, *context);
    if (!patternData)
        return false;

    // Draw pattern
    context->save();

    const SVGRenderStyle& svgStyle = style.svgStyle();

    if (resourceMode & ApplyToFillMode) {
        context->setAlpha(svgStyle.fillOpacity());
        context->setFillPattern(*patternData->pattern);
        context->setFillRule(svgStyle.fillRule());
    } else if (resourceMode & ApplyToStrokeMode) {
        if (svgStyle.vectorEffect() == VE_NON_SCALING_STROKE)
            patternData->pattern->setPatternSpaceTransform(transformOnNonScalingStroke(&renderer, patternData->transform));
        context->setAlpha(svgStyle.strokeOpacity());
        context->setStrokePattern(*patternData->pattern);
        SVGRenderSupport::applyStrokeStyleToContext(context, style, renderer);
    }

    if (resourceMode & ApplyToTextMode) {
        if (resourceMode & ApplyToFillMode) {
            context->setTextDrawingMode(TextModeFill);

#if USE(CG)
            context->applyFillPattern();
#endif
        } else if (resourceMode & ApplyToStrokeMode) {
            context->setTextDrawingMode(TextModeStroke);

#if USE(CG)
            context->applyStrokePattern();
#endif
        }
    }

    return true;
}

void RenderSVGResourcePattern::postApplyResource(RenderElement&, GraphicsContext*& context, unsigned short resourceMode, const Path* path, const RenderSVGShape* shape)
{
    ASSERT(context);
    ASSERT(resourceMode != ApplyToDefaultMode);

    if (resourceMode & ApplyToFillMode) {
        if (path)
            context->fillPath(*path);
        else if (shape)
            shape->fillShape(*context);
    }
    if (resourceMode & ApplyToStrokeMode) {
        if (path)
            context->strokePath(*path);
        else if (shape)
            shape->strokeShape(*context);
    }

    context->restore();
}

static inline FloatRect calculatePatternBoundaries(const PatternAttributes& attributes,
                                                   const FloatRect& objectBoundingBox,
                                                   const SVGPatternElement& patternElement)
{
    return SVGLengthContext::resolveRectangle(&patternElement, attributes.patternUnits(), objectBoundingBox, attributes.x(), attributes.y(), attributes.width(), attributes.height());
}

bool RenderSVGResourcePattern::buildTileImageTransform(RenderElement& renderer,
                                                       const PatternAttributes& attributes,
                                                       const SVGPatternElement& patternElement,
                                                       FloatRect& patternBoundaries,
                                                       AffineTransform& tileImageTransform) const
{
    FloatRect objectBoundingBox = renderer.objectBoundingBox();
    patternBoundaries = calculatePatternBoundaries(attributes, objectBoundingBox, patternElement); 
    if (patternBoundaries.width() <= 0 || patternBoundaries.height() <= 0)
        return false;

    AffineTransform viewBoxCTM = SVGFitToViewBox::viewBoxToViewTransform(attributes.viewBox(), attributes.preserveAspectRatio(), patternBoundaries.width(), patternBoundaries.height());

    // Apply viewBox/objectBoundingBox transformations.
    if (!viewBoxCTM.isIdentity())
        tileImageTransform = viewBoxCTM;
    else if (attributes.patternContentUnits() == SVGUnitTypes::SVG_UNIT_TYPE_OBJECTBOUNDINGBOX)
        tileImageTransform.scale(objectBoundingBox.width(), objectBoundingBox.height());

    return true;
}

std::unique_ptr<ImageBuffer> RenderSVGResourcePattern::createTileImage(const PatternAttributes& attributes, const FloatRect& tileBoundaries, const FloatRect& absoluteTileBoundaries, const AffineTransform& tileImageTransform, FloatRect& clampedAbsoluteTileBoundaries, RenderingMode renderingMode) const
{
    clampedAbsoluteTileBoundaries = ImageBuffer::clampedRect(absoluteTileBoundaries);
    auto tileImage = SVGRenderingContext::createImageBuffer(absoluteTileBoundaries, clampedAbsoluteTileBoundaries, ColorSpaceSRGB, renderingMode);
    if (!tileImage)
        return nullptr;

    GraphicsContext& tileImageContext = tileImage->context();

    // The image buffer represents the final rendered size, so the content has to be scaled (to avoid pixelation).
    tileImageContext.scale(FloatSize(clampedAbsoluteTileBoundaries.width() / tileBoundaries.width(),
                                      clampedAbsoluteTileBoundaries.height() / tileBoundaries.height()));

    // Apply tile image transformations.
    if (!tileImageTransform.isIdentity())
        tileImageContext.concatCTM(tileImageTransform);

    AffineTransform contentTransformation;
    if (attributes.patternContentUnits() == SVGUnitTypes::SVG_UNIT_TYPE_OBJECTBOUNDINGBOX)
        contentTransformation = tileImageTransform;

    // Draw the content into the ImageBuffer.
    for (auto& child : childrenOfType<SVGElement>(*attributes.patternContentElement())) {
        if (!child.renderer())
            continue;
        if (child.renderer()->needsLayout())
            return nullptr;
        SVGRenderingContext::renderSubtreeToImageBuffer(tileImage.get(), *child.renderer(), contentTransformation);
    }

    return tileImage;
}

}
