#ifndef TM1638_H
#define TM1638_H
#include <stdint.h>
void tm_init(void);
void tm_poll(uint32_t now_ms);
uint8_t tm_key(void);
uint8_t tm_pressed(void);
uint8_t tm_released(void);
#endif