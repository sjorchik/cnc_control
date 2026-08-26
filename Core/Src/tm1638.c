#include "tm1638.h"
#include "board.h"

static volatile uint8_t v_key, v_press, v_release;

/* ---------- автоповтор ----------
 * Діє лише для осьових кнопок (X/Y/Z +/-) — безпечно в усіх режимах:
 * кроковий джог, правка F/S, оверрайди в Run, навігація в SD.
 * Командні/тумблерні кнопки не повторюються.
 */
#define REPEAT_DELAY_MS   500   /* утримання до початку повтору */
#define REPEAT_PERIOD_MS  200   /* період повтору */

static uint8_t is_repeatable(uint8_t k){
  switch(k){
    case 3:  /* XDOWN */
    case 19: /* XUP   */
    case 12: /* YDOWN */
    case 10: /* YUP   */
    case 7:  /* ZDOWN */
    case 8:  /* ZUP   */
      return 1;
    default:
      return 0;
  }
}

static void dly_us(void){ volatile int i = 10; while(i--); }
static void sda(uint8_t v){ HAL_GPIO_WritePin(TM_DIO_PORT, TM_DIO_PIN, v ? GPIO_PIN_SET : GPIO_PIN_RESET); }
static uint8_t sda_in(void){ return HAL_GPIO_ReadPin(TM_DIO_PORT, TM_DIO_PIN) == GPIO_PIN_SET; }
static void clk(uint8_t v){ HAL_GPIO_WritePin(TM_CLK_PORT, TM_CLK_PIN, v ? GPIO_PIN_SET : GPIO_PIN_RESET); }

static void send(uint8_t b){
  for(uint8_t i = 0; i < 8; i++){
    sda(b & 1); b >>= 1;
    clk(1); dly_us(); clk(0); dly_us();
  }
}
static uint8_t recv(void){
  uint8_t v = 0;
  sda(1);
  for(uint8_t i = 0; i < 8; i++){
    v >>= 1;
    if(sda_in()) v |= 0x80;
    clk(1); dly_us(); clk(0); dly_us();
  }
  return v;
}

static uint8_t scan_raw(void){
  uint8_t key = 0;
  HAL_GPIO_WritePin(TM_STB_PORT, TM_STB_PIN, GPIO_PIN_RESET);
  send(0x42);
  uint8_t c[4];
  for(uint8_t i = 0; i < 4; i++) c[i] = recv();
  HAL_GPIO_WritePin(TM_STB_PORT, TM_STB_PIN, GPIO_PIN_SET);
  for(uint8_t i = 0; i < 4; i++){
    if(c[i] == 0x04) key = 1 + (2*i);
    if(c[i] == 0x40) key = 2 + (2*i);
    if(c[i] == 0x02) key = 9 + (2*i);
    if(c[i] == 0x20) key = 10 + (2*i);
    if(c[i] == 0x01) key = 17 + (2*i);
    if(c[i] == 0x10) key = 18 + (2*i);
  }
  return key;
}

void tm_init(void){ v_key = v_press = v_release = 0; }

void tm_poll(uint32_t now){
  static uint32_t t = 0, cnt = 0, last = 0xFF;
  static uint8_t stable = 0, prev = 0;
  static uint32_t next_rep = 0;

  if(now - t < 10) return;
  t = now;

  uint8_t r = scan_raw();
  if(r == last){ if(cnt < 255) cnt++; } else { last = r; cnt = 0; }
  if(cnt >= 3) stable = last;

  if(stable != prev){
    if(prev) v_release = prev;
    if(stable){
      v_press = stable;
      next_rep = now + REPEAT_DELAY_MS;
    }
    prev = stable;
  }
  else if(stable && is_repeatable(stable) && now >= next_rep){
    v_press = stable;                    /* імпульс автоповтору */
    next_rep = now + REPEAT_PERIOD_MS;
  }

  v_key = stable;
}

uint8_t tm_key(void){ return v_key; }
uint8_t tm_pressed(void){ uint8_t k = v_press; v_press = 0; return k; }
uint8_t tm_released(void){ uint8_t k = v_release; v_release = 0; return k; }
