#include "main.h"
#include "ui.h"
#include "ili9341.h"
#include "tm1638.h"
#include "fluidnc.h"
#include "settings.h"
#include <string.h>
#include <stdio.h>

#define K_PRB   1
#define K_UNLC  2
#define K_XDOWN 3
#define K_HOME  4
#define K_ZZER  5
#define K_Z0    6
#define K_ZDOWN 7
#define K_ZUP   8
#define K_YUP   10
#define K_XY0   11
#define K_YDOWN 12
#define K_YZER  13
#define K_XZER  14
#define K_AXLK  15
#define K_START 16
#define K_JOG   18
#define K_XUP   19
#define K_SPJG  20
#define K_RST   21
#define K_SDCRD 22
#define K_SPND  23
#define K_HOLD  24

#define SPN_LONG_MS 800

/* Прогресбар: центрований */
#define BAR_FX 50
#define BAR_FY 26
#define BAR_FW 220
#define BAR_FH 20
#define BAR_X  52
#define BAR_Y  28
#define BAR_W  216
#define BAR_H  16

/* Список файлів SD */
#define SD_ROW_H  24
#define SD_Y0     30
#define SD_TXT_X  22
#define SD_ARR_X  6

/* Великі координати: scale 3 (цифра 18x24), 3 рядки */
#define POS_Y0   74
#define POS_DY   32
#define MPOS_X   38
#define WPOS_X   190
#define POS_W    126

static uint8_t  screen;
static uint8_t  jog_mode;
static uint8_t  edit_mode;
static uint16_t feed;
static uint8_t  profile;
static uint8_t  power[2];
static bool     sp_on, ax_lock, jog_active;
static uint8_t  sd_cur, sd_cnt;
static fnc_file_t sd_files[8];
static uint8_t  fprc = 100, sprc = 100;
static bool     stat_dirty = true;

static bool     jog_dirty = false;
static bool     edit_dirty = false;
static bool     ax_dirty = false;
static bool     prof_dirty = false;

static uint32_t dyn_t;
static uint8_t  sd_cur_drawn = 255;
static uint16_t bar_w = 0;
static bool     spn_held, spn_long;
static uint32_t spn_t;
static settings_t cfg;
static bool     save_pending;
static uint32_t save_t;

static const char *jog_names[5] = {"JOG","0.1","1","10","100"};
static const char *jog_dist[5]  = {"9999","0.1","1","10","100"};

/* ---------- кеш полів ---------- */
#define FIELD_N   24
#define FIELD_STR 40

typedef struct { uint16_t x, y; uint8_t sc; uint16_t col; char s[FIELD_STR]; } fslot_t;
static fslot_t fslots[FIELD_N];

static void field(uint16_t x, uint16_t y, uint8_t sc, uint16_t w, const char *s, uint16_t fg){
  uint8_t maxc = w / (6*sc);
  if(maxc > FIELD_STR-1) maxc = FIELD_STR-1;
  char buf[FIELD_STR];
  uint8_t i = 0;
  while(i < maxc && s[i]) { buf[i] = s[i]; i++; }
  while(i < maxc) buf[i++] = ' ';
  buf[maxc] = 0;
  fslot_t *sl = 0;
  for(uint8_t k = 0; k < FIELD_N; k++)
    if(fslots[k].sc && fslots[k].x == x && fslots[k].y == y && fslots[k].sc == sc){ sl = &fslots[k]; break; }
  if(!sl)
    for(uint8_t k = 0; k < FIELD_N; k++)
      if(fslots[k].sc == 0){ sl = &fslots[k]; break; }
  if(sl && sl->col == fg && strncmp(sl->s, buf, maxc) == 0) return;
  for(i = 0; i < maxc; i++){
    if(!sl || sl->s[i] != buf[i] || sl->col != fg){
      char one[2]; one[0] = buf[i]; one[1] = 0;
      lcd_text(x + i*6*sc, y, sc, one, fg, C_BLACK);
    }
  }
  if(sl){ sl->x = x; sl->y = y; sl->sc = sc; sl->col = fg; memcpy(sl->s, buf, maxc+1); }
}

static void fields_invalidate(void){ memset(fslots, 0, sizeof(fslots)); }

static void lcd_text_center(uint16_t ax, uint16_t ay, uint16_t aw, uint16_t ah,
                            uint8_t scale, const char *s, uint16_t fg, uint16_t bg){
  uint16_t tw = (uint16_t)strlen(s) * 6 * scale;
  uint16_t th = 8 * scale;
  uint16_t x = ax + (aw > tw ? (aw - tw) / 2 : 0);
  uint16_t y = ay + (ah > th ? (ah - th) / 2 : 0);
  lcd_text(x, y, scale, s, fg, bg);
}

static void draw_cursor_arrow(uint16_t x, uint16_t y, uint16_t col){
  for(uint8_t i = 0; i < 16; i++){
    uint8_t d = (i < 8) ? i : (15 - i);
    lcd_fill(x, y + i, 3 + d, 1, col);
  }
}

/* Координати: рівно 7 символів, мінус біля цифр, вирівняно вправо */
static void fmt_pos(char *out, float v){
  int mil = (int)(v*100.0f + (v >= 0 ? 0.5f : -0.5f));
  char neg = (mil < 0) ? 1 : 0;
  if(neg) mil = -mil;
  if(mil > 99999) mil = 99999;
  char tmp[12];
  snprintf(tmp, sizeof(tmp), "%d.%02d", mil/100, mil%100);
  uint8_t len = (uint8_t)strlen(tmp);
  uint8_t pos = 0;
  uint8_t total = (uint8_t)(len + neg);
  while(pos + total < 7) out[pos++] = ' ';
  if(neg) out[pos++] = '-';
  memcpy(out + pos, tmp, len + 1);
}

static void do_save(void){
  cfg.feed = feed; cfg.jog_mode = jog_mode; cfg.profile = profile;
  cfg.power[0] = power[0]; cfg.power[1] = power[1];
  settings_save(&cfg);
}

static void request_save(void){ save_pending = true; save_t = HAL_GetTick(); }

static void tool_cmd(void){
  char b[24];
  snprintf(b, sizeof(b), "M3 S%u", (unsigned)power[profile] * 10);
  fnc_cmd(b);
  fnc_cmd("G1 F1000");
}

/* ================= ЧАСТКОВЕ ОНОВЛЕННЯ ================= */

static void draw_jog_buttons(void){
  const fnc_data_t *s = fnc_get();
  if(s->state != FNC_RUN){
    for(uint8_t i = 0; i < 5; i++){
      uint16_t x = 4 + i*63;
      uint8_t sel = (jog_mode == i);
      lcd_fill(x, 26, 60, 26, sel ? C_WHITE : C_BLACK);
      lcd_frame(x, 26, 60, 26, C_WHITE);
      lcd_text_center(x, 26, 60, 26, 2, jog_names[i],
                      sel ? C_BLUE : C_YELLOW, sel ? C_WHITE : C_BLACK);
    }
  }
}

static void draw_edit_mode_fields(void){
  lcd_fill(90, 182, 109, 20, (edit_mode == 1) ? C_WHITE : C_BLACK);
  lcd_text_center(90, 182, 109, 20, 2, profile ? "LASER" : "SPINDLE",
                  (edit_mode == 1) ? C_BLUE : C_OLIVE,
                  (edit_mode == 1) ? C_WHITE : C_BLACK);
  lcd_fill(210, 182, 105, 20, (edit_mode == 2) ? C_WHITE : C_BLACK);
  lcd_text_center(210, 182, 105, 20, 2, "FEED",
                  (edit_mode == 2) ? C_BLUE : C_OLIVE,
                  (edit_mode == 2) ? C_WHITE : C_BLACK);
}

static void draw_axis_lock(void){
  lcd_text_center(4, 207, 76, 29, 2, ax_lock ? "FREE" : "LOCK",
                  ax_lock ? C_OLIVE : C_MAROON, C_BLACK);
}

static void draw_profile(void){
  lcd_fill(90, 182, 109, 20, (edit_mode == 1) ? C_WHITE : C_BLACK);
  lcd_text_center(90, 182, 109, 20, 2, profile ? "LASER" : "SPINDLE",
                  (edit_mode == 1) ? C_BLUE : C_OLIVE,
                  (edit_mode == 1) ? C_WHITE : C_BLACK);
}

/* ================= СТАТИКА ================= */
static void draw_main_static(void){
  const fnc_data_t *s = fnc_get();
  lcd_fill(0, 0, LCD_W, LCD_H, C_BLACK);

  if(s->state != FNC_RUN){
    for(uint8_t i = 0; i < 5; i++){
      uint16_t x = 4 + i*63;
      uint8_t sel = (jog_mode == i);
      lcd_fill(x, 26, 60, 26, sel ? C_WHITE : C_BLACK);
      lcd_frame(x, 26, 60, 26, C_WHITE);
      lcd_text_center(x, 26, 60, 26, 2, jog_names[i],
                      sel ? C_BLUE : C_YELLOW, sel ? C_WHITE : C_BLACK);
    }
  } else {
    lcd_fill(0, 26, LCD_W, 26, C_BLACK);
    lcd_frame(BAR_FX, BAR_FY, BAR_FW, BAR_FH, C_WHITE);
    lcd_fill(BAR_X - 1, BAR_Y - 1, BAR_W + 2, BAR_H + 2, C_BLACK);
  }

  /* Шапки колонок координат (scale 2, центровані над стовпчиками) */
  lcd_text(77, 56, 2, "MPos", C_WHITE, C_BLACK);
  lcd_text(229, 56, 2, "WPos", C_WHITE, C_BLACK);

  /* --- Картка AXIS --- */
  lcd_frame(4, 178, 76, 58, C_WHITE);
  lcd_text_center(4, 178, 76, 29, 2, "AXIS", C_OLIVE, C_BLACK);
  lcd_text_center(4, 207, 76, 29, 2, ax_lock ? "FREE" : "LOCK",
                  ax_lock ? C_OLIVE : C_MAROON, C_BLACK);

  /* --- Картка SPINDLE/LASER --- */
  lcd_frame(89, 178, 111, 58, C_WHITE);
  lcd_fill(90, 182, 109, 20, (edit_mode == 1) ? C_WHITE : C_BLACK);
  lcd_text_center(90, 182, 109, 20, 2, profile ? "LASER" : "SPINDLE",
                  (edit_mode == 1) ? C_BLUE : C_OLIVE,
                  (edit_mode == 1) ? C_WHITE : C_BLACK);

  /* --- Картка FEED --- */
  lcd_frame(209, 178, 107, 58, C_WHITE);
  lcd_fill(210, 182, 105, 20, (edit_mode == 2) ? C_WHITE : C_BLACK);
  lcd_text_center(210, 182, 105, 20, 2, "FEED",
                  (edit_mode == 2) ? C_BLUE : C_OLIVE,
                  (edit_mode == 2) ? C_WHITE : C_BLACK);
  /* "V6" прибрано */
}

static void draw_sd_static(void){
  lcd_fill(0, 0, LCD_W, LCD_H, C_BLACK);
  lcd_text(4, 2, 2, "SD CARD", C_WHITE, C_BLACK);
  if(sd_cnt == 0) lcd_text(4, 60, 2, "NO FILES", C_RED, C_BLACK);
  for(uint8_t i = 0; i < sd_cnt; i++){
    const char *dn = sd_files[i].name;
    if(!strncmp(dn, "/sd/", 4)) dn += 4;
    else if(dn[0] == '/') dn += 1;
    char b[32];
    snprintf(b, sizeof(b), "%.16s %luk", dn, (unsigned long)(sd_files[i].size/1024));
    lcd_text(SD_TXT_X, SD_Y0 + i*SD_ROW_H + 4, 2, b, C_WHITE, C_BLACK);
  }
  lcd_text(4, 228, 1, "Y+/- NAV  XY0=RUN  RST=BACK", C_OLIVE, C_BLACK);
  sd_cur_drawn = 255;
}

/* ================= ДИНАМІКА ================= */
static void draw_main_dyn(void){
  const fnc_data_t *s = fnc_get();
  bool running = (s->state == FNC_RUN);
  char b[24];

  const char *stxt = "-----"; uint16_t sc = C_YELLOW;
  switch(s->state){
    case FNC_IDLE:  stxt = "IDLE";  sc = C_YELLOW; break;
    case FNC_RUN:   stxt = "RUN";   sc = C_GREEN;  break;
    case FNC_HOLD:  stxt = "HOLD";  sc = C_BLUE;   break;
    case FNC_JOG:   stxt = "JOG";   sc = C_GREEN;  break;
    case FNC_HOME:  stxt = "HOME";  sc = C_GREEN;  break;
    case FNC_ALARM: stxt = "ALARM"; sc = C_RED;    break;
    default: break;
  }
  if(!s->connected){ stxt = "NO LINK"; sc = C_RED; }
  field(4, 4, 2, 110, stxt, sc);

  if(running){
    const char *f = strchr(s->sd_file, '/');
    char nm[22];
    snprintf(nm, sizeof(nm), "%.21s", f ? f+1 : s->sd_file);
    field(120, 2, 1, 150, nm, C_YELLOW);
    snprintf(b, sizeof(b), "%3u%%", (unsigned)s->sd_pct);
    field(280, 2, 1, 36, b, C_YELLOW);

    uint16_t bw = (uint16_t)((BAR_W * s->sd_pct) / 100);
    if(bw > BAR_W) bw = BAR_W;
    if(bw > bar_w)      lcd_fill(BAR_X + bar_w, BAR_Y, bw - bar_w, BAR_H, C_BLUE);
    else if(bw < bar_w) lcd_fill(BAR_X + bw, BAR_Y, bar_w - bw, BAR_H, C_BLACK);
    bar_w = bw;
  } else {
    field(120, 2, 1, 196, s->msg[0] ? s->msg : "", C_OLIVE);
  }

  if(s->ip[0]){ snprintf(b, sizeof(b), "IP:%s", s->ip); field(120, 12, 1, 196, b, C_OLIVE); }
  else field(120, 12, 1, 196, "", C_OLIVE);

  /* Великі координати: scale 3 (18x24) */
  for(uint8_t a = 0; a < 3; a++){
    uint16_t y = POS_Y0 + a*POS_DY;
    uint16_t lc = ((s->pins >> a) & 1) ? C_RED : C_WHITE;
    char lb[3]; lb[0] = (char)('X'+a); lb[1] = ':'; lb[2] = 0;
    field(4, y + 4, 2, 30, lb, lc);
    char t[16];
    fmt_pos(t, s->mpos[a]); field(MPOS_X, y, 3, POS_W, t, C_WHITE);
    fmt_pos(t, s->wpos[a]); field(WPOS_X, y, 3, POS_W, t, C_WHITE);
  }

  if(running){
    snprintf(b, sizeof(b), "F:%5u", (unsigned)s->feed);
    field(102, 202, 2, 99, b, C_OLIVE);
    snprintf(b, sizeof(b), "S:%5u", (unsigned)s->speed);
    field(102, 218, 2, 99, b, C_OLIVE);

    snprintf(b, sizeof(b), "F:%3u%%", (unsigned)fprc);
    field(226, 202, 2, 95, b, C_OLIVE);
    snprintf(b, sizeof(b), "S:%3u%%", (unsigned)sprc);
    field(226, 218, 2, 95, b, C_OLIVE);
  } else {
    snprintf(b, sizeof(b), "%3u%% %s", (unsigned)power[profile], sp_on ? "ON" : "OFF");
    field(96, 210, 2, 99, b, sp_on ? C_MAROON : C_OLIVE);
    snprintf(b, sizeof(b), "%5u", (unsigned)feed);
    field(232, 210, 2, 95, b, C_OLIVE);
  }
}

static void draw_sd_dyn(void){
  if(sd_cur_drawn != sd_cur){
    if(sd_cur_drawn != 255 && sd_cur_drawn < sd_cnt)
      lcd_fill(4, SD_Y0 + sd_cur_drawn*SD_ROW_H, 16, SD_ROW_H, C_BLACK);
    if(sd_cnt) draw_cursor_arrow(SD_ARR_X, SD_Y0 + sd_cur*SD_ROW_H + 4, C_YELLOW);
    sd_cur_drawn = sd_cur;
  }
}

/* ================= КНОПКИ ================= */
static void on_key_main(uint8_t k){
  const fnc_data_t *s = fnc_get();

  if(s->state == FNC_RUN){
    switch(k){
      case K_YUP:   if(fprc < 200) fprc += 10; fnc_rt(0x91); break;
      case K_YDOWN: if(fprc > 10)  fprc -= 10; fnc_rt(0x92); break;
      case K_XUP:   if(fprc < 200) fprc++;    fnc_rt(0x93); break;
      case K_XDOWN: if(fprc > 10)  fprc--;    fnc_rt(0x94); break;
      case K_XY0:   fprc = 100; fnc_rt(0x90); break;
      case K_ZUP:   if(sprc < 200) sprc += 10; fnc_rt(0x9A); break;
      case K_ZDOWN: if(sprc > 10)  sprc -= 10; fnc_rt(0x9B); break;
      case K_Z0:    sprc = 100; fnc_rt(0x99); break;
      case K_HOLD:  fnc_rt('!'); break;
      case K_START: fnc_rt('~'); break;
      case K_UNLC:  fnc_cmd("$X"); break;
      case K_RST:   fnc_rt(0x18); break;
      default: break;
    }
    return;
  }

  switch(k){
    case K_JOG:
      jog_mode = (jog_mode+1) % 5;
      jog_dirty = true;
      request_save();
      break;

    case K_SPJG:
      edit_mode = (edit_mode+1) % 3;
      edit_dirty = true;
      break;

    case K_XDOWN: case K_XUP: case K_YDOWN:
    case K_YUP:   case K_ZDOWN: case K_ZUP: {
      char ax = (k == K_XDOWN || k == K_XUP) ? 'X' : (k == K_YDOWN || k == K_YUP) ? 'Y' : 'Z';
      const char *sgn = (k == K_XDOWN || k == K_YDOWN || k == K_ZDOWN) ? "-" : "";
      if(edit_mode == 1){
        if(k == K_XUP   && power[profile] < 99) power[profile]++;
        if(k == K_XDOWN && power[profile] > 1)  power[profile]--;
        if(k == K_YUP   && power[profile] < 90) power[profile] += 10;
        if(k == K_YDOWN && power[profile] > 10) power[profile] -= 10;
        if(k == K_ZUP   && power[profile] < 90) power[profile] += 10;
        if(k == K_ZDOWN && power[profile] > 10) power[profile] -= 10;
        if(sp_on) tool_cmd();
        request_save();
      } else if(edit_mode == 2){
        int d = (k == K_XUP || k == K_XDOWN) ? 100 : 1000;
        if(k == K_XUP || k == K_YUP || k == K_ZUP) feed = (feed + d > 15000) ? 15000 : (uint16_t)(feed + d);
        else feed = (feed < d) ? 100 : (uint16_t)(feed - d);
        request_save();
      } else {
        char dist[12];
        if(jog_mode == 0){
          if(!jog_active){
            snprintf(dist, sizeof(dist), "%s9999", sgn);
            fnc_jog(ax, dist, feed);
            jog_active = true;
          }
        } else {
          snprintf(dist, sizeof(dist), "%s%s", sgn, jog_dist[jog_mode]);
          fnc_jog(ax, dist, feed);
        }
      }
      break;
    }
    case K_XY0:  if(edit_mode == 0) fnc_cmd("G0 X0 Y0"); else if(edit_mode == 1){ power[profile] = 30; request_save(); } else { feed = 5000; request_save(); } break;
    case K_Z0:   if(edit_mode == 0) fnc_cmd("G0 Z0");    else if(edit_mode == 1){ power[profile] = 30; request_save(); } else { feed = 5000; request_save(); } break;
    case K_XZER: fnc_cmd("G10 L20 P1 X0"); break;
    case K_YZER: fnc_cmd("G10 L20 P1 Y0"); break;
    case K_ZZER: fnc_cmd("G10 L20 P1 Z0"); break;
    case K_PRB:  fnc_cmd("G38.6 Z-40 F100 P5"); fnc_cmd("$J=G90 G21 F1000 Z9.5"); break;
    case K_UNLC: fnc_clear_msg(); fnc_cmd("$X"); break;
    case K_HOME: fnc_cmd("$H"); break;
    case K_HOLD: fnc_rt('!'); break;
    case K_START:fnc_rt('~'); break;
    case K_AXLK:
      ax_lock = !ax_lock;
      fnc_cmd(ax_lock ? "$MD" : "$ME");
      ax_dirty = true;
      break;
    case K_SDCRD:
      screen = 1; sd_cur = 0;
      fnc_sd_list(sd_files, 8, &sd_cnt);
      stat_dirty = true;
      break;
    case K_RST:  fnc_rt(0x18); break;
    default: break;
  }
}

static void on_key_sd(uint8_t k){
  switch(k){
    case K_YUP:   if(sd_cnt) sd_cur = (sd_cur == 0) ? sd_cnt-1 : sd_cur-1; break;
    case K_YDOWN: if(sd_cnt) sd_cur = (sd_cur+1) % sd_cnt; break;
    case K_XY0:
      if(sd_cnt) fnc_run_sd(sd_files[sd_cur].name);
      screen = 0;
      break;
    case K_RST:   screen = 0; break;
    default: break;
  }
}

/* ================= ЦИКЛ ================= */
void ui_init(void){
  settings_load(&cfg);
  feed = cfg.feed;
  jog_mode = cfg.jog_mode;
  profile = cfg.profile & 1;
  power[0] = cfg.power[0];
  power[1] = cfg.power[1];
  screen = 0;
  stat_dirty = true;
  sd_cur_drawn = 255;
  bar_w = 0;
  fields_invalidate();
}

void ui_tick(uint32_t now){
  tm_poll(now);
  fnc_poll(now);
  const fnc_data_t *s = fnc_get();
  bool running = (s->state == FNC_RUN);

  if(save_pending && (now - save_t > 1000)){ save_pending = false; do_save(); }

  uint8_t pr = tm_pressed(), rl = tm_released();

  if(pr == K_SPND && screen == 0 && !running){
    spn_held = true; spn_long = false; spn_t = now;
  } else if(pr){
    if(screen == 0) on_key_main(pr); else on_key_sd(pr);
  }

  if(screen == 0 && !running){
    if(spn_held && !spn_long && tm_key() == K_SPND && (now - spn_t >= SPN_LONG_MS)){
      spn_long = true;
      profile ^= 1;
      if(sp_on) tool_cmd();
      prof_dirty = true;
      request_save();
    }
    if(rl == K_SPND){
      spn_held = false;
      if(!spn_long){
        sp_on = !sp_on;
        if(sp_on) tool_cmd(); else fnc_cmd("M5");
        dyn_t = 0;
      }
    }
  } else {
    if(rl == K_SPND) spn_held = false;
  }

  if(jog_active && rl){ fnc_jog_cancel(); jog_active = false; }

  static uint8_t p_screen = 255, p_run = 255;
  uint8_t run = running ? 1 : 0;

  if(screen != p_screen || run != p_run){
    p_screen = screen;
    p_run = run;
    stat_dirty = true;
  }

  if(stat_dirty){
    stat_dirty = false;
    fields_invalidate();
    bar_w = 0;
    if(screen == 0) draw_main_static(); else draw_sd_static();
    dyn_t = 0;
    jog_dirty = false;
    edit_dirty = false;
    ax_dirty = false;
    prof_dirty = false;
  } else {
    if(screen == 0){
      if(jog_dirty)  { jog_dirty  = false; draw_jog_buttons(); }
      if(edit_dirty) { edit_dirty = false; draw_edit_mode_fields(); }
      if(ax_dirty)   { ax_dirty   = false; draw_axis_lock(); }
      if(prof_dirty) { prof_dirty = false; draw_profile(); }
    }
  }

  if(now - dyn_t >= 120){
    dyn_t = now;
    if(screen == 0) draw_main_dyn(); else draw_sd_dyn();
  }
}
