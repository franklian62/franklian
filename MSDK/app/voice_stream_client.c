/*!
    \file    voice_stream_client.c
    \brief   Connect to a PC TCP server and receive PCM16 mono audio.
*/

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include "app_cfg.h"

#ifdef CONFIG_VOICE_DEMO

#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "wrapper_os.h"
#include "wifi_management.h"
#include "wifi_netlink.h"
#include "voice_i2s_out.h"
#include "voice_stream_client.h"

#define VOICE_STREAM_TASK_STACK         2048
#define VOICE_STREAM_TASK_PRIO          OS_TASK_PRIORITY(1)
#define VOICE_RX_CHUNK                  1024
#define VOICE_HEADER_LEN                16
#define VOICE_HEADER_MAGIC0             'V'
#define VOICE_HEADER_MAGIC1             '5'
#define VOICE_HEADER_MAGIC2             '5'
#define VOICE_HEADER_MAGIC3             '3'
#define VOICE_PROTOCOL_VERSION          1
#define VOICE_RECONNECT_DELAY_MS        3000
#define VOICE_RECV_TIMEOUT_MS           5000
#define VOICE_STATUS_INTERVAL_MS        2000

static os_task_t voice_stream_task_handle = NULL;

static uint16_t voice_le16(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint32_t voice_le32(const uint8_t *buf)
{
    return (uint32_t)buf[0] |
           ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) |
           ((uint32_t)buf[3] << 24);
}

static int voice_recv_exact(int fd, uint8_t *buf, uint32_t len)
{
    uint32_t got = 0;

    while (got < len) {
        int ret = recv(fd, buf + got, len - got, 0);
        if (ret == 0) {
            return 0;
        }
        if (ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return -2;
            }
            return -1;
        }
        got += (uint32_t)ret;
    }

    return 1;
}

static int voice_wifi_connect(void)
{
    char *ssid = VOICE_WIFI_SSID;
    char *password = VOICE_WIFI_PASSWORD;
    struct mac_scan_result candidate;

    if (ssid == NULL || ssid[0] == '\0') {
        printf("voice: VOICE_WIFI_SSID is empty\r\n");
        return -1;
    }

    if (password != NULL && password[0] == '\0') {
        password = NULL;
    }

    printf("voice: scanning Wi-Fi SSID %s\r\n", ssid);
    if (wifi_management_scan(1, ssid) == 0) {
        sys_memset(&candidate, 0, sizeof(candidate));
        if (wifi_netlink_candidate_ap_find(WIFI_VIF_INDEX_DEFAULT, NULL, ssid, &candidate) != 0) {
            printf("voice: target SSID not found, trying connect anyway\r\n");
        }
    } else {
        printf("voice: Wi-Fi scan failed, trying connect anyway\r\n");
    }

    printf("voice: connecting Wi-Fi SSID %s\r\n", ssid);
    if (wifi_management_connect(ssid, password, 1) != 0) {
        printf("voice: Wi-Fi connection failed\r\n");
        return -1;
    }

    printf("voice: Wi-Fi connected\r\n");
    return 0;
}

static int voice_tcp_connect(void)
{
    int fd;
    int opt = 1;
    int recv_timeout = VOICE_RECV_TIMEOUT_MS;
    struct sockaddr_in server_addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("voice: socket create failed, errno=%d\r\n", errno);
        return -1;
    }

    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&recv_timeout, sizeof(recv_timeout));

    sys_memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_len = sizeof(server_addr);
    server_addr.sin_port = htons(VOICE_SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(VOICE_SERVER_IP);

    printf("voice: connecting TCP %s:%u\r\n", VOICE_SERVER_IP, VOICE_SERVER_PORT);
    if (connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        printf("voice: TCP connect failed, errno=%d\r\n", errno);
        close(fd);
        return -1;
    }

    printf("voice: TCP connected\r\n");
    return fd;
}

static int voice_handle_stream(int fd)
{
    uint8_t header[VOICE_HEADER_LEN];
    uint8_t rx_buf[VOICE_RX_CHUNK];
    uint32_t payload_len;
    uint32_t received;
    uint32_t last_status_tick;
    int ret;
    uint16_t version;
    uint16_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;

    ret = voice_recv_exact(fd, header, sizeof(header));
    if (ret <= 0) {
        return ret;
    }

    if (header[0] != VOICE_HEADER_MAGIC0 ||
        header[1] != VOICE_HEADER_MAGIC1 ||
        header[2] != VOICE_HEADER_MAGIC2 ||
        header[3] != VOICE_HEADER_MAGIC3) {
        printf("voice: bad stream magic\r\n");
        return -1;
    }

    version = voice_le16(&header[4]);
    sample_rate = voice_le16(&header[6]);
    channels = voice_le16(&header[8]);
    bits_per_sample = voice_le16(&header[10]);
    payload_len = voice_le32(&header[12]);

    printf("voice: stream header v%u, %u Hz, %u ch, %u bit, %lu bytes\r\n",
           version, sample_rate, channels, bits_per_sample, (unsigned long)payload_len);

    if (version != VOICE_PROTOCOL_VERSION ||
        sample_rate != 16000 ||
        channels != 1 ||
        bits_per_sample != 16 ||
        (payload_len & 1)) {
        printf("voice: unsupported stream format\r\n");
        return -1;
    }

    voice_i2s_out_reset();
    received = 0;
    last_status_tick = sys_current_time_get();

    while (received < payload_len) {
        uint32_t want = payload_len - received;
        if (want > sizeof(rx_buf)) {
            want = sizeof(rx_buf);
        }

        ret = voice_recv_exact(fd, rx_buf, want);
        if (ret <= 0) {
            printf("voice: stream receive ended, ret=%d errno=%d\r\n", ret, errno);
            return ret;
        }

        voice_i2s_out_push(rx_buf, want);
        received += want;

        if (sys_current_time_get() - last_status_tick >= VOICE_STATUS_INTERVAL_MS) {
            last_status_tick = sys_current_time_get();
            printf("voice: rx %lu/%lu, buffered=%lu, dropped=%lu, underrun=%lu\r\n",
                   (unsigned long)received,
                   (unsigned long)payload_len,
                   (unsigned long)voice_i2s_out_buffered(),
                   (unsigned long)voice_i2s_out_dropped(),
                   (unsigned long)voice_i2s_out_underruns());
        }
    }

    printf("voice: stream complete, buffered=%lu, dropped=%lu, underrun=%lu\r\n",
           (unsigned long)voice_i2s_out_buffered(),
           (unsigned long)voice_i2s_out_dropped(),
           (unsigned long)voice_i2s_out_underruns());

    return 1;
}

static void voice_stream_task(void *param)
{
    int fd;

    (void)param;

    if (voice_i2s_out_start() != 0) {
        printf("voice: i2s output start failed\r\n");
        voice_stream_task_handle = NULL;
        sys_task_delete(NULL);
    }

    while (1) {
        if (voice_wifi_connect() != 0) {
            sys_ms_sleep(VOICE_RECONNECT_DELAY_MS);
            continue;
        }

        while (1) {
            fd = voice_tcp_connect();
            if (fd < 0) {
                sys_ms_sleep(VOICE_RECONNECT_DELAY_MS);
                continue;
            }

            while (voice_handle_stream(fd) > 0) {
                ;
            }

            printf("voice: closing TCP socket\r\n");
            shutdown(fd, SHUT_RD);
            close(fd);
            sys_ms_sleep(VOICE_RECONNECT_DELAY_MS);
        }
    }
}

int voice_stream_client_start(void)
{
    if (voice_stream_task_handle != NULL) {
        return 0;
    }

    voice_stream_task_handle = sys_task_create_dynamic((const uint8_t *)"voice_stream",
            VOICE_STREAM_TASK_STACK, VOICE_STREAM_TASK_PRIO, voice_stream_task, NULL);
    if (voice_stream_task_handle == NULL) {
        printf("voice: create stream task failed\r\n");
        return -1;
    }

    return 0;
}

#endif /* CONFIG_VOICE_DEMO */
