#include "oled.h"

/*
   OLED 接线：
   VCC -> 3.3V
   GND -> GND
   SCL -> PB6
   SDA -> PB7

   常见 0.96 寸 OLED 地址：
   7位地址：0x3C
   写地址：0x78
*/

#define OLED_GPIO_PORT     GPIOB
#define OLED_GPIO_CLK      RCC_APB2Periph_GPIOB

#define OLED_SCL_PIN       GPIO_Pin_6
#define OLED_SDA_PIN       GPIO_Pin_7

#define OLED_I2C_ADDR      0x78

#define OLED_SCL_H()       GPIO_SetBits(OLED_GPIO_PORT, OLED_SCL_PIN)
#define OLED_SCL_L()       GPIO_ResetBits(OLED_GPIO_PORT, OLED_SCL_PIN)

#define OLED_SDA_H()       GPIO_SetBits(OLED_GPIO_PORT, OLED_SDA_PIN)
#define OLED_SDA_L()       GPIO_ResetBits(OLED_GPIO_PORT, OLED_SDA_PIN)

static void OLED_I2C_Delay(void)
{
    volatile uint8_t i;

    for (i = 0; i < 10; i++)
    {
    }
}

static void OLED_DelayMs(uint32_t ms)
{
    uint32_t i;
    uint32_t j;

    for (i = 0; i < ms; i++)
    {
        for (j = 0; j < 8000; j++)
        {
        }
    }
}

static void OLED_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(OLED_GPIO_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Pin = OLED_SCL_PIN | OLED_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_Init(OLED_GPIO_PORT, &GPIO_InitStructure);

    OLED_SCL_H();
    OLED_SDA_H();
}

static void OLED_I2C_Start(void)
{
    OLED_SDA_H();
    OLED_SCL_H();
    OLED_I2C_Delay();

    OLED_SDA_L();
    OLED_I2C_Delay();

    OLED_SCL_L();
    OLED_I2C_Delay();
}

static void OLED_I2C_Stop(void)
{
    OLED_SCL_L();
    OLED_SDA_L();
    OLED_I2C_Delay();

    OLED_SCL_H();
    OLED_I2C_Delay();

    OLED_SDA_H();
    OLED_I2C_Delay();
}

static void OLED_I2C_WriteByte(uint8_t data)
{
    uint8_t i;

    for (i = 0; i < 8; i++)
    {
        if (data & 0x80)
        {
            OLED_SDA_H();
        }
        else
        {
            OLED_SDA_L();
        }

        OLED_I2C_Delay();

        OLED_SCL_H();
        OLED_I2C_Delay();

        OLED_SCL_L();
        OLED_I2C_Delay();

        data <<= 1;
    }

    /*
       第 9 个时钟是 ACK 位。
       这里暂时不读取 ACK，只释放 SDA。
    */
    OLED_SDA_H();
    OLED_I2C_Delay();

    OLED_SCL_H();
    OLED_I2C_Delay();

    OLED_SCL_L();
    OLED_I2C_Delay();
}

static void OLED_WriteCommand(uint8_t command)
{
    OLED_I2C_Start();
    OLED_I2C_WriteByte(OLED_I2C_ADDR);
    OLED_I2C_WriteByte(0x00);
    OLED_I2C_WriteByte(command);
    OLED_I2C_Stop();
}

static void OLED_WriteData(uint8_t data)
{
    OLED_I2C_Start();
    OLED_I2C_WriteByte(OLED_I2C_ADDR);
    OLED_I2C_WriteByte(0x40);
    OLED_I2C_WriteByte(data);
    OLED_I2C_Stop();
}

static void OLED_SetPos(uint8_t page, uint8_t column)
{
    OLED_WriteCommand(0xB0 + page);
    OLED_WriteCommand(0x00 + (column & 0x0F));
    OLED_WriteCommand(0x10 + ((column >> 4) & 0x0F));
}

void OLED_Clear(void)
{
    uint8_t page;
    uint8_t column;

    for (page = 0; page < 8; page++)
    {
        OLED_SetPos(page, 0);

        for (column = 0; column < 128; column++)
        {
            OLED_WriteData(0x00);
        }
    }
}

void OLED_Fill(uint8_t data)
{
    uint8_t page;
    uint8_t column;

    for (page = 0; page < 8; page++)
    {
        OLED_SetPos(page, 0);

        for (column = 0; column < 128; column++)
        {
            OLED_WriteData(data);
        }
    }
}

void OLED_Init(void)
{
    OLED_GPIO_Init();

    OLED_DelayMs(100);

    OLED_WriteCommand(0xAE);    // 关闭显示
    OLED_WriteCommand(0x20);    // 设置内存寻址模式
    OLED_WriteCommand(0x10);    // 页寻址模式
    OLED_WriteCommand(0xB0);    // 设置页起始地址
    OLED_WriteCommand(0xC8);    // COM 扫描方向
    OLED_WriteCommand(0x00);    // 低列地址
    OLED_WriteCommand(0x10);    // 高列地址
    OLED_WriteCommand(0x40);    // 起始行地址
    OLED_WriteCommand(0x81);    // 对比度
    OLED_WriteCommand(0x7F);
    OLED_WriteCommand(0xA1);    // 段重映射
    OLED_WriteCommand(0xA6);    // 正常显示
    OLED_WriteCommand(0xA8);    // 多路复用率
    OLED_WriteCommand(0x3F);
    OLED_WriteCommand(0xA4);    // 显示内容来自 RAM
    OLED_WriteCommand(0xD3);    // 显示偏移
    OLED_WriteCommand(0x00);
    OLED_WriteCommand(0xD5);    // 显示时钟分频
    OLED_WriteCommand(0x80);
    OLED_WriteCommand(0xD9);    // 预充电周期
    OLED_WriteCommand(0xF1);
    OLED_WriteCommand(0xDA);    // COM 引脚配置
    OLED_WriteCommand(0x12);
    OLED_WriteCommand(0xDB);    // VCOMH
    OLED_WriteCommand(0x40);
    OLED_WriteCommand(0x8D);    // 电荷泵
    OLED_WriteCommand(0x14);
    OLED_WriteCommand(0xAF);    // 开启显示

    OLED_Clear();
}

static const uint8_t *OLED_GetMiniFont(char ch)
{
    static const uint8_t font_space[5] = {0x00,0x00,0x00,0x00,0x00};
    static const uint8_t font_colon[5] = {0x00,0x36,0x36,0x00,0x00};

    static const uint8_t font_L[5] = {0x7F,0x40,0x40,0x40,0x40};
    static const uint8_t font_i[5] = {0x00,0x44,0x7D,0x40,0x00};
    static const uint8_t font_g[5] = {0x48,0x54,0x54,0x54,0x3C};
    static const uint8_t font_h[5] = {0x7F,0x08,0x04,0x04,0x78};
    static const uint8_t font_t[5] = {0x04,0x3F,0x44,0x40,0x20};

    static const uint8_t font_B[5] = {0x7F,0x49,0x49,0x49,0x36};
    static const uint8_t font_r[5] = {0x7C,0x08,0x04,0x04,0x08};
    static const uint8_t font_D[5] = {0x7F,0x41,0x41,0x22,0x1C};
    static const uint8_t font_a[5] = {0x20,0x54,0x54,0x54,0x78};
    static const uint8_t font_k[5] = {0x7F,0x10,0x28,0x44,0x00};
	
	static const uint8_t font_0[5] = {0x3E,0x51,0x49,0x45,0x3E};
	static const uint8_t font_1[5] = {0x00,0x42,0x7F,0x40,0x00};
	static const uint8_t font_2[5] = {0x42,0x61,0x51,0x49,0x46};
	static const uint8_t font_3[5] = {0x21,0x41,0x45,0x4B,0x31};
	static const uint8_t font_4[5] = {0x18,0x14,0x12,0x7F,0x10};
	static const uint8_t font_5[5] = {0x27,0x45,0x45,0x45,0x39};
	static const uint8_t font_6[5] = {0x3C,0x4A,0x49,0x49,0x30};
	static const uint8_t font_7[5] = {0x01,0x71,0x09,0x05,0x03};
	static const uint8_t font_8[5] = {0x36,0x49,0x49,0x49,0x36};
	static const uint8_t font_9[5] = {0x06,0x49,0x49,0x29,0x1E};
	
	
    switch (ch)
    {
        case ' ': return font_space;
        case ':': return font_colon;

        case 'L': return font_L;
        case 'i': return font_i;
        case 'g': return font_g;
        case 'h': return font_h;
        case 't': return font_t;

        case 'B': return font_B;
        case 'r': return font_r;
        case 'D': return font_D;
        case 'a': return font_a;
        case 'k': return font_k;

		case '0': return font_0;
		case '1': return font_1;
		case '2': return font_2;
		case '3': return font_3;
		case '4': return font_4;
		case '5': return font_5;
		case '6': return font_6;
		case '7': return font_7;
		case '8': return font_8;
		case '9': return font_9;
		
        default: return font_space;
    }
}

void OLED_ShowChar(uint8_t page, uint8_t column, char ch)
{
    uint8_t i;
    const uint8_t *font;

    font = OLED_GetMiniFont(ch);

    OLED_SetPos(page, column);

    for (i = 0; i < 5; i++)
    {
        OLED_WriteData(font[i]);
    }

    OLED_WriteData(0x00);    // 字符间隔
}

void OLED_ShowString(uint8_t page, uint8_t column, const char *str)
{
    while (*str != '\0')
    {
        OLED_ShowChar(page, column, *str);
        column += 6;
        str++;

        if (column > 122)
        {
            break;
        }
    }
}

void OLED_ShowNum(uint8_t page, uint8_t column, uint16_t num, uint8_t len)
{
    uint8_t i;
    uint16_t div = 1;
    uint8_t digit;

    for (i = 1; i < len; i++)
    {
        div *= 10;
    }

    for (i = 0; i < len; i++)
    {
        digit = num / div;
        OLED_ShowChar(page, column, digit + '0');

        num %= div;
        div /= 10;
        column += 6;
    }
}
