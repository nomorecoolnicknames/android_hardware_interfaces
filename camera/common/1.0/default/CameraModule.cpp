/*
 * Copyright (C) 2016 The Android Open Source Project
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

#define LOG_TAG "CamComm1.0-CamModule"
#define ATRACE_TAG ATRACE_TAG_CAMERA
//#define LOG_NDEBUG 0

#include <utils/Trace.h>
#include <dlfcn.h>
#include <vector>
#include <cutils/properties.h>

#include "CameraModule.h"

namespace android {
namespace hardware {
namespace camera {
namespace common {
namespace V1_0 {
namespace helper {

void CameraModule::deriveCameraCharacteristicsKeys(
        uint32_t deviceVersion, CameraMetadata &chars) {
    ATRACE_CALL();

    Vector<int32_t> derivedCharKeys;
    Vector<int32_t> derivedRequestKeys;
    Vector<int32_t> derivedResultKeys;
    // Keys added in HAL3.3
    if (deviceVersion < CAMERA_DEVICE_API_VERSION_3_3) {
        Vector<uint8_t> controlModes;
        uint8_t data = ANDROID_CONTROL_AE_LOCK_AVAILABLE_TRUE;
        chars.update(ANDROID_CONTROL_AE_LOCK_AVAILABLE, &data, /*count*/1);
        data = ANDROID_CONTROL_AWB_LOCK_AVAILABLE_TRUE;
        chars.update(ANDROID_CONTROL_AWB_LOCK_AVAILABLE, &data, /*count*/1);
        controlModes.push(ANDROID_CONTROL_MODE_AUTO);
        camera_metadata_entry entry = chars.find(ANDROID_CONTROL_AVAILABLE_SCENE_MODES);
        if (entry.count > 1 || entry.data.u8[0] != ANDROID_CONTROL_SCENE_MODE_DISABLED) {
            controlModes.push(ANDROID_CONTROL_MODE_USE_SCENE_MODE);
        }

        // Only advertise CONTROL_OFF mode if 3A manual controls are supported.
        bool isManualAeSupported = false;
        bool isManualAfSupported = false;
        bool isManualAwbSupported = false;
        entry = chars.find(ANDROID_CONTROL_AE_AVAILABLE_MODES);
        if (entry.count > 0) {
            for (size_t i = 0; i < entry.count; i++) {
                if (entry.data.u8[i] == ANDROID_CONTROL_AE_MODE_OFF) {
                    isManualAeSupported = true;
                    break;
                }
            }
        }
        entry = chars.find(ANDROID_CONTROL_AF_AVAILABLE_MODES);
        if (entry.count > 0) {
            for (size_t i = 0; i < entry.count; i++) {
                if (entry.data.u8[i] == ANDROID_CONTROL_AF_MODE_OFF) {
                    isManualAfSupported = true;
                    break;
                }
            }
        }
        entry = chars.find(ANDROID_CONTROL_AWB_AVAILABLE_MODES);
        if (entry.count > 0) {
            for (size_t i = 0; i < entry.count; i++) {
                if (entry.data.u8[i] == ANDROID_CONTROL_AWB_MODE_OFF) {
                    isManualAwbSupported = true;
                    break;
                }
            }
        }
        if (isManualAeSupported && isManualAfSupported && isManualAwbSupported) {
            controlModes.push(ANDROID_CONTROL_MODE_OFF);
        }

        chars.update(ANDROID_CONTROL_AVAILABLE_MODES, controlModes);

        entry = chars.find(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS);
        // HAL3.2 devices passing existing CTS test should all support all LSC modes and LSC map
        bool lensShadingModeSupported = false;
        if (entry.count > 0) {
            for (size_t i = 0; i < entry.count; i++) {
                if (entry.data.i32[i] == ANDROID_SHADING_MODE) {
                    lensShadingModeSupported = true;
                    break;
                }
            }
        }
        Vector<uint8_t> lscModes;
        Vector<uint8_t> lscMapModes;
        lscModes.push(ANDROID_SHADING_MODE_FAST);
        lscModes.push(ANDROID_SHADING_MODE_HIGH_QUALITY);
        lscMapModes.push(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF);
        if (lensShadingModeSupported) {
            lscModes.push(ANDROID_SHADING_MODE_OFF);
            lscMapModes.push(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_ON);
        }
        chars.update(ANDROID_SHADING_AVAILABLE_MODES, lscModes);
        chars.update(ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES, lscMapModes);

        derivedCharKeys.push(ANDROID_CONTROL_AE_LOCK_AVAILABLE);
        derivedCharKeys.push(ANDROID_CONTROL_AWB_LOCK_AVAILABLE);
        derivedCharKeys.push(ANDROID_CONTROL_AVAILABLE_MODES);
        derivedCharKeys.push(ANDROID_SHADING_AVAILABLE_MODES);
        derivedCharKeys.push(ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES);

        // Need update android.control.availableHighSpeedVideoConfigurations since HAL3.3
        // adds batch size to this array.
        entry = chars.find(ANDROID_CONTROL_AVAILABLE_HIGH_SPEED_VIDEO_CONFIGURATIONS);
        if (entry.count > 0) {
            Vector<int32_t> highSpeedConfig;
            for (size_t i = 0; i < entry.count; i += 4) {
                highSpeedConfig.add(entry.data.i32[i]); // width
                highSpeedConfig.add(entry.data.i32[i + 1]); // height
                highSpeedConfig.add(entry.data.i32[i + 2]); // fps_min
                highSpeedConfig.add(entry.data.i32[i + 3]); // fps_max
                highSpeedConfig.add(1); // batchSize_max. default to 1 for HAL3.2
            }
            chars.update(ANDROID_CONTROL_AVAILABLE_HIGH_SPEED_VIDEO_CONFIGURATIONS,
                    highSpeedConfig);
        }
    }

    // Keys added in HAL3.4
    if (deviceVersion < CAMERA_DEVICE_API_VERSION_3_4) {
        // Check if HAL supports RAW_OPAQUE output
        camera_metadata_entry entry = chars.find(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);
        bool supportRawOpaque = false;
        bool supportAnyRaw = false;
        const int STREAM_CONFIGURATION_SIZE = 4;
        const int STREAM_FORMAT_OFFSET = 0;
        const int STREAM_WIDTH_OFFSET = 1;
        const int STREAM_HEIGHT_OFFSET = 2;
        const int STREAM_IS_INPUT_OFFSET = 3;
        Vector<int32_t> rawOpaqueSizes;

        for (size_t i=0; i < entry.count; i += STREAM_CONFIGURATION_SIZE) {
            int32_t format = entry.data.i32[i + STREAM_FORMAT_OFFSET];
            int32_t width = entry.data.i32[i + STREAM_WIDTH_OFFSET];
            int32_t height = entry.data.i32[i + STREAM_HEIGHT_OFFSET];
            int32_t isInput = entry.data.i32[i + STREAM_IS_INPUT_OFFSET];
            if (isInput == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT &&
                    format == HAL_PIXEL_FORMAT_RAW_OPAQUE) {
                supportRawOpaque = true;
                rawOpaqueSizes.push(width);
                rawOpaqueSizes.push(height);
                // 2 bytes per pixel. This rough estimation is only used when
                // HAL does not fill in the opaque raw size
                rawOpaqueSizes.push(width * height *2);
            }
            if (isInput == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT &&
                    (format == HAL_PIXEL_FORMAT_RAW16 ||
                     format == HAL_PIXEL_FORMAT_RAW10 ||
                     format == HAL_PIXEL_FORMAT_RAW12 ||
                     format == HAL_PIXEL_FORMAT_RAW_OPAQUE)) {
                supportAnyRaw = true;
            }
        }

        if (supportRawOpaque) {
            entry = chars.find(ANDROID_SENSOR_OPAQUE_RAW_SIZE);
            if (entry.count == 0) {
                // Fill in estimated value if HAL does not list it
                chars.update(ANDROID_SENSOR_OPAQUE_RAW_SIZE, rawOpaqueSizes);
                derivedCharKeys.push(ANDROID_SENSOR_OPAQUE_RAW_SIZE);
            }
        }

        // Check if HAL supports any RAW output, if so, fill in postRawSensitivityBoost range
        if (supportAnyRaw) {
            int32_t defaultRange[2] = {100, 100};
            entry = chars.find(ANDROID_CONTROL_POST_RAW_SENSITIVITY_BOOST_RANGE);
            if (entry.count == 0) {
                // Fill in default value (100, 100)
                chars.update(
                        ANDROID_CONTROL_POST_RAW_SENSITIVITY_BOOST_RANGE,
                        defaultRange, 2);
                derivedCharKeys.push(ANDROID_CONTROL_POST_RAW_SENSITIVITY_BOOST_RANGE);
                // Actual request/results will be derived by camera device.
                derivedRequestKeys.push(ANDROID_CONTROL_POST_RAW_SENSITIVITY_BOOST);
                derivedResultKeys.push(ANDROID_CONTROL_POST_RAW_SENSITIVITY_BOOST);
            }
        }
    }

    // Add those newly added keys to AVAILABLE_CHARACTERISTICS_KEYS
    // This has to be done at this end of this function.
    if (derivedCharKeys.size() > 0) {
        appendAvailableKeys(
                chars, ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS, derivedCharKeys);
    }
    if (derivedRequestKeys.size() > 0) {
        appendAvailableKeys(
                chars, ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, derivedRequestKeys);
    }
    if (derivedResultKeys.size() > 0) {
        appendAvailableKeys(
                chars, ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, derivedResultKeys);
    }
    return;
}

// Meizu MX6 (m95) rear camera, IMX386: the MTK vendor static-metadata table
// for this sensor omits RAW stream configurations entirely, which blocks RAW
// capture and any client (e.g. Google Camera HDR+) that requires the maximum
// RAW size to match the pixel array / active array size. The kernel sensor
// driver's capture mode is exactly pixel-array sized (4032x3016), so
// advertise a RAW16 output of that size, keep the min-frame-duration and
// stall-duration tables paired with it, and make sure the RAW capability bit
// is set. Strictly a no-op unless this is the back camera with this exact
// sensor geometry; RAW16 rows of any other size are dropped, because an
// undersized RAW maximum is precisely the mismatch such clients reject.
// Second half of the same consistency problem, and the one that actually keeps
// Google Camera from starting on m95: the OTHER camera advertises the RAW
// capability while publishing no RAW stream configuration at all. A client that
// enumerates cameras and asks each one for a usable RAW format finds a camera
// claiming RAW and offering none, which is how "no HDR+ compatible raw format"
// is reached before any size is ever compared. Metadata that promises a
// capability it cannot serve is simply wrong, so drop the claim rather than the
// camera.
static void dropUnbackedRawCapability(CameraMetadata &chars) {
    const int STREAM_CONFIGURATION_SIZE = 4;
    camera_metadata_entry cfg = chars.find(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);
    if ((cfg.count % STREAM_CONFIGURATION_SIZE) != 0) {
        return;
    }
    for (size_t i = 0; i + STREAM_CONFIGURATION_SIZE <= cfg.count; i += STREAM_CONFIGURATION_SIZE) {
        const int32_t format = cfg.data.i32[i];
        if ((format == HAL_PIXEL_FORMAT_RAW16 || format == HAL_PIXEL_FORMAT_RAW10 ||
             format == HAL_PIXEL_FORMAT_RAW12 || format == HAL_PIXEL_FORMAT_RAW_OPAQUE) &&
            cfg.data.i32[i + 3] == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT) {
            return;   // a real RAW output exists; the claim is backed
        }
    }

    camera_metadata_entry caps = chars.find(ANDROID_REQUEST_AVAILABLE_CAPABILITIES);
    if (caps.count == 0) {
        return;
    }
    std::vector<uint8_t> kept;
    kept.reserve(caps.count);
    bool removed = false;
    for (size_t i = 0; i < caps.count; i++) {
        if (caps.data.u8[i] == ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW) {
            removed = true;
            continue;
        }
        kept.push_back(caps.data.u8[i]);
    }
    if (removed) {
        ALOGI("%s: dropping unbacked RAW capability (no RAW stream configuration)", __FUNCTION__);
        chars.update(ANDROID_REQUEST_AVAILABLE_CAPABILITIES, kept.data(), kept.size());
    }
}


// Meizu MX6 (m95): let Google Camera in.
//
// GCam refuses to start with "No reprocessing input sizes supported for
// ImageFormat: YUV_420_888" -- it asks the characteristics for the reprocessing
// input list and throws when it is empty. The vendor publishes no reprocessing
// at all here: maxNumInputStreams is zero and the capability list ends at
// BURST_CAPTURE.
//
// The vendor HAL3 layer does know about input streams -- it carries checks that
// exist only for them, such as refusing rotation on one -- so this is enabling
// something that is there rather than inventing it. It is still a claim the
// pipeline has to honour, so it is gated: persist.camera.reprocessing=0 turns it
// off without a reflash, and the very first thing to try if capture regresses.
//
// The vendor side of the same claim (the input row in the stream table and the
// format map that AppStreamMgr::checkStream validates against) is published by
// libm95_camera_metadata_raw, since that table belongs to the HAL, not here.
// Meizu MX6 (m95): offer the sensor's larger sizes as YUV as well as JPEG.
//
// Google Camera photographs through a YUV stream and picks the largest one the
// characteristics offer, so with the vendor's ceiling of 1920x1088 its pictures
// come out at about two megapixels while the stock application gets twelve. The
// same sensor plainly produces the larger frames -- the JPEG path does exactly
// that -- and the vendor's own stream check, once it stops refusing them, lets
// the request through.
//
// This is a claim about the pipeline rather than about metadata, so it is off by
// default and the first thing to switch off if capture misbehaves:
//
//   setprop persist.camera.big_yuv 1
//
// If the pipeline will not produce a frame at these sizes the configuration
// fails and the application falls back or reports an error; that is a visible
// outcome rather than a silent one.
static void offerLargeYuvSizes(int facing, CameraMetadata &chars) {
    if (!property_get_bool("persist.camera.big_yuv", false)) {
        return;
    }

    // Each module gets its own sizes and no one else's. Offering the rear
    // module's list to the front one made it upscale from 2592x1944 into a
    // 4096x3072 buffer: a larger file carrying no more detail than the sensor
    // gave, soft and noisy for the trouble. The front module gets exactly its
    // own pixel array instead, which is a size it really produces -- without it
    // Google Camera falls back to the vendor's 1920x1088 ceiling and takes two
    // megapixel selfies from a five megapixel sensor.
    static const int32_t kFrontSizes[][2] = {{2592, 1944}};
    static const int32_t kRearSizes[][2] = {{4096, 3072}, {3264, 2448}, {2560, 1920}};
    const int32_t (*sizes)[2] = (facing == CAMERA_FACING_BACK) ? kRearSizes : kFrontSizes;
    const size_t sizeCount = (facing == CAMERA_FACING_BACK)
            ? sizeof(kRearSizes) / sizeof(kRearSizes[0])
            : sizeof(kFrontSizes) / sizeof(kFrontSizes[0]);

    const int32_t kFormat = HAL_PIXEL_FORMAT_YCbCr_420_888;
    const int STREAM_CONFIGURATION_SIZE = 4;

    camera_metadata_entry configs = chars.find(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);
    if (configs.count == 0 || (configs.count % STREAM_CONFIGURATION_SIZE) != 0) {
        return;
    }
    std::vector<int32_t> updated(configs.data.i32, configs.data.i32 + configs.count);
    std::vector<int64_t> durations;
    camera_metadata_entry mins = chars.find(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS);
    durations.assign(mins.data.i64, mins.data.i64 + mins.count);

    for (size_t n = 0; n < sizeCount; n++) {
        const int32_t width = sizes[n][0];
        const int32_t height = sizes[n][1];
        bool present = false;
        for (size_t i = 0; i + 3 < updated.size(); i += STREAM_CONFIGURATION_SIZE) {
            if (updated[i] == kFormat && updated[i + 1] == width && updated[i + 2] == height) {
                present = true;
                break;
            }
        }
        if (present) {
            continue;
        }
        updated.push_back(kFormat);
        updated.push_back(width);
        updated.push_back(height);
        updated.push_back(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT);
        // The vendor times its full-array JPEG row at fifteen frames a second;
        // a YUV frame of the same size cannot be quicker.
        durations.push_back(kFormat);
        durations.push_back(width);
        durations.push_back(height);
        // Published at thirty frames a second rather than the fifteen this
        // sensor really sustains at these sizes, and the reason is visibility
        // rather than optimism: the framework hides anything slower than twenty
        // from getOutputSizes and offers it only through
        // getHighResolutionOutputSizes. Google Camera asks the first question
        // only, so a truthful figure here means it never sees these sizes --
        // measured, it went on choosing 1920x1088 while 4096x3072 sat in the
        // table unused.
        durations.push_back(33333333LL);
    }

    chars.update(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, updated.data(), updated.size());
    if (!durations.empty()) {
        chars.update(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS, durations.data(),
                     durations.size());
    }
}

static void enableYuvReprocessing(CameraMetadata &chars) {
    // Off by default, and the default is the measurement's verdict rather than
    // caution. Both forms were declared and then asked to perform: the YUV one
    // configures a session, accepts the input frame, and takes the provider down
    // with error 4 when the reprocess request is submitted; the private one
    // configures, allocates its opaque ZSL buffers -- the HAL clearly knows that
    // path, it logs HW_CAMERA_ZSL usage and CAMERA_OPAQUE format -- and then
    // never returns a frame, until Camera3-Device gives up with "Error waiting
    // for HAL to drain: Connection timed out".
    //
    // A capability that configures and then fails to execute is worse than an
    // absent one: applications believe it. Set persist.camera.reprocessing=1 to
    // put the declaration back for further work on it -- tools/reproc-probe
    // measures both paths and names which step failed.
    if (!property_get_bool("persist.camera.reprocessing", false)) {
        return;
    }

    const int32_t kInputFormat = HAL_PIXEL_FORMAT_YCbCr_420_888;
    const int32_t kInputWidth = 1920;
    const int32_t kInputHeight = 1088;

    // The input row may already have arrived through the vendor tags; adding it
    // twice would leave the framework with a duplicate configuration.
    camera_metadata_entry configs =
            chars.find(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);
    bool haveInput = false;
    for (size_t i = 0; i + 3 < configs.count; i += 4) {
        if (configs.data.i32[i + 3] == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_INPUT) {
            haveInput = true;
            break;
        }
    }
    if (!haveInput) {
        std::vector<int32_t> updated(configs.data.i32, configs.data.i32 + configs.count);
        updated.push_back(kInputFormat);
        updated.push_back(kInputWidth);
        updated.push_back(kInputHeight);
        updated.push_back(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_INPUT);
        updated.push_back(HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED);
        updated.push_back(kInputWidth);
        updated.push_back(kInputHeight);
        updated.push_back(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_INPUT);
        chars.update(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, updated.data(),
                     updated.size());
    }

    if (chars.find(ANDROID_SCALER_AVAILABLE_INPUT_OUTPUT_FORMATS_MAP).count == 0) {
        // per input format: the format, how many outputs follow, then those outputs
        const int32_t map[] = {
                kInputFormat, 2, kInputFormat, HAL_PIXEL_FORMAT_BLOB,
                HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 2, kInputFormat,
                HAL_PIXEL_FORMAT_BLOB,
        };
        chars.update(ANDROID_SCALER_AVAILABLE_INPUT_OUTPUT_FORMATS_MAP, map, 8);
    }

    const int32_t inputStreams = 1;
    chars.update(ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS, &inputStreams, 1);

    // How many frames a reprocess request may stall the pipeline for. Four is
    // what MediaTek publishes on the platforms where it enables this at all.
    const int32_t maxCaptureStall = 4;
    chars.update(ANDROID_REPROCESS_MAX_CAPTURE_STALL, &maxCaptureStall, 1);

    // Both forms are declared, because which of them the pipeline can actually
    // execute is exactly what is being established: the YUV one configures and
    // then dies on the request, and MediaTek's own zero-shutter-lag path is the
    // private one.
    camera_metadata_entry caps = chars.find(ANDROID_REQUEST_AVAILABLE_CAPABILITIES);
    std::vector<uint8_t> capabilities(caps.data.u8, caps.data.u8 + caps.count);
    bool haveYuv = false;
    bool havePrivate = false;
    for (size_t i = 0; i < caps.count; i++) {
        haveYuv |= caps.data.u8[i] == ANDROID_REQUEST_AVAILABLE_CAPABILITIES_YUV_REPROCESSING;
        havePrivate |=
                caps.data.u8[i] == ANDROID_REQUEST_AVAILABLE_CAPABILITIES_PRIVATE_REPROCESSING;
    }
    if (!haveYuv) {
        capabilities.push_back(ANDROID_REQUEST_AVAILABLE_CAPABILITIES_YUV_REPROCESSING);
    }
    if (!havePrivate) {
        capabilities.push_back(ANDROID_REQUEST_AVAILABLE_CAPABILITIES_PRIVATE_REPROCESSING);
    }
    chars.update(ANDROID_REQUEST_AVAILABLE_CAPABILITIES, capabilities.data(),
                 capabilities.size());
}

void CameraModule::normalizeRawStreamConfigurations(int facing, CameraMetadata &chars) {
    // GCam builds HDR+ static metadata for every camera it enumerates and throws
    // "No HDR+ compatible raw format supported" on any module without a RAW
    // configuration, so the front camera -- which has none -- can take the app
    // down even though the back one is described correctly. Advertising RAW that a
    // module cannot actually produce is exactly what dropUnbackedRawCapability
    // exists to prevent, so this stays behind a property instead of being on by
    // default: setprop persist.camera.raw_front 1 and restart cameraserver to try
    // it. Judge the result by photographs from BOTH cameras, never by whether GCam
    // reaches its viewfinder.
    // The RAW row this function adds is NOT backed by the pipeline. Measured
    // 2026-08-18 with a headless Camera2 probe: configuring a single
    // RAW_SENSOR 4032x3016 stream fails, and the vendor says why in as many
    // words -- AppStreamMgr rejects "unsupported size w:4032 h:3016 for
    // format 32", exactly as it rejects the same size for JPEG. Advertising a
    // capability the device does not have is what dropUnbackedRawCapability
    // exists to prevent, and for a while this function was doing precisely
    // that, so the claim is off by default now. It was added to get libgcam
    // past its first barrier, and libgcam is blocked further along by a
    // reprocessing capability this pipeline also lacks, so nothing is lost.
    // setprop persist.camera.fake_raw 1 restores it for that experiment.
    // Two independent switches live below. The RAW claim is off by default because
    // the pipeline refuses a RAW stream at this size (measured); the native JPEG
    // size is a separate question with its own gate. An earlier version of this
    // guard returned outright and silently disabled the JPEG path too, which is
    // why 4000x3008 never appeared in the table.
    const bool wantFakeRaw = property_get_bool("persist.camera.fake_raw", false);
    const bool kNativeJpegOnly = !wantFakeRaw;
    if (!wantFakeRaw) {
        dropUnbackedRawCapability(chars);
    }

    const bool rawFront = property_get_bool("persist.camera.raw_front", false);
    if (facing != CAMERA_FACING_BACK && !rawFront) {
        // Not our sensor: nothing to add, but an unbacked RAW claim here is what
        // makes RAW-capable clients refuse the whole device, so clear it.
        dropUnbackedRawCapability(chars);
        return;
    }

    camera_metadata_entry entry = chars.find(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE);
    if (entry.count != 2) {
        return;
    }
    const int32_t rawWidth = entry.data.i32[0];
    const int32_t rawHeight = entry.data.i32[1];
    // Restrict to the known-good sensor (IMX386, kernel capture mode
    // 4032x3016); leave every other module's metadata untouched.
    // The size restriction guards the back module only: the front one is
    // described by whatever its own pixel array says, since the point there is
    // to have a RAW row at all rather than to match a known-good capture mode.
    if (facing == CAMERA_FACING_BACK && (rawWidth != 4032 || rawHeight != 3016)) {
        return;
    }

    const int STREAM_CONFIGURATION_SIZE = 4;
    entry = chars.find(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);
    if (entry.count == 0 || (entry.count % STREAM_CONFIGURATION_SIZE) != 0) {
        return;
    }

    // Rebuild the stream configuration list: keep exactly one RAW16 OUTPUT
    // row at full pixel-array size, drop RAW16 OUTPUT rows of any other size.
    static const int32_t kNativeJpegWidth = 4000;
    static const int32_t kNativeJpegHeight = 3008;
    bool hasFullRaw = false;
    bool hasNativeBlob = false;
    bool addedNativeBlob = false;
    bool configsChanged = false;
    Vector<int32_t> configs;
    configs.setCapacity(entry.count + STREAM_CONFIGURATION_SIZE);
    for (size_t i = 0; i < entry.count; i += STREAM_CONFIGURATION_SIZE) {
        int32_t format = entry.data.i32[i];
        int32_t width = entry.data.i32[i + 1];
        int32_t height = entry.data.i32[i + 2];
        int32_t isInput = entry.data.i32[i + 3];
        if (format == HAL_PIXEL_FORMAT_BLOB &&
                isInput == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT &&
                width == kNativeJpegWidth && height == kNativeJpegHeight) {
            hasNativeBlob = true;
        }
        // Only prune other RAW rows when we are actually substituting our own.
        // Until 2026-08-18 this ran unconditionally, so with the claim switched
        // off it still deleted whatever RAW sizes the vendor advertised natively
        // -- and since the pipeline refuses a RAW stream at the pixel-array size
        // we were substituting, that could have been discarding the only size it
        // would have accepted. Nobody would have seen it: the rows simply stopped
        // being published.
        if (!kNativeJpegOnly && format == HAL_PIXEL_FORMAT_RAW16 &&
                isInput == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT) {
            if (width == rawWidth && height == rawHeight && !hasFullRaw) {
                hasFullRaw = true;
            } else {
                configsChanged = true;
                continue;
            }
        }
        configs.push(format);
        configs.push(width);
        configs.push(height);
        configs.push(isInput);
    }
    if (!hasFullRaw && !kNativeJpegOnly) {
        configs.push(HAL_PIXEL_FORMAT_RAW16);
        configs.push(rawWidth);
        configs.push(rawHeight);
        configs.push(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT);
        configsChanged = true;
    }
    // The vendor table offers 4096x3072 as its largest JPEG, which is larger than
    // this sensor's 4032x3016 array and cannot be produced: selecting it yields no
    // file at all, silently (measured 2026-08-18). The sensor's own capture mode
    // is the pixel array, so the honest native size is missing from the list
    // entirely and the largest usable one is 3264x2448. Adding the array-sized
    // BLOB row is behind a property because it is a claim about what the pipeline
    // can encode, not merely about how it is described: setprop
    // persist.camera.native_jpeg 1, reboot, and judge it by whether a photograph
    // at that size actually appears.
    // The size to offer is 4000x3008, not the pixel array. FACT: a photograph
    // taken by the stock Flyme camera on this very handset is 4000x3008, so the
    // vendor JPEG encoder does produce twelve megapixels -- it simply refuses
    // 4032x3016, which is what earlier attempts asked for and what AppStreamMgr
    // rejected by name. The array size is the sensor's; this is the encoder's.
    if (property_get_bool("persist.camera.native_jpeg", false) && !hasNativeBlob) {
        configs.push(HAL_PIXEL_FORMAT_BLOB);
        configs.push(kNativeJpegWidth);
        configs.push(kNativeJpegHeight);
        configs.push(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT);
        configsChanged = true;
        addedNativeBlob = true;
    }
    if (configsChanged) {
        chars.update(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, configs);
    }

    // Frame duration for the RAW stream. This looks like a free choice and is
    // not: StreamConfigurationMap splits OUTPUT sizes into two buckets, and a
    // size whose minimum frame duration is slower than 20 fps goes into the
    // high-resolution bucket when the camera claims BURST_CAPTURE, as ours does.
    // getOutputSizes() never reports that bucket -- only
    // getHighResolutionOutputSizes() does -- so a "conservative" duration copied
    // from the slowest existing stream (66666666 ns, 15 fps) made the full-size
    // RAW configuration invisible to every client that asks the ordinary way,
    // libgcam among them. Staying at 30 fps is not an invented claim either: the
    // driver's capture mode for this sensor is 4032x3016 at 30. Keep this below
    // 50000000 or full-resolution RAW disappears again without any error to
    // explain it.
    const int64_t rawDuration = 33333333LL; // 30 fps; MUST stay under 50000000

    // Every stream configuration must have paired rows in both duration
    // tables or the framework rejects the device outright; mirror the RAW
    // edit into each. Entries are (format, width, height, duration) int64.
    const uint32_t durationTags[2] = {
            ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
            ANDROID_SCALER_AVAILABLE_STALL_DURATIONS,
    };
    for (size_t t = 0; t < 2; t++) {
        entry = chars.find(durationTags[t]);
        if ((entry.count % STREAM_CONFIGURATION_SIZE) != 0) {
            continue;
        }
        bool hasRawRow = false;
        bool tableChanged = false;
        Vector<int64_t> durations;
        durations.setCapacity(entry.count + STREAM_CONFIGURATION_SIZE);
        for (size_t i = 0; i < entry.count; i += STREAM_CONFIGURATION_SIZE) {
            int64_t format = entry.data.i64[i];
            int64_t width = entry.data.i64[i + 1];
            int64_t height = entry.data.i64[i + 2];
            int64_t duration = entry.data.i64[i + 3];
            if (!kNativeJpegOnly && format == HAL_PIXEL_FORMAT_RAW16) {
                if (width == rawWidth && height == rawHeight && !hasRawRow) {
                    hasRawRow = true;
                } else {
                    // Paired row of a dropped RAW configuration.
                    tableChanged = true;
                    continue;
                }
            }
            durations.push(format);
            durations.push(width);
            durations.push(height);
            durations.push(duration);
        }
        if (!hasRawRow) {
            if (addedNativeBlob) {
                durations.push((int64_t)HAL_PIXEL_FORMAT_BLOB);
                durations.push((int64_t)kNativeJpegWidth);
                durations.push((int64_t)kNativeJpegHeight);
                durations.push(rawDuration);
                tableChanged = true;
            }
            durations.push((int64_t)HAL_PIXEL_FORMAT_RAW16);
            durations.push((int64_t)rawWidth);
            durations.push((int64_t)rawHeight);
            durations.push(rawDuration);
            tableChanged = true;
        }
        if (tableChanged) {
            chars.update(durationTags[t], durations);
        }
    }

    // A RAW stream configuration is only honored by clients when the RAW
    // capability is advertised as well.
    entry = chars.find(ANDROID_REQUEST_AVAILABLE_CAPABILITIES);
    bool hasRawCap = false;
    for (size_t i = 0; i < entry.count; i++) {
        if (entry.data.u8[i] == ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW) {
            hasRawCap = true;
            break;
        }
    }
    if (!hasRawCap) {
        Vector<uint8_t> capabilities;
        capabilities.setCapacity(entry.count + 1);
        capabilities.appendArray(entry.data.u8, entry.count);
        capabilities.push(ANDROID_REQUEST_AVAILABLE_CAPABILITIES_RAW);
        chars.update(ANDROID_REQUEST_AVAILABLE_CAPABILITIES, capabilities);
    }
}

void CameraModule::appendAvailableKeys(CameraMetadata &chars,
        int32_t keyTag, const Vector<int32_t>& appendKeys) {
    camera_metadata_entry entry = chars.find(keyTag);
    Vector<int32_t> availableKeys;
    availableKeys.setCapacity(entry.count + appendKeys.size());
    for (size_t i = 0; i < entry.count; i++) {
        availableKeys.push(entry.data.i32[i]);
    }
    for (size_t i = 0; i < appendKeys.size(); i++) {
        availableKeys.push(appendKeys[i]);
    }
    chars.update(keyTag, availableKeys);
}

CameraModule::CameraModule(camera_module_t *module) : mNumberOfCameras(0) {
    if (module == NULL) {
        ALOGE("%s: camera hardware module must not be null", __FUNCTION__);
        assert(0);
    }
    mModule = module;
}

CameraModule::~CameraModule()
{
    while (mCameraInfoMap.size() > 0) {
        camera_info cameraInfo = mCameraInfoMap.editValueAt(0);
        if (cameraInfo.static_camera_characteristics != NULL) {
            free_camera_metadata(
                    const_cast<camera_metadata_t*>(cameraInfo.static_camera_characteristics));
        }
        mCameraInfoMap.removeItemsAt(0);
    }

    while (mPhysicalCameraInfoMap.size() > 0) {
        camera_metadata_t* metadata = mPhysicalCameraInfoMap.editValueAt(0);
        if (metadata != NULL) {
            free_camera_metadata(metadata);
        }
        mPhysicalCameraInfoMap.removeItemsAt(0);
    }
}

int CameraModule::init() {
    ATRACE_CALL();
    int res = OK;
    if (getModuleApiVersion() >= CAMERA_MODULE_API_VERSION_2_4 &&
            mModule->init != NULL) {
        ATRACE_BEGIN("camera_module->init");
        res = mModule->init();
        ATRACE_END();
    }
    mNumberOfCameras = getNumberOfCameras();
    mCameraInfoMap.setCapacity(mNumberOfCameras);
    return res;
}

int CameraModule::getCameraInfo(int cameraId, struct camera_info *info) {
    ATRACE_CALL();
    Mutex::Autolock lock(mCameraInfoLock);
    if (cameraId < 0) {
        ALOGE("%s: Invalid camera ID %d", __FUNCTION__, cameraId);
        return -EINVAL;
    }

    // Only override static_camera_characteristics for API2 devices
    int apiVersion = mModule->common.module_api_version;
    if (apiVersion < CAMERA_MODULE_API_VERSION_2_0) {
        int ret;
        ATRACE_BEGIN("camera_module->get_camera_info");
        ret = mModule->get_camera_info(cameraId, info);
        // Fill in this so CameraService won't be confused by
        // possibly 0 device_version
        info->device_version = CAMERA_DEVICE_API_VERSION_1_0;
        ATRACE_END();
        return ret;
    }

    ssize_t index = mCameraInfoMap.indexOfKey(cameraId);
    if (index == NAME_NOT_FOUND) {
        // Get camera info from raw module and cache it
        camera_info rawInfo, cameraInfo;
        ATRACE_BEGIN("camera_module->get_camera_info");
        int ret = mModule->get_camera_info(cameraId, &rawInfo);
        ATRACE_END();
        if (ret != 0) {
            return ret;
        }
        int deviceVersion = rawInfo.device_version;
        if (deviceVersion < CAMERA_DEVICE_API_VERSION_3_0) {
            // static_camera_characteristics is invalid
            *info = rawInfo;
            return ret;
        }
        CameraMetadata m;
        m.append(rawInfo.static_camera_characteristics);
        // Normalize before deriveCameraCharacteristicsKeys so that, on
        // device versions < 3.4, the stock derivation sees the RAW rows and
        // fills the RAW-dependent keys (e.g. postRawSensitivityBoost range).
        normalizeRawStreamConfigurations(rawInfo.facing, m);
        enableYuvReprocessing(m);
        offerLargeYuvSizes(rawInfo.facing, m);
        deriveCameraCharacteristicsKeys(rawInfo.device_version, m);
        cameraInfo = rawInfo;
        cameraInfo.static_camera_characteristics = m.release();
        index = mCameraInfoMap.add(cameraId, cameraInfo);
    }

    assert(index != NAME_NOT_FOUND);
    // return the cached camera info
    *info = mCameraInfoMap[index];
    return OK;
}

int CameraModule::getPhysicalCameraInfo(int physicalCameraId, camera_metadata_t **physicalInfo) {
    ATRACE_CALL();
    Mutex::Autolock lock(mCameraInfoLock);
    if (physicalCameraId < mNumberOfCameras) {
        ALOGE("%s: Invalid physical camera ID %d", __FUNCTION__, physicalCameraId);
        return -EINVAL;
    }

    // Only query physical camera info for 2.5 version for newer
    int apiVersion = mModule->common.module_api_version;
    if (apiVersion < CAMERA_MODULE_API_VERSION_2_5) {
        ALOGE("%s: Module version must be at least 2.5 to handle getPhysicalCameraInfo",
                __FUNCTION__);
        return -ENODEV;
    }
    if (mModule->get_physical_camera_info == nullptr) {
        ALOGE("%s: get_physical_camera is NULL for module version 2.5", __FUNCTION__);
        return -EINVAL;
    }

    ssize_t index = mPhysicalCameraInfoMap.indexOfKey(physicalCameraId);
    if (index == NAME_NOT_FOUND) {
        // Get physical camera characteristics, and cache it
        camera_metadata_t *info = nullptr;
        ATRACE_BEGIN("camera_module->get_physical_camera_info");
        int ret = mModule->get_physical_camera_info(physicalCameraId, &info);
        ATRACE_END();
        if (ret != 0) {
            return ret;
        }

        // The camera_metadata_t returned by get_physical_camera_info could be using
        // more memory than necessary due to unused reserved space. Reduce the
        // size by appending it to a new CameraMetadata object, which internally
        // calls resizeIfNeeded.
        CameraMetadata m;
        m.append(info);
        camera_metadata_t* derivedMetadata = m.release();
        index = mPhysicalCameraInfoMap.add(physicalCameraId, derivedMetadata);
    }

    assert(index != NAME_NOT_FOUND);
    *physicalInfo = mPhysicalCameraInfoMap[index];
    return OK;
}

int CameraModule::getDeviceVersion(int cameraId) {
    ssize_t index = mDeviceVersionMap.indexOfKey(cameraId);
    if (index == NAME_NOT_FOUND) {
        int deviceVersion;
        if (getModuleApiVersion() >= CAMERA_MODULE_API_VERSION_2_0) {
            struct camera_info info;
            getCameraInfo(cameraId, &info);
            deviceVersion = info.device_version;
        } else {
            deviceVersion = CAMERA_DEVICE_API_VERSION_1_0;
        }
        index = mDeviceVersionMap.add(cameraId, deviceVersion);
    }
    assert(index != NAME_NOT_FOUND);
    return mDeviceVersionMap[index];
}

int CameraModule::open(const char* id, struct hw_device_t** device) {
    int res;
    ATRACE_BEGIN("camera_module->open");
    res = filterOpenErrorCode(mModule->common.methods->open(&mModule->common, id, device));
    ATRACE_END();
    return res;
}

bool CameraModule::isOpenLegacyDefined() const {
    if (getModuleApiVersion() < CAMERA_MODULE_API_VERSION_2_3) {
        return false;
    }
    return mModule->open_legacy != NULL;
}

int CameraModule::openLegacy(
        const char* id, uint32_t halVersion, struct hw_device_t** device) {
    int res;
    ATRACE_BEGIN("camera_module->open_legacy");
    res = mModule->open_legacy(&mModule->common, id, halVersion, device);
    ATRACE_END();
    return res;
}

int CameraModule::getNumberOfCameras() {
    int numCameras;
    ATRACE_BEGIN("camera_module->get_number_of_cameras");
    numCameras = mModule->get_number_of_cameras();
    ATRACE_END();
    return numCameras;
}

int CameraModule::setCallbacks(const camera_module_callbacks_t *callbacks) {
    int res = OK;
    ATRACE_BEGIN("camera_module->set_callbacks");
    if (getModuleApiVersion() >= CAMERA_MODULE_API_VERSION_2_1) {
        res = mModule->set_callbacks(callbacks);
    }
    ATRACE_END();
    return res;
}

bool CameraModule::isVendorTagDefined() const {
    return mModule->get_vendor_tag_ops != NULL;
}

void CameraModule::getVendorTagOps(vendor_tag_ops_t* ops) {
    if (mModule->get_vendor_tag_ops) {
        ATRACE_BEGIN("camera_module->get_vendor_tag_ops");
        mModule->get_vendor_tag_ops(ops);
        ATRACE_END();
    }
}

bool CameraModule::isSetTorchModeSupported() const {
    if (getModuleApiVersion() >= CAMERA_MODULE_API_VERSION_2_4) {
        if (mModule->set_torch_mode == NULL) {
            ALOGE("%s: Module 2.4 device must support set torch API!",
                    __FUNCTION__);
            return false;
        }
        return true;
    }
    return false;
}

int CameraModule::setTorchMode(const char* camera_id, bool enable) {
    int res = INVALID_OPERATION;
    if (mModule->set_torch_mode != NULL) {
        ATRACE_BEGIN("camera_module->set_torch_mode");
        res = mModule->set_torch_mode(camera_id, enable);
        ATRACE_END();
    }
    return res;
}

int CameraModule::isStreamCombinationSupported(int cameraId, camera_stream_combination_t *streams) {
    int res = INVALID_OPERATION;
    if (mModule->is_stream_combination_supported != NULL) {
        ATRACE_BEGIN("camera_module->is_stream_combination_supported");
        res = mModule->is_stream_combination_supported(cameraId, streams);
        ATRACE_END();
    }
    return res;
}

void CameraModule::notifyDeviceStateChange(uint64_t deviceState) {
   if (getModuleApiVersion() >= CAMERA_MODULE_API_VERSION_2_5 &&
           mModule->notify_device_state_change != NULL) {
       ATRACE_BEGIN("camera_module->notify_device_state_change");
       ALOGI("%s: calling notify_device_state_change with state %" PRId64, __FUNCTION__,
               deviceState);
       mModule->notify_device_state_change(deviceState);
       ATRACE_END();
   }
}

bool CameraModule::isLogicalMultiCamera(
        const common::V1_0::helper::CameraMetadata& metadata,
        std::unordered_set<std::string>* physicalCameraIds) {
    if (physicalCameraIds == nullptr) {
        ALOGE("%s: physicalCameraIds must not be null", __FUNCTION__);
        return false;
    }

    bool isLogicalMultiCamera = false;
    camera_metadata_ro_entry_t capabilities =
            metadata.find(ANDROID_REQUEST_AVAILABLE_CAPABILITIES);
    for (size_t i = 0; i < capabilities.count; i++) {
        if (capabilities.data.u8[i] ==
                ANDROID_REQUEST_AVAILABLE_CAPABILITIES_LOGICAL_MULTI_CAMERA) {
            isLogicalMultiCamera = true;
            break;
        }
    }

    if (isLogicalMultiCamera) {
        camera_metadata_ro_entry_t entry =
                metadata.find(ANDROID_LOGICAL_MULTI_CAMERA_PHYSICAL_IDS);
        const uint8_t* ids = entry.data.u8;
        size_t start = 0;
        for (size_t i = 0; i < entry.count; ++i) {
            if (ids[i] == '\0') {
                if (start != i) {
                    const char* physicalId = reinterpret_cast<const char*>(ids+start);
                    physicalCameraIds->emplace(physicalId);
                }
                start = i + 1;
            }
        }
    }
    return isLogicalMultiCamera;
}

status_t CameraModule::filterOpenErrorCode(status_t err) {
    switch(err) {
        case NO_ERROR:
        case -EBUSY:
        case -EINVAL:
        case -EUSERS:
            return err;
        default:
            break;
    }
    return -ENODEV;
}

void CameraModule::removeCamera(int cameraId) {
    // Skip HAL1 devices which isn't cached in mCameraInfoMap and don't advertise
    // static_camera_characteristics
    if (getDeviceVersion(cameraId) >= CAMERA_DEVICE_API_VERSION_3_0) {
        std::unordered_set<std::string> physicalIds;
        camera_metadata_t *metadata = const_cast<camera_metadata_t*>(
                mCameraInfoMap.valueFor(cameraId).static_camera_characteristics);
        common::V1_0::helper::CameraMetadata hidlMetadata(metadata);

        if (isLogicalMultiCamera(hidlMetadata, &physicalIds)) {
            for (const auto& id : physicalIds) {
                int idInt = std::stoi(id);
                if (mPhysicalCameraInfoMap.indexOfKey(idInt) >= 0) {
                    free_camera_metadata(mPhysicalCameraInfoMap[idInt]);
                    mPhysicalCameraInfoMap.removeItem(idInt);
                } else {
                    ALOGE("%s: Cannot find corresponding static metadata for physical id %s",
                            __FUNCTION__, id.c_str());
                }
            }
        }
    }

    mCameraInfoMap.removeItem(cameraId);
    mDeviceVersionMap.removeItem(cameraId);
}

uint16_t CameraModule::getModuleApiVersion() const {
    return mModule->common.module_api_version;
}

const char* CameraModule::getModuleName() const {
    return mModule->common.name;
}

uint16_t CameraModule::getHalApiVersion() const {
    return mModule->common.hal_api_version;
}

const char* CameraModule::getModuleAuthor() const {
    return mModule->common.author;
}

void* CameraModule::getDso() {
    return mModule->common.dso;
}

} // namespace helper
} // namespace V1_0
} // namespace common
} // namespace camera
} // namespace hardware
} // namespace android
