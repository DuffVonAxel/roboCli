#include "wifiSta.h"

void app_main(void)
{
    nvs_init();
    wifi_init_sta();
    ESP_LOGI(TAG, "Modo STA.");

    roboAcao("mover","-10");
    vTaskDelay(200);
    roboAcao("girar","30");

    while (1)
    {
        vTaskDelay(10);
    }
}
