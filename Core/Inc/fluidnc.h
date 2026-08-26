#ifndef FLUIDNC_H
#define FLUIDNC_H
#include <stdint.h>
#include <stdbool.h>

typedef enum { FNC_UNKNOWN, FNC_IDLE, FNC_RUN, FNC_HOLD, FNC_JOG,
               FNC_HOME, FNC_ALARM, FNC_CHECK, FNC_DOOR } fnc_state_e;

typedef struct {
  fnc_state_e state;
  float   mpos[3], wpos[3], wco[3];
  uint16_t feed, speed;
  uint8_t  pins;
  char     sd_file[32];
  uint8_t  sd_pct;
  char     msg[48];
  uint8_t  msg_id;
  char     ip[20];
  uint32_t last_rx;
  bool     connected;
} fnc_data_t;

typedef struct { char name[48]; uint32_t size; } fnc_file_t;

void fnc_init(void);
void fnc_poll(uint32_t now);
const fnc_data_t* fnc_get(void);
void fnc_sd_list(fnc_file_t *dst, uint8_t max, uint8_t *cnt);
void fnc_cmd(const char *s);
void fnc_rt(uint8_t b);
void fnc_jog(char axis, const char *dist, uint16_t feed);
void fnc_jog_cancel(void);
void fnc_run_sd(const char *name);
void fnc_clear_msg(void);
#endif
