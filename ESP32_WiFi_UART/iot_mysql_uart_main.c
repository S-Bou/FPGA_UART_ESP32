/*WiFi manager to check DDBB state of some devices*/

#include <stdio.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_tls.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "string.h"
#include "datacon.h"

static const int RX_BUF_SIZE = 1024;
static const char* TAG = "MyModule";
static char* green_uart_state  = "";
static char* red_uart_state  = "";
int phplen = 0;
char phpinfo[200];
char status[3];
static bool green_state = false; 
static bool red_state = false; 
static const char *TX_GREEN_TASK_TAG = "TX_Green_task";
static const char *TX_RED_TASK_TAG = "TX_Red_task";

// ------------------------ GPIOs CONFIG ---------------------------------------------------------------------

#define ONBOARD_LED_GPIO GPIO_NUM_2
#define TXD_PIN GPIO_NUM_4
#define RXD_PIN GPIO_NUM_5

static uint8_t onboard_led_state = 0;

static void configure_gpios(void)
{
    ESP_LOGI(TAG, "Configuration of GPIOs");
    gpio_reset_pin(ONBOARD_LED_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(ONBOARD_LED_GPIO, GPIO_MODE_OUTPUT);
}

// ------------------------ WiFi CONNECTION -------------------------------------------------------------------

static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
        printf("WiFi connecting ... \n");
        break;
    case WIFI_EVENT_STA_CONNECTED:
        printf("WiFi connected ... \n");
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        printf("WiFi lost connection ... \n");
        onboard_led_state = 1; 
        gpio_set_level(ONBOARD_LED_GPIO, onboard_led_state); 
        break;
    case IP_EVENT_STA_GOT_IP:
        printf("WiFi got IP ... \n\n");
        break;
    default:
        break;
    }
}

void wifi_connection()
{
    // 1 - Wi-Fi/LwIP Init Phase
    esp_netif_init();                    // TCP/IP initiation 					s1.1
    esp_event_loop_create_default();     // event loop 			                s1.2
    esp_netif_create_default_wifi_sta(); // WiFi station 	                    s1.3
    wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_initiation); // 					                    s1.4
    // 2 - Wi-Fi Configuration Phase
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
    wifi_config_t wifi_configuration = {
        .sta = {
        .ssid = SSID,
        .password = PASS}};
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);
    // 3 - Wi-Fi Start Phase
    esp_wifi_start();
    // 4- Wi-Fi Connect Phase
    esp_wifi_connect();
}

// ------------------------ UART CONFIG -----------------------------------------------------------------------

void init_UART(void) {
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // We won't use a buffer for sending data.
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

int sendData(const char* logName, const char* data)
{
    const int len = strlen(data);
    const int txBytes = uart_write_bytes(UART_NUM_1, data, len);
    ESP_LOGI(logName, "Wrote %d bytes -> %s", txBytes, data);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    return txBytes;
}

static void tx_task(void *arg)
{
    esp_log_level_set(TX_GREEN_TASK_TAG, ESP_LOG_INFO);
    esp_log_level_set(TX_RED_TASK_TAG, ESP_LOG_INFO);

    while (1) {
        green_uart_state = green_state ? "A" : "a";
        sendData(TX_GREEN_TASK_TAG, green_uart_state);

        red_uart_state = red_state ? "B" : "b";
        sendData(TX_RED_TASK_TAG, red_uart_state);
    }
}

// ------------------------ GET EVENT -------------------------------------------------------------------------

esp_err_t client_event_get_handler(esp_http_client_event_handle_t evt)
{
    static char *output_buffer;
    static int output_len;

    switch (evt->event_id)
    {
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)){
                if (evt->user_data){
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                }else{
                    if(output_buffer == NULL){
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if(output_buffer == NULL){
                            ESP_LOGE(TAG, "Failled to alocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                    phplen = evt->data_len;
                    for (int8_t i = 0; i < evt->data_len; i++)
                    {
                        phpinfo[i] = output_buffer[i];
                    }
                }
                output_len += evt->data_len;
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL){
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;

        case HTTP_EVENT_DISCONNECTED:
            //ESP_LOGI(TAG, "HTTP_EVENT_DISCONECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0){
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            if (output_buffer != NULL){
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;

        default:
            break;
    }
    return ESP_OK;
}

static void find_dev_status(void){
    
    for (int8_t i=0; i < phplen; i++)
    {
        if(phpinfo[i] == '#' && phpinfo[i+1] == '0' && phpinfo[i+2] == '1' ){
            printf("Finded: %c%c%c -> ", phpinfo[i], phpinfo[i+1], phpinfo[i+2]);
            i = i+4;
            if(phpinfo[i] == 'n'){
                printf("Green = on\n");
                green_state = true;             

            }else if(phpinfo[i] == 'f'){
                printf("Green = off\n");   
                green_state = false;                                   
            }

        }else if(phpinfo[i] == '#' && phpinfo[i+1] == '0' && phpinfo[i+2] == '2' ){
            printf("Finded: %c%c%c -> ", phpinfo[i], phpinfo[i+1], phpinfo[i+2]);
            i = i+4;
            if(phpinfo[i] == 'n'){
                printf("Red = on\n");
                red_state = true;                     

            }else if(phpinfo[i] == 'f'){
                printf("Red = off\n");
                red_state = false;                                     
            }
        } 
    }
}

static void client_get_function()
{
    esp_http_client_config_t config_get = {
        .url = IP_FILE_PHP,
        .method = HTTP_METHOD_GET,
        .event_handler = client_event_get_handler
    };
        
    esp_http_client_handle_t client_get = esp_http_client_init(&config_get);

    printf("1 ...........\n");
    esp_http_client_perform(client_get);
    printf("2 ...........\n\n");
    esp_http_client_cleanup(client_get);

    find_dev_status();
}

// -------------------------- MAIN ------------------------------------------------------------------------

void app_main(void)
{
    configure_gpios();

    init_UART();
    xTaskCreate(tx_task, "uart_tx_task", 1024*2, NULL, configMAX_PRIORITIES-1, NULL);    

    nvs_flash_init();
    wifi_connection();

    vTaskDelay(3000 / portTICK_PERIOD_MS);
    printf("WIFI was initiated ...........\n\n");
    onboard_led_state = 0; 
    gpio_set_level(ONBOARD_LED_GPIO, onboard_led_state); 

    while(1){
        client_get_function();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
