/*
 * st7789.h - ST7789V 240x320 SPI TFT driver (landscape 320x240)
 * Target: STM32F103RB (Nucleo-64), HAL, C99, blocking SPI
 */
#ifndef ST7789_H
#define ST7789_H

#include "main.h"
#include <stdint.h>

/* ---- 화면 크기 (가로 모드) ---- */
#define ST7789_WIDTH    320
#define ST7789_HEIGHT   240

/* 240x320 풀 패널은 오프셋 0. 240x240 모듈이면 여기서 보정한다. */
#define ST7789_XOFF     0
#define ST7789_YOFF     0

/* ---- 핀맵 ---- */
#define ST7789_CS_PORT   GPIOA
#define ST7789_CS_PIN    GPIO_PIN_4
#define ST7789_DC_PORT   GPIOB
#define ST7789_DC_PIN    GPIO_PIN_0
#define ST7789_RST_PORT  GPIOB
#define ST7789_RST_PIN   GPIO_PIN_1

/* ---- RGB565 ---- */
#define ST7789_RGB(r, g, b) \
    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define ST7789_BLACK    0x0000
#define ST7789_WHITE    0xFFFF
#define ST7789_RED      0xF800
#define ST7789_GREEN    0x07E0
#define ST7789_BLUE     0x001F
#define ST7789_YELLOW   0xFFE0
#define ST7789_CYAN     0x07FF
#define ST7789_MAGENTA  0xF81F
#define ST7789_ORANGE   0xFD20
#define ST7789_GRAY     0x8410
#define ST7789_DARKGRAY 0x4208

/* ---- API ---- */
void ST7789_Init(SPI_HandleTypeDef *hspi);
void ST7789_FillScreen(uint16_t color);
void ST7789_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void ST7789_DrawPixel(int16_t x, int16_t y, uint16_t color);
void ST7789_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void ST7789_DrawChar(int16_t x, int16_t y, char ch,
                     uint16_t fg, uint16_t bg, uint8_t scale);
void ST7789_DrawString(int16_t x, int16_t y, const char *s,
                       uint16_t fg, uint16_t bg, uint8_t scale);

/* 1 = 소프트웨어 SPI(진단용), 0 = 하드웨어 SPI1 */
void ST7789_UseBitBang(uint8_t on);

/* 하드웨어 SPI 상태를 기록하고 소프트웨어 SPI로 화면에 출력 */
void ST7789_DiagScreen(SPI_HandleTypeDef *hspi);

/* 디스플레이 반전. 모듈에 따라 색이 뒤집혀 보이면 0으로 호출 */
void ST7789_SetInversion(uint8_t on);

#endif /* ST7789_H */
