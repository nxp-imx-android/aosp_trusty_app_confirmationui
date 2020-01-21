/*
 * Copyright 2020, The Android Open Source Project
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

#include <layouts/device_parameters.h>

namespace teeui {

optional<context<ConUIParameters>> getDeviceContext(Devices d, bool magnified) {
    optional<context<ConUIParameters>> ctx;
    switch (d) {
    case Devices::BLUELINE: {
        ctx = context<ConUIParameters>(17.42075974, 3.0);
        ctx->setParam<RightEdgeOfScreen>(1080_px);
        ctx->setParam<BottomOfScreen>(2160_px);
        ctx->setParam<PowerButtonTop>(20.26_mm);
        ctx->setParam<PowerButtonBottom>(30.26_mm);
        ctx->setParam<VolUpButtonTop>(40.26_mm);
        ctx->setParam<VolUpButtonBottom>(50.26_mm);
        break;
    }
    case Devices::CROSSHATCH: {
        ctx = context<ConUIParameters>(20.42958729, 3.5);
        ctx->setParam<RightEdgeOfScreen>(1440_px);
        ctx->setParam<BottomOfScreen>(2960_px);
        ctx->setParam<PowerButtonTop>(34.146_mm);
        ctx->setParam<PowerButtonBottom>(44.146_mm);
        ctx->setParam<VolUpButtonTop>(54.146_mm);
        ctx->setParam<VolUpButtonBottom>(64.146_mm);
        break;
    }
    default:
        return ctx;
    }
    if (magnified) {
        ctx->setParam<DefaultFontSize>(18_dp);
        ctx->setParam<BodyFontSize>(20_dp);
    } else {
        ctx->setParam<DefaultFontSize>(14_dp);
        ctx->setParam<BodyFontSize>(16_dp);
    }
    return ctx;
}

}  // namespace teeui
