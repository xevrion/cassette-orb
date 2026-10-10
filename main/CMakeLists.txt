idf_component_register(
    SRCS "app_main.cpp" "wifi.cpp" "timesync.cpp" "poller.cpp" "artdec.cpp"
         "../core/nowplaying.cpp" "../core/deck.cpp" "../core/scene.cpp"
    INCLUDE_DIRS "." "../core"
    REQUIRES roundkit waveshare__esp32_s3_touch_amoled_1_75c lvgl__lvgl espressif__esp_new_jpeg
             esp_wifi wpa_supplicant esp_netif esp_event esp_http_client esp-tls mbedtls nvs_flash esp_timer lwip
)

# idf.py build -DCASSETTE_DEMO=1 pretends a song is playing, to time the busiest frames.
if(CASSETTE_DEMO)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CASSETTE_DEMO=1)
endif()

# idf.py build -DCASSETTE_URL=http://host:8000/now-playing.json polls a local test server instead of the site.
if(CASSETTE_URL)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "CASSETTE_URL=\"${CASSETTE_URL}\"")
endif()

# idf.py build -DCASSETTE_TRACE=1 logs the largest free internal DMA block four times a second.
if(CASSETTE_TRACE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CASSETTE_TRACE=1)
endif()
