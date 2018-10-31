/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "Sensors.h"

#include <android/hardware/sensors/2.0/types.h>
#include <log/log.h>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_0 {
namespace implementation {

using ::android::hardware::sensors::V1_0::Event;
using ::android::hardware::sensors::V1_0::OperationMode;
using ::android::hardware::sensors::V1_0::RateLevel;
using ::android::hardware::sensors::V1_0::Result;
using ::android::hardware::sensors::V1_0::SharedMemInfo;

Sensors::Sensors() : mEventQueueFlag(nullptr) {
    std::shared_ptr<AccelSensor> accel =
        std::make_shared<AccelSensor>(1 /* sensorHandle */, this /* callback */);
    mSensors[accel->getSensorInfo().sensorHandle] = accel;
}

Sensors::~Sensors() {
    deleteEventFlag();
}

// Methods from ::android::hardware::sensors::V2_0::ISensors follow.
Return<void> Sensors::getSensorsList(getSensorsList_cb _hidl_cb) {
    std::vector<SensorInfo> sensors;
    for (const auto& sensor : mSensors) {
        sensors.push_back(sensor.second->getSensorInfo());
    }

    // Call the HIDL callback with the SensorInfo
    _hidl_cb(sensors);

    return Void();
}

Return<Result> Sensors::setOperationMode(OperationMode /* mode */) {
    // TODO implement
    return Result{};
}

Return<Result> Sensors::activate(int32_t sensorHandle, bool enabled) {
    auto sensor = mSensors.find(sensorHandle);
    if (sensor != mSensors.end()) {
        sensor->second->activate(enabled);
        return Result::OK;
    }
    return Result::BAD_VALUE;
}

Return<Result> Sensors::initialize(
    const ::android::hardware::MQDescriptorSync<Event>& eventQueueDescriptor,
    const ::android::hardware::MQDescriptorSync<uint32_t>& wakeLockDescriptor,
    const sp<ISensorsCallback>& sensorsCallback) {
    Result result = Result::OK;

    // Save a reference to the callback
    mCallback = sensorsCallback;

    // Create the Event FMQ from the eventQueueDescriptor. Reset the read/write positions.
    mEventQueue =
        std::make_unique<EventMessageQueue>(eventQueueDescriptor, true /* resetPointers */);

    // Ensure that any existing EventFlag is properly deleted
    deleteEventFlag();

    // Create the EventFlag that is used to signal to the framework that sensor events have been
    // written to the Event FMQ
    if (EventFlag::createEventFlag(mEventQueue->getEventFlagWord(), &mEventQueueFlag) != OK) {
        result = Result::BAD_VALUE;
    }

    // Create the Wake Lock FMQ that is used by the framework to communicate whenever WAKE_UP
    // events have been successfully read and handled by the framework.
    mWakeLockQueue =
        std::make_unique<WakeLockMessageQueue>(wakeLockDescriptor, true /* resetPointers */);

    if (!mCallback || !mEventQueue || !mWakeLockQueue || mEventQueueFlag == nullptr) {
        result = Result::BAD_VALUE;
    }

    return result;
}

Return<Result> Sensors::batch(int32_t sensorHandle, int64_t samplingPeriodNs,
                              int64_t /* maxReportLatencyNs */) {
    auto sensor = mSensors.find(sensorHandle);
    if (sensor != mSensors.end()) {
        sensor->second->batch(samplingPeriodNs);
        return Result::OK;
    }
    return Result::BAD_VALUE;
}

Return<Result> Sensors::flush(int32_t sensorHandle) {
    auto sensor = mSensors.find(sensorHandle);
    if (sensor != mSensors.end()) {
        return sensor->second->flush();
    }
    return Result::BAD_VALUE;
}

Return<Result> Sensors::injectSensorData(const Event& /* event */) {
    // TODO implement
    return Result{};
}

Return<void> Sensors::registerDirectChannel(const SharedMemInfo& /* mem */,
                                            registerDirectChannel_cb _hidl_cb) {
    _hidl_cb(Result::INVALID_OPERATION, 0 /* channelHandle */);
    return Return<void>();
}

Return<Result> Sensors::unregisterDirectChannel(int32_t /* channelHandle */) {
    return Result::INVALID_OPERATION;
}

Return<void> Sensors::configDirectReport(int32_t /* sensorHandle */, int32_t /* channelHandle */,
                                         RateLevel /* rate */, configDirectReport_cb _hidl_cb) {
    _hidl_cb(Result::INVALID_OPERATION, 0 /* reportToken */);
    return Return<void>();
}

void Sensors::postEvents(const std::vector<Event>& events) {
    std::lock_guard<std::mutex> l(mLock);

    // TODO: read events from the Wake Lock FMQ in the right place
    std::vector<uint32_t> tmp(mWakeLockQueue->availableToRead());
    mWakeLockQueue->read(tmp.data(), mWakeLockQueue->availableToRead());

    mEventQueue->write(events.data(), events.size());
    mEventQueueFlag->wake(static_cast<uint32_t>(EventQueueFlagBits::READ_AND_PROCESS));
}

void Sensors::deleteEventFlag() {
    status_t status = EventFlag::deleteEventFlag(&mEventQueueFlag);
    if (status != OK) {
        ALOGI("Failed to delete event flag: %d", status);
    }
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace sensors
}  // namespace hardware
}  // namespace android
