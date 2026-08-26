#include "fluidnc.h"
#include "main.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef  hdma_usart1_rx;

#define RXSZ 1024
static uint8_t  rxBuf[RXSZ];
static uint16_t lastPos;
static char     line[256];
static uint16_t lineLen;

static fnc_data_t st;
static fnc_file_t files[8];
static uint8_t  nfiles;
static uint32_t last_q;
static uint32_t last_ip_req;

void fnc_init(void){
  memset(&st, 0, sizeof(st));
  nfiles = 0; lastPos = 0; lineLen = 0; last_ip_req = 0;
  HAL_UART_Receive_DMA(&huart1, rxBuf, RXSZ);
}
const fnc_data_t* fnc_get(void){ return &st; }

void fnc_rt(uint8_t b){ HAL_UART_Transmit(&huart1, &b, 1, 20); }

void fnc_cmd(const char *s){
  char b[64];
  snprintf(b, sizeof(b), "%s\n", s);
  HAL_UART_Transmit(&huart1, (uint8_t*)b, (uint16_t)strlen(b), 100);
}

void fnc_clear_msg(void){ st.msg[0] = 0; }

void fnc_jog(char axis, const char *dist, uint16_t feed){
  char b[48];
  snprintf(b, sizeof(b), "$J=G91 G21 F%u %c%s", (unsigned)feed, axis, dist);
  fnc_cmd(b);
}
void fnc_jog_cancel(void){ fnc_rt(0x85); }

void fnc_run_sd(const char *name){
  const char *nm = name;
  while(*nm == '/' || *nm == ' ') nm++;
  char b[64];
  snprintf(b, sizeof(b), "$ESP220=sd/%s", nm);
  fnc_cmd(b);
}

static int parse_xyz(const char *p, float *v){
  char *e;
  v[0] = strtof(p, &e);   if(*e != ',') return 0;
  v[1] = strtof(e+1, &e); if(*e != ',') return 0;
  v[2] = strtof(e+1, &e);
  return 1;
}

static void parse_ip(const char *l){
  const char *p = strstr(l, "IP:");
  if(p) p += 3; else p = l;
  while(*p && (*p < '0' || *p > '9')) p++;
  uint8_t i = 0;
  while(*p && ((*p >= '0' && *p <= '9') || *p == '.') && i < sizeof(st.ip)-1) st.ip[i++] = *p++;
  st.ip[i] = 0;
}

static void parse_status(char *s){
  char *save = NULL, *tok = strtok_r(s, "|", &save);
  if(!tok) return;

  st.pins = 0;
  uint8_t hm = 0, hw = 0, hc = 0;

  if(!strncmp(tok, "Idle", 4))        st.state = FNC_IDLE;
  else if(!strncmp(tok, "Run", 3))    st.state = FNC_RUN;
  else if(!strncmp(tok, "Hold", 4))   st.state = FNC_HOLD;
  else if(!strncmp(tok, "Jog", 3))    st.state = FNC_JOG;
  else if(!strncmp(tok, "Home", 4))   st.state = FNC_HOME;
  else if(!strncmp(tok, "Alarm", 5))  st.state = FNC_ALARM;
  else if(!strncmp(tok, "Check", 5))  st.state = FNC_CHECK;
  else if(!strncmp(tok, "Door", 4))   st.state = FNC_DOOR;
  else st.state = FNC_UNKNOWN;

  while((tok = strtok_r(NULL, "|", &save))){
    if(!strncmp(tok, "MPos:", 5)){ if(parse_xyz(tok+5, st.mpos)) hm = 1; }
    else if(!strncmp(tok, "WPos:", 5)){ if(parse_xyz(tok+5, st.wpos)) hw = 1; }
    else if(!strncmp(tok, "WCO:", 4)){ if(parse_xyz(tok+4, st.wco)) hc = 1; }
    else if(!strncmp(tok, "FS:", 3)){
      st.feed = (uint16_t)atoi(tok+3);
      char *c = strchr(tok, ',');
      if(c) st.speed = (uint16_t)atoi(c+1);
    }
    else if(!strncmp(tok, "Pn:", 3)){
      for(char *p = tok+3; *p; p++){
        if(*p == 'X') st.pins |= 1;
        if(*p == 'Y') st.pins |= 2;
        if(*p == 'Z') st.pins |= 4;
      }
    }
    else if(!strncmp(tok, "SD:", 3)){
      st.sd_pct = (uint8_t)atoi(tok+3);
      char *c = strchr(tok, ',');
      if(c){
        strncpy(st.sd_file, c+1, sizeof(st.sd_file)-1);
        st.sd_file[sizeof(st.sd_file)-1] = 0;
      }
    }
  }

  if(!hm && hw && hc) for(uint8_t i = 0; i < 3; i++) st.mpos[i] = st.wpos[i] + st.wco[i];
  if(!hw && hm && hc) for(uint8_t i = 0; i < 3; i++) st.wpos[i] = st.mpos[i] - st.wco[i];

  st.last_rx = HAL_GetTick();
  st.connected = true;
}

static void handle_line(char *l){
  if(l[0] == '<'){
    size_t n = strlen(l);
    if(n && l[n-1] == '>') l[n-1] = 0;
    parse_status(l+1);
  }
  else if(strstr(l, "IP:") || (l[0] >= '0' && l[0] <= '9' && strchr(l, '.'))){
    parse_ip(l);
  }
  else if(!strncmp(l, "[FILE:", 6)){
    if(nfiles >= 8) return;
    char *bar = strchr(l, '|');
    char *sz  = strstr(l, "SIZE:");
    if(bar) *bar = 0;
    const char *name = l + 6;
    if(strstr(name, "WPSettings") || strstr(name, "IndexerVolumeGuid")) return;
    strncpy(files[nfiles].name, name, sizeof(files[0].name)-1);
    files[nfiles].name[sizeof(files[0].name)-1] = 0;
    files[nfiles].size = sz ? (uint32_t)atol(sz+5) : 0;
    nfiles++;
  }
  else if(!strncmp(l, "[MSG:", 5)){
    strncpy(st.msg, l+5, sizeof(st.msg)-1);
    st.msg[sizeof(st.msg)-1] = 0;
    size_t ml = strlen(st.msg);
    if(ml && st.msg[ml-1] == ']') st.msg[ml-1] = 0;
    st.msg_id++;
  }
  else if(!strncmp(l, "error", 5)){
    snprintf(st.msg, sizeof(st.msg), "ERR %s", l);
    st.msg_id++;
  }
}

static void pump(void){
  uint16_t pos = (uint16_t)(RXSZ - __HAL_DMA_GET_COUNTER(&hdma_usart1_rx));
  if(pos != lastPos){ st.last_rx = HAL_GetTick(); st.connected = true; }
  while(pos != lastPos){
    uint8_t b = rxBuf[lastPos];
    lastPos = (lastPos + 1) % RXSZ;
    if(b == '\n' || b == '\r'){
      if(lineLen){ line[lineLen] = 0; handle_line(line); lineLen = 0; }
    }
    else if(lineLen < sizeof(line)-1) line[lineLen++] = (char)b;
  }
}

void fnc_poll(uint32_t now){
  if(huart1.RxState != HAL_UART_STATE_BUSY_RX){
    lineLen = 0; lastPos = 0;
    HAL_UART_AbortReceive(&huart1);
    HAL_UART_Receive_DMA(&huart1, rxBuf, RXSZ);
  }
  pump();
  if(now - last_q > 150){ last_q = now; fnc_rt('?'); }
  if(st.connected && (now - st.last_rx > 8000)) st.connected = false;

  if(st.connected && ((st.ip[0] == 0 && now - last_ip_req > 2000) || now - last_ip_req > 60000)){
    last_ip_req = now;
    fnc_cmd("$ESP111");
  }
}

void fnc_sd_list(fnc_file_t *dst, uint8_t max, uint8_t *cnt){
  nfiles = 0;
  fnc_cmd("$ESP210");
  uint32_t t0 = HAL_GetTick();
  while(HAL_GetTick() - t0 < 600){ pump(); }
  *cnt = (nfiles < max) ? nfiles : max;
  for(uint8_t i = 0; i < *cnt; i++) dst[i] = files[i];
}
