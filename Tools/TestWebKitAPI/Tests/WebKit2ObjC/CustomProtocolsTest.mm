/*
 * Copyright (C) 2012 Apple Inc. All rights reserved.
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

#if WK_HAVE_C_SPI

#import "Test.h"

#import "PlatformUtilities.h"
#import "TestBrowsingContextLoadDelegate.h"
#import "TestProtocol.h"
#import <wtf/RetainPtr.h>

#if WK_API_ENABLED && PLATFORM(MAC)

static bool testFinished = false;

@interface CustomProtocolsLoadDelegate : NSObject <WKBrowsingContextLoadDelegate>
@end

@implementation CustomProtocolsLoadDelegate

- (void)browsingContextControllerDidStartProvisionalLoad:(WKBrowsingContextController *)sender
{
    EXPECT_TRUE([sender.provisionalURL.absoluteString isEqualToString:@"http://redirect/?test"]);
}

- (void)browsingContextControllerDidReceiveServerRedirectForProvisionalLoad:(WKBrowsingContextController *)sender
{
    EXPECT_TRUE([sender.provisionalURL.absoluteString isEqualToString:@"http://test/"]);
}

- (void)browsingContextControllerDidCommitLoad:(WKBrowsingContextController *)sender
{
    EXPECT_TRUE([sender.committedURL.absoluteString isEqualToString:@"http://test/"]);
}

- (void)browsingContextControllerDidFinishLoad:(WKBrowsingContextController *)sender
{
    EXPECT_FALSE(sender.isLoading);
    testFinished = true;
}

@end

namespace TestWebKitAPI {

TEST(WebKit2CustomProtocolsTest, MainResource)
{
    [NSURLProtocol registerClass:[TestProtocol class]];
    [WKBrowsingContextController registerSchemeForCustomProtocol:[TestProtocol scheme]];

    RetainPtr<WKProcessGroup> processGroup = adoptNS([[WKProcessGroup alloc] init]);
    RetainPtr<WKBrowsingContextGroup> browsingContextGroup = adoptNS([[WKBrowsingContextGroup alloc] initWithIdentifier:@"TestIdentifier"]);
    RetainPtr<WKView> wkView = adoptNS([[WKView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) processGroup:processGroup.get() browsingContextGroup:browsingContextGroup.get()]);
    RetainPtr<CustomProtocolsLoadDelegate> loadDelegate = adoptNS([[CustomProtocolsLoadDelegate alloc] init]);
    [wkView browsingContextController].loadDelegate = loadDelegate.get();
    [[wkView browsingContextController] loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:[NSString stringWithFormat:@"%@://redirect?test", [TestProtocol scheme]]]]];

    Util::run(&testFinished);
    [NSURLProtocol unregisterClass:[TestProtocol class]];
    [WKBrowsingContextController unregisterSchemeForCustomProtocol:[TestProtocol scheme]];
}

} // namespace TestWebKitAPI

#endif // WK_API_ENABLED

#endif
