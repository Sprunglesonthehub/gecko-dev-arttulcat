/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "src/effects/colorfilters/SkRuntimeColorFilter.h"

#include "include/core/SkCapabilities.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkData.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkString.h"
#include "include/effects/SkLumaColorFilter.h"
#include "include/effects/SkOverdrawColorFilter.h"
#include "include/effects/SkRuntimeEffect.h"
#include "include/private/SkSLSampleUsage.h"
#include "include/private/base/SkDebug.h"
#include "include/private/base/SkFloatingPoint.h"
#include "include/private/base/SkTArray.h"
#include "src/core/SkColorData.h"
#include "src/core/SkEffectPriv.h"
#include "src/core/SkKnownRuntimeEffects.h"
#include "src/core/SkPicturePriv.h"
#include "src/core/SkReadBuffer.h"
#include "src/core/SkRuntimeEffectPriv.h"
#include "src/core/SkWriteBuffer.h"
#include "src/shaders/SkShaderBase.h"
#include "src/sksl/codegen/SkSLRasterPipelineBuilder.h"

#include <cstdint>
#include <string>
#include <utility>

#if defined(SK_BUILD_FOR_DEBUGGER)
    constexpr bool kLenientSkSLDeserialization = true;
#else
    constexpr bool kLenientSkSLDeserialization = false;
#endif

SkRuntimeColorFilter::SkRuntimeColorFilter(sk_sp<SkRuntimeEffect> effect,
                                           sk_sp<const SkData> uniforms,
                                           SkSpan<const SkRuntimeEffect::ChildPtr> children)
        : fEffect(std::move(effect))
        , fUniforms(std::move(uniforms))
        , fChildren(children.begin(), children.end()) {}

bool SkRuntimeColorFilter::appendStages(const SkStageRec& rec, bool) const {
    if (!SkRuntimeEffectPriv::CanDraw(SkCapabilities::RasterBackend().get(), fEffect.get())) {
        // SkRP has support for many parts of #version 300 already, but for now, we restrict its
        // usage in runtime effects to just #version 100.
        return false;
    }
    if (const SkSL::RP::Program* program = fEffect->getRPProgram(/*debugTrace=*/nullptr)) {
        SkSpan<const float> uniforms =
                SkRuntimeEffectPriv::UniformsAsSpan(fEffect->uniforms(),
                                                    fUniforms,
                                                    /*alwaysCopyIntoAlloc=*/false,
                                                    rec.fDstCS,
                                                    rec.fAlloc);
        SkShaders::MatrixRec matrix(SkMatrix::I());
        matrix.markCTMApplied();
        RuntimeEffectRPCallbacks callbacks(rec, matrix, fChildren, fEffect->fSampleUsages);
        bool success = program->appendStages(rec.fPipeline, rec.fAlloc, &callbacks, uniforms);
        return success;
    }
    return false;
}

bool SkRuntimeColorFilter::onIsAlphaUnchanged() const {
    return fEffect->isAlphaUnchanged();
}

void SkRuntimeColorFilter::flatten(SkWriteBuffer& buffer) const {
    if (SkKnownRuntimeEffects::IsSkiaKnownRuntimeEffect(fEffect->fStableKey)) {
        // We only serialize Skia-internal stableKeys. First party stable keys are not serialized.
        buffer.write32(fEffect->fStableKey);
    } else {
        buffer.write32(0);
        buffer.writeString(fEffect->source().c_str());
    }
    buffer.writeDataAsByteArray(fUniforms.get());
    SkRuntimeEffectPriv::WriteChildEffects(buffer, fChildren);
}

SkRuntimeEffect* SkRuntimeColorFilter::asRuntimeEffect() const { return fEffect.get(); }

sk_sp<SkFlattenable> SkRuntimeColorFilter::CreateProc(SkReadBuffer& buffer) {
    if (!buffer.validate(buffer.allowSkSL())) {
        return nullptr;
    }

    sk_sp<SkRuntimeEffect> effect;
    if (!buffer.isVersionLT(SkPicturePriv::kSerializeStableKeys)) {
        uint32_t candidateStableKey = buffer.readUInt();
        effect = SkKnownRuntimeEffects::MaybeGetKnownRuntimeEffect(candidateStableKey);
        if (!effect && !buffer.validate(candidateStableKey == 0)) {
            return nullptr;
        }
    }

    if (!effect) {
        SkString sksl;
        buffer.readString(&sksl);
        effect = SkMakeCachedRuntimeEffect(SkRuntimeEffect::MakeForColorFilter, std::move(sksl));
    }
    if constexpr (!kLenientSkSLDeserialization) {
        if (!buffer.validate(effect != nullptr)) {
            return nullptr;
        }
    }

    sk_sp<SkData> uniforms = buffer.readByteArrayAsData();

    skia_private::STArray<4, SkRuntimeEffect::ChildPtr> children;
    if (!SkRuntimeEffectPriv::ReadChildEffects(buffer, effect.get(), &children)) {
        return nullptr;
    }

    if constexpr (kLenientSkSLDeserialization) {
        if (!effect) {
            SkDebugf("Serialized SkSL failed to compile. Ignoring/dropping SkSL color filter.\n");
            return nullptr;
        }
    }

    return effect->makeColorFilter(std::move(uniforms), SkSpan(children));
}

/////////////////////////////////////////////////////////////////////////////////////////////////

sk_sp<SkColorFilter> SkColorFilters::Lerp(float weight, sk_sp<SkColorFilter> cf0,
                                                        sk_sp<SkColorFilter> cf1) {
    using namespace SkKnownRuntimeEffects;

    if (!cf0 && !cf1) {
        return nullptr;
    }
    if (SkIsNaN(weight)) {
        return nullptr;
    }

    if (cf0 == cf1) {
        return cf0; // or cf1
    }

    if (weight <= 0) {
        return cf0;
    }
    if (weight >= 1) {
        return cf1;
    }

    const SkRuntimeEffect* lerpEffect = GetKnownRuntimeEffect(StableKey::kLerp);

    sk_sp<SkColorFilter> inputs[] = {cf0,cf1};
    return lerpEffect->makeColorFilter(SkData::MakeWithCopy(&weight, sizeof(weight)),
                                       inputs, std::size(inputs));
}

sk_sp<SkColorFilter> SkLumaColorFilter::Make() {
    using namespace SkKnownRuntimeEffects;

    const SkRuntimeEffect* lumaEffect = GetKnownRuntimeEffect(StableKey::kLuma);

    return lumaEffect->makeColorFilter(SkData::MakeEmpty());
}

sk_sp<SkColorFilter> SkOverdrawColorFilter::MakeWithSkColors(const SkColor colors[kNumColors]) {
    using namespace SkKnownRuntimeEffects;

    const SkRuntimeEffect* overdrawEffect = GetKnownRuntimeEffect(StableKey::kOverdraw);

    auto data = SkData::MakeUninitialized(kNumColors * sizeof(SkPMColor4f));
    SkPMColor4f* premul = (SkPMColor4f*)data->writable_data();
    for (int i = 0; i < kNumColors; ++i) {
        premul[i] = SkColor4f::FromColor(colors[i]).premul();
    }
    return overdrawEffect->makeColorFilter(std::move(data));
}
