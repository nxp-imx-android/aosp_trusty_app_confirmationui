# Copyright (C) 2019 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += \
	$(LOCAL_DIR)/src/main.cpp \
	$(LOCAL_DIR)/src/manifest.c \
	$(LOCAL_DIR)/src/secure_input_tracker.cpp \
	$(LOCAL_DIR)/src/trusty_operation.cpp \
	$(LOCAL_DIR)/src/trusty_time_stamper.cpp \

MODULE_CPPFLAGS := -std=c++17 -fno-short-enums -fno-exceptions
MODULE_CPPFLAGS += -fno-threadsafe-statics -fno-rtti

MODULE_DEPS += \
	trusty/user/base/lib/libc-trusty \
	trusty/user/base/lib/libstdc++-trusty \
	trusty/user/base/lib/rng \
	trusty/user/base/lib/teeui-stub \
	trusty/user/base/lib/tipc \
	external/boringssl \

include make/module.mk

