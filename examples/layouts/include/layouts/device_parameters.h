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

#pragma once

#include <stdint.h>
#include "layout.h"

namespace teeui {

/*
 * TODO: Instead of defining an enum value for each device, we need to find a
 * more generic way to select the layout parameters for the device we are
 * running on, if the client of this library is supposed to be generic.
 */
enum class Devices : uint32_t {
    BLUELINE,
    CROSSHATCH,
};

optional<context<ConUIParameters>> getDeviceContext(Devices d, bool magnified);

}  // namespace teeui
