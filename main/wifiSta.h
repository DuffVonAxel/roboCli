#ifndef WIFISTA_H
#define WIFISTA_H

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "config.h"

/* RTOS */
#define CONFIG_FREERTOS_HZ 100									// Definicao da Espressif. Escala de tempo base (vTaskDelay).

/* WiFi */
#define CONFIG_ESP_WIFI_SSID            "RoboV1R1"              // SSID do AP.
#define CONFIG_ESP_WIFI_PASSWORD        "abcdefgh"              // Senha do AP.

// Pelo SDKCONFIG
#define CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM       10         // Define quantas pilhas estaticas (min).
#define CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM      32         // Define quantas pilhas dinamicas (max).
#define CONFIG_ESP32_WIFI_TX_BUFFER_TYPE             1          // Define o tipo de Buffer da Tx do WiFi.
#define CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK                       // Define a seguranca da conexao.
#define ESP_MAXIMUM_RETRY                            5          // Numero de tentativas de conexao.

#define MAX_HTTP_RECV_BUFFER 512                                // Tamanho (em bytes) do buffer de entrada.
#define MAX_HTTP_OUTPUT_BUFFER 2048                             // Tamanho (em bytes) do buffer de saida.

#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK

/* Grupo de eventos do FreeRTOS para sinalizar quando estamos conectados. */
static EventGroupHandle_t s_wifi_event_group;

/* O grupo de eventos permite varios bits para cada evento, mas nos preocupamos apenas com dois eventos:
 * - estamos conectados ao AP com um IP.
 * - falhamos ao conectar apos a quantidade maxima de tentativas. */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "ESP_Cliente";

static int s_retry_num = 0;

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
    {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        if (s_retry_num < ESP_MAXIMUM_RETRY) 
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Tentando conexao no AP.");
        } else 
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"Falha conexao no AP.");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Peguei IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    // ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = 
    {
        .sta = 
        {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
            
            /*  Definir uma senha implica que a estacao se conectara a todos os modos de seguranca, incluindo WEP/WPA.
                No entanto, esses modos sao obsoletos e nao sao aconselhaveis de serem usados. Caso seu ponto de acesso 
                nao suporte WPA2, esse modo pode ser ativado comentando a linha abaixo.*/

	     .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
	     .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finalizado.");
    /* Aguardando ate que a conexao seja estabelecida (WIFI_CONNECTED_BIT) ou falha na conexao pelo numero maximo 
    de novas tentativas (WIFI_FAIL_BIT). Os bits sao definidos por event_handler() (veja acima). */

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    /* xEventGroupWaitBits() retorna os bits antes do retorno da chamada, portanto, podemos testar qual evento realmente aconteceu. */
    if (bits & WIFI_CONNECTED_BIT) 
    {
        ESP_LOGI(TAG, "Conectado ao AP SSID:%s Senha:%s", CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
    } else if (bits & WIFI_FAIL_BIT) 
    {
        ESP_LOGI(TAG, "Falhou ao conectar SSID:%s, Senha:%s", CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
    } else 
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* O evento nao sera processado apos o cancelamento do registro. */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

void nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
    {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
}

esp_err_t _http_event_handle(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "Erro.");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "Conectado");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "Enviando.");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "Enviado.");
            printf("%.*s", evt->data_len, (char*)evt->data);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "Dados, tamanho=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) 
            {
                printf("%.*s", evt->data_len, (char*)evt->data);
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "Finalizado.");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Desconectado.");
            break;
    }
    return ESP_OK;
}

static void roboAcao(char tipoQuery[], char valor[])
{
    unsigned char cnt;                      // Variavel para o laco de limpeza.
    char texto[40];                         // Varial com a string completa (URL+URL+QUERY+DADOS).
    for(cnt=0;cnt<40;cnt++) {texto[cnt]=0;} // Laco de limpeza da string.
    strcat(texto,roboURL);                  // Adiciona a URL.
    strcat(texto,roboURI);                  // Adiciona a URI.
    strcat(texto,"?");                      // Adiciona Separador.
    strcat(texto,tipoQuery);                // Adiciona o tipo da QUERY.
    strcat(texto,"=");                      // Adiciona o equate.
    strcat(texto,valor);                    // Adiciona o valor.
    ESP_LOGI(TAG,"Valor: %s", texto);       // Envia ao LOG.
    
    esp_http_client_config_t config = 
    {
        .url = texto,
        .event_handler = _http_event_handle,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if(err == ESP_OK) 
    {
    ESP_LOGI(TAG, "Status= %d, Tamanho= %d", esp_http_client_get_status_code(client), esp_http_client_get_content_length(client));
    }
    esp_http_client_cleanup(client);
}

#endif

/* Links
https://blog.csdn.net/chen244798611/article/details/97187326
https://github.com/SIMS-IOT-Devices/FreeRTOS-ESP-IDF-HTTP-Client
https://docs.espressif.com/projects/esp-idf/en/v4.3/esp32/api-reference/protocols/esp_http_client.html
*/
