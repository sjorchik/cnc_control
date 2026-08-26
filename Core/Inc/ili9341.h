#ifndef ILI9341_H
#define ILI9341_H
#include <stdint.h>

#define LCD_W 320
#define LCD_H 240

#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_BLUE   0x001F
#define C_RED    0xF800
#define C_GREEN  0x07E0
#define C_YELLOW 0xFFE0
#define C_OLIVE  0x7BE0
#define C_MAROON 0x7800
#define C_NAVY   0x000F

void lcd_init(void);
void lcd_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t col);
void lcd_frame(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t col);
void lcd_text(uint16_t x, uint16_t y, uint8_t scale, const char *s, uint16_t fg, uint16_t bg);
void lcd_text_r(uint16_t xr, uint16_t y, uint8_t scale, const char *s, uint16_t fg, uint16_t bg);
#endif