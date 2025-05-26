/*
 * Copyright (C) 2025 Inspur Group Co., Ltd. Unpublished
 *
 * Inspur Group Co., Ltd.
 * Proprietary & Confidential
 *
 * This source code and the algorithms implemented therein constitute
 * confidential information and may comprise trade secrets of Inspur
 * or its associates, and any use thereof is subject to the terms and
 * conditions of the Non-Disclosure Agreement pursuant to which this
 * source code was originally received.
 */

#ifndef PLATFORM_H
#define PLATFORM_H
#include <stddef.h>

typedef enum {
    PROPERTY_SERIAL = 0,
    PROPERTY_MACADDR,
    PROPERTY_DEVICEID,
    PROPERTY_DEVICE_SECRET,
    _PROPERTY_MAX
} PROPERTY_e;

// client should implement interface
int platform_get_property(PROPERTY_e which, char* data, int len);
int platform_set_property(PROPERTY_e which, char* data);
#endif // PLATFORM_H
