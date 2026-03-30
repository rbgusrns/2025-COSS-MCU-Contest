// ======================= panel.h =======================
#ifndef __PANEL_H
#define __PANEL_H

#include "stm32f4xx_hal.h"

#define PANEL_WIDTH 128
#define PANEL_HEIGHT 32
#define PANEL_ROW_GROUPS 16
#define BIT_DEPTH 3

// GPIOA: DATA + CLK + LAT
#define PANEL_PORT_DATA GPIOA
#define PIN_R1 GPIO_PIN_0
#define PIN_G1 GPIO_PIN_1
#define PIN_B1 GPIO_PIN_2
#define PIN_R2 GPIO_PIN_3
#define PIN_G2 GPIO_PIN_4
#define PIN_B2 GPIO_PIN_5
#define PIN_CLK GPIO_PIN_6
#define PIN_LAT GPIO_PIN_7
#define DATA_MASK (PIN_R1|PIN_G1|PIN_B1|PIN_R2|PIN_G2|PIN_B2)

// GPIOB: A,B,C,D + OE
#define PANEL_PORT_CTRL GPIOB
#define PIN_A GPIO_PIN_5
#define PIN_B GPIO_PIN_6
#define PIN_C GPIO_PIN_7
#define PIN_D GPIO_PIN_8
#define PIN_OE GPIO_PIN_9
#define ROW_MASK (PIN_A|PIN_B|PIN_C|PIN_D)

void Panel_GPIO_Init(void);
void Panel_SetPixel(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b);

void Panel_StartRefresh(void);   // TIM 시작용
void Panel_ScanISR(void);        // TIM 인터럽트에서 호출

#endif
