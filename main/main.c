#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include <inttypes.h>

#define LED_PIN_1        6
#define MOTOR_PIN        5                     // затвор MOSFET / база транзистора
#define LEDC_MODE        LEDC_LOW_SPEED_MODE   // на ESP32-S3 доступний лише low-speed
#define LEDC_CHANNEL1    LEDC_CHANNEL_0
#define LEDC_CHANNEL_MOTOR LEDC_CHANNEL_1
#define LEDC_TIMER_NUM   LEDC_TIMER_0
#define LEDC_FREQUENCY   1000                  // 1 kHz
#define LEDC_BITS        10
#define LEDC_RESOLUTION  LEDC_TIMER_10_BIT     // має збігатися з LEDC_BITS
#define LEDC_MAX_DUTY    ((1 << LEDC_BITS) - 1) // 1023

#define POT_ADC_UNIT     ADC_UNIT_1
#define POT_ADC_CHANNEL  ADC_CHANNEL_3         // GPIO4
static adc_oneshot_unit_handle_t adc1_handle;

static void set_duty(ledc_channel_t channel, uint32_t duty)
{
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, channel));
}

static void set_leds(uint32_t brightness)
{
    set_duty(LEDC_CHANNEL1, brightness);
}

static void set_motor(uint32_t speed)
{
    set_duty(LEDC_CHANNEL_MOTOR, speed);
}

static void ledc_init(void)
{
    // Один таймер на обидва світлодіоди
    ledc_timer_config_t timer_config = {
        .speed_mode      = LEDC_MODE,
        .duty_resolution = LEDC_RESOLUTION,
        .timer_num       = LEDC_TIMER_NUM,
        .freq_hz         = LEDC_FREQUENCY,
        .clk_cfg         = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    // Канал 0 → LED 1
    ledc_channel_config_t ch1 = {
        .gpio_num   = LED_PIN_1,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL1,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER_NUM,
        .duty       = 0,
        .hpoint     = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch1));

    // Канал 1 → мотор (той самий таймер, старт зі стопу)
    ledc_channel_config_t ch_motor = {
        .gpio_num   = MOTOR_PIN,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL_MOTOR,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER_NUM,
        .duty       = 0,
        .hpoint     = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_motor));
}

static void adc_init(void)
{
    // 1. Ініціалізуємо блок ADC1
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = POT_ADC_UNIT,
        .clk_src = 0,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    // 2. Налаштовуємо канал ADC_CHANNEL_3 (GPIO4)
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,           
        .bitwidth = ADC_BITWIDTH_DEFAULT,   // За замовчуванням 12 біт (0..4095) 
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, POT_ADC_CHANNEL, &config));
}


void app_main(void)
{
    ledc_init();
    adc_init();

    int adc_raw_value = 0;

    while (1) {
      // Читання сирого значення з потенціометра (0...4095)
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, POT_ADC_CHANNEL, &adc_raw_value));

        // Масштабуємо 12-бітне значення АЦП у 10-бітну роздільну здатність ШІМ (зсув на 2 біти вправо або ділення на 4)
        uint32_t brightness = adc_raw_value >> 2; 

        // Захист від випадкового виходу за межі (на випадок шумів АЦП)
        if (brightness > LEDC_MAX_DUTY) {
            brightness = LEDC_MAX_DUTY;
        }
        ESP_LOGI("ADC", "raw: %d, brightness: %" PRIu32, adc_raw_value, brightness);

        // Оновлюємо яскравість світлодіодів
        set_leds(brightness);
        set_motor(brightness);

        // Невелика затримка, щоб не перевантажувати процесор частими опитуваннями
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}