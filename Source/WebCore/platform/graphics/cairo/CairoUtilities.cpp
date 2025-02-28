/*
 * Copyright (C) 2010 Igalia S.L.
 * Copyright (C) 2011 ProFUSION embedded systems
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
#include "CairoUtilities.h"

#if USE(CAIRO)

#include "AffineTransform.h"
#include "Color.h"
#include "FloatPoint.h"
#include "FloatRect.h"
#include "IntRect.h"
#include "Path.h"
#include "PlatformPathCairo.h"
#include "RefPtrCairo.h"
#include <wtf/Assertions.h>
#include <wtf/Vector.h>

#if ENABLE(ACCELERATED_2D_CANVAS)
#if PLATFORM(WAYLAND)
#include <wayland-egl.h>
#endif
#include <cairo-gl.h>
#endif

namespace WebCore {

void copyContextProperties(cairo_t* srcCr, cairo_t* dstCr)
{
    cairo_set_antialias(dstCr, cairo_get_antialias(srcCr));

    size_t dashCount = cairo_get_dash_count(srcCr);
    Vector<double> dashes(dashCount);

    double offset;
    cairo_get_dash(srcCr, dashes.data(), &offset);
    cairo_set_dash(dstCr, dashes.data(), dashCount, offset);
    cairo_set_line_cap(dstCr, cairo_get_line_cap(srcCr));
    cairo_set_line_join(dstCr, cairo_get_line_join(srcCr));
    cairo_set_line_width(dstCr, cairo_get_line_width(srcCr));
    cairo_set_miter_limit(dstCr, cairo_get_miter_limit(srcCr));
    cairo_set_fill_rule(dstCr, cairo_get_fill_rule(srcCr));
}

void setSourceRGBAFromColor(cairo_t* context, const Color& color)
{
    float red, green, blue, alpha;
    color.getRGBA(red, green, blue, alpha);
    cairo_set_source_rgba(context, red, green, blue, alpha);
}

void appendPathToCairoContext(cairo_t* to, cairo_t* from)
{
    auto cairoPath = cairo_copy_path(from);
    cairo_append_path(to, cairoPath);
    cairo_path_destroy(cairoPath);
}

void setPathOnCairoContext(cairo_t* to, cairo_t* from)
{
    cairo_new_path(to);
    appendPathToCairoContext(to, from);
}

void appendWebCorePathToCairoContext(cairo_t* context, const Path& path)
{
    if (path.isEmpty())
        return;
    appendPathToCairoContext(context, path.platformPath()->context());
}

void appendRegionToCairoContext(cairo_t* to, const cairo_region_t* region)
{
    if (!region)
        return;

    const int rectCount = cairo_region_num_rectangles(region);
    for (int i = 0; i < rectCount; ++i) {
        cairo_rectangle_int_t rect;
        cairo_region_get_rectangle(region, i, &rect);
        cairo_rectangle(to, rect.x, rect.y, rect.width, rect.height);
    }
}

cairo_operator_t toCairoOperator(CompositeOperator op)
{
    switch (op) {
    case CompositeClear:
        return CAIRO_OPERATOR_CLEAR;
    case CompositeCopy:
        return CAIRO_OPERATOR_SOURCE;
    case CompositeSourceOver:
        return CAIRO_OPERATOR_OVER;
    case CompositeSourceIn:
        return CAIRO_OPERATOR_IN;
    case CompositeSourceOut:
        return CAIRO_OPERATOR_OUT;
    case CompositeSourceAtop:
        return CAIRO_OPERATOR_ATOP;
    case CompositeDestinationOver:
        return CAIRO_OPERATOR_DEST_OVER;
    case CompositeDestinationIn:
        return CAIRO_OPERATOR_DEST_IN;
    case CompositeDestinationOut:
        return CAIRO_OPERATOR_DEST_OUT;
    case CompositeDestinationAtop:
        return CAIRO_OPERATOR_DEST_ATOP;
    case CompositeXOR:
        return CAIRO_OPERATOR_XOR;
    case CompositePlusDarker:
        return CAIRO_OPERATOR_DARKEN;
    case CompositePlusLighter:
        return CAIRO_OPERATOR_ADD;
    case CompositeDifference:
        return CAIRO_OPERATOR_DIFFERENCE;
    default:
        return CAIRO_OPERATOR_SOURCE;
    }
}
cairo_operator_t toCairoOperator(BlendMode blendOp)
{
    switch (blendOp) {
    case BlendModeNormal:
        return CAIRO_OPERATOR_OVER;
    case BlendModeMultiply:
        return CAIRO_OPERATOR_MULTIPLY;
    case BlendModeScreen:
        return CAIRO_OPERATOR_SCREEN;
    case BlendModeOverlay:
        return CAIRO_OPERATOR_OVERLAY;
    case BlendModeDarken:
        return CAIRO_OPERATOR_DARKEN;
    case BlendModeLighten:
        return CAIRO_OPERATOR_LIGHTEN;
    case BlendModeColorDodge:
        return CAIRO_OPERATOR_COLOR_DODGE;
    case BlendModeColorBurn:
        return CAIRO_OPERATOR_COLOR_BURN;
    case BlendModeHardLight:
        return CAIRO_OPERATOR_HARD_LIGHT;
    case BlendModeSoftLight:
        return CAIRO_OPERATOR_SOFT_LIGHT;
    case BlendModeDifference:
        return CAIRO_OPERATOR_DIFFERENCE;
    case BlendModeExclusion:
        return CAIRO_OPERATOR_EXCLUSION;
    case BlendModeHue:
        return CAIRO_OPERATOR_HSL_HUE;
    case BlendModeSaturation:
        return CAIRO_OPERATOR_HSL_SATURATION;
    case BlendModeColor:
        return CAIRO_OPERATOR_HSL_COLOR;
    case BlendModeLuminosity:
        return CAIRO_OPERATOR_HSL_LUMINOSITY;
    default:
        return CAIRO_OPERATOR_OVER;
    }
}

void drawPatternToCairoContext(cairo_t* cr, cairo_surface_t* image, const IntSize& imageSize, const FloatRect& tileRect,
                               const AffineTransform& patternTransform, const FloatPoint& phase, cairo_operator_t op, const FloatRect& destRect)
{
    // Avoid NaN
    if (!std::isfinite(phase.x()) || !std::isfinite(phase.y()))
       return;

    cairo_save(cr);

    RefPtr<cairo_surface_t> clippedImageSurface = 0;
    if (tileRect.size() != imageSize) {
        IntRect imageRect = enclosingIntRect(tileRect);
        clippedImageSurface = adoptRef(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, imageRect.width(), imageRect.height()));
        RefPtr<cairo_t> clippedImageContext = adoptRef(cairo_create(clippedImageSurface.get()));
        cairo_set_source_surface(clippedImageContext.get(), image, -tileRect.x(), -tileRect.y());
        cairo_paint(clippedImageContext.get());
        image = clippedImageSurface.get();
    }

    cairo_pattern_t* pattern = cairo_pattern_create_for_surface(image);
    cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);

    cairo_matrix_t patternMatrix = cairo_matrix_t(patternTransform);
    cairo_matrix_t phaseMatrix = {1, 0, 0, 1, phase.x() + tileRect.x() * patternTransform.a(), phase.y() + tileRect.y() * patternTransform.d()};
    cairo_matrix_t combined;
    cairo_matrix_multiply(&combined, &patternMatrix, &phaseMatrix);
    cairo_matrix_invert(&combined);
    cairo_pattern_set_matrix(pattern, &combined);

    cairo_set_operator(cr, op);
    cairo_set_source(cr, pattern);
    cairo_pattern_destroy(pattern);
    cairo_rectangle(cr, destRect.x(), destRect.y(), destRect.width(), destRect.height());
    cairo_fill(cr);

    cairo_restore(cr);
}

PassRefPtr<cairo_surface_t> copyCairoImageSurface(cairo_surface_t* originalSurface)
{
    // Cairo doesn't provide a way to copy a cairo_surface_t.
    // See http://lists.cairographics.org/archives/cairo/2007-June/010877.html
    // Once cairo provides the way, use the function instead of this.
    IntSize size = cairoSurfaceSize(originalSurface);
    RefPtr<cairo_surface_t> newSurface = adoptRef(cairo_surface_create_similar(originalSurface,
        cairo_surface_get_content(originalSurface), size.width(), size.height()));

    RefPtr<cairo_t> cr = adoptRef(cairo_create(newSurface.get()));
    cairo_set_source_surface(cr.get(), originalSurface, 0, 0);
    cairo_set_operator(cr.get(), CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr.get());
    return newSurface.release();
}

void copyRectFromCairoSurfaceToContext(cairo_surface_t* from, cairo_t* to, const IntSize& offset, const IntRect& rect)
{
    cairo_set_source_surface(to, from, offset.width(), offset.height());
    cairo_rectangle(to, rect.x(), rect.y(), rect.width(), rect.height());
    cairo_fill(to);
}

void copyRectFromOneSurfaceToAnother(cairo_surface_t* from, cairo_surface_t* to, const IntSize& sourceOffset, const IntRect& rect, const IntSize& destOffset, cairo_operator_t cairoOperator)
{
    RefPtr<cairo_t> context = adoptRef(cairo_create(to));
    cairo_translate(context.get(), destOffset.width(), destOffset.height());
    cairo_set_operator(context.get(), cairoOperator);
    copyRectFromCairoSurfaceToContext(from, context.get(), sourceOffset, rect);
}

IntSize cairoSurfaceSize(cairo_surface_t* surface)
{
    switch (cairo_surface_get_type(surface)) {
    case CAIRO_SURFACE_TYPE_IMAGE:
        return IntSize(cairo_image_surface_get_width(surface), cairo_image_surface_get_height(surface));
#if ENABLE(ACCELERATED_2D_CANVAS)
    case CAIRO_SURFACE_TYPE_GL:
        return IntSize(cairo_gl_surface_get_width(surface), cairo_gl_surface_get_height(surface));
#endif
    default:
        ASSERT_NOT_REACHED();
        return IntSize();
    }
}

void flipImageSurfaceVertically(cairo_surface_t* surface)
{
    ASSERT(cairo_surface_get_type(surface) == CAIRO_SURFACE_TYPE_IMAGE);

    IntSize size = cairoSurfaceSize(surface);
    ASSERT(!size.isEmpty());

    int stride = cairo_image_surface_get_stride(surface);
    int halfHeight = size.height() / 2;

    uint8_t* source = static_cast<uint8_t*>(cairo_image_surface_get_data(surface));
    std::unique_ptr<uint8_t[]> tmp = std::make_unique<uint8_t[]>(stride);

    for (int i = 0; i < halfHeight; ++i) {
        uint8_t* top = source + (i * stride);
        uint8_t* bottom = source + ((size.height()-i-1) * stride);

        memcpy(tmp.get(), top, stride);
        memcpy(top, bottom, stride);
        memcpy(bottom, tmp.get(), stride);
    }
}

void cairoSurfaceSetDeviceScale(cairo_surface_t* surface, double xScale, double yScale)
{
    // This function was added pretty much simultaneous to when 1.13 was branched.
#if HAVE(CAIRO_SURFACE_SET_DEVICE_SCALE)
    cairo_surface_set_device_scale(surface, xScale, yScale);
#else
    UNUSED_PARAM(surface);
    ASSERT_UNUSED(xScale, 1 == xScale);
    ASSERT_UNUSED(yScale, 1 == yScale);
#endif
}

void cairoSurfaceGetDeviceScale(cairo_surface_t* surface, double& xScale, double& yScale)
{
#if HAVE(CAIRO_SURFACE_SET_DEVICE_SCALE)
    cairo_surface_get_device_scale(surface, &xScale, &yScale);
#else
    UNUSED_PARAM(surface);
    xScale = 1;
    yScale = 1;
#endif
}

} // namespace WebCore

#endif // USE(CAIRO)
