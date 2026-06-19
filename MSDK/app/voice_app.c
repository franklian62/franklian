/*!
    \file    voice_app.c
    \brief   Wi-Fi voice demo application entry.
*/

#include <stdio.h>
#include "app_cfg.h"

#ifdef CONFIG_VOICE_DEMO

#include "log_uart.h"
#include "voice_codec_es8311.h"
#include "voice_danger_key_sim.h"
#include "voice_prompt_player.h"
#include "voice_stream_client.h"
#include "voice_tone_test.h"
#include "voice_app.h"

void voice_app_start(void)
{
#ifdef CONFIG_VOICE_AI_AUDIO_BOARD
    int codec_ret = voice_codec_es8311_init();

#ifdef LOG_UART
    log_uart_init();
#endif

    if (codec_ret != 0) {
        printf("voice: AI audio board codec init failed\r\n");
    } else {
        printf("voice: AI audio board codec init done\r\n");
    }
#endif

#ifdef CONFIG_VOICE_TONE_TEST
    if (voice_tone_test_start() != 0) {
        printf("voice: tone test start failed\r\n");
    }
#endif

#ifdef CONFIG_VOICE_PROMPT_DANGER
    if (voice_prompt_player_start() != 0) {
        printf("voice: prompt player start failed\r\n");
    }
#endif

#ifdef CONFIG_VOICE_DANGER_KEY_SIM
    if (voice_danger_key_sim_start() != 0) {
        printf("voice: danger key simulator start failed\r\n");
    }
#endif

#ifdef CONFIG_VOICE_WIFI_STREAM
    printf("voice: demo start, PC server %s:%u, SSID %s\r\n",
           VOICE_SERVER_IP, VOICE_SERVER_PORT, VOICE_WIFI_SSID);

    if (voice_stream_client_start() != 0) {
        printf("voice: stream client start failed\r\n");
    }
#endif
}

#endif /* CONFIG_VOICE_DEMO */
