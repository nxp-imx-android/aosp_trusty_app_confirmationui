/*
 * Copyright 2019, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "trusty_confirmation_ui.h"

#include <layouts/device_parameters.h>
#include <layouts/layout.h>

#include <teeui/error.h>
#include <teeui/localization/ConfirmationUITranslations.h>
#include <teeui/utils.h>

#include <interface/secure_fb/secure_fb.h>

#include "trusty_operation.h"

#include <inttypes.h>
#include <stdio.h>

#include <trusty_log.h>

#define TLOG_TAG "confirmationui"

using teeui::ResponseCode;

static constexpr const teeui::Color kColorEnabled = 0xff212121;
static constexpr const teeui::Color kColorDisabled = 0xffbdbdbd;
static constexpr const teeui::Color kColorEnabledInv = 0xffdedede;
static constexpr const teeui::Color kColorDisabledInv = 0xff424242;
static constexpr const teeui::Color kColorBackground = 0xffffffff;
static constexpr const teeui::Color kColorBackgroundInv = 0xff212121;
static constexpr const teeui::Color kColorShieldInv = 0xffc4cb80;
static constexpr const teeui::Color kColorShield = 0xff778500;

template <typename Label, typename Layout>
static teeui::Error updateString(Layout* layout) {
    using namespace teeui;
    const char* str;
    auto& label = std::get<Label>(*layout);

    str = localization::lookup(TranslationId(label.textId()));
    if (str == nullptr) {
        TLOGW("Given translation_id %" PRIu64 " not found", label.textId());
        return Error::Localization;
    }
    label.setText({str, str + strlen(str)});
    return Error::OK;
}

template <typename Context>
static void updateColorScheme(Context* ctx, bool inverted) {
    using namespace teeui;
    if (inverted) {
        ctx->template setParam<ShieldColor>(kColorShieldInv);
        ctx->template setParam<ColorText>(kColorDisabledInv);
        ctx->template setParam<ColorBG>(kColorBackgroundInv);
    } else {
        ctx->template setParam<ShieldColor>(kColorShield);
        ctx->template setParam<ColorText>(kColorDisabled);
        ctx->template setParam<ColorBG>(kColorBackground);
    }
    return;
}

static teeui::Color alfaCombineChannel(uint32_t shift,
                                       double alfa,
                                       teeui::Color a,
                                       teeui::Color b) {
    a >>= shift;
    a &= 0xff;
    b >>= shift;
    b &= 0xff;
    double acc = alfa * a + (1 - alfa) * b;
    if (acc <= 0)
        return 0;
    uint32_t result = acc;
    if (result > 255)
        return 255 << shift;
    return result << shift;
}

template <typename... Elements>
static teeui::Error drawElements(std::tuple<Elements...>& layout,
                                 const teeui::PixelDrawer& drawPixel) {
    // Error::operator|| is overloaded, so we don't get short circuit
    // evaluation. But we get the first error that occurs. We will still try and
    // draw the remaining elements in the order they appear in the layout tuple.
    return (std::get<Elements>(layout).draw(drawPixel) || ...);
}

static ResponseCode teeuiError2ResponseCode(const teeui::Error& e) {
    switch (e.code()) {
    case teeui::Error::OK:
        return ResponseCode::OK;
    case teeui::Error::NotInitialized:
        return ResponseCode::UIError;
    case teeui::Error::FaceNotLoaded:
        return ResponseCode::UIErrorMissingGlyph;
    case teeui::Error::CharSizeNotSet:
        return ResponseCode::UIError;
    case teeui::Error::GlyphNotLoaded:
        return ResponseCode::UIErrorMissingGlyph;
    case teeui::Error::GlyphNotRendered:
        return ResponseCode::UIErrorMissingGlyph;
    case teeui::Error::GlyphNotExtracted:
        return ResponseCode::UIErrorMissingGlyph;
    case teeui::Error::UnsupportedPixelFormat:
        return ResponseCode::UIError;
    case teeui::Error::OutOfBoundsDrawing:
        return ResponseCode::UIErrorMessageTooLong;
    case teeui::Error::BBoxComputation:
        return ResponseCode::UIErrorMessageTooLong;
    case teeui::Error::OutOfMemory:
        return ResponseCode::UIErrorMessageTooLong;
    case teeui::Error::Localization:
        return ResponseCode::UIError;
    default:
        return ResponseCode::UIError;
    }
}

teeui::Error TrustyConfirmationUI::updateTranslations() {
    using namespace teeui;
    if (auto error = updateString<LabelOK>(&layout_))
        return error;
    if (auto error = updateString<LabelCancel>(&layout_))
        return error;
    if (auto error = updateString<LabelTitle>(&layout_))
        return error;
    if (auto error = updateString<LabelHint>(&layout_))
        return error;
    return Error::OK;
}

ResponseCode TrustyConfirmationUI::start(const char* prompt,
                                         const char* lang_id,
                                         bool inverted,
                                         bool magnified) {
    enabled_ = true;
    inverted_ = inverted;
    auto rc = trusty_secure_fb_get_secure_fb(&fb_info_);
    if (rc != TTUI_ERROR_OK) {
        TLOGE("trusty_secure_fb_get_secure_fb returned  %d\n", rc);
        return ResponseCode::UIError;
    }
    if (fb_info_.pixel_format != TTUI_PF_RGBA8) {
        TLOGE("Unknown pixel format %u\n", fb_info_.pixel_format);
        return ResponseCode::UIError;
    }

    using namespace teeui;
    optional<context<ConUIParameters>> ctx;

    /*
     * TODO: find a more generic way to determine the device we are running on.
     * b/148079189
     */
    if (fb_info_.width == 1080 && fb_info_.height == 2160) {
        ctx = getDeviceContext(Devices::BLUELINE, magnified);
    } else if (fb_info_.width == 1440 && fb_info_.height == 2960) {
        ctx = getDeviceContext(Devices::CROSSHATCH, magnified);
    }

    if (!ctx) {
        TLOGE("no suitable layout found for screen resolution of %ux%u\n",
              fb_info_.width, fb_info_.height);
        return ResponseCode::UIError;
    }

    layout_ = instantiateLayout(ConfUILayout(), *ctx);

    localization::selectLangId(lang_id);
    if (auto error = updateTranslations()) {
        return teeuiError2ResponseCode(error);
    }
    updateColorScheme(&*ctx, inverted_);

    std::get<LabelBody>(layout_).setText({prompt, prompt + strlen(prompt)});

    showInstructions(false /* enable */);
    auto render_error = renderAndSwap();
    if (render_error != ResponseCode::OK) {
        stop();
    }
    return render_error;
}

ResponseCode TrustyConfirmationUI::renderAndSwap() {
    auto drawPixel = teeui::makePixelDrawer([&, this](uint32_t x, uint32_t y,
                                                      teeui::Color color)
                                                    -> teeui::Error {
        TLOGD("px %u %u: %08x", x, y, color);
        size_t pos = y * fb_info_.line_stride + x * fb_info_.pixel_stride;
        TLOGD("pos: %zu, bufferSize: %" PRIu32 "\n", pos, fb_info_.size);
        if (pos >= fb_info_.size) {
            return teeui::Error::OutOfBoundsDrawing;
        }
        double alfa = (color & 0xff000000) >> 24;
        alfa /= 255.0;
        auto& pixel = *reinterpret_cast<teeui::Color*>(fb_info_.buffer + pos);

        pixel = alfaCombineChannel(0, alfa, color, pixel) |
                alfaCombineChannel(8, alfa, color, pixel) |
                alfaCombineChannel(16, alfa, color, pixel);
        return teeui::Error::OK;
    });

    TLOGI("begin rendering\n");

    teeui::Color bgColor = kColorBackground;
    if (inverted_) {
        bgColor = kColorBackgroundInv;
    }
    uint8_t* line_iter = fb_info_.buffer;
    for (uint32_t yi = 0; yi < fb_info_.height; ++yi) {
        auto pixel_iter = line_iter;
        for (uint32_t xi = 0; xi < fb_info_.width; ++xi) {
            *reinterpret_cast<uint32_t*>(pixel_iter) = bgColor;
            pixel_iter += fb_info_.pixel_stride;
        }
        line_iter += fb_info_.line_stride;
    }

    if (auto error = drawElements(layout_, drawPixel)) {
        TLOGE("Element drawing failed: %u\n", error.code());
        return teeuiError2ResponseCode(error);
    }

    auto rc = trusty_secure_fb_display_next(&fb_info_, false);
    if (rc != TTUI_ERROR_OK) {
        TLOGE("trusty_secure_fb_display_next returned  %d\n", rc);
        return ResponseCode::UIError;
    }

    return ResponseCode::OK;
}

ResponseCode TrustyConfirmationUI::showInstructions(bool enable) {
    using namespace teeui;
    if (enabled_ == enable)
        return ResponseCode::OK;
    enabled_ = enable;
    Color color;
    if (enable) {
        if (inverted_)
            color = kColorEnabledInv;
        else
            color = kColorEnabled;
    } else {
        if (inverted_)
            color = kColorDisabledInv;
        else
            color = kColorDisabled;
    }
    std::get<LabelOK>(layout_).setTextColor(color);
    std::get<LabelCancel>(layout_).setTextColor(color);
    ResponseCode rc = ResponseCode::OK;
    if (enable) {
        rc = renderAndSwap();
        if (rc != ResponseCode::OK) {
            stop();
        }
    }
    return ResponseCode::OK;
}

void TrustyConfirmationUI::stop() {
    TLOGI("calling gui stop\n");
    trusty_secure_fb_release_display();
    TLOGI("calling gui stop - done\n");
}
