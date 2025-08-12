/********************************************************************************
 *                                  头文件包含
 ********************************************************************************/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2s_std.h"

/* 你的板级支持包 (BSP) */
#include "esp32_s3_szp.h"

/* AVI 播放器组件 */
#include "avi_player.h"

/* JPEG 解码器组件 */
#include "esp_jpeg_dec.h"


/********************************************************************************
 *                                  全局变量
 ********************************************************************************/
static const char *TAG = "MAIN_APP";
static bool is_playing_finished = false;
extern esp_codec_dev_handle_t play_dev_handle;

/********************************************************************************
 *                           AVI 播放器回调函数定义
 ********************************************************************************/

void my_video_callback(frame_data_t *data, void *arg)
{
    jpeg_dec_handle_t jpeg_dec_handle = NULL;
    jpeg_dec_io_t *jpeg_io = NULL;
    jpeg_dec_header_info_t header_info;
    esp_err_t err = ESP_OK;

    uint8_t *rgb_buffer = (uint8_t *)heap_caps_aligned_alloc(16, 320 * 240 * 2, MALLOC_CAP_SPIRAM);
    if (rgb_buffer == NULL) {
        ESP_LOGE("VIDEO_CB", "无法为 RGB 对齐缓冲区分配内存");
        return;
    }

    jpeg_io = (jpeg_dec_io_t *)calloc(1, sizeof(jpeg_dec_io_t));
    if (jpeg_io == NULL) {
        ESP_LOGE("VIDEO_CB", "无法为 IO 结构体分配内存");
        free(rgb_buffer);
        return;
    }

    jpeg_dec_config_t jpeg_dec_cfg = {
        .output_type = JPEG_PIXEL_FORMAT_RGB565_BE,
        .rotate = JPEG_ROTATE_0D,
    };

    err = jpeg_dec_open(&jpeg_dec_cfg, &jpeg_dec_handle);
    if (err != ESP_OK) {
        ESP_LOGE("VIDEO_CB", "创建 JPEG 解码器失败 (0x%x)", err);
        goto exit;
    }

    jpeg_io->inbuf = data->data;
    jpeg_io->inbuf_len = data->data_bytes;
    jpeg_io->outbuf = rgb_buffer;

    err = jpeg_dec_parse_header(jpeg_dec_handle, jpeg_io, &header_info);
    if (err != ESP_OK) {
        ESP_LOGE("VIDEO_CB", "解析 JPEG 头部失败 (0x%x)", err);
        goto exit;
    }

    err = jpeg_dec_process(jpeg_dec_handle, jpeg_io);
    if (err == ESP_OK) {
        extern esp_lcd_panel_handle_t panel_handle; 
        esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, header_info.width, header_info.height, rgb_buffer);
    } else {
        ESP_LOGE("VIDEO_CB", "JPEG 解码失败 (0x%x)", err);
    }

exit:
    if (jpeg_dec_handle) {
        jpeg_dec_close(jpeg_dec_handle);
    }
    if (jpeg_io) {
        free(jpeg_io);
    }
    if (rgb_buffer) {
        free(rgb_buffer);
    }
}

void my_audio_callback(frame_data_t *data, void *arg)
{
    size_t bytes_written = 0;
    bsp_i2s_write(data->data, data->data_bytes, &bytes_written, portMAX_DELAY);
}

/**
 * @brief 音频参数设置回调函数 (【最终兼容版】留空)
 */
void my_audio_set_clock_cb(uint32_t rate, uint32_t bits_cfg, uint32_t ch, void *arg)
{
    ESP_LOGI("AUDIO_SET_CLOCK", "AVI请求设置音频参数: 采样率=%lu, 位深=%lu, 声道=%lu", rate, bits_cfg, ch);
    // 现在我们可以安全地调用这个 BSP 函数了
    bsp_codec_set_fs(rate, bits_cfg, (i2s_slot_mode_t)ch);
}


void my_play_end_cb(void *arg)
{
    is_playing_finished = true;
    ESP_LOGI("PLAY_END_CB", "视频播放结束!");
}

/********************************************************************************
 *                                主应用程序入口
 ********************************************************************************/
void app_main(void)
{
    ESP_LOGI(TAG, "----------------- AVI 播放器 Demo -----------------");

    // 1. 初始化基础硬件
    bsp_i2c_init();
    pca9557_init();
    bsp_lcd_init();

    // 2. 【关键修改】只初始化 Codec 句柄，但不进行最终配置
    bsp_codec_init(); 
    
    // 3. 初始化文件系统
    bsp_spiffs_mount();

    // 4. 配置 avi_player 组件
    avi_player_config_t config = {
        .buffer_size = 128 * 1024,
        .video_cb = my_video_callback,
        .audio_cb = my_audio_callback,
        .audio_set_clock_cb = my_audio_set_clock_cb, // 指向我们功能完整的版本
        .avi_play_end_cb = my_play_end_cb,
        .priority = 5,
    };

    // 5. 初始化并启动播放器
    avi_player_handle_t player_handle;
    avi_player_init(config, &player_handle);
    
    // 6. 开启功放 (在播放前开启)
    pa_en(1);
    
    // 【新增】强制将音量设置到最大，并解除静音
    bsp_codec_mute_set(false);
    bsp_codec_volume_set(100, NULL); // 设置音量为 100%

    const char *avi_file = "/spiffs/video_for_esp32.avi";
    ESP_LOGI(TAG, "开始播放文件: %s", avi_file);
    esp_err_t ret = avi_player_play_from_file(player_handle, avi_file);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "播放失败，请检查文件是否存在以及格式是否正确！");
        return;
    }

    // 7. 等待播放结束
    while (!is_playing_finished) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // 8. 清理资源
    ESP_LOGI(TAG, "播放结束，正在清理资源...");
    avi_player_deinit(player_handle);
    esp_vfs_spiffs_unregister("storage");
    
    ESP_LOGI(TAG, "---------------------- 演示结束 ----------------------");
    lcd_set_color(0x0000); // 清屏为黑色
}

