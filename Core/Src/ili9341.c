#include "ili9341.h"
#include "font5x7.h"
#include "board.h"
#include <string.h>

extern SPI_HandleTypeDef hspi1;

#define LCD_ROTATION 0x28

#define CS_LOW()  HAL_GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  GPIO_PIN_RESET)
#define CS_HIGH() HAL_GPIO_WritePin(LCD_CS_PORT,  LCD_CS_PIN,  GPIO_PIN_SET)
#define DC_CMD()  HAL_GPIO_WritePin(LCD_DC_PORT,  LCD_DC_PIN,  GPIO_PIN_RESET)
#define DC_DATA() HAL_GPIO_WritePin(LCD_DC_PORT,  LCD_DC_PIN,  GPIO_PIN_SET)

static void wr_cmd(uint8_t c){ DC_CMD();  CS_LOW(); HAL_SPI_Transmit(&hspi1, &c, 1, 50); CS_HIGH(); }
static void wr_d8(uint8_t d){ DC_DATA(); CS_LOW(); HAL_SPI_Transmit(&hspi1, &d, 1, 50); CS_HIGH(); }
static void wr_buf(const uint8_t *p, uint16_t n){
  DC_DATA(); CS_LOW();
  HAL_SPI_Transmit(&hspi1, (uint8_t*)p, n, 200);
  CS_HIGH();
}
static void cmd2(uint8_t c, uint8_t d){ wr_cmd(c); wr_d8(d); }

static void set_win(uint16_t x, uint16_t y, uint16_t w, uint16_t h){
  uint16_t xe = x + w - 1, ye = y + h - 1;
  wr_cmd(0x2A); wr_d8(x >> 8); wr_d8(x); wr_d8(xe >> 8); wr_d8(xe);
  wr_cmd(0x2B); wr_d8(y >> 8); wr_d8(y); wr_d8(ye >> 8); wr_d8(ye);
  wr_cmd(0x2C);
}

void lcd_init(void){
  HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET); HAL_Delay(10);
  HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET);  HAL_Delay(120);

  wr_cmd(0xEF); wr_d8(0x03); wr_d8(0x80); wr_d8(0x02);
  wr_cmd(0xCF); wr_d8(0x00); wr_d8(0xC1); wr_d8(0x30);
  wr_cmd(0xED); wr_d8(0x64); wr_d8(0x03); wr_d8(0x12); wr_d8(0x81);
  wr_cmd(0xE8); wr_d8(0x85); wr_d8(0x00); wr_d8(0x78);
  wr_cmd(0xCB); wr_d8(0x39); wr_d8(0x2C); wr_d8(0x00); wr_d8(0x34); wr_d8(0x02);
  wr_cmd(0xF7); wr_d8(0x20);
  wr_cmd(0xEA); wr_d8(0x00); wr_d8(0x00);
  cmd2(0xC0, 0x23);
  cmd2(0xC1, 0x10);
  wr_cmd(0xC5); wr_d8(0x3E); wr_d8(0x28);
  cmd2(0xC7, 0x86);
  cmd2(0x36, LCD_ROTATION);
  cmd2(0x3A, 0x55);
  wr_cmd(0xB1); wr_d8(0x00); wr_d8(0x18);
  wr_cmd(0xB6); wr_d8(0x08); wr_d8(0x82); wr_d8(0x27);
  cmd2(0xF2, 0x00);
  cmd2(0x26, 0x01);
  static const uint8_t g1[15] = {0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,0x37,0x07,0x10,0x03,0x0E,0x09,0x00};
  static const uint8_t g2[15] = {0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0x0F,0x48,0x08,0x2F,0x0C,0x31,0x36,0x0F};
  wr_cmd(0xE0); for(int i = 0; i < 15; i++) wr_d8(g1[i]);
  wr_cmd(0xE1); for(int i = 0; i < 15; i++) wr_d8(g2[i]);
  wr_cmd(0x11); HAL_Delay(150);
  wr_cmd(0x29); HAL_Delay(50);
  lcd_fill(0, 0, LCD_W, LCD_H, C_BLACK);
}

void lcd_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t col){
  if(x + w > LCD_W || y + h > LCD_H) return;
  set_win(x, y, w, h);
  static uint8_t row[640];
  for(uint16_t i = 0; i < w; i++){ row[2*i] = col >> 8; row[2*i+1] = col & 0xFF; }
  for(uint16_t r = 0; r < h; r++) wr_buf(row, (uint16_t)(w*2));
}

void lcd_frame(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t col){
  lcd_fill(x, y, w, 1, col);
  lcd_fill(x, y+h-1, w, 1, col);
  lcd_fill(x, y, 1, h, col);
  lcd_fill(x+w-1, y, 1, h, col);
}

void lcd_text(uint16_t x, uint16_t y, uint8_t scale, const char *s, uint16_t fg, uint16_t bg){
  if(scale < 1) scale = 1;
  if(scale > 3) scale = 3;
  uint8_t pw = 6*scale, ph = 8*scale;
  static uint8_t px[6*3*8*3*2];
  while(*s){
    char c = *s++;
    if(c < 32 || c > 127) c = '?';
    const uint8_t *g = font5x7 + (c-32)*5;
    if(x + pw > LCD_W){ x = 0; y += ph; if(y + ph > LCD_H) return; }
    set_win(x, y, pw, ph);
    uint16_t n = 0;
    for(uint8_t fy = 0; fy < 8; fy++)
      for(uint8_t sy = 0; sy < scale; sy++)
        for(uint8_t fx = 0; fx < 6; fx++)
          for(uint8_t sx = 0; sx < scale; sx++){
            uint16_t col = (fx < 5 && fy < 7 && (g[fx] >> fy & 1)) ? fg : bg;
            px[n++] = col >> 8;
            px[n++] = col & 0xFF;
          }
    wr_buf(px, (uint16_t)(pw*ph*2));
    x += pw;
  }
}

void lcd_text_r(uint16_t xr, uint16_t y, uint8_t scale, const char *s, uint16_t fg, uint16_t bg){
  uint16_t w = (uint16_t)strlen(s) * 6 * scale;
  lcd_text(xr > w ? xr - w : 0, y, scale, s, fg, bg);
}