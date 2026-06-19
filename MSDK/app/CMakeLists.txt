
cmake_minimum_required(VERSION 3.15)


target_sources(${TARGET_EXE}
    PRIVATE
        atcmd.c
        ../ble/app/ble_init.c
        cmd_shell.c
        iperf.c
        main.c
        mqtt_app/mqtt_cmd.c
        ota_demo.c
        ping.c
        voice_app.c
        voice_codec_es8311.c
        voice_danger_key_sim.c
        voice_i2s_out.c
        voice_prompt_danger.c
        voice_prompt_player.c
        voice_stream_client.c
        voice_tone_test.c
    )


