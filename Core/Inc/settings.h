#ifndef SETTINGS_H
#define SETTINGS_H
#include <stdint.h>

typedef struct {
  uint16_t feed;
  uint8_t  jog_mode;
  uint8_t  profile;      /* 0 = SPINDLE, 1 = LASER */
  uint8_t  power[2];     /* % потужності профілів */
} settings_t;

void settings_load(settings_t *s);
void settings_save(const settings_t *s);
#endif
