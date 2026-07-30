#include "app_config.h"
#include "bsp_st7789.h"
#include "delay.h"
#include "lcd_dma.h"

#define ST7789_GPIO_PORT            GPIOB
#define ST7789_GPIO_CLK             RCC_APB2Periph_GPIOB
#define ST7789_SPI_CLK              RCC_APB1Periph_SPI2

#define ST7789_SCK_PIN              GPIO_Pin_13
#define ST7789_MOSI_PIN             GPIO_Pin_15
#define ST7789_RST_PIN              GPIO_Pin_10
#define ST7789_DC_PIN               GPIO_Pin_14
#define ST7789_BLK_PIN              GPIO_Pin_12

#define ST7789_RST_HIGH()           GPIO_SetBits(ST7789_GPIO_PORT, ST7789_RST_PIN)
#define ST7789_RST_LOW()            GPIO_ResetBits(ST7789_GPIO_PORT, ST7789_RST_PIN)
#define ST7789_DC_HIGH()            GPIO_SetBits(ST7789_GPIO_PORT, ST7789_DC_PIN)
#define ST7789_DC_LOW()             GPIO_ResetBits(ST7789_GPIO_PORT, ST7789_DC_PIN)
#define ST7789_BLK_HIGH()           GPIO_SetBits(ST7789_GPIO_PORT, ST7789_BLK_PIN)
#define ST7789_BLK_LOW()            GPIO_ResetBits(ST7789_GPIO_PORT, ST7789_BLK_PIN)

#define ST7789_LINE_BUFFER_COUNT     APP_LCD_DMA_LINE_BUFFERS // Double-buffered line count.
#define ST7789_LINE_BUFFER_SIZE      (ST7789_WIDTH * 2U)      // One RGB565 display line.

static uint8_t ST7789_LINE_BUFFER[ST7789_LINE_BUFFER_COUNT][ST7789_LINE_BUFFER_SIZE];

typedef enum
{
    ST7789_ANIM_DELTA_IDLE = 0,        // No asynchronous delta job is active.
    ST7789_ANIM_DELTA_SCAN,            // Scanning delta runs for the next changed segment.
    ST7789_ANIM_DELTA_REPEAT,          // Repeating the current decoded segment vertically.
    ST7789_ANIM_DELTA_ERROR            // Delta stream was invalid or DMA reported an error.
} ST7789_AnimDeltaState;

typedef struct
{
    const uint8_t *delta;              // Encoded delta stream.
    uint32_t delta_size;               // Encoded delta byte count.
    uint32_t position;                 // Current read offset in delta.
    uint32_t payload_start;            // Start offset for the current changed run payload.
    uint32_t payload_size;             // Current changed run payload byte count.
    const uint16_t *palette;           // RGB565 palette.
    uint16_t source_width;             // Source image width before scaling.
    uint16_t source_height;            // Source image height before scaling.
    uint16_t source_y;                 // Current source row.
    uint16_t source_x;                 // Current source column.
    uint16_t run_length;               // Current source-pixel run length.
    uint8_t scale;                     // Integer display scale.
    uint8_t repeat_y;                  // Vertical repeat index for the current run.
    uint8_t buffer_index;              // Next free DMA line buffer index.
    ST7789_AnimDeltaState state;       // Current asynchronous parser state.
    uint8_t active;                    // Nonzero while a job is in progress.
} ST7789_AnimDeltaJob;

static ST7789_AnimDeltaJob ST7789_ANIM_DELTA_JOB;

static uint8_t ST7789_IsDMAReady(void);
static uint8_t ST7789_TryStartBufferDMA(const uint8_t *data, uint16_t length);
static void ST7789_FinishBufferDMA(void);

/*
 * 初始化 ST7789 使用的 GPIOB 引脚。
 *
 * PB13 和 PB15 配置为 SPI2 复用推挽输出，分别连接屏幕 SCL 和 SDA。
 * PB10、PB14、PB12 配置为普通推挽输出，分别控制复位、命令数据选择和
 * 背光。初始化期间先关闭背光，避免控制器显存尚未清空时出现闪屏。
 *
 * 参数：
 * 无。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 打开 GPIOB 时钟并改变 PB10、PB12、PB13、PB14、PB15 的工作模式。
 */
void ST7789_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(ST7789_GPIO_CLK, ENABLE);

    gpio.GPIO_Pin = ST7789_SCK_PIN | ST7789_MOSI_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(ST7789_GPIO_PORT, &gpio);

    gpio.GPIO_Pin = ST7789_RST_PIN | ST7789_DC_PIN | ST7789_BLK_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(ST7789_GPIO_PORT, &gpio);

    ST7789_RST_HIGH();
    ST7789_DC_HIGH();
    ST7789_BLK_LOW();
}

/*
 * 初始化用于 ST7789 单向发送的硬件 SPI2。
 *
 * SPI2 配置为主机、8 位、MSB 先发送、软件 NSS 和单线发送。时钟极性
 * 与采样边沿由 ST7789_SPI_MODE 选择，当前实测使用 Mode 3；若更换屏幕
 * 模块，可改为 Mode 0 复测。APB1 为 36 MHz 时，2 分频得到约 18 MHz 时钟，
 * 用于把帧差写入时间压缩到单次屏幕扫描周期附近。
 *
 * 参数：
 * 无。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 打开并重新配置 SPI2 和 DMA1 Channel5。
 */
void ST7789_SPI_Init(void)
{
    SPI_InitTypeDef spi;

    RCC_APB1PeriphClockCmd(ST7789_SPI_CLK, ENABLE);
    SPI_I2S_DeInit(SPI2);

    spi.SPI_Direction = SPI_Direction_1Line_Tx;
    spi.SPI_Mode = SPI_Mode_Master;
    spi.SPI_DataSize = SPI_DataSize_8b;
#if ST7789_SPI_MODE == 0U
    spi.SPI_CPOL = SPI_CPOL_Low;
    spi.SPI_CPHA = SPI_CPHA_1Edge;
#elif ST7789_SPI_MODE == 3U
    spi.SPI_CPOL = SPI_CPOL_High;
    spi.SPI_CPHA = SPI_CPHA_2Edge;
#else
#error "ST7789_SPI_MODE must be 0 or 3"
#endif
    spi.SPI_NSS = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = ST7789_SPI_PRESCALER;
    spi.SPI_FirstBit = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial = 7U;

    SPI_Init(SPI2, &spi);
    SPI_NSSInternalSoftwareConfig(SPI2, SPI_NSSInternalSoft_Set);
    LCD_DMA_Init();

    SPI_Cmd(SPI2, ENABLE);
}

/*
 * 将一个字节加入连续 SPI 像素数据流。
 *
 * 这里只等待发送寄存器可写，不等待移位寄存器完成。连续像素可以紧密进入
 * SPI2，避免每个字节之间产生空闲时钟；调用者在切换 DC 或发送新命令前
 * 必须执行 ST7789_WaitStreamComplete()。
 *
 * 参数：
 * data：需要发送的 8 位像素数据。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 向 SPI2 数据寄存器写入一个字节，返回时最后一个字节可能仍在移位发送。
 */
static void ST7789_WriteStreamByte(uint8_t data)
{
    ST7789_FinishBufferDMA();

    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET)
    {
    }

    SPI_I2S_SendData(SPI2, data);
}

/*
 * 等待连续 SPI 数据流的最后一个字节发送完成。
 *
 * 该同步点只放在一段连续像素之后，而不是每个字节之后。完成后才能改变 DC
 * 电平或重新设置地址窗口，防止最后一个像素被控制器解释为命令。
 *
 * 参数：
 * 无。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 阻塞到 SPI2 的 BSY 标志清零。
 */
static void ST7789_WaitStreamComplete(void)
{
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY) == SET)
    {
    }
}

/*
 * Check whether SPI2 TX DMA can accept a new buffer.
 *
 * The LCD DMA module owns the channel state. Running its task first releases a
 * completed transfer from COMPLETE_PENDING to IDLE, which lets synchronous
 * legacy drawing code start the next line without duplicating DMA state here.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * 1: DMA1 Channel5 is idle.
 * 0: DMA1 Channel5 is still transmitting or recovering.
 */
static uint8_t ST7789_IsDMAReady(void)
{
    LCD_DMA_Task();
    return (LCD_DMA_IsBusy() == 0U) ? 1U : 0U;
}

/*
 * Ask the LCD DMA module to recover after a transfer error.
 *
 * ST7789 keeps this wrapper so older image and delta paths do not touch the
 * new DMA state machine directly. Actual channel disable, pending-bit clear,
 * and SPI2 DMA request recovery are handled by LCD_DMA_Task.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May recover DMA1 Channel5 through the LCD DMA module.
 */
static void ST7789_RecoverDMA(void)
{
    LCD_DMA_Task();
}

/*
 * Wait until the active SPI2 TX DMA transfer is fully released.
 *
 * This compatibility wrapper keeps the current blocking ST7789 drawing API
 * intact while the DMA state machine lives in lcd_dma.c. LCD_DMA_WaitReady
 * also runs the DMA task before sleeping, preventing a COMPLETE_PENDING state
 * from being missed after the interrupt has already fired.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 */
static void ST7789_WaitDMAReady(void)
{
    LCD_DMA_WaitReady();
}

/*
 * Try to start one SPI2 TX DMA transfer from a prepared buffer.
 *
 * The ST7789 driver has already set the address window and DC data mode before
 * this helper is called. It packages the byte buffer into an LCD_DMA_Transfer
 * and lets lcd_dma.c own all DMA channel registers and error recovery.
 *
 * Parameters:
 * data: First byte to send through SPI2.
 * length: Number of bytes to transmit, from 1 to 65535.
 *
 * Return value:
 * 1: Transfer was started.
 * 0: Parameters are invalid or DMA is still busy.
 *
 * Side effects:
 * May start DMA1 Channel5 through LCD_DMA_Start.
 */
static uint8_t ST7789_TryStartBufferDMA(const uint8_t *data, uint16_t length)
{
    LCD_DMA_Transfer transfer;

    if ((data == 0) || (length == 0U) || (ST7789_IsDMAReady() == 0U))
    {
        return 0U;
    }

    transfer.x = 0U;
    transfer.y = 0U;
    transfer.width = 0U;
    transfer.height = 0U;
    transfer.data = data;
    transfer.data_length = length;
    transfer.callback = 0;
    transfer.user_data = 0;

    return LCD_DMA_Start(&transfer);
}

/*
 * Start one SPI2 TX DMA transfer and wait only when the channel is busy.
 *
 * Synchronous display functions use this wrapper to preserve their existing
 * call contract while still sharing the interrupt-completion path with the
 * asynchronous animation state machine.
 *
 * Parameters:
 * data: First byte to send through SPI2.
 * length: Number of bytes to transmit, from 1 to 65535.
 *
 * Return value:
 * None.
 */
static void ST7789_StartBufferDMA(const uint8_t *data, uint16_t length)
{
    if ((data == 0) || (length == 0U))
    {
        return;
    }

    while (ST7789_TryStartBufferDMA(data, length) == 0U)
    {
        __WFI();
    }
}

/*
 * Finish the current SPI2 TX DMA stream before changing display state.
 *
 * DMA completion only means the last byte has been written to SPI2->DR. The
 * final BSY wait is still required before changing DC or the ST7789 address
 * window, otherwise the last data byte can be interpreted with the next mode.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 */
static void ST7789_FinishBufferDMA(void)
{
    ST7789_WaitDMAReady();
    ST7789_WaitStreamComplete();
}

/*
 * Fill the shared DMA line buffer with one RGB565 color.
 *
 * The buffer stores high byte first because ST7789 expects RGB565 data in
 * big-endian byte order. The caller limits pixel_count to the visible line
 * width, so the fixed one-line buffer is never overrun.
 *
 * Parameters:
 * buffer: Target line buffer not currently owned by DMA.
 * color: RGB565 color value.
 * pixel_count: Number of pixels to encode into buffer.
 *
 * Return value:
 * Number of bytes prepared for DMA transmission.
 */
static uint16_t ST7789_FillColorLine(
    uint8_t *buffer,
    uint16_t color,
    uint16_t pixel_count
)
{
    uint16_t pixel;
    uint16_t offset;

    offset = 0U;
    for (pixel = 0U; pixel < pixel_count; pixel++)
    {
        buffer[offset++] = (uint8_t)(color >> 8);
        buffer[offset++] = (uint8_t)color;
    }

    return offset;
}

/*
 * Decode packed 4-bit palette pixels into the shared DMA line buffer.
 *
 * Each input byte contains two source pixels, high nibble first. Every source
 * pixel is expanded horizontally by scale and converted to RGB565 high-byte,
 * low-byte order ready for one SPI DMA transfer.
 *
 * Parameters:
 * buffer: Target line buffer not currently owned by DMA.
 * packed_pixels: Packed 4-bit palette indices for the start of the run.
 * palette: Sixteen RGB565 colors.
 * pixel_count: Number of source pixels to decode.
 * scale: Horizontal expansion factor.
 *
 * Return value:
 * Number of bytes prepared for DMA transmission.
 */
static uint16_t ST7789_BuildIndexed4Line(
    uint8_t *buffer,
    const uint8_t *packed_pixels,
    const uint16_t *palette,
    uint16_t pixel_count,
    uint8_t scale
)
{
    uint16_t pixel;
    uint16_t offset;

    offset = 0U;
    for (pixel = 0U; pixel < pixel_count; pixel++)
    {
        uint8_t packed;
        uint8_t palette_index;
        uint8_t repeat_x;
        uint16_t color;

        packed = packed_pixels[pixel >> 1];
        if ((pixel & 1U) == 0U)
        {
            palette_index = (uint8_t)(packed >> 4);
        }
        else
        {
            palette_index = (uint8_t)(packed & 0x0FU);
        }
        color = palette[palette_index];

        for (repeat_x = 0U; repeat_x < scale; repeat_x++)
        {
            buffer[offset++] = (uint8_t)(color >> 8);
            buffer[offset++] = (uint8_t)color;
        }
    }

    return offset;
}

/*
 * 通过 SPI2 发送一个字节。
 *
 * 函数先等待发送寄存器为空，再写入数据，并等待 SPI2 完成最后一个时钟。
 * 屏幕没有 MISO 和 CS，因此这里只执行单向发送，也不切换片选信号。
 *
 * 参数：
 * data：需要发送的 8 位数据。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 在 PB13 和 PB15 上产生一次由 ST7789_SPI_MODE 指定的字节传输。
 */
void ST7789_WriteByte(uint8_t data)
{
    ST7789_WriteStreamByte(data);
    ST7789_WaitStreamComplete();
}

/*
 * 向 ST7789 写入一个命令字节。
 *
 * 发送前把 DC 拉低，传输结束后保持当前电平。七针模块没有 CS 引脚，
 * 默认模块内部已经保持选中状态。
 *
 * 参数：
 * cmd：ST7789 命令码。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 改变 PB14 电平并发送一个 SPI 字节。
 */
void ST7789_WriteCmd(uint8_t cmd)
{
    ST7789_DC_LOW();
    ST7789_WriteByte(cmd);
}

/*
 * 向 ST7789 写入一个 8 位参数或显存数据。
 *
 * 参数：
 * data：需要发送的数据。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 把 PB14 拉高并发送一个 SPI 字节。
 */
void ST7789_WriteData8(uint8_t data)
{
    ST7789_DC_HIGH();
    ST7789_WriteByte(data);
}

/*
 * 按 ST7789 要求的大端顺序写入一个 RGB565 数据。
 *
 * RGB565 的高字节必须先发送，低字节后发送。若颜色红蓝颠倒，应优先
 * 检查 MADCTL 的 BGR 位，而不是交换这两个字节。
 *
 * 参数：
 * data：16 位寄存器参数或 RGB565 像素值。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 连续发送两个 SPI 数据字节。
 */
void ST7789_WriteData16(uint16_t data)
{
    ST7789_WriteData8((uint8_t)(data >> 8));
    ST7789_WriteData8((uint8_t)data);
}

/*
 * 执行 ST7789 硬件复位时序。
 *
 * 复位脚先保持高电平，再产生不少于 10 ms 的低脉冲，最后等待控制器
 * 从复位状态恢复。调用本函数前必须已经执行 delay_init()。
 *
 * 参数：
 * 无。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 翻转 PB10，并阻塞约 150 ms。
 */
void ST7789_Reset(void)
{
    ST7789_RST_HIGH();
    delay_ms(10U);
    ST7789_RST_LOW();
    delay_ms(20U);
    ST7789_RST_HIGH();
    delay_ms(120U);
}

/*
 * 打开 ST7789 模块背光。
 *
 * 参数：
 * 无。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 将 PB12 输出高电平。
 */
void ST7789_Backlight_On(void)
{
    ST7789_BLK_HIGH();
}

/*
 * 关闭 ST7789 模块背光。
 *
 * 参数：
 * 无。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 将 PB12 输出低电平。
 */
void ST7789_Backlight_Off(void)
{
    ST7789_BLK_LOW();
}

/*
 * 设置下一次显存写入使用的矩形地址窗口。
 *
 * 输入坐标使用 240x240 可见区域坐标。函数在发送 CASET 和 RASET 前
 * 自动叠加面板偏移，并在最后发送 RAMWR。若图像整体偏移，应修改头文件
 * 中的 ST7789_X_OFFSET 或 ST7789_Y_OFFSET。
 *
 * 参数：
 * x0：窗口左上角 X 坐标。
 * y0：窗口左上角 Y 坐标。
 * x1：窗口右下角 X 坐标，包含该列。
 * y1：窗口右下角 Y 坐标，包含该行。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 更新 ST7789 的列地址、行地址，并进入显存写入状态。
 */
void ST7789_SetAddressWindow(
    uint16_t x0,
    uint16_t y0,
    uint16_t x1,
    uint16_t y1
)
{
    x0 = (uint16_t)(x0 + ST7789_X_OFFSET);
    x1 = (uint16_t)(x1 + ST7789_X_OFFSET);
    y0 = (uint16_t)(y0 + ST7789_Y_OFFSET);
    y1 = (uint16_t)(y1 + ST7789_Y_OFFSET);

    ST7789_WriteCmd(0x2AU);
    ST7789_WriteData16(x0);
    ST7789_WriteData16(x1);

    ST7789_WriteCmd(0x2BU);
    ST7789_WriteData16(y0);
    ST7789_WriteData16(y1);

    ST7789_WriteCmd(0x2CU);
}

/*
 * Put the ST7789 bus into data mode for pixel memory writes.
 *
 * ST7789_SetAddressWindow ends by sending RAMWR as a command, leaving DC low.
 * External renderers that submit a prepared DMA pixel buffer must raise DC
 * before starting SPI2 TX DMA, while command helpers continue to manage DC
 * internally.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Drives PB14 high for following data bytes.
 */
void ST7789_BeginDataWrite(void)
{
    ST7789_DC_HIGH();
}

/*
 * Wait until any active pixel transfer has fully left SPI2.
 *
 * DMA completion only means the last byte reached SPI2->DR. Before another
 * address window or command is sent, the SPI BSY flag must also clear so the
 * final pixel byte cannot be interpreted under the next DC state.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * May wait for DMA completion and SPI2 BSY to clear.
 */
void ST7789_WaitWriteComplete(void)
{
    ST7789_FinishBufferDMA();
}

/*
 * 使用一个 RGB565 颜色填充整个 240x240 可见区域。
 *
 * 函数预先构建两个相同的一行 RGB565 DMA 缓冲区并交替发送，不创建
 * 全屏缓冲区，因此不消耗 115200 字节 RAM。
 *
 * 参数：
 * color：需要填充的 RGB565 颜色。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 覆盖屏幕整个可见区域。
 */
void ST7789_Clear(uint16_t color)
{
    uint16_t row;
    uint16_t byte_count;
    uint8_t buffer_index;

    ST7789_SetAddressWindow(
        0U,
        0U,
        ST7789_WIDTH - 1U,
        ST7789_HEIGHT - 1U
    );

    ST7789_DC_HIGH();
    byte_count = ST7789_FillColorLine(
        ST7789_LINE_BUFFER[0],
        color,
        ST7789_WIDTH
    );
    (void)ST7789_FillColorLine(
        ST7789_LINE_BUFFER[1],
        color,
        ST7789_WIDTH
    );
    for (row = 0U; row < ST7789_HEIGHT; row++)
    {
        buffer_index = (uint8_t)(row & 1U);
        ST7789_StartBufferDMA(ST7789_LINE_BUFFER[buffer_index], byte_count);
    }
    ST7789_FinishBufferDMA();
}

/*
 * 使用单一 RGB565 颜色填充可见区域中的矩形。
 *
 * 宽度或高度为 0、起点位于屏幕外时不执行写入。矩形越过右边界或下边界时
 * 自动裁剪，防止地址窗口超过当前 240x240 可见区域。函数预先构建
 * 两个相同的一行 DMA 缓冲区，不申请矩形或全屏缓冲区。
 *
 * 参数：
 * x：矩形左上角 X 坐标。
 * y：矩形左上角 Y 坐标。
 * width：矩形宽度，单位为像素。
 * height：矩形高度，单位为像素。
 * color：矩形填充使用的 RGB565 颜色。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 覆盖指定矩形范围内的 ST7789 显存。
 */
void ST7789_FillRect(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    uint16_t color
)
{
    uint16_t row;
    uint16_t byte_count;
    uint8_t buffer_index;

    if ((width == 0U) || (height == 0U) ||
        (x >= ST7789_WIDTH) || (y >= ST7789_HEIGHT))
    {
        return;
    }

    if (width > (uint16_t)(ST7789_WIDTH - x))
    {
        width = (uint16_t)(ST7789_WIDTH - x);
    }
    if (height > (uint16_t)(ST7789_HEIGHT - y))
    {
        height = (uint16_t)(ST7789_HEIGHT - y);
    }

    ST7789_SetAddressWindow(
        x,
        y,
        (uint16_t)(x + width - 1U),
        (uint16_t)(y + height - 1U)
    );

    ST7789_DC_HIGH();
    byte_count = ST7789_FillColorLine(
        ST7789_LINE_BUFFER[0],
        color,
        width
    );
    (void)ST7789_FillColorLine(
        ST7789_LINE_BUFFER[1],
        color,
        width
    );
    for (row = 0U; row < height; row++)
    {
        buffer_index = (uint8_t)(row & 1U);
        ST7789_StartBufferDMA(ST7789_LINE_BUFFER[buffer_index], byte_count);
    }
    ST7789_FinishBufferDMA();
}

/*
 * 将 4 位索引图片按整数倍放大后写满当前可见区域。
 *
 * 每个源数据字节保存两个像素索引，高半字节在前。函数逐源行、逐像素解码，
 * 并在水平和垂直方向重复 scale 次。函数按行构建 RGB565 DMA 缓冲区，
 * 只有放大后的宽高恰好等于当前屏幕尺寸时才执行，避免错误图片参数造成越界。
 *
 * 参数：
 * image：4 位索引图片数据指针。
 * palette：16 项 RGB565 调色板指针。
 * source_width：源图片宽度，必须为偶数。
 * source_height：源图片高度。
 * scale：水平和垂直放大倍数。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 覆盖整个 ST7789 可见区域；不申请全屏帧缓冲。
 */
void ST7789_ShowIndexed4Image(
    const uint8_t *image,
    const uint16_t *palette,
    uint16_t source_width,
    uint16_t source_height,
    uint8_t scale
)
{
    uint16_t source_y;
    uint8_t repeat_y;
    uint8_t buffer_index;

    if ((image == 0) || (palette == 0) || (scale == 0U) ||
        ((source_width & 1U) != 0U) ||
        ((uint32_t)source_width * scale != ST7789_WIDTH) ||
        ((uint32_t)source_height * scale != ST7789_HEIGHT))
    {
        return;
    }

    ST7789_SetAddressWindow(
        0U,
        0U,
        ST7789_WIDTH - 1U,
        ST7789_HEIGHT - 1U
    );
    ST7789_DC_HIGH();
    buffer_index = 0U;

    for (source_y = 0U; source_y < source_height; source_y++)
    {
        uint16_t byte_count;

        byte_count = ST7789_BuildIndexed4Line(
            ST7789_LINE_BUFFER[buffer_index],
            &image[((uint32_t)source_y * source_width) >> 1],
            palette,
            source_width,
            scale
        );
        ST7789_StartBufferDMA(ST7789_LINE_BUFFER[buffer_index], byte_count);
        buffer_index ^= 1U;

        for (repeat_y = 1U; repeat_y < scale; repeat_y++)
        {
            byte_count = ST7789_BuildIndexed4Line(
                ST7789_LINE_BUFFER[buffer_index],
                &image[((uint32_t)source_y * source_width) >> 1],
                palette,
                source_width,
                scale
            );
            ST7789_StartBufferDMA(ST7789_LINE_BUFFER[buffer_index], byte_count);
            buffer_index ^= 1U;
        }
    }
    ST7789_FinishBufferDMA();
}

/*
 * 将一帧 4 位索引差分数据应用到 ST7789 当前显存。
 *
 * 数据按源图每一行分别编码。控制字节最高位为 1 时跳过未变化像素，
 * 最高位为 0 时后随打包的 4 位调色板索引并绘制变化像素；低 7 位加 1
 * 表示本段源像素数。每个绘制段单独设置窗口并放大，未变化区域继续保留
 * 屏幕上一帧内容，因此不需要在 STM32 的 RAM 中保存完整帧缓冲。
 *
 * 参数：
 * delta：当前帧的行式差分数据。
 * delta_size：差分数据字节数，用于防止读取越界。
 * palette：16 色 RGB565 调色板。
 * source_width：差分数据的源图宽度。
 * source_height：差分数据的源图高度。
 * scale：源像素在水平和垂直方向的整数放大倍数。
 *
 * 返回值：
 * 无。参数或数据不合法时立即停止当前帧更新。
 *
 * 副作用：
 * 仅改写差分数据标记为变化的 ST7789 显存区域；不分配完整帧缓冲。
 */
void ST7789_ApplyIndexed4Delta(
    const uint8_t *delta,
    uint32_t delta_size,
    const uint16_t *palette,
    uint16_t source_width,
    uint16_t source_height,
    uint8_t scale
)
{
    uint32_t position;
    uint16_t source_y;
    uint8_t buffer_index;

    if ((delta == 0) || (palette == 0) || (scale == 0U) ||
        ((uint32_t)source_width * scale != ST7789_WIDTH) ||
        ((uint32_t)source_height * scale != ST7789_HEIGHT))
    {
        return;
    }

    position = 0U;
    buffer_index = 0U;
    for (source_y = 0U; source_y < source_height; source_y++)
    {
        uint16_t source_x;

        source_x = 0U;
        while (source_x < source_width)
        {
            uint8_t control;
            uint16_t run_length;

            if (position >= delta_size)
            {
                ST7789_FinishBufferDMA();
                return;
            }

            control = delta[position++];
            run_length = (uint16_t)((control & 0x7FU) + 1U);
            if (run_length > (uint16_t)(source_width - source_x))
            {
                ST7789_FinishBufferDMA();
                return;
            }

            if ((control & 0x80U) == 0U)
            {
                uint32_t payload_size;
                uint16_t byte_count;
                uint8_t repeat_y;

                payload_size = (run_length + 1U) >> 1;
                if ((position + payload_size) > delta_size)
                {
                    ST7789_FinishBufferDMA();
                    return;
                }

                ST7789_SetAddressWindow(
                    (uint16_t)(source_x * scale),
                    (uint16_t)(source_y * scale),
                    (uint16_t)((source_x + run_length) * scale - 1U),
                    (uint16_t)((source_y + 1U) * scale - 1U)
                );
                ST7789_DC_HIGH();

                byte_count = ST7789_BuildIndexed4Line(
                    ST7789_LINE_BUFFER[buffer_index],
                    &delta[position],
                    palette,
                    run_length,
                    scale
                );
                ST7789_StartBufferDMA(ST7789_LINE_BUFFER[buffer_index], byte_count);
                buffer_index ^= 1U;
                for (repeat_y = 1U; repeat_y < scale; repeat_y++)
                {
                    byte_count = ST7789_BuildIndexed4Line(
                        ST7789_LINE_BUFFER[buffer_index],
                        &delta[position],
                        palette,
                        run_length,
                        scale
                    );
                    ST7789_StartBufferDMA(ST7789_LINE_BUFFER[buffer_index], byte_count);
                    buffer_index ^= 1U;
                }
                position += payload_size;
            }
            source_x = (uint16_t)(source_x + run_length);
        }
    }
    ST7789_FinishBufferDMA();
}

/*
 * Start an asynchronous ST7789 delta-frame update.
 *
 * The job stores only parser state and reuses the driver's two line buffers.
 * The first complete frame is still drawn by ST7789_ShowIndexed4Image; this
 * entry point is for later delta streams that update only changed runs.
 *
 * Parameters:
 * delta: Encoded row-local delta stream.
 * delta_size: Number of bytes available in delta; zero means no pixels changed.
 * palette: Sixteen RGB565 palette entries.
 * source_width: Source image width before scaling.
 * source_height: Source image height before scaling.
 * scale: Integer expansion factor to the ST7789 visible area.
 *
 * Return value:
 * 1: Job was accepted.
 * 0: Parameters are invalid or another display DMA job is active.
 *
 * Side effects:
 * Resets the asynchronous delta parser state.
 */
uint8_t ST7789_AnimDeltaStart(
    const uint8_t *delta,
    uint32_t delta_size,
    const uint16_t *palette,
    uint16_t source_width,
    uint16_t source_height,
    uint8_t scale
)
{
    if (((delta == 0) && (delta_size != 0U)) || (palette == 0) || (scale == 0U) ||
        (ST7789_ANIM_DELTA_JOB.active != 0U) || (ST7789_IsDMAReady() == 0U) ||
        ((uint32_t)source_width * scale != ST7789_WIDTH) ||
        ((uint32_t)source_height * scale != ST7789_HEIGHT))
    {
        return 0U;
    }

    if (LCD_DMA_GetState() == LCD_DMA_STATE_ERROR)
    {
        ST7789_RecoverDMA();
    }

    if (delta_size == 0U)
    {
        ST7789_ANIM_DELTA_JOB.state = ST7789_ANIM_DELTA_IDLE;
        ST7789_ANIM_DELTA_JOB.active = 0U;
        return 1U;
    }

    ST7789_ANIM_DELTA_JOB.delta = delta;
    ST7789_ANIM_DELTA_JOB.delta_size = delta_size;
    ST7789_ANIM_DELTA_JOB.position = 0U;
    ST7789_ANIM_DELTA_JOB.payload_start = 0U;
    ST7789_ANIM_DELTA_JOB.payload_size = 0U;
    ST7789_ANIM_DELTA_JOB.palette = palette;
    ST7789_ANIM_DELTA_JOB.source_width = source_width;
    ST7789_ANIM_DELTA_JOB.source_height = source_height;
    ST7789_ANIM_DELTA_JOB.source_y = 0U;
    ST7789_ANIM_DELTA_JOB.source_x = 0U;
    ST7789_ANIM_DELTA_JOB.run_length = 0U;
    ST7789_ANIM_DELTA_JOB.scale = scale;
    ST7789_ANIM_DELTA_JOB.repeat_y = 0U;
    ST7789_ANIM_DELTA_JOB.buffer_index = 0U;
    ST7789_ANIM_DELTA_JOB.state = ST7789_ANIM_DELTA_SCAN;
    ST7789_ANIM_DELTA_JOB.active = 1U;

    return 1U;
}

/*
 * Query whether an asynchronous ST7789 delta-frame update is active.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * 1: A delta update is still being parsed or transmitted.
 * 0: No asynchronous delta update is active.
 */
uint8_t ST7789_AnimDeltaBusy(void)
{
    if (ST7789_ANIM_DELTA_JOB.active != 0U)
    {
        return 1U;
    }

    return 0U;
}

/*
 * Advance the asynchronous ST7789 delta-frame update by one DMA-sized step.
 *
 * The function returns quickly while DMA is busy. When DMA is idle it may
 * parse skip runs, set one changed address window, build one line buffer, and
 * start one DMA transfer. Repeated vertical scale lines are sent on later
 * calls, allowing the main loop to keep servicing other cooperative tasks.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * ST7789_ANIM_DELTA_RESULT_BUSY: The job is still active after this call.
 * ST7789_ANIM_DELTA_RESULT_DONE: The job completed normally or is idle.
 * ST7789_ANIM_DELTA_RESULT_ERROR: DMA failed or the delta stream was invalid.
 *
 * Side effects:
 * Sends changed ST7789 pixel runs through SPI2 TX DMA.
 */
ST7789_AnimDeltaResult ST7789_AnimDeltaTask(void)
{
    while (ST7789_ANIM_DELTA_JOB.active != 0U)
    {
        uint8_t buffer_index;
        uint16_t byte_count;

        if (ST7789_IsDMAReady() == 0U)
        {
            return ST7789_ANIM_DELTA_RESULT_BUSY;
        }
        if (LCD_DMA_GetState() == LCD_DMA_STATE_ERROR)
        {
            ST7789_RecoverDMA();
            ST7789_ANIM_DELTA_JOB.state = ST7789_ANIM_DELTA_ERROR;
            ST7789_ANIM_DELTA_JOB.active = 0U;
            return ST7789_ANIM_DELTA_RESULT_ERROR;
        }

        if (ST7789_ANIM_DELTA_JOB.state == ST7789_ANIM_DELTA_REPEAT)
        {
            if (ST7789_ANIM_DELTA_JOB.repeat_y < ST7789_ANIM_DELTA_JOB.scale)
            {
                buffer_index = ST7789_ANIM_DELTA_JOB.buffer_index;
                byte_count = ST7789_BuildIndexed4Line(
                    ST7789_LINE_BUFFER[buffer_index],
                    &ST7789_ANIM_DELTA_JOB.delta[ST7789_ANIM_DELTA_JOB.payload_start],
                    ST7789_ANIM_DELTA_JOB.palette,
                    ST7789_ANIM_DELTA_JOB.run_length,
                    ST7789_ANIM_DELTA_JOB.scale
                );
                if (ST7789_TryStartBufferDMA(ST7789_LINE_BUFFER[buffer_index], byte_count) == 0U)
                {
                    return ST7789_ANIM_DELTA_RESULT_BUSY;
                }
                ST7789_ANIM_DELTA_JOB.buffer_index ^= 1U;
                ST7789_ANIM_DELTA_JOB.repeat_y++;
                return ST7789_ANIM_DELTA_RESULT_BUSY;
            }

            ST7789_WaitStreamComplete();
            ST7789_ANIM_DELTA_JOB.position =
                ST7789_ANIM_DELTA_JOB.payload_start + ST7789_ANIM_DELTA_JOB.payload_size;
            ST7789_ANIM_DELTA_JOB.source_x =
                (uint16_t)(ST7789_ANIM_DELTA_JOB.source_x + ST7789_ANIM_DELTA_JOB.run_length);
            ST7789_ANIM_DELTA_JOB.repeat_y = 0U;
            ST7789_ANIM_DELTA_JOB.state = ST7789_ANIM_DELTA_SCAN;
        }

        while (ST7789_ANIM_DELTA_JOB.source_y < ST7789_ANIM_DELTA_JOB.source_height)
        {
            uint8_t control;

            if (ST7789_ANIM_DELTA_JOB.source_x >= ST7789_ANIM_DELTA_JOB.source_width)
            {
                ST7789_ANIM_DELTA_JOB.source_x = 0U;
                ST7789_ANIM_DELTA_JOB.source_y++;
                continue;
            }

            if (ST7789_ANIM_DELTA_JOB.position >= ST7789_ANIM_DELTA_JOB.delta_size)
            {
                ST7789_ANIM_DELTA_JOB.state = ST7789_ANIM_DELTA_ERROR;
                ST7789_ANIM_DELTA_JOB.active = 0U;
                return ST7789_ANIM_DELTA_RESULT_ERROR;
            }

            control = ST7789_ANIM_DELTA_JOB.delta[ST7789_ANIM_DELTA_JOB.position++];
            ST7789_ANIM_DELTA_JOB.run_length = (uint16_t)((control & 0x7FU) + 1U);
            if (ST7789_ANIM_DELTA_JOB.run_length >
                (uint16_t)(ST7789_ANIM_DELTA_JOB.source_width - ST7789_ANIM_DELTA_JOB.source_x))
            {
                ST7789_ANIM_DELTA_JOB.state = ST7789_ANIM_DELTA_ERROR;
                ST7789_ANIM_DELTA_JOB.active = 0U;
                return ST7789_ANIM_DELTA_RESULT_ERROR;
            }

            if ((control & 0x80U) != 0U)
            {
                ST7789_ANIM_DELTA_JOB.source_x =
                    (uint16_t)(ST7789_ANIM_DELTA_JOB.source_x + ST7789_ANIM_DELTA_JOB.run_length);
                continue;
            }

            ST7789_ANIM_DELTA_JOB.payload_start = ST7789_ANIM_DELTA_JOB.position;
            ST7789_ANIM_DELTA_JOB.payload_size = (ST7789_ANIM_DELTA_JOB.run_length + 1U) >> 1;
            if ((ST7789_ANIM_DELTA_JOB.position + ST7789_ANIM_DELTA_JOB.payload_size) >
                ST7789_ANIM_DELTA_JOB.delta_size)
            {
                ST7789_ANIM_DELTA_JOB.state = ST7789_ANIM_DELTA_ERROR;
                ST7789_ANIM_DELTA_JOB.active = 0U;
                return ST7789_ANIM_DELTA_RESULT_ERROR;
            }

            ST7789_SetAddressWindow(
                (uint16_t)(ST7789_ANIM_DELTA_JOB.source_x * ST7789_ANIM_DELTA_JOB.scale),
                (uint16_t)(ST7789_ANIM_DELTA_JOB.source_y * ST7789_ANIM_DELTA_JOB.scale),
                (uint16_t)((ST7789_ANIM_DELTA_JOB.source_x + ST7789_ANIM_DELTA_JOB.run_length) *
                           ST7789_ANIM_DELTA_JOB.scale - 1U),
                (uint16_t)((ST7789_ANIM_DELTA_JOB.source_y + 1U) *
                           ST7789_ANIM_DELTA_JOB.scale - 1U)
            );
            ST7789_DC_HIGH();

            ST7789_ANIM_DELTA_JOB.repeat_y = 0U;
            ST7789_ANIM_DELTA_JOB.state = ST7789_ANIM_DELTA_REPEAT;
            break;
        }

        if (ST7789_ANIM_DELTA_JOB.source_y >= ST7789_ANIM_DELTA_JOB.source_height)
        {
            if (ST7789_ANIM_DELTA_JOB.position != ST7789_ANIM_DELTA_JOB.delta_size)
            {
                ST7789_ANIM_DELTA_JOB.state = ST7789_ANIM_DELTA_ERROR;
                ST7789_ANIM_DELTA_JOB.active = 0U;
                ST7789_WaitStreamComplete();
                return ST7789_ANIM_DELTA_RESULT_ERROR;
            }

            ST7789_ANIM_DELTA_JOB.state = ST7789_ANIM_DELTA_IDLE;
            ST7789_ANIM_DELTA_JOB.active = 0U;
            ST7789_WaitStreamComplete();
            return ST7789_ANIM_DELTA_RESULT_DONE;
        }
    }

    return ST7789_ANIM_DELTA_RESULT_DONE;
}


/*
 * 初始化 240x240 RGB565 ST7789 屏幕。
 *
 * 流程依次完成 GPIO、SPI2、硬件复位、软件复位、退出睡眠、扫描方向、
 * RGB565 色深、电源和 gamma 参数配置，然后打开显示。背光在清黑屏后
 * 才打开，防止上电时显示随机显存内容。
 *
 * 参数：
 * 无。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 配置 GPIOB 和 SPI2，复位显示控制器，清空屏幕并打开背光。
 */
void ST7789_Init(void)
{
    static const uint8_t gamma_positive[14] =
    {
        0xD0U, 0x04U, 0x0DU, 0x11U, 0x13U, 0x2BU, 0x3FU,
        0x54U, 0x4CU, 0x18U, 0x0DU, 0x0BU, 0x1FU, 0x23U
    };
    static const uint8_t gamma_negative[14] =
    {
        0xD0U, 0x04U, 0x0CU, 0x11U, 0x13U, 0x2CU, 0x3FU,
        0x44U, 0x51U, 0x2FU, 0x1FU, 0x1FU, 0x20U, 0x23U
    };
    uint8_t index;

    ST7789_GPIO_Init();
    ST7789_SPI_Init();
    ST7789_Backlight_Off();
    ST7789_Reset();

    ST7789_WriteCmd(0x01U);
    delay_ms(150U);

    ST7789_WriteCmd(0x11U);
    delay_ms(120U);

    ST7789_WriteCmd(0x36U);
    ST7789_WriteData8(ST7789_MADCTL_VALUE);

    ST7789_WriteCmd(0x3AU);
    ST7789_WriteData8(0x55U);

    ST7789_WriteCmd(0xB2U);
    ST7789_WriteData8(0x0CU);
    ST7789_WriteData8(0x0CU);
    ST7789_WriteData8(0x00U);
    ST7789_WriteData8(0x33U);
    ST7789_WriteData8(0x33U);

    ST7789_WriteCmd(0xB7U);
    ST7789_WriteData8(0x35U);
    ST7789_WriteCmd(0xBBU);
    ST7789_WriteData8(0x19U);
    ST7789_WriteCmd(0xC0U);
    ST7789_WriteData8(0x2CU);
    ST7789_WriteCmd(0xC2U);
    ST7789_WriteData8(0x01U);
    ST7789_WriteCmd(0xC3U);
    ST7789_WriteData8(0x12U);
    ST7789_WriteCmd(0xC4U);
    ST7789_WriteData8(0x20U);
    ST7789_WriteCmd(0xC6U);
    ST7789_WriteData8(0x0FU);

    ST7789_WriteCmd(0xD0U);
    ST7789_WriteData8(0xA4U);
    ST7789_WriteData8(0xA1U);

    ST7789_WriteCmd(0xE0U);
    for (index = 0U; index < 14U; index++)
    {
        ST7789_WriteData8(gamma_positive[index]);
    }

    ST7789_WriteCmd(0xE1U);
    for (index = 0U; index < 14U; index++)
    {
        ST7789_WriteData8(gamma_negative[index]);
    }

    ST7789_WriteCmd(0x21U);
    ST7789_WriteCmd(0x13U);
    ST7789_WriteCmd(0x29U);
    delay_ms(20U);

    ST7789_Clear(ST7789_BLACK);
    ST7789_Backlight_On();
}
