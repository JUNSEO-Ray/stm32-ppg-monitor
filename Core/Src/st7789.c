/*
 * st7789.c - ST7789V driver, blocking SPI (bring-up 단계용)
 * DMA 전환은 화면 점등 검증 후에 한다.
 */
#include "st7789.h"
#include "font5x7.h"

/* 1 = 진단용 소프트웨어 SPI, 0 = 하드웨어 SPI1. ST7789_UseBitBang()으로 변경 */
static uint8_t st_use_bb = 0;

static SPI_HandleTypeDef *st_spi;

/* 한 줄 최대 320픽셀 x 2바이트 */
static uint8_t line_buf[ST7789_WIDTH * 2];

#define CS_LOW()   HAL_GPIO_WritePin(ST7789_CS_PORT,  ST7789_CS_PIN,  GPIO_PIN_RESET)
#define CS_HIGH()  HAL_GPIO_WritePin(ST7789_CS_PORT,  ST7789_CS_PIN,  GPIO_PIN_SET)
#define DC_CMD()   HAL_GPIO_WritePin(ST7789_DC_PORT,  ST7789_DC_PIN,  GPIO_PIN_RESET)
#define DC_DAT()   HAL_GPIO_WritePin(ST7789_DC_PORT,  ST7789_DC_PIN,  GPIO_PIN_SET)
#define RST_LOW()  HAL_GPIO_WritePin(ST7789_RST_PORT, ST7789_RST_PIN, GPIO_PIN_RESET)
#define RST_HIGH() HAL_GPIO_WritePin(ST7789_RST_PORT, ST7789_RST_PIN, GPIO_PIN_SET)

/* ------------------------------------------------------------------ */
/* 저수준 전송                                                          */
/* ------------------------------------------------------------------ */

/*
 * 진단용 소프트웨어 SPI (Mode 0, MSB first).
 * SPI 주변장치를 쓰지 않고 PA5(SCK), PA7(MOSI)를 GPIO로 직접 토글한다.
 * 이것으로 화면이 나오면 하드웨어 SPI 설정 쪽 문제, 안 나오면 배선/모듈 쪽 문제.
 */
#define SCK_H()   HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET)
#define SCK_L()   HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET)
#define MOSI_H()  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_SET)
#define MOSI_L()  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET)

static void bb_delay(void)
{
    volatile uint8_t i;
    for (i = 0; i < 10; i++) {
    }
}

static void bb_tx(const uint8_t *d, uint16_t n)
{
    uint8_t b, bit;

    while (n--) {
        b = *d++;
        for (bit = 0; bit < 8; bit++) {
            if (b & 0x80) { MOSI_H(); } else { MOSI_L(); }
            bb_delay();
            SCK_H();            /* 화면은 SCK 상승 엣지에서 비트를 읽는다 */
            bb_delay();
            SCK_L();
            b <<= 1;
        }
    }
}

static void bb_pins_init(void)
{
    GPIO_InitTypeDef g = {0};

    g.Pin   = GPIO_PIN_5 | GPIO_PIN_7;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &g);     /* SPI 대체 기능을 덮어쓰고 일반 출력으로 전환 */
    SCK_L();
    MOSI_L();
}
static void st_tx(const uint8_t *d, uint16_t n)
{
    if (st_use_bb) {
        bb_tx(d, n);
    } else {
        HAL_SPI_Transmit(st_spi, (uint8_t *)d, n, HAL_MAX_DELAY);
    }
}

void ST7789_UseBitBang(uint8_t on)
{
    st_use_bb = on ? 1u : 0u;
}

static void st_cmd(uint8_t c)
{
    DC_CMD();
    st_tx(&c, 1);
}

static void st_dat(const uint8_t *d, uint16_t n)
{
    DC_DAT();
    st_tx(d, n);
}

static void st_dat8(uint8_t v)
{
    DC_DAT();
    st_tx(&v, 1);
}

/* 명령 + 파라미터를 한 번에 */
static void st_cmd_n(uint8_t c, const uint8_t *p, uint8_t n)
{
    st_cmd(c);
    if (n) {
        st_dat(p, n);
    }
}

/* ------------------------------------------------------------------ */
/* 주소 윈도우                                                          */
/* ------------------------------------------------------------------ */

static void st_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t b[4];

    x0 += ST7789_XOFF; x1 += ST7789_XOFF;
    y0 += ST7789_YOFF; y1 += ST7789_YOFF;

    b[0] = (uint8_t)(x0 >> 8); b[1] = (uint8_t)x0;
    b[2] = (uint8_t)(x1 >> 8); b[3] = (uint8_t)x1;
    st_cmd_n(0x2A, b, 4);               /* CASET */

    b[0] = (uint8_t)(y0 >> 8); b[1] = (uint8_t)y0;
    b[2] = (uint8_t)(y1 >> 8); b[3] = (uint8_t)y1;
    st_cmd_n(0x2B, b, 4);               /* RASET */

    st_cmd(0x2C);                        /* RAMWR */
    DC_DAT();
}

/* ------------------------------------------------------------------ */
/* 초기화                                                              */
/* ------------------------------------------------------------------ */

void ST7789_Init(SPI_HandleTypeDef *hspi)
{
    static const uint8_t porctrl[]  = { 0x0C, 0x0C, 0x00, 0x33, 0x33 };
    static const uint8_t pwctrl1[]  = { 0xA4, 0xA1 };
    static const uint8_t gamma_p[]  = { 0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B,
                                        0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B,
                                        0x1F, 0x23 };
    static const uint8_t gamma_n[]  = { 0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C,
                                        0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F,
                                        0x20, 0x23 };

    st_spi = hspi;
    if (st_use_bb) {
        bb_pins_init();
    }

    CS_HIGH();
    DC_DAT();

    /*
     * SPI 사전 활성화.
     * STM32F1 SPI는 첫 전송 때 켜지며(SPE=1), 그 순간 SCK가 한 번 튈 수 있다.
     * CS가 Low인 상태에서 튀면 화면이 이를 1비트로 받아 이후 모든 바이트가 밀린다.
     * CS를 High로 둔 채 더미 1바이트를 보내 SPI를 미리 켜고 SCK를 idle 레벨로 고정한다.
     */
    if (!st_use_bb) {
        uint8_t dummy = 0x00;
        HAL_SPI_Transmit(st_spi, &dummy, 1, HAL_MAX_DELAY);
    }

    /* 하드웨어 리셋 */
    RST_HIGH(); HAL_Delay(10);
    RST_LOW();  HAL_Delay(20);
    RST_HIGH(); HAL_Delay(150);

    CS_LOW();

    st_cmd(0x01);               /* SWRESET */
    HAL_Delay(150);

    st_cmd(0x11);               /* SLPOUT  */
    HAL_Delay(150);

    st_cmd(0x3A); st_dat8(0x55);        /* COLMOD: 16bit/pixel */
    HAL_Delay(10);

    /* MADCTL 0x60 = MX | MV  -> 가로 320x240 */
    st_cmd(0x36); st_dat8(0x60);

    st_cmd_n(0xB2, porctrl, 5);         /* PORCTRL  */
    st_cmd(0xB7); st_dat8(0x35);        /* GCTRL    */
    st_cmd(0xBB); st_dat8(0x19);        /* VCOMS    */
    st_cmd(0xC0); st_dat8(0x2C);        /* LCMCTRL  */
    st_cmd(0xC2); st_dat8(0x01);        /* VDVVRHEN */
    st_cmd(0xC3); st_dat8(0x12);        /* VRHS     */
    st_cmd(0xC4); st_dat8(0x20);        /* VDVS     */
    st_cmd(0xC6); st_dat8(0x0F);        /* FRCTRL2: 60Hz */
    st_cmd_n(0xD0, pwctrl1, 2);         /* PWCTRL1  */
    st_cmd_n(0xE0, gamma_p, 14);        /* PVGAMCTRL */
    st_cmd_n(0xE1, gamma_n, 14);        /* NVGAMCTRL */

    st_cmd(0x21);               /* INVON - ST7789 IPS 패널 기본 */
    HAL_Delay(10);
    st_cmd(0x13);               /* NORON */
    HAL_Delay(10);
    CS_HIGH();

    /*
     * 화면 메모리(GRAM)는 모듈에 전원이 있는 한 이전 그림을 유지한다.
     * 화면을 켜기(DISPON) 전에 먼저 검은색으로 지워서 이전 그림이 보이지 않게 한다.
     */
    ST7789_FillScreen(ST7789_BLACK);

    CS_LOW();
    st_cmd(0x29);               /* DISPON */
    HAL_Delay(50);
    CS_HIGH();
}

void ST7789_SetInversion(uint8_t on)
{
    CS_LOW();
    st_cmd(on ? 0x21 : 0x20);
    CS_HIGH();
}

/* ------------------------------------------------------------------ */
/* 그리기                                                              */
/* ------------------------------------------------------------------ */

void ST7789_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    int16_t i;
    uint16_t n;
    uint8_t hi, lo;

    /* 클리핑 */
    if (w <= 0 || h <= 0) return;
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ST7789_WIDTH)  w = ST7789_WIDTH  - x;
    if (y + h > ST7789_HEIGHT) h = ST7789_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    hi = (uint8_t)(color >> 8);
    lo = (uint8_t)(color & 0xFF);
    for (n = 0; n < (uint16_t)w; n++) {
        line_buf[n * 2]     = hi;
        line_buf[n * 2 + 1] = lo;
    }

    CS_LOW();
    st_set_window((uint16_t)x, (uint16_t)y,
                  (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
    for (i = 0; i < h; i++) {
        st_tx(line_buf, (uint16_t)(w * 2));
    }
    CS_HIGH();
}

void ST7789_FillScreen(uint16_t color)
{
    ST7789_FillRect(0, 0, ST7789_WIDTH, ST7789_HEIGHT, color);
}

void ST7789_DrawPixel(int16_t x, int16_t y, uint16_t color)
{
    uint8_t b[2];

    if (x < 0 || y < 0 || x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;

    b[0] = (uint8_t)(color >> 8);
    b[1] = (uint8_t)(color & 0xFF);

    CS_LOW();
    st_set_window((uint16_t)x, (uint16_t)y, (uint16_t)x, (uint16_t)y);
    st_tx(b, 2);
    CS_HIGH();
}

void ST7789_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (w <= 0 || h <= 0) return;
    ST7789_FillRect(x,         y,         w, 1, color);
    ST7789_FillRect(x,         y + h - 1, w, 1, color);
    ST7789_FillRect(x,         y,         1, h, color);
    ST7789_FillRect(x + w - 1, y,         1, h, color);
}

/* ------------------------------------------------------------------ */
/* 텍스트 (5x7 폰트, 문자 셀 6x8)                                       */
/* ------------------------------------------------------------------ */

void ST7789_DrawChar(int16_t x, int16_t y, char ch,
                     uint16_t fg, uint16_t bg, uint8_t scale)
{
    const uint8_t *glyph;
    uint16_t cw, chh, row, col, i, c;
    uint8_t fr, fc, on;

    if (scale == 0) scale = 1;
    if ((uint8_t)ch < 0x20 || (uint8_t)ch > 0x7E) ch = '?';

    glyph = font5x7[(uint8_t)ch - 0x20];
    cw  = (uint16_t)(6 * scale);
    chh = (uint16_t)(8 * scale);

    if (x < 0 || y < 0 ||
        x + (int16_t)cw > ST7789_WIDTH || y + (int16_t)chh > ST7789_HEIGHT) {
        return;     /* bring-up 단계에서는 부분 클리핑 생략 */
    }

    CS_LOW();
    st_set_window((uint16_t)x, (uint16_t)y,
                  (uint16_t)(x + cw - 1), (uint16_t)(y + chh - 1));

    for (row = 0; row < chh; row++) {
        fr = (uint8_t)(row / scale);
        i = 0;
        for (col = 0; col < cw; col++) {
            fc = (uint8_t)(col / scale);
            on = (fc < 5) ? (uint8_t)((glyph[fc] >> fr) & 0x01) : 0;
            c  = on ? fg : bg;
            line_buf[i++] = (uint8_t)(c >> 8);
            line_buf[i++] = (uint8_t)(c & 0xFF);
        }
        st_tx(line_buf, (uint16_t)(cw * 2));
    }
    CS_HIGH();
}

void ST7789_DrawString(int16_t x, int16_t y, const char *s,
                       uint16_t fg, uint16_t bg, uint8_t scale)
{
    if (scale == 0) scale = 1;
    while (*s) {
        ST7789_DrawChar(x, y, *s++, fg, bg, scale);
        x += (int16_t)(6 * scale);
        if (x + (int16_t)(6 * scale) > ST7789_WIDTH) break;
    }
}

/* ------------------------------------------------------------------ */
/* 진단: 하드웨어 SPI 상태를 기록한 뒤 소프트웨어 SPI로 화면에 출력       */
/* ------------------------------------------------------------------ */

static void hex32(char *out, uint32_t v)
{
    static const char hx[] = "0123456789ABCDEF";
    int i;
    for (i = 7; i >= 0; i--) {
        out[i] = hx[v & 0xFu];
        v >>= 4;
    }
    out[8] = '\0';
}

static void diag_line(int16_t y, const char *label, uint32_t v)
{
    char buf[24];
    uint8_t i = 0;

    while (*label && i < 12) {
        buf[i++] = *label++;
    }
    hex32(&buf[i], v);
    ST7789_DrawString(4, y, buf, ST7789_WHITE, ST7789_BLACK, 2);
}

void ST7789_DiagScreen(SPI_HandleTypeDef *hspi)
{
    uint8_t probe[4] = { 0x00, 0x00, 0x00, 0x00 };
    uint32_t ret, state, err, cr1, sr, crl, mapr, apb2, ms, t0;

    st_spi = hspi;

    /* 1) CS를 High로 둔 채(화면은 무시) 하드웨어 SPI로 4바이트 시험 전송 */
    CS_HIGH();
    t0    = HAL_GetTick();
    ret   = (uint32_t)HAL_SPI_Transmit(hspi, probe, 4, 100);
    ms    = HAL_GetTick() - t0;
    state = (uint32_t)hspi->State;
    err   = hspi->ErrorCode;
    cr1   = hspi->Instance->CR1;
    sr    = hspi->Instance->SR;
    crl   = GPIOA->CRL;        /* PA0~PA7 핀 모드 */
    mapr  = AFIO->MAPR;        /* 핀 재배치 설정 */
    apb2  = RCC->APB2ENR;      /* 주변장치 클럭 */

    /* 2) 동작이 확인된 소프트웨어 SPI로 전환해서 결과 출력 */
    ST7789_UseBitBang(1);
    ST7789_Init(hspi);

    ST7789_DrawString(4, 2, "SPI DIAG", ST7789_YELLOW, ST7789_BLACK, 2);
    diag_line( 24, "RET  ", ret);
    diag_line( 44, "MS   ", ms);
    diag_line( 64, "STATE", state);
    diag_line( 84, "ERR  ", err);
    diag_line(104, "CR1  ", cr1);
    diag_line(124, "SR   ", sr);
    diag_line(144, "CRL  ", crl);
    diag_line(164, "MAPR ", mapr);
    diag_line(184, "APB2 ", apb2);
}
