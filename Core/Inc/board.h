#ifndef BOARD_H
#define BOARD_H
#include "main.h"

#define LCD_CS_PORT  GPIOA
#define LCD_CS_PIN   GPIO_PIN_3
#define LCD_DC_PORT  GPIOA
#define LCD_DC_PIN   GPIO_PIN_2
#define LCD_RST_PORT GPIOA
#define LCD_RST_PIN  GPIO_PIN_0

#define TM_STB_PORT  GPIOC
#define TM_STB_PIN   GPIO_PIN_13
#define TM_CLK_PORT  GPIOC
#define TM_CLK_PIN   GPIO_PIN_14
#define TM_DIO_PORT  GPIOC
#define TM_DIO_PIN   GPIO_PIN_15
#endif