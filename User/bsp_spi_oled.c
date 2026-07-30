#include "bsp_spi_oled.h"
#include "delay.h"

#define OLED_SPI_PORT              GPIOB
#define OLED_SPI_GPIO_CLK          RCC_APB2Periph_GPIOB
#define OLED_SPI_CLK               RCC_APB1Periph_SPI2

#define OLED_SPI_SCK_PIN           GPIO_Pin_13
#define OLED_SPI_MOSI_PIN          GPIO_Pin_15
#define OLED_SPI_CS_PIN            GPIO_Pin_12
#define OLED_SPI_DC_PIN            GPIO_Pin_14
#define OLED_SPI_RST_PIN           GPIO_Pin_10

#define OLED_SPI_CS_HIGH()         GPIO_SetBits(OLED_SPI_PORT, OLED_SPI_CS_PIN)
#define OLED_SPI_CS_LOW()          GPIO_ResetBits(OLED_SPI_PORT, OLED_SPI_CS_PIN)
#define OLED_SPI_DC_HIGH()         GPIO_SetBits(OLED_SPI_PORT, OLED_SPI_DC_PIN)
#define OLED_SPI_DC_LOW()          GPIO_ResetBits(OLED_SPI_PORT, OLED_SPI_DC_PIN)
#define OLED_SPI_RST_HIGH()        GPIO_SetBits(OLED_SPI_PORT, OLED_SPI_RST_PIN)
#define OLED_SPI_RST_LOW()         GPIO_ResetBits(OLED_SPI_PORT, OLED_SPI_RST_PIN)

/*
 * 开始一次 OLED SPI 传输。
 *
 * 在 CS 拉低前先设置 DC，确保 OLED 能从第一个时钟开始正确区分命令和显示数据。
 * 整页刷新期间保持 CS 为低电平，可减少无意义的 GPIO 翻转。
 *
 * 参数：
 * data_mode：0 表示命令，非 0 表示显示数据。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 改变 OLED 的 DC 和 CS 引脚电平。
 */
static void OLED_SPI_BeginTransfer(uint8_t data_mode)
{
    if (data_mode != 0U)
    {
        OLED_SPI_DC_HIGH();
    }
    else
    {
        OLED_SPI_DC_LOW();
    }

    OLED_SPI_CS_LOW();
}

/*
 * 结束当前 OLED SPI 传输。
 *
 * 先等待发送寄存器为空，再等待 SPI 总线结束忙状态，保证最后一位数据仍在
 * CS 有效期间送达 OLED，之后才释放片选。
 *
 * 参数：
 * 无。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 将 OLED 的 CS 引脚置为高电平。
 */
static void OLED_SPI_EndTransfer(void)
{
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET)
    {
    }

    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY) == SET)
    {
    }

    OLED_SPI_CS_HIGH();
}

/*
 * 初始化四线 OLED 使用的 GPIOB 和硬件 SPI2。
 *
 * STM32 提供 SPI 时钟，因此配置为主机；OLED 只接收数据，不需要 MISO，
 * 使用单线发送即可。常见 SSD1306 模块使用 8 位、MSB 优先、Mode 0。
 * PB12 由软件直接控制 OLED 片选，因此 SPI 内部采用软件 NSS。
 *
 * 参数：
 * 无。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 打开 GPIOB、SPI2 时钟，并配置 PB10、PB12 至 PB15。
 */
void OLED_SPI_Init(void)
{
    GPIO_InitTypeDef gpio;
    SPI_InitTypeDef spi;

    RCC_APB2PeriphClockCmd(OLED_SPI_GPIO_CLK, ENABLE);
    RCC_APB1PeriphClockCmd(OLED_SPI_CLK, ENABLE);

    gpio.GPIO_Pin = OLED_SPI_SCK_PIN | OLED_SPI_MOSI_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(OLED_SPI_PORT, &gpio);

    gpio.GPIO_Pin = OLED_SPI_CS_PIN | OLED_SPI_DC_PIN | OLED_SPI_RST_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(OLED_SPI_PORT, &gpio);

    OLED_SPI_CS_HIGH();
    OLED_SPI_DC_LOW();
    OLED_SPI_RST_HIGH();

    SPI_I2S_DeInit(SPI2);
    spi.SPI_Direction = SPI_Direction_1Line_Tx;       // OLED 不回传数据，无需 MISO
    spi.SPI_Mode = SPI_Mode_Master;                   // STM32 负责产生 SPI 时钟
    spi.SPI_DataSize = SPI_DataSize_8b;               // SSD1306 命令和数据均为 8 位
    spi.SPI_CPOL = SPI_CPOL_Low;                      // Mode 0：空闲时钟为低电平
    spi.SPI_CPHA = SPI_CPHA_1Edge;                    // Mode 0：第一个边沿采样
    spi.SPI_NSS = SPI_NSS_Soft;                       // PB12 作为 GPIO 软件控制 CS
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;
    spi.SPI_FirstBit = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial = 7;
    SPI_Init(SPI2, &spi);
    SPI_NSSInternalSoftwareConfig(SPI2, SPI_NSSInternalSoft_Set);
    SPI_Cmd(SPI2, ENABLE);
}

/*
 * 通过 SPI2 发送一个字节。
 *
 * 这里只等待硬件发送寄存器可写，不产生毫秒级阻塞；DC 和 CS 由上层传输函数
 * 按命令或数据类型统一控制。
 *
 * 参数：
 * data：要发送给 OLED 的字节。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 写入 SPI2 数据寄存器。
 */
void OLED_SPI_WriteByte(uint8_t data)
{
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET)
    {
    }

    SPI_I2S_SendData(SPI2, data);
}

/*
 * 向 SSD1306 发送一个命令字节。
 *
 * 参数：
 * cmd：SSD1306 命令。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 选中 OLED，并通过 SPI2 发送一个命令。
 */
void OLED_SPI_WriteCmd(uint8_t cmd)
{
    OLED_SPI_BeginTransfer(0U);
    OLED_SPI_WriteByte(cmd);
    OLED_SPI_EndTransfer();
}

/*
 * 向 SSD1306 发送一个显示数据字节。
 *
 * 参数：
 * data：按 SSD1306 页格式排列的纵向 8 个像素。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 选中 OLED，并通过 SPI2 发送一个显示数据字节。
 */
void OLED_SPI_WriteData(uint8_t data)
{
    OLED_SPI_BeginTransfer(1U);
    OLED_SPI_WriteByte(data);
    OLED_SPI_EndTransfer();
}

/*
 * 产生 SSD1306 硬件复位脉冲。
 *
 * RST 先保持低电平再恢复高电平，并在电平切换之间保留短延时。该延时只用于
 * 上电初始化，不会出现在动画任务中。
 *
 * 参数：
 * 无。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 驱动 PB10，并阻塞约 30 ms。
 */
void OLED_SPI_Reset(void)
{
    OLED_SPI_RST_HIGH();
    delay_ms(10U);
    OLED_SPI_RST_LOW();
    delay_ms(10U);
    OLED_SPI_RST_HIGH();
    delay_ms(10U);
}

/*
 * 设置后续显示数据使用的 SSD1306 页地址和列地址。
 *
 * 参数：
 * page：页序号，范围 0 至 7。
 * col：列序号，范围 0 至 127。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 发送三条 SSD1306 地址命令；坐标无效时不发送。
 */
void OLED_SPI_SetPos(uint8_t page, uint8_t col)
{
    if ((page >= OLED_SPI_PAGE_COUNT) || (col >= OLED_SPI_WIDTH))
    {
        return;
    }

    OLED_SPI_WriteCmd((uint8_t)(0xB0U + page));
    OLED_SPI_WriteCmd((uint8_t)(0x00U + (col & 0x0FU)));
    OLED_SPI_WriteCmd((uint8_t)(0x10U + ((col >> 4) & 0x0FU)));
}

/*
 * 清零 SSD1306 显示 RAM 的全部 1024 字节。
 *
 * 每页使用一次连续 SPI 传输。该函数用于初始化或明确的画面切换，不应在
 * 主循环每次迭代时反复清屏。
 *
 * 参数：
 * 无。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 用零覆盖 OLED 的全部显示 RAM。
 */
void OLED_SPI_Clear(void)
{
    uint8_t page;
    uint8_t col;

    for (page = 0U; page < OLED_SPI_PAGE_COUNT; page++)
    {
        OLED_SPI_SetPos(page, 0U);
        OLED_SPI_BeginTransfer(1U);

        for (col = 0U; col < OLED_SPI_WIDTH; col++)
        {
            OLED_SPI_WriteByte(0x00U);
        }

        OLED_SPI_EndTransfer();
    }
}

/*
 * 初始化连接在 SPI2 上的 SSD1306 128x64 OLED。
 *
 * 先初始化硬件 SPI 并复位 OLED，再依次设置页寻址、扫描方向、电荷泵、
 * 显示时序、对比度和显示开关，最后清空显示 RAM。
 *
 * 参数：
 * 无。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 初始化 SPI2 和 OLED，并清空屏幕。
 */
void OLED_SPI_PanelInit(void)
{
    OLED_SPI_Init();
    OLED_SPI_Reset();

    OLED_SPI_WriteCmd(0xAEU);
    OLED_SPI_WriteCmd(0x20U);
    OLED_SPI_WriteCmd(0x02U);
    OLED_SPI_WriteCmd(0xB0U);
    OLED_SPI_WriteCmd(0xC8U);
    OLED_SPI_WriteCmd(0x00U);
    OLED_SPI_WriteCmd(0x10U);
    OLED_SPI_WriteCmd(0x40U);
    OLED_SPI_WriteCmd(0x81U);
    OLED_SPI_WriteCmd(0x7FU);
    OLED_SPI_WriteCmd(0xA1U);
    OLED_SPI_WriteCmd(0xA6U);
    OLED_SPI_WriteCmd(0xA8U);
    OLED_SPI_WriteCmd(0x3FU);
    OLED_SPI_WriteCmd(0xA4U);
    OLED_SPI_WriteCmd(0xD3U);
    OLED_SPI_WriteCmd(0x00U);
    OLED_SPI_WriteCmd(0xD5U);
    OLED_SPI_WriteCmd(0x80U);
    OLED_SPI_WriteCmd(0xD9U);
    OLED_SPI_WriteCmd(0xF1U);
    OLED_SPI_WriteCmd(0xDAU);
    OLED_SPI_WriteCmd(0x12U);
    OLED_SPI_WriteCmd(0xDBU);
    OLED_SPI_WriteCmd(0x40U);
    OLED_SPI_WriteCmd(0x8DU);
    OLED_SPI_WriteCmd(0x14U);
    OLED_SPI_WriteCmd(0xAFU);

    OLED_SPI_Clear();
}

/*
 * 获取一个字符对应的 5 列测试字模。
 *
 * 字库只包含启动提示文字需要的字符，避免为单次硬件测试占用过多 Flash。
 * 不支持的字符按空格显示。
 *
 * 参数：
 * ch：要查询的字符。
 *
 * 返回值：
 * 指向 5 个页格式字模字节的指针。
 *
 * 副作用：
 * 无。
 */
static const uint8_t *OLED_SPI_GetTestFont(char ch)
{
    static const uint8_t font_space[5] = {0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
    static const uint8_t font_d[5] = {0x38U, 0x44U, 0x44U, 0x48U, 0x7FU};
    static const uint8_t font_e[5] = {0x38U, 0x54U, 0x54U, 0x54U, 0x18U};
    static const uint8_t font_i[5] = {0x00U, 0x44U, 0x7DU, 0x40U, 0x00U};
    static const uint8_t font_l[5] = {0x00U, 0x41U, 0x7FU, 0x40U, 0x00U};
    static const uint8_t font_o[5] = {0x38U, 0x44U, 0x44U, 0x44U, 0x38U};
    static const uint8_t font_p[5] = {0x7CU, 0x14U, 0x14U, 0x14U, 0x08U};
    static const uint8_t font_s[5] = {0x48U, 0x54U, 0x54U, 0x54U, 0x20U};

    switch (ch)
    {
        case 'D':
        case 'd':
            return font_d;
        case 'E':
        case 'e':
            return font_e;
        case 'I':
        case 'i':
            return font_i;
        case 'L':
        case 'l':
            return font_l;
        case 'O':
        case 'o':
            return font_o;
        case 'P':
        case 'p':
            return font_p;
        case 'S':
        case 's':
            return font_s;
        default:
            return font_space;
    }
}

/*
 * 显示一个 5x7 测试字符，并追加一列字符间隔。
 *
 * 参数：
 * page：目标页，范围 0 至 7。
 * col：字符左侧列，范围 0 至 122。
 * ch：使用精简启动字库显示的字符。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 向 OLED 显示 RAM 写入 6 字节。
 */
void OLED_SPI_ShowChar6x8(uint8_t page, uint8_t col, char ch)
{
    uint8_t index;
    const uint8_t *font;

    if ((page >= OLED_SPI_PAGE_COUNT) || (col > 122U))
    {
        return;
    }

    font = OLED_SPI_GetTestFont(ch);
    OLED_SPI_SetPos(page, col);
    OLED_SPI_BeginTransfer(1U);

    for (index = 0U; index < 5U; index++)
    {
        OLED_SPI_WriteByte(font[index]);
    }

    OLED_SPI_WriteByte(0x00U);
    OLED_SPI_EndTransfer();
}

/*
 * 使用精简 6x8 字库显示以空字符结尾的启动字符串。
 *
 * 参数：
 * page：目标页，范围 0 至 7。
 * col：字符串起始列。
 * text：以空字符结尾的字符串指针。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 将支持的字符写入 OLED 显示 RAM。
 */
void OLED_SPI_ShowString6x8(uint8_t page, uint8_t col, const char *text)
{
    if (text == 0)
    {
        return;
    }

    while ((*text != '\0') && (col <= 122U))
    {
        OLED_SPI_ShowChar6x8(page, col, *text);
        col = (uint8_t)(col + 6U);
        text++;
    }
}

/*
 * 显示一张 SSD1306 页格式的完整 128x64 图片。
 *
 * 输入数组由连续 8 页组成，每页 128 字节；每字节最低位对应该页最上方像素。
 * 函数直接从 Flash 读取常量数组，不使用 RAM 帧缓冲和动态内存。
 *
 * 参数：
 * image：指向 OLED_SPI_FRAME_SIZE 字节图片数据的指针。
 *
 * 返回值：
 * 无。
 *
 * 副作用：
 * 覆盖 OLED 的全部显示 RAM；空指针不会产生写操作。
 */
void OLED_SPI_ShowImage128x64(const uint8_t *image)
{
    uint8_t page;
    uint8_t col;
    uint16_t offset;

    if (image == 0)
    {
        return;
    }

    offset = 0U;
    for (page = 0U; page < OLED_SPI_PAGE_COUNT; page++)
    {
        OLED_SPI_SetPos(page, 0U);
        OLED_SPI_BeginTransfer(1U);

        for (col = 0U; col < OLED_SPI_WIDTH; col++)
        {
            OLED_SPI_WriteByte(image[offset]);
            offset++;
        }

        OLED_SPI_EndTransfer();
    }
}
