#include "settings.h"
#include "main.h"

#define SETTINGS_ADDR 0x0800F800UL
#define MAGIC         0xC0DF   /* змінено, щоб старі збережені значення скинулись */

static uint16_t crcsum(const uint16_t *d, uint16_t n){
  uint16_t s = 0;
  for(uint16_t i = 0; i < n; i++) s += d[i];
  return s;
}

void settings_load(settings_t *s){
  const volatile uint16_t *p = (const volatile uint16_t*)SETTINGS_ADDR;
  uint16_t d[5];
  for(int i = 0; i < 5; i++) d[i] = p[i];

  if(d[0] == MAGIC && d[4] == crcsum(d, 4)){
    s->feed     = d[1];
    s->jog_mode = d[2] & 0xFF;
    s->profile  = (d[2] >> 8) & 0x01;
    s->power[0] = d[3] & 0xFF;
    s->power[1] = (d[3] >> 8) & 0xFF;
    if(s->feed < 100 || s->feed > 15000) s->feed = 5000;
    if(s->jog_mode > 4) s->jog_mode = 0;
    if(s->power[0] < 1 || s->power[0] > 100) s->power[0] = 30;
    if(s->power[1] < 1 || s->power[1] > 100) s->power[1] = 100;
  } else {
    s->feed = 5000; s->jog_mode = 0; s->profile = 0;
    s->power[0] = 30;    /* шпиндель */
    s->power[1] = 100;   /* лазер — відразу багато, щоб пройшов поріг */
  }
}

void settings_save(const settings_t *s){
  uint16_t d[5];
  d[0] = MAGIC;
  d[1] = s->feed;
  d[2] = (uint16_t)(s->jog_mode | (s->profile << 8));
  d[3] = (uint16_t)(s->power[0] | (s->power[1] << 8));
  d[4] = crcsum(d, 4);

  HAL_FLASH_Unlock();
  FLASH_EraseInitTypeDef er;
  uint32_t page_err = 0;
  er.TypeErase   = FLASH_TYPEERASE_PAGES;
  er.PageAddress = SETTINGS_ADDR;
  er.NbPages     = 1;
  HAL_FLASHEx_Erase(&er, &page_err);
  for(int i = 0; i < 5; i++)
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, SETTINGS_ADDR + (uint32_t)i*2, d[i]);
  HAL_FLASH_Lock();
}
