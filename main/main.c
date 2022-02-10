/* HTTP File Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <sys/param.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_spiffs.h"
#include "esp_netif.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "sdkconfig.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "soc/soc_caps.h"
#if SOC_SDMMC_HOST_SUPPORTED
#include "driver/sdmmc_host.h"
#endif
#include "esp_timer.h"
#include <ds18x20.h>

/* This example demonstrates how to create file server
 * using esp_http_server. This file has only startup code.
 * Look in file_server.c for the implementation */

static const gpio_num_t SENSOR_GPIO = 21;
//static const uint32_t LOOP_DELAY_MS = 500;
#define MAX_SENSORS 8
//static const int RESCAN_INTERVAL = 8;

#include "TempSensorType.h"

TempSensorType _TempSensors[MAX_SENSORS];
//ds18x20_addr_t addrs[MAX_SENSORS];
//float temps[MAX_SENSORS];
size_t sensor_count = 0;

#define EXAMPLE_ESP_WIFI_CHANNEL 1
#define EXAMPLE_MAX_STA_CONN 4

#define MOUNT_POINT "/spiffs"
static const char *TAG="WStation";
/* ESP32-S2/C3 doesn't have an SD Host peripheral, always use SPI,
 * ESP32 can choose SPI or SDMMC Host, SPI is used by default: */

#ifndef CONFIG_EXAMPLE_USE_SDMMC_HOST
#define USE_SPI_MODE
#endif
// DMA channel to be used by the SPI peripheral
#if CONFIG_IDF_TARGET_ESP32
#define SPI_DMA_CHAN    1
// on ESP32-S2, DMA channel must be the same as host id
#elif CONFIG_IDF_TARGET_ESP32S2
#define SPI_DMA_CHAN    host.slot
#elif CONFIG_IDF_TARGET_ESP32C3
// on ESP32-C3, DMA channels are shared with all other peripherals
#define SPI_DMA_CHAN    1
#endif //CONFIG_IDF_TARGET_ESP32

// When testing SD and SPI modes, keep in mind that once the card has been
// initialized in SPI mode, it can not be reinitialized in SD mode without
// toggling power to the card.
#ifdef CONFIG_EXAMPLE_MOUNT_SD_CARD
static sdmmc_card_t* mount_card = NULL;
static char * mount_base_path = MOUNT_POINT;
#endif
#ifdef USE_SPI_MODE
// Pin mapping when using SPI mode.
// With this mapping, SD card can be used both in SPI and 1-line SD mode.
// Note that a pull-up on CS line is required in SD mode.
#if CONFIG_IDF_TARGET_ESP32C3
#define PIN_NUM_MISO 2
#define PIN_NUM_MOSI 7
#define PIN_NUM_CLK  6
#define PIN_NUM_CS   10
#else
#define PIN_NUM_MISO 2
#define PIN_NUM_MOSI 15
#define PIN_NUM_CLK  14
#define PIN_NUM_CS   13
#endif // CONFIG_IDF_TARGET_ESP32C3
#endif //USE_SPI_MODE

/* Function to initialize SPIFFS */
static esp_err_t init_spiffs(void)
{
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,   // This decides the maximum number of files that can be created on the storage
      .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    return ESP_OK;
}

/* Declare the function which starts the file server.
 * Implementation of this function is to be found in
 * file_server.c */
esp_err_t start_file_server(const char *base_path);
#ifdef CONFIG_EXAMPLE_MOUNT_SD_CARD
void sdcard_mount(void)
{
    /*sd_card part code*/
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif // EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t* card;
    const char mount_point[] = MOUNT_POINT;
    ESP_LOGI(TAG, "Initializing SD card");

#ifndef USE_SPI_MODE
    ESP_LOGI(TAG, "Using SDMMC peripheral");
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    // To use 1-line SD mode, uncomment the following line:
    // slot_config.width = 1;

    // GPIOs 15, 2, 4, 12, 13 should have external 10k pull-ups.
    // Internal pull-ups are not sufficient. However, enabling internal pull-ups
    // does make a difference some boards, so we do that here.
    gpio_set_pull_mode(15, GPIO_PULLUP_ONLY);   // CMD, needed in 4- and 1- line modes
    gpio_set_pull_mode(2, GPIO_PULLUP_ONLY);    // D0, needed in 4- and 1-line modes
    gpio_set_pull_mode(4, GPIO_PULLUP_ONLY);    // D1, needed in 4-line mode only
    gpio_set_pull_mode(12, GPIO_PULLUP_ONLY);   // D2, needed in 4-line mode only
    gpio_set_pull_mode(13, GPIO_PULLUP_ONLY);   // D3, needed in 4- and 1-line modes

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);
#else
    ESP_LOGI(TAG, "Using SPI peripheral");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CHAN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        ESP_ERROR_CHECK(ret);
    }

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);
    mount_card = card;
#endif //USE_SPI_MODE
    if(ret != ESP_OK){
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        ESP_ERROR_CHECK(ret);
    }
    sdmmc_card_print_info(stdout, card);

}

static esp_err_t unmount_card(const char* base_path, sdmmc_card_t* card)
{
#ifdef USE_SPI_MODE
    esp_err_t err = esp_vfs_fat_sdcard_unmount(base_path, card);
#else
    esp_err_t err = esp_vfs_fat_sdmmc_unmount();
#endif
    ESP_ERROR_CHECK(err);
#ifdef USE_SPI_MODE
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    err = spi_bus_free(host.slot);
#endif
    ESP_ERROR_CHECK(err);

    return err;
}

#endif //CONFIG_EXAMPLE_MOUNT_SD_CARD

int _TempSensorsInit()
{
    gpio_set_pull_mode(SENSOR_GPIO, GPIO_PULLUP_ONLY);
    return 0;
}

int _TempSensorsScan()
{
    ds18x20_addr_t addrs[MAX_SENSORS];

    esp_err_t res = ds18x20_scan_devices(SENSOR_GPIO, addrs, MAX_SENSORS, &sensor_count);
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "Sensors scan error %d (%s)", res, esp_err_to_name(res));
        return 0;
    }

    if (!sensor_count)
    {
        ESP_LOGW(TAG, "No sensors detected!");
        return 0;
    }

    for (int i = 0; i < sensor_count;i++)
        _TempSensors[i].addr = addrs[i];

    ESP_LOGI(TAG, "%d sensors detected", sensor_count);
    return sensor_count;
}

void _TempSensorsUpdate()
{
    ESP_LOGI(TAG, "Measuring...");

    float temps[MAX_SENSORS];
    ds18x20_addr_t addrs[MAX_SENSORS];
    for (int i = 0; i < sensor_count; i++)
        addrs[i] = _TempSensors[i].addr;

    esp_err_t res = ds18x20_measure_and_read_multi(SENSOR_GPIO, addrs, sensor_count, temps);
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "Sensors read error %d (%s)", res, esp_err_to_name(res));
        return;
    }

    for (int j = 0; j < sensor_count; j++)
    {
        float temp_c = temps[j];
        float temp_f = (temp_c * 1.8) + 32;
        _TempSensors[j].temp = temp_c;
        // Float is used in printf(). You need non-default configuration in
        // sdkconfig for ESP8266, which is enabled by default for this
        // example. See sdkconfig.defaults.esp8266
        ESP_LOGI(TAG, "Sensor %08x%08x (%s) reports %.3f°C (%.3f°F)",
                (uint32_t)(addrs[j] >> 32), (uint32_t)addrs[j],
                (addrs[j] & 0xff) == DS18B20_FAMILY_ID ? "DS18B20" : "DS18S20",
                temp_c, temp_f);
    }

}

static const char * _SdStartHeader = "======Start========\r\n";
FILE* _TempSensLogFile = NULL;
char _TempSensLogFileName[32] = {0};

void _LogFileStart()
{
    for (int i = 1; i <= 1000; i++)
    {
        sprintf(_TempSensLogFileName, "%s/t_%i.txt", MOUNT_POINT, i);

        //if( access( _TempSensLogFileName, F_OK ) == 0 ) 
        struct stat   st;   
        if (stat(_TempSensLogFileName, &st) == 0)
        {
            ESP_LOGI(TAG, "File exists %x", errno);
            continue;
        }

        ESP_LOGI(TAG, "Opening file");
        _TempSensLogFile = fopen(_TempSensLogFileName, "w");
        if (_TempSensLogFile != NULL) 
        {
            ESP_LOGI(TAG, "Opened log file: %s", _TempSensLogFileName);
            fprintf(_TempSensLogFile, _SdStartHeader);
            for (int i = 0; i < sensor_count; i++)
                fprintf(_TempSensLogFile, "%08x%08x;", (uint32_t)(_TempSensors[i].addr >> 32), (uint32_t)_TempSensors[i].addr);
            fprintf(_TempSensLogFile, "\r\n");
            return;
        }
        ESP_LOGI(TAG, "Warning. Can't open file %s", _TempSensLogFileName);
    };
    _TempSensLogFileName[0] = 0;
}

void _LogFileWrite(const char* format, ...)
{
    if (_TempSensLogFile == NULL)
        return ;
    else
    {
        va_list args;
        va_start(args, format);
        vfprintf(_TempSensLogFile, format, args);
        va_end(args);
    }

	return ;
}

void _LogFileClose()
{
    if (_TempSensLogFile != NULL)
    {
        fflush(_TempSensLogFile);
        fclose(_TempSensLogFile);
        _TempSensLogFile = NULL;
    };

    _TempSensLogFileName[0] = 0;
}    

void _LogFileFlush()
{
    if (_TempSensLogFile != NULL)
    {
        fflush(_TempSensLogFile);
        fclose(_TempSensLogFile);
    };

    if (_TempSensLogFileName[0])
        _TempSensLogFile = fopen(_TempSensLogFileName, "a");
}    


void SaveTemperatureSensors(void *arg)
{
    ESP_LOGI(TAG, "SaveTemperatureSensors");
    _TempSensorsUpdate();

    for (int i = 0; i < sensor_count; i++)
        _LogFileWrite("%.3f;", _TempSensors[i].temp);

    _LogFileWrite("\r\n");
    _LogFileFlush();
}

void TempSensorsReScan()
{
    _LogFileClose();
}

void TempSensorsStartNewLog()
{
    _LogFileClose();
    _LogFileStart();
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

void wifi_init_softap(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = CONFIG_EXAMPLE_WIFI_SSID,
            .ssid_len = strlen(CONFIG_EXAMPLE_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = CONFIG_EXAMPLE_WIFI_PASSWORD,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(CONFIG_EXAMPLE_WIFI_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             CONFIG_EXAMPLE_WIFI_SSID, CONFIG_EXAMPLE_WIFI_PASSWORD, EXAMPLE_ESP_WIFI_CHANNEL);
}

void wifi_init_connection(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
}

void app_main(void)
{
    /*Mount the SDcard first if needed.*/
#ifdef CONFIG_EXAMPLE_MOUNT_SD_CARD
    sdcard_mount();
#endif

    _TempSensorsInit();

#ifdef CONFIG_EXAMPLE_WIFI_USE_AP_MODE
    wifi_init_softap();
#else
    wifi_init_connection();
#endif

    /* Initialize file storage */
    ESP_ERROR_CHECK(init_spiffs());

    /* Start the file server */
    ESP_ERROR_CHECK(start_file_server("/spiffs"));

#ifdef CONFIG_EXAMPLE_MOUNT_SD_CARD
    //deinitialize the bus after all devices are removed
    ESP_ERROR_CHECK(unmount_card(mount_base_path, mount_card));
#endif
    //ESP_LOGI(TAG, "5:%i", (int)esp_timer_get_time());

    _TempSensorsScan();

    esp_timer_create_args_t saveTmer = 
    {
        SaveTemperatureSensors,
        NULL,
        ESP_TIMER_TASK,
        "SaveTemp",
        true
    };

    esp_timer_handle_t hTimer;

    esp_timer_create(&saveTmer, &hTimer);

    esp_timer_start_periodic(hTimer, 2000000);
}
