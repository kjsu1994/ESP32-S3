#ifndef ESP32S3_CONFIG_H
#define ESP32S3_CONFIG_H

#include "home_iot_tls.h"

#if __has_include("config.local.h")
#include "config.local.h"
#else
#include "config.example.h"
#endif

#ifdef BOARD_HAS_CAMERA
#include "boards/esp32s3_camera_ov5640.h"
#else
#include "boards/esp32s3_devkitc.h"
#endif

#include "config.defaults.h"

#ifdef BOARD_HAS_CAMERA
#if !defined(CAMERA_PIN_XCLK) || !defined(CAMERA_PIN_SIOD) || !defined(CAMERA_PIN_SIOC) \
        || !defined(CAMERA_PIN_Y2) || !defined(CAMERA_PIN_Y3) || !defined(CAMERA_PIN_Y4) \
        || !defined(CAMERA_PIN_Y5) || !defined(CAMERA_PIN_Y6) || !defined(CAMERA_PIN_Y7) \
        || !defined(CAMERA_PIN_Y8) || !defined(CAMERA_PIN_Y9) || !defined(CAMERA_PIN_VSYNC) \
        || !defined(CAMERA_PIN_HREF) || !defined(CAMERA_PIN_PCLK)
#error "BOARD_HAS_CAMERA requires a camera board pin map. Include a camera board profile or define CAMERA_PIN_* values."
#endif
#endif

#endif
