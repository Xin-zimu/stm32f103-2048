#include "app_st7789_test.h"
#include "bsp_st7789.h"

#define TEST_DARK_GRAY             0x4208U // 25 percent gray
#define TEST_MID_GRAY              0x8410U // 50 percent gray
#define TEST_LIGHT_GRAY            0xC618U // 75 percent gray

/*
 * Draw a fixed 240x240 panel orientation and color diagnostic pattern.
 *
 * The white outer border verifies full-screen coverage. Asymmetric L markers
 * identify every corner: yellow at top-left, cyan at top-right, magenta at
 * bottom-left, and white at bottom-right. RGB and CMY blocks verify RGB565
 * channel order, while the center cross and gray steps expose mirroring,
 * clipping, and gross gamma errors. The image is assembled from rectangles,
 * so it requires no frame buffer or large Flash image.
 *
 * Parameters:
 * None.
 *
 * Return value:
 * None.
 *
 * Side effects:
 * Overwrites the complete visible ST7789 area and leaves the test image on
 * screen until another drawing operation is performed.
 */
void App_ST7789_ShowDiagnosticPattern(void)
{
    ST7789_Clear(ST7789_BLACK);

    ST7789_FillRect(0U, 0U, ST7789_WIDTH, 4U, ST7789_WHITE);
    ST7789_FillRect(
        0U,
        ST7789_HEIGHT - 4U,
        ST7789_WIDTH,
        4U,
        ST7789_WHITE
    );
    ST7789_FillRect(0U, 0U, 4U, ST7789_HEIGHT, ST7789_WHITE);
    ST7789_FillRect(
        ST7789_WIDTH - 4U,
        0U,
        4U,
        ST7789_HEIGHT,
        ST7789_WHITE
    );

    ST7789_FillRect(8U, 8U, 48U, 8U, ST7789_YELLOW);
    ST7789_FillRect(8U, 8U, 8U, 48U, ST7789_YELLOW);

    ST7789_FillRect(184U, 8U, 48U, 8U, ST7789_CYAN);
    ST7789_FillRect(224U, 8U, 8U, 48U, ST7789_CYAN);

    ST7789_FillRect(8U, 224U, 48U, 8U, ST7789_MAGENTA);
    ST7789_FillRect(8U, 184U, 8U, 48U, ST7789_MAGENTA);

    ST7789_FillRect(184U, 224U, 48U, 8U, ST7789_WHITE);
    ST7789_FillRect(224U, 184U, 8U, 48U, ST7789_WHITE);

    ST7789_FillRect(20U, 56U, 60U, 36U, ST7789_RED);
    ST7789_FillRect(90U, 56U, 60U, 36U, ST7789_GREEN);
    ST7789_FillRect(160U, 56U, 60U, 36U, ST7789_BLUE);

    ST7789_FillRect(20U, 100U, 60U, 36U, ST7789_CYAN);
    ST7789_FillRect(90U, 100U, 60U, 36U, ST7789_MAGENTA);
    ST7789_FillRect(160U, 100U, 60U, 36U, ST7789_YELLOW);

    ST7789_FillRect(116U, 146U, 8U, 48U, ST7789_WHITE);
    ST7789_FillRect(92U, 166U, 56U, 8U, ST7789_WHITE);
    ST7789_FillRect(118U, 148U, 4U, 44U, ST7789_RED);
    ST7789_FillRect(94U, 168U, 52U, 4U, ST7789_BLUE);

    ST7789_FillRect(72U, 202U, 28U, 14U, TEST_DARK_GRAY);
    ST7789_FillRect(106U, 202U, 28U, 14U, TEST_MID_GRAY);
    ST7789_FillRect(140U, 202U, 28U, 14U, TEST_LIGHT_GRAY);
}
