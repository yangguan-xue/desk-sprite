// Independent RAM diagnostic, SPEC-007 / TASK-022. No Flash/NVS writes.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "driver/touch_pad.h"
#include "soc/touch_sensor_channel.h"

_Static_assert(TOUCH_PAD_NUM5_GPIO_NUM == 5, "TP_KEY must map to GPIO5");

static esp_err_t wr(uint8_t reg, uint8_t value) {
    uint8_t data[] = {reg, value};
    return i2c_master_write_to_device(I2C_NUM_0, 0x18, data, 2, pdMS_TO_TICKS(100));
}
static esp_err_t rd(uint8_t reg, uint8_t *value) {
    return i2c_master_write_read_device(I2C_NUM_0, 0x18, &reg, 1, value, 1, pdMS_TO_TICKS(100));
}
static bool audio_probe(bool play_sound) {
    i2c_config_t ic = {.mode=I2C_MODE_MASTER, .sda_io_num=40, .scl_io_num=39,
        .sda_pullup_en=GPIO_PULLUP_ENABLE, .scl_pullup_en=GPIO_PULLUP_ENABLE,
        .master.clk_speed=100000};
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &ic));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
    uint8_t id[3] = {0}; bool ok = true;
    for (unsigned n=0; n<3; ++n) {
        esp_err_t err = rd(0xfd+n, &id[n]);
        printf("PROBE codec_reg=0x%02x value=0x%02x result=%d\n", 0xfd+n, id[n], err);
        ok = ok && err == ESP_OK;
    }
    // Identity guard: do not send the ES8311 sequence to an unknown device.
    if (!ok || id[0]!=0x83 || id[1]!=0x11) return false;
    if (!play_sound) { printf("PROBE audio_identity_only no_repeat_tone\n"); return true; }
    i2s_config_t cfg = {.mode=I2S_MODE_MASTER|I2S_MODE_TX, .sample_rate=16000,
        .bits_per_sample=I2S_BITS_PER_SAMPLE_16BIT, .channel_format=I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format=I2S_COMM_FORMAT_STAND_I2S, .intr_alloc_flags=0,
        .dma_buf_count=4, .dma_buf_len=256, .use_apll=false,
        .tx_desc_auto_clear=true, .fixed_mclk=4096000};
    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL)!=ESP_OK) return false;
    i2s_pin_config_t pins = {.mck_io_num=38,.bck_io_num=16,.ws_io_num=7,
        .data_out_num=4,.data_in_num=I2S_PIN_NO_CHANGE};
    ok = i2s_set_pin(I2S_NUM_0, &pins)==ESP_OK;
    ok = ok && wr(0,0x1f)==ESP_OK; vTaskDelay(pdMS_TO_TICKS(20));
    // Sequence from the matching QuotaClock mono_audio.h (Apache-2.0).
    const uint8_t regs[][2]={{0,0},{0,0x80},{1,0x3f},{2,0},{3,0x10},{4,0x10},
        {5,0},{6,3},{7,0},{8,0xff},{9,0x0c},{0x0a,0x0c},{0x0d,1},{0x0e,2},
        {0x12,0},{0x13,0x10},{0x1c,0x6a},{0x37,8},{0x32,0xbf},{0x31,0x60}};
    for (unsigned n=0;ok && n<sizeof(regs)/sizeof(regs[0]);++n) ok=wr(regs[n][0],regs[n][1])==ESP_OK;
    if (ok) {
        ok=wr(0x31,0)==ESP_OK;
        i2s_zero_dma_buffer(I2S_NUM_0); gpio_set_level(8,1); vTaskDelay(pdMS_TO_TICKS(20));
        int16_t block[256*2];
        for (int start=0;ok && start<8000;start+=256) {
            int count=(8000-start)<256?(8000-start):256;
            for(int n=0;n<count;++n) {
                int p=start+n;
                float env=p<2880?fminf(1, fminf(p/240.0f,(2880-p)/240.0f)):0;
                int16_t v=(int16_t)(sinf(2*3.14159265f*880*p/16000)*env*1200);
                block[n*2]=block[n*2+1]=v;
            }
            size_t written=0;
            ok=i2s_write(I2S_NUM_0,block,count*4,&written,pdMS_TO_TICKS(200))==ESP_OK && written==(size_t)count*4;
        }
    }
    gpio_set_level(8,0); wr(0x31,0x60); i2s_zero_dma_buffer(I2S_NUM_0);
    i2s_driver_uninstall(I2S_NUM_0);
    printf("PROBE audio_i2s_and_register_writes=%s volume=20 beeps=1\n",ok?"ok":"failed");
    return ok;
}

static spi_device_handle_t lcd;
static void tx(bool data, const void *buf, size_t size) {
    gpio_set_level(17,data);
    // With DMA disabled, each transaction must fit the hardware FIFO.
    const uint8_t *bytes=buf;
    while(size) {
        size_t count=size>64?64:size;
        spi_transaction_t t={.length=count*8,.tx_buffer=bytes};
        ESP_ERROR_CHECK(spi_device_polling_transmit(lcd,&t));
        bytes+=count;size-=count;
    }
}
static void cmd(uint8_t c,const uint8_t *data,size_t count) {
    tx(false,&c,1); if(count)tx(true,data,count);
}
static void display_probe(void) {
    gpio_set_direction(17,GPIO_MODE_OUTPUT);gpio_set_direction(6,GPIO_MODE_OUTPUT);
    gpio_set_level(6,0);vTaskDelay(pdMS_TO_TICKS(20));gpio_set_level(6,1);vTaskDelay(pdMS_TO_TICKS(120));
    spi_bus_config_t bus={.mosi_io_num=1,.miso_io_num=-1,.sclk_io_num=18,
        .quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=64};
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_DISABLED));
    // TFT_eSPI.h selects SPI_MODE3 for ST7789_2, not the generic mode 0.
    spi_device_interface_config_t device={.clock_speed_hz=27000000,.mode=3,.spics_io_num=-1,.queue_size=1};
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST,&device,&lcd));
    cmd(0x01,NULL,0);vTaskDelay(pdMS_TO_TICKS(150));
    cmd(0x11,NULL,0);vTaskDelay(pdMS_TO_TICKS(500));
    uint8_t format=0x55,orient=0;
    cmd(0x3a,&format,1);vTaskDelay(pdMS_TO_TICKS(10));cmd(0x36,&orient,1);
    cmd(0x21,NULL,0);vTaskDelay(pdMS_TO_TICKS(10));
    cmd(0x13,NULL,0);vTaskDelay(pdMS_TO_TICKS(10));
    cmd(0x29,NULL,0);vTaskDelay(pdMS_TO_TICKS(500));
    uint8_t range[]={0,0,0,239};cmd(0x2a,range,4);cmd(0x2b,range,4);cmd(0x2c,NULL,0);
    uint8_t row[480];
    for (int y=0;y<240;++y) {
        for(int x=0;x<240;++x) {
            uint16_t c=x<80?0xf800:x<160?0x07e0:0x001f;
            if(y>=208)c=y<216?0xf81f:y<224?0x07ff:y<232?0xffe0:0xffff;
            if(x<8||y<8||x>=232)c=0xffff;
            row[x*2]=c>>8;row[x*2+1]=c;
        }
        tx(true,row,sizeof(row));
    }
    ledc_timer_config_t timer={.speed_mode=LEDC_LOW_SPEED_MODE,.duty_resolution=LEDC_TIMER_8_BIT,
        .timer_num=LEDC_TIMER_0,.freq_hz=5000,.clk_cfg=LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t channel={.gpio_num=2,.speed_mode=LEDC_LOW_SPEED_MODE,.channel=LEDC_CHANNEL_0,
        .timer_sel=LEDC_TIMER_0,.duty=205};
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
    printf("PROBE display_pattern=rgb_8px_white_frame_bottom_magenta_cyan_yellow_white spi_mode=3 spi_hz=27000000 backlight_raw=205\n");
}
void app_main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    printf("PROBE ram_only start reset_reason=%d\n",esp_reset_reason());
    gpio_set_direction(8,GPIO_MODE_OUTPUT);gpio_set_level(8,0);
    bool audio=audio_probe(false);
    display_probe();
    // Seller S3 schematic: TP_KEY directly connects to IO5. Only enable this pad.
    // Initialization follows IDF v5.1.2 touch_sensor_v2/touch_pad_read (CC0-1.0).
    ESP_ERROR_CHECK(touch_pad_init());
    ESP_ERROR_CHECK(touch_pad_config(TOUCH_PAD_NUM5));
    ESP_ERROR_CHECK(touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER));
    ESP_ERROR_CHECK(touch_pad_fsm_start());
    vTaskDelay(pdMS_TO_TICKS(100));
    printf("PROBE native_touch gpio=5 channel=5 idle_first_ms=5000 sample_ms=100 duration_ms=60000\n");
    int64_t started=esp_timer_get_time();
    for(int tick=0;tick<600;++tick) {
        uint32_t raw=0;
        esp_err_t err=touch_pad_read_raw_data(TOUCH_PAD_NUM5,&raw);
        printf("PROBE touch_raw t_ms=%ld value=%lu result=%d\n",
            (long)((esp_timer_get_time()-started)/1000),(unsigned long)raw,err);
        if(tick%10==0) printf("PROBE alive seconds=%d heap=%lu audio=%s\n",tick/10,
            (unsigned long)esp_get_free_heap_size(),audio?"ok":"failed");
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_ERROR_CHECK(touch_pad_fsm_stop());
    ESP_ERROR_CHECK(touch_pad_deinit());
    printf("PROBE done returning_to_flash\n");
    esp_restart();
}
