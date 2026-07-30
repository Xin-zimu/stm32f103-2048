#include "app_anim.h"
#include "anim_frames.h"
#include "bsp_spi_oled.h"
#include "timing.h"

#define ANIM_TEXT_STAGE_MS        1000U    // 文字测试保留时间
#define ANIM_SINGLE_STAGE_MS      1000U    // 单张图片测试保留时间

typedef enum
{
    ANIM_STAGE_TEXT = 0,                   // 显示启动文字
    ANIM_STAGE_SINGLE_IMAGE,               // 保持第一张测试图片
    ANIM_STAGE_PLAYBACK                    // 循环播放全部动画帧
} AnimStage;

static AnimStage g_anim_stage = ANIM_STAGE_TEXT;
static uint32_t g_anim_last_tick = 0U;
static uint8_t g_anim_frame_index = 0U;

/*
 * 初始化非阻塞 OLED 动画演示。
 *
 * 第一阶段先显示文字，便于在写入图片前检查 SPI 通信和屏幕方向。
 * 函数只记录当前计时状态，不通过延时阻塞主循环。
 *
 * 参数：
 * 无。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 清空 SPI OLED，显示“SPI OLED”，并复位动画状态。
 */
void App_Anim_Init(void)
{
    OLED_SPI_Clear();
    OLED_SPI_ShowString6x8(3U, 40U, "SPI OLED");

    g_anim_stage = ANIM_STAGE_TEXT;
    g_anim_last_tick = Timing_GetTick();
    g_anim_frame_index = 0U;
}

/*
 * 无毫秒延时地推进分阶段 OLED 演示和动画。
 *
 * 无符号计数器相减可正确处理 Timing_GetTick 溢出。流程先显示文字，再保持
 * 第一张图片，最后按 ANIM_FRAME_INTERVAL_MS 循环播放全部帧。帧数组始终
 * 以 const 形式保存在 Flash。
 *
 * 参数：
 * 无。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 仅在当前阶段时间到达后刷新完整 SPI OLED。
 */
void App_Anim_Task(void)
{
    uint32_t now;
    uint32_t elapsed;

    now = Timing_GetTick();
    elapsed = now - g_anim_last_tick;

    if (g_anim_stage == ANIM_STAGE_TEXT)
    {
        if (elapsed >= ANIM_TEXT_STAGE_MS)
        {
            OLED_SPI_ShowImage128x64(anim_frames[0]);
            g_anim_stage = ANIM_STAGE_SINGLE_IMAGE;
            g_anim_last_tick = now;
            g_anim_frame_index = 1U;
        }
    }
    else if (g_anim_stage == ANIM_STAGE_SINGLE_IMAGE)
    {
        if (elapsed >= ANIM_SINGLE_STAGE_MS)
        {
            OLED_SPI_ShowImage128x64(anim_frames[g_anim_frame_index]);
            g_anim_stage = ANIM_STAGE_PLAYBACK;
            g_anim_last_tick = now;
            g_anim_frame_index++;

            if (g_anim_frame_index >= ANIM_FRAME_COUNT)
            {
                g_anim_frame_index = 0U;
            }
        }
    }
    else if (elapsed >= ANIM_FRAME_INTERVAL_MS)
    {
        OLED_SPI_ShowImage128x64(anim_frames[g_anim_frame_index]);
        g_anim_last_tick = now;
        g_anim_frame_index++;

        if (g_anim_frame_index >= ANIM_FRAME_COUNT)
        {
            g_anim_frame_index = 0U;
        }
    }
}
