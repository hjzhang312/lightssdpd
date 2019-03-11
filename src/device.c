/**
 * @copyright (C) 2013 eVideo. All rights reserved.
 *
 * @file device.c 
 * @brief define device information
 *
 * @history
 *  2013-7-28 create by xuwf
 *  2013-8-5 fixed rtspport != 0 when http header hadn't rtspport field bugs. 
 *  
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef ENABLE_CONVERT_CODING
#include <iconv.h>
#include <uchardet/uchardet.h>
#endif

#include "device.h"
#include "device_private.h"

static void convert_encode_to_utf8(const char* src, char* dst, int len) {
#ifdef ENABLE_CONVERT_CODING
    /* detect encode */
    uchardet_t* det = uchardet_new();
    char encode[64] = {0};
    size_t inbyte = strlen(src);

    /* copy */
    strncpy(dst, src, len);

    /* detect code */
    if (det) {
        /* extend length to detect more precise */
        char buf[128] = {0};
        inbyte = 0;
        while (inbyte < 64) {
            snprintf(buf, sizeof(buf), "%s%s", buf, src);
            inbyte = strlen(buf);
        }
        uchardet_handle_data(det, buf, inbyte+1);
        uchardet_data_end(det);
        const char* p= uchardet_get_charset(det);
        if (p) {
            /* Note: should copy to buf*/
            strncpy(encode, p, sizeof(encode));
        }
        uchardet_delete(det);
    }

    /* iconv */
    if (encode[0]) {
        const char* pin = src;
        char* pout = dst;
        size_t outbyte = len;

        iconv_t cd = iconv_open("utf-8", encode);
        if (cd == (iconv_t)-1) {
            return;
        }
        iconv(cd, (char**)&pin, &inbyte, (char**)&pout, &outbyte);
        iconv_close(cd);
    }
#else 
    /* copy */
    strncpy(dst, src, len);
#endif
}

static char* device_type_strs[DEVICE_TYPE_ALL] = {
    "UNKNOWN",
    "IPC",
    "IHOME"
};

int device_type_valid(device_type_t type) {
    if (type > DEVICE_TYPE_UNKNOWN || type < DEVICE_TYPE_ALL) return 1;
    return 0;
}

const char* convert_type_to_string(device_type_t type) {
    if (type >= 0 && type < DEVICE_TYPE_ALL) {
        return device_type_strs[type]; 
    }

    return NULL;
}

device_type_t convert_string_to_type(const char* str) {
    int i = 0;
    if (!str) return DEVICE_TYPE_UNKNOWN;

    for (i = 0; i < DEVICE_TYPE_ALL; i++) {
        if (!strcasecmp(device_type_strs[i], str)) return i;
    }
    return DEVICE_TYPE_UNKNOWN;
}

device_t* ssdp_device_factory(device_type_t type, const char* name,  const char* ip, const char* mac) {
    size_t size = 0;
    switch (type) {
        case DEVICE_TYPE_IPC:
            size = sizeof(ipc_device_t);
            break;
        case DEVICE_TYPE_IHOME:
            size = sizeof(ihome_device_t);
            break;
        default:
            break;
    }

    if (size > 0) {
        device_t* dev = calloc(1, size);
        if (dev) {
            dev->type = type;
            if (name) { 
                convert_encode_to_utf8(name, dev->name, sizeof(dev->name));
            }
            if (ip) strncpy(dev->ip, ip, sizeof(dev->ip));
            if (mac) strncpy(dev->mac, mac, sizeof(dev->mac));
        }
        return dev;
    } 
    return NULL;
}

void ssdp_device_free(device_t* dev) {
    if (dev) free(dev);
}

