#ifndef ESP32S3_BOARD_CAMERA_OV5640_H
#define ESP32S3_BOARD_CAMERA_OV5640_H

#include "esp32s3_devkitc.h"

#ifndef CAMERA_SENSOR_NAME
#define CAMERA_SENSOR_NAME "OV5640"
#endif

#ifndef CAMERA_EXPECTED_SENSOR_PID
#define CAMERA_EXPECTED_SENSOR_PID 0x5640
#endif

#ifndef CAMERA_XCLK_FREQ_HZ
#define CAMERA_XCLK_FREQ_HZ 14000000
#endif

#ifndef CAMERA_PIN_PWDN
#define CAMERA_PIN_PWDN -1
#endif

#ifndef CAMERA_PIN_RESET
#define CAMERA_PIN_RESET -1
#endif

#ifndef CAMERA_PIN_XCLK
#define CAMERA_PIN_XCLK 15
#endif

#ifndef CAMERA_PIN_SIOD
#define CAMERA_PIN_SIOD 4
#endif

#ifndef CAMERA_PIN_SIOC
#define CAMERA_PIN_SIOC 5
#endif

#ifndef CAMERA_PIN_Y9
#define CAMERA_PIN_Y9 16
#endif

#ifndef CAMERA_PIN_Y8
#define CAMERA_PIN_Y8 17
#endif

#ifndef CAMERA_PIN_Y7
#define CAMERA_PIN_Y7 18
#endif

#ifndef CAMERA_PIN_Y6
#define CAMERA_PIN_Y6 12
#endif

#ifndef CAMERA_PIN_Y5
#define CAMERA_PIN_Y5 10
#endif

#ifndef CAMERA_PIN_Y4
#define CAMERA_PIN_Y4 8
#endif

#ifndef CAMERA_PIN_Y3
#define CAMERA_PIN_Y3 9
#endif

#ifndef CAMERA_PIN_Y2
#define CAMERA_PIN_Y2 11
#endif

#ifndef CAMERA_PIN_VSYNC
#define CAMERA_PIN_VSYNC 6
#endif

#ifndef CAMERA_PIN_HREF
#define CAMERA_PIN_HREF 7
#endif

#ifndef CAMERA_PIN_PCLK
#define CAMERA_PIN_PCLK 13
#endif

#endif
