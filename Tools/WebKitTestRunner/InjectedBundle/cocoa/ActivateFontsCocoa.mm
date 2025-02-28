/*
 * Copyright (C) 2010-2015 Apple Inc. All rights reserved.
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

#import "config.h"
#import "ActivateFonts.h"

#import <CoreFoundation/CoreFoundation.h>
#import <CoreText/CTFontManager.h>
#import <wtf/ObjcRuntimeExtras.h>
#import <wtf/RetainPtr.h>

#if USE(APPKIT)
#import <AppKit/AppKit.h>
#endif

@interface WKTRFontActivatorDummyClass : NSObject
@end

@implementation WKTRFontActivatorDummyClass
@end

namespace WTR {

#if USE(APPKIT)

#if !ENABLE(PLATFORM_FONT_LOOKUP)
static NSSet *allowedFontFamilySet()
{
    static NSSet *fontFamilySet = [[NSSet setWithObjects:
        @"Ahem",
        @"Al Bayan",
        @"American Typewriter",
        @"Andale Mono",
        @"Apple Braille",
        @"Apple Color Emoji",
        @"Apple Chancery",
        @"Apple Garamond BT",
        @"Apple LiGothic",
        @"Apple LiSung",
        @"Apple Symbols",
        @"AppleGothic",
        @"AppleMyungjo",
        @"Arial Black",
        @"Arial Hebrew",
        @"Arial Narrow",
        @"Arial Rounded MT Bold",
        @"Arial Unicode MS",
        @"Arial",
        @"Ayuthaya",
        @"Baghdad",
        @"Baskerville",
        @"BiauKai",
        @"Big Caslon",
        @"Brush Script MT",
        @"Chalkboard",
        @"Chalkduster",
        @"Charcoal CY",
        @"Cochin",
        @"Comic Sans MS",
        @"Copperplate",
        @"Corsiva Hebrew",
        @"Courier New",
        @"Courier",
        @"DecoType Naskh",
        @"Devanagari MT",
        @"Didot",
        @"Euphemia UCAS",
        @"Futura",
        @"GB18030 Bitmap",
        @"Geeza Pro",
        @"Geneva CY",
        @"Geneva",
        @"Georgia",
        @"Gill Sans",
        @"Gujarati MT",
        @"GungSeo",
        @"Gurmukhi MT",
        @"HeadLineA",
        @"Hei",
        @"Heiti SC",
        @"Heiti TC",
        @"Helvetica CY",
        @"Helvetica Neue",
        @"Helvetica",
        @"Herculanum",
        @"Hiragino Kaku Gothic Pro",
        @"Hiragino Kaku Gothic ProN",
        @"Hiragino Kaku Gothic Std",
        @"Hiragino Kaku Gothic StdN",
        @"Hiragino Maru Gothic Pro",
        @"Hiragino Maru Gothic ProN",
        @"Hiragino Mincho Pro",
        @"Hiragino Mincho ProN",
        @"Hiragino Sans GB",
        @"Hoefler Text",
        @"Impact",
        @"InaiMathi",
        @"Kai",
        @"Kailasa",
        @"Kokonor",
        @"Krungthep",
        @"KufiStandardGK",
        @"LiHei Pro",
        @"LiSong Pro",
        @"Lucida Grande",
        @"Marker Felt",
        @"Menlo",
        @"Microsoft Sans Serif",
        @"Monaco",
        @"Mshtakan",
        @"Nadeem",
        @"New Peninim MT",
        @"Optima",
        @"Osaka",
        @"Papyrus",
        @"PCMyungjo",
        @"PilGi",
        @"Plantagenet Cherokee",
        @"Raanana",
        @"Sathu",
        @"Silom",
        @"Skia",
        @"Songti SC",
        @"Songti TC",
        @"STFangsong",
        @"STHeiti",
        @"STIXGeneral",
        @"STIXSizeOneSym",
        @"STKaiti",
        @"STSong",
        @"Symbol",
        @"System Font",
        @"Tahoma",
        @"Thonburi",
        @"Times New Roman",
        @"Times",
        @"Trebuchet MS",
        @"Verdana",
        @"Webdings",
        @"WebKit WeightWatcher",
        @"FontWithFeaturesOTF",
        @"FontWithFeaturesTTF",
        @"Wingdings 2",
        @"Wingdings 3",
        @"Wingdings",
        @"Zapf Dingbats",
        @"Zapfino",
        nil] retain];
    
    return fontFamilySet;
}

static NSSet *systemHiddenFontFamilySet()
{
    static NSSet *fontFamilySet = [[NSSet setWithObjects:
        @".LucidaGrandeUI",
        nil] retain];

    return fontFamilySet;
}

static IMP appKitAvailableFontFamiliesIMP;
static IMP appKitAvailableFontsIMP;

static NSArray *wtr_NSFontManager_availableFontFamilies(id self, SEL _cmd)
{
    static NSArray *availableFontFamilies;
    if (availableFontFamilies)
        return availableFontFamilies;
    
    NSArray *availableFamilies = wtfCallIMP<id>(appKitAvailableFontFamiliesIMP, self, _cmd);

    NSMutableSet *prunedFamiliesSet = [NSMutableSet setWithArray:availableFamilies];
    [prunedFamiliesSet intersectSet:allowedFontFamilySet()];

    availableFontFamilies = [[prunedFamiliesSet allObjects] retain];
    return availableFontFamilies;
}

static NSArray *wtr_NSFontManager_availableFonts(id self, SEL _cmd)
{
    static NSArray *availableFonts;
    if (availableFonts)
        return availableFonts;
    
    NSSet *allowedFamilies = allowedFontFamilySet();
    NSMutableArray *availableFontList = [[NSMutableArray alloc] initWithCapacity:[allowedFamilies count] * 2];
    for (NSString *fontFamily in allowedFontFamilySet()) {
        NSArray* fontsForFamily = [[NSFontManager sharedFontManager] availableMembersOfFontFamily:fontFamily];
        for (NSArray* fontInfo in fontsForFamily) {
            // Font name is the first entry in the array.
            [availableFontList addObject:[fontInfo objectAtIndex:0]];
        }
    }

    for (NSString *hiddenFontFamily in systemHiddenFontFamilySet())
        [availableFontList addObject:hiddenFontFamily];

    availableFonts = availableFontList;
    return availableFonts;
}

static void swizzleNSFontManagerMethods()
{
    Method availableFontFamiliesMethod = class_getInstanceMethod(objc_getClass("NSFontManager"), @selector(availableFontFamilies));
    ASSERT(availableFontFamiliesMethod);
    if (!availableFontFamiliesMethod) {
        NSLog(@"Failed to swizzle the \"availableFontFamilies\" method on NSFontManager");
        return;
    }
    
    appKitAvailableFontFamiliesIMP = method_setImplementation(availableFontFamiliesMethod, (IMP)wtr_NSFontManager_availableFontFamilies);

    Method availableFontsMethod = class_getInstanceMethod(objc_getClass("NSFontManager"), @selector(availableFonts));
    ASSERT(availableFontsMethod);
    if (!availableFontsMethod) {
        NSLog(@"Failed to swizzle the \"availableFonts\" method on NSFontManager");
        return;
    }
    
    appKitAvailableFontsIMP = method_setImplementation(availableFontsMethod, (IMP)wtr_NSFontManager_availableFonts);
}
#endif

// Activating system copies of these fonts overrides any others that could be preferred, such as ones
// in /Library/Fonts/Microsoft, and which don't always have the same metrics.
// FIXME: Switch to a solution from <rdar://problem/19553550> once it's available.
static void activateSystemCoreWebFonts()
{
    NSArray *coreWebFontNames = @[
        @"Andale Mono",
        @"Arial",
        @"Arial Black",
        @"Comic Sans MS",
        @"Courier New",
        @"Georgia",
        @"Impact",
        @"Times New Roman",
        @"Trebuchet MS",
        @"Verdana",
        @"Webdings"
    ];

    NSArray *fontFiles = [[NSFileManager defaultManager] contentsOfDirectoryAtURL:[NSURL fileURLWithPath:@"/Library/Fonts" isDirectory:YES]
        includingPropertiesForKeys:@[NSURLFileResourceTypeKey, NSURLNameKey] options:0 error:0];

    for (NSURL *fontURL in fontFiles) {
        NSString *resourceType;
        NSString *fileName;
        if (![fontURL getResourceValue:&resourceType forKey:NSURLFileResourceTypeKey error:0]
            || ![fontURL getResourceValue:&fileName forKey:NSURLNameKey error:0])
            continue;
        if (![resourceType isEqualToString:NSURLFileResourceTypeRegular])
            continue;

        // Activate all font variations, such as Arial Bold Italic.ttf. This algorithm is not 100% precise, as it
        // also activates e.g. Arial Unicode, which is not a variation of Arial.
        for (NSString *coreWebFontName in coreWebFontNames) {
            if ([fileName hasPrefix:coreWebFontName]) {
                CTFontManagerRegisterFontsForURL((CFURLRef)fontURL, kCTFontManagerScopeProcess, 0);
                break;
            }
        }
    }
}

#endif // USE(APPKIT)

void activateFonts()
{
    static const char* fontFileNames[] = {
        "AHEM____.TTF",
        "WebKitWeightWatcher100.ttf",
        "WebKitWeightWatcher200.ttf",
        "WebKitWeightWatcher300.ttf",
        "WebKitWeightWatcher400.ttf",
        "WebKitWeightWatcher500.ttf",
        "WebKitWeightWatcher600.ttf",
        "WebKitWeightWatcher700.ttf",
        "WebKitWeightWatcher800.ttf",
        "WebKitWeightWatcher900.ttf",
        "FontWithFeatures.otf",
        "FontWithFeatures.ttf",
        0
    };

    NSMutableArray *fontURLs = [NSMutableArray array];
    NSURL *resourcesDirectory = [[NSBundle bundleForClass:[WKTRFontActivatorDummyClass class]] resourceURL];
    for (unsigned i = 0; fontFileNames[i]; ++i) {
        NSURL *fontURL = [resourcesDirectory URLByAppendingPathComponent:[NSString stringWithUTF8String:fontFileNames[i]]];
        [fontURLs addObject:[fontURL absoluteURL]];
    }

    CFArrayRef errors = 0;
    if (!CTFontManagerRegisterFontsForURLs((CFArrayRef)fontURLs, kCTFontManagerScopeProcess, &errors)) {
        NSLog(@"Failed to activate fonts: %@", errors);
        CFRelease(errors);
        exit(1);
    }

#if USE(APPKIT)
#if !ENABLE(PLATFORM_FONT_LOOKUP)
    swizzleNSFontManagerMethods();
#endif
    activateSystemCoreWebFonts();
#endif // USE(APPKIT)
}

}

