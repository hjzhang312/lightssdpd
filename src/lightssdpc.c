/**
 * @copyright (C) 2013 eVideo. All rights reserved.
 *
 * @file lightssdpc.c
 * @brief ssdp client interface implement
 *
 * @history
 *  2013-07-29 create by xuwf
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include "config.h"
#include "ssdp.h"
#include "ssdp_packet.h"
#include "lightssdpc.h"
#include "list.h"
#include "device.h"
#include "device_private.h"

/* search result */
struct search_result {
    list_t* list;
    pthread_mutex_t lock;
    device_type_t search_type;
};
typedef struct search_result search_result_t;

/* return 1 if match, otherwise return 0 */
static int device_match(void* a, void* b) {
    device_t* dev = (device_t* )b;
    char* mac = (char* )a;

    if (!a || !b) return 0;

    return !strcasecmp(dev->mac, mac);
}

static search_result_t* search_result_alloc(void) {
    search_result_t* result = malloc(sizeof(search_result_t));
    assert(result);

    /* list */
    result->list = list_new();
    assert(result->list);

    result->list->match = device_match;

    /* Note: didn't set free callback */

    /* mutex */
    pthread_mutex_init(&result->lock, NULL);
    return result;
}

static void search_result_free(search_result_t* result) {
    if (result) {
        if (result->list) {
            list_destroy(result->list);
            result->list = NULL;
        }
        pthread_mutex_destroy(&result->lock);
    }
}

/* ssdp client process callback */
static void ssdpc_process(ssdp_packet_t* pkt) {
    http_method_t method = pkt->parser.method;
    search_result_t* result = pkt->extra_data;
    int response = pkt->parser.type;
    device_type_t device_type \
        = convert_string_to_type(ssdp_packet_header_find(pkt, HDR_FIELD_DEVICE_TYPE));
    device_type_t search_type = result->search_type;

    /* ignore unknown device type */
    if (!device_type_valid(device_type)) return;

    if (method == HTTP_NOTIFY || response == HTTP_RESPONSE) {
        if (search_type == DEVICE_TYPE_ALL || search_type == device_type) {
            const char* ip = ssdp_packet_header_find(pkt, HDR_FIELD_IPADDR);
            const char* mac = ssdp_packet_header_find(pkt, HDR_FIELD_MAC);
            const char* name = ssdp_packet_header_find(pkt, HDR_FIELD_DNAME);

            /* avoid other thread push */
            pthread_mutex_lock(&result->lock);

            if (!list_find(result->list, (void*)mac)) { /* update */
                device_t* dev = ssdp_device_factory(device_type, name, ip, mac);
                if (device_type == DEVICE_TYPE_IPC) {
                    const char* rtsp_port = ssdp_packet_header_find(pkt, HDR_FIELD_RTSP_PORT);
                    if(rtsp_port) ((ipc_device_t*)dev)->rtsp_port = atoi(rtsp_port);
                }

                if (dev && result->list) {
                    list_node_t* node = list_node_new(dev);
                    list_rpush(result->list, node);
                }
            }

            pthread_mutex_unlock(&result->lock);
        }
    }
}

static void ssdpc_send_msearch(int sock) {
    char buf[1024] =  {0};
    snprintf(buf, sizeof(buf), 
                "M-SEARCH * HTTP/1.1\r\n"
                "ST:UPnP:rootdevice\r\n"
                "MX:3\r\n"
                "Man:ssdp:discover\r\n"
                "HOST:%s:%d\r\n"
                "\r\n", SSDP_MULTICAST_ADDR, SSDP_PORT);
    

    int len = strlen(buf);
    struct sockaddr_in dst_addr;
    memset(&dst_addr, 0, sizeof(dst_addr));
    dst_addr.sin_family = AF_INET;
    dst_addr.sin_port = htons(SSDP_PORT);
    dst_addr.sin_addr.s_addr = inet_addr(SSDP_MULTICAST_ADDR);

    ssize_t bytes = sendto(sock, buf, len, 0, (struct sockaddr* )&dst_addr, 
            sizeof(struct sockaddr_in));

    if (bytes != len) {
        printf("msearch error\n");
    }
}

ssdp_result_t* ssdp_search_device(device_type_t type, long timeout) {
    search_result_t* search_result = NULL; 
    ssdp_result_t* ssdp_result = NULL;
    int i = 0;
        
    /* Socket */
    int ssdp_sock = ssdp_socket_init();
    if (ssdp_sock < 0) {
        perror("ssdp socket init fail\n");
        return NULL;
    }

    /* Send M-Search request */
    ssdpc_send_msearch(ssdp_sock);
    ssdpc_send_msearch(ssdp_sock);

    search_result = search_result_alloc();
    search_result->search_type = type;

    /* Recieve loop */
    ssdp_receive_loop(ssdp_sock, ssdpc_process, timeout, (void*)search_result);

    /* Process result */
    int count = search_result->list ? search_result->list->len: 0;

    ssdp_result = calloc(1, sizeof(ssdp_result_t)+count*sizeof(device_t*));
    assert(ssdp_result);
    
    ssdp_result->count = count;
    for (i = 0; i < count; i++) {
        ssdp_result->devices[i] = (list_at(search_result->list, i))->val; 
    }

    /* Free resource */
    search_result_free(search_result);
    ssdp_socket_deinit(ssdp_sock);

    return ssdp_result;
}

void ssdp_result_free(ssdp_result_t* result) {
    int i = 0;

    if (result) {
        for (i = 0; i < result->count; i++) {
            ssdp_device_free(result->devices[i]);
            result->devices[i] = NULL;
        }
        free(result);
    }
}

