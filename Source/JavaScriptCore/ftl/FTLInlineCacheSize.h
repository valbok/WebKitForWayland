/*
 * Copyright (C) 2013, 2015 Apple Inc. All rights reserved.
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

#ifndef FTLInlineCacheSize_h
#define FTLInlineCacheSize_h

#if ENABLE(FTL_JIT)

namespace JSC {

namespace DFG {
struct Node;
}

namespace FTL {

size_t sizeOfGetById();
size_t sizeOfPutById();
size_t sizeOfCall();
size_t sizeOfCallVarargs();
size_t sizeOfTailCallVarargs();
size_t sizeOfCallForwardVarargs();
size_t sizeOfTailCallForwardVarargs();
size_t sizeOfConstructVarargs();
size_t sizeOfConstructForwardVarargs();
size_t sizeOfIn();
size_t sizeOfBitAnd();
size_t sizeOfBitOr();
size_t sizeOfBitXor();
size_t sizeOfBitLShift();
size_t sizeOfBitRShift();
size_t sizeOfBitURShift();
size_t sizeOfArithDiv();
size_t sizeOfArithMul();
size_t sizeOfArithSub();
size_t sizeOfValueAdd();
#if ENABLE(MASM_PROBE)
size_t sizeOfProbe();
#endif

size_t sizeOfICFor(DFG::Node*);

} } // namespace JSC::FTL

#endif // ENABLE(FTL_JIT)

#endif // FTLInlineCacheSize_h

