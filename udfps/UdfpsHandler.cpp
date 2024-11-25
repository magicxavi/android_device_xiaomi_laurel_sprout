/*
 * Copyright (C) 2022 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "UdfpsHander.laurel_sprout"

#include "UdfpsHandler.h"

#include <android-base/logging.h>
#include <fcntl.h>
#include <poll.h>
#include <thread>
#include <unistd.h>
#include <android-base/strings.h>
#include <cutils/properties.h>
#include <hardware/hardware.h>
#include <inttypes.h>
#include <fstream>

#define COMMAND_NIT 10
#define PARAM_NIT_FOD 1
#define PARAM_NIT_NONE 0

#define UDFPS_STATUS_PATH "/sys/class/touch/tp_dev/fod_status"
#define UDFPS_STATUS_ON 1
#define UDFPS_STATUS_OFF 0

template <typename T>
static void set(const std::string& path, const T& value) {
    std::ofstream file(path);
    file << value;
}

static const char* kFodUiPaths[] = {
        "/sys/devices/platform/soc/soc:qcom,dsi-display-primary/fod_ui",
        "/sys/devices/platform/soc/soc:qcom,dsi-display/fod_ui",
};

static bool readBool(int fd) {
    char c;
    int rc;

    rc = lseek(fd, 0, SEEK_SET);
    if (rc) {
        LOG(ERROR) << "failed to seek fd, err: " << rc;
        return false;
    }

    rc = read(fd, &c, sizeof(char));
    if (rc != 1) {
        LOG(ERROR) << "failed to read bool from fd, err: " << rc;
        return false;
    }

    return c != '0';
}

class LaurelSproutUdfpsHander : public UdfpsHandler {
  public:
    void init(fingerprint_device_t *device) {
        mDevice = device;

        std::thread([this]() {
            int fodUiFd;
            for (auto& path : kFodUiPaths) {
                fodUiFd = open(path, O_RDONLY);
                if (fodUiFd >= 0) {
                    break;
                }
            }

            if (fodUiFd < 0) {
                LOG(ERROR) << "failed to open fd, err: " << fodUiFd;
                return;
            }

            struct pollfd fodUiPoll = {
                    .fd = fodUiFd,
                    .events = POLLERR | POLLPRI,
                    .revents = 0,
            };

            while (true) {
                int rc = poll(&fodUiPoll, 1, -1);
                if (rc < 0) {
                    LOG(ERROR) << "failed to poll fd, err: " << rc;
                    continue;
                }

                bool fodUi = readBool(fodUiFd);

                mDevice->extCmd(mDevice, COMMAND_NIT, fodUi ? PARAM_NIT_FOD : PARAM_NIT_NONE);
                if (!fodUi) {
                    set(UDFPS_STATUS_PATH, UDFPS_STATUS_OFF);
                }
            }
        }).detach();
    }

    void onFingerDown(uint32_t /*x*/, uint32_t /*y*/, float /*minor*/, float /*major*/) {
        set(UDFPS_STATUS_PATH, UDFPS_STATUS_ON);
    }

    void onFingerUp() {
        // nothing
    }
    
    void onAcquired(int32_t result, int32_t vendorCode) {
        LOG(INFO) << __func__ << " result: " << result << " vendorCode: " << vendorCode;
        if (result == FINGERPRINT_ACQUIRED_GOOD) {
            set(UDFPS_STATUS_PATH, UDFPS_STATUS_OFF);
        } else if (vendorCode == 21 || vendorCode == 23) {
            /*
             * vendorCode = 21 waiting for fingerprint authentication
             * vendorCode = 23 waiting for fingerprint enroll
             */
            set(UDFPS_STATUS_PATH, UDFPS_STATUS_ON);
        }
    }

    void cancel() {
        set(UDFPS_STATUS_PATH, UDFPS_STATUS_OFF);
    }
  private:
    fingerprint_device_t *mDevice;
};

static UdfpsHandler* create() {
    return new LaurelSproutUdfpsHander();
}

static void destroy(UdfpsHandler* handler) {
    delete handler;
}

extern "C" UdfpsHandlerFactory UDFPS_HANDLER_FACTORY = {
    .create = create,
    .destroy = destroy,
};
