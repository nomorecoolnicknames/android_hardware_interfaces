/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "android.hardware.camera.device@3.3-convert-impl"
#include <cutils/properties.h>
#include <log/log.h>

#include "include/convert.h"

namespace android {
namespace hardware {
namespace camera {
namespace device {
namespace V3_3 {
namespace implementation {

using ::android::hardware::graphics::common::V1_0::Dataspace;
using ::android::hardware::graphics::common::V1_0::PixelFormat;
using ::android::hardware::camera::device::V3_2::BufferUsageFlags;

void convertToHidl(const Camera3Stream* src, HalStream* dst) {
    dst->overrideDataSpace = src->data_space;
    dst->v3_2.id = src->mId;
    dst->v3_2.overrideFormat = (PixelFormat) src->format;
    dst->v3_2.maxBuffers = src->max_buffers;
    // Off unless set (Meizu MX6): the MediaTek
    // HAL3 answers max_buffers = 1 for a RAW16 output, so with Google Camera's
    // ZSL request (preview + RAW16 4032x3016 on every frame) only one request
    // can be in flight and the session runs at 3 fps while the sensor delivers
    // 16.6 and the CPUs sit idle. persist.camera.raw_maxbuf=N
    // advertises N to the framework instead; whether the HAL really copes with
    // N RAW buffers outstanding is exactly what the experiment measures.
    if (src->format == HAL_PIXEL_FORMAT_RAW16) {
        int32_t want = property_get_int32("persist.camera.raw_maxbuf", 0);
        if (want > (int32_t)src->max_buffers && want <= 8) {
            ALOGI("%s: RAW16 stream %d maxBuffers %u -> %d (persist.camera.raw_maxbuf)",
                  __FUNCTION__, src->mId, src->max_buffers, want);
            dst->v3_2.maxBuffers = want;
        }
    }
    if (src->stream_type == CAMERA3_STREAM_OUTPUT) {
        dst->v3_2.consumerUsage = (BufferUsageFlags)0;
        dst->v3_2.producerUsage = (BufferUsageFlags)src->usage;
    } else if (src->stream_type == CAMERA3_STREAM_INPUT) {
        dst->v3_2.producerUsage = (BufferUsageFlags)0;
        dst->v3_2.consumerUsage = (BufferUsageFlags)src->usage;
    } else {
        //Should not reach here per current HIDL spec, but we might end up adding
        // bi-directional stream to HIDL.
        ALOGW("%s: Stream type %d is not currently supported!",
                __FUNCTION__, src->stream_type);
    }

#ifdef TARGET_CAMERA_OVERRIDE_FORMAT_FROM_RESERVED
    HalStream* halStream = NULL;
    if (src->reserved[0] != NULL) {
        halStream = (HalStream*)(src->reserved[0]);
    } else if (src->reserved[1] != NULL) {
        halStream = (HalStream*)(src->reserved[1]);
    }

    if (halStream != NULL) {
        dst->v3_2.overrideFormat  = (PixelFormat) halStream->v3_2.overrideFormat;
        if (src->stream_type == CAMERA3_STREAM_OUTPUT) {
            dst->v3_2.producerUsage = (BufferUsageFlags)halStream->v3_2.producerUsage;
        } else if (src->stream_type == CAMERA3_STREAM_INPUT) {
            dst->v3_2.consumerUsage = (BufferUsageFlags)halStream->v3_2.consumerUsage;
        }
    }
#endif
}

void convertToHidl(const camera3_stream_configuration_t& src, HalStreamConfiguration* dst) {
    dst->streams.resize(src.num_streams);
    for (uint32_t i = 0; i < src.num_streams; i++) {
        convertToHidl(static_cast<Camera3Stream*>(src.streams[i]), &dst->streams[i]);
    }
    return;
}

}  // namespace implementation
}  // namespace V3_3
}  // namespace device
}  // namespace camera
}  // namespace hardware
}  // namespace android
