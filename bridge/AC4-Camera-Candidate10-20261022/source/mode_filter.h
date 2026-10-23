// Display-mode filter for the resolution menu.
// The game builds one menu entry per resolution. Listing every DXGI mode instead
// produced ~600 entries on a 4K/144 Hz monitor (each mode is reported twice, plus
// 23.976/24/25/29.97/... rates), and the game then hid the "resolution" row.
// This picks a short list: per resolution one entry per distinct refresh rate,
// exact integer rates preferred (60 over 59.94), at most MaxModes entries.
#pragma once
#include <string.h>
#include <math.h>
#include <stdint.h>

struct ModeEntry { uint32_t w, h, num, den, format, scanline, scaling; };  // DXGI_MODE_DESC, 0x1c bytes

enum { kModeCap = 4096 };
static unsigned char modeSelected[kModeCap];
static uint32_t modeCand[kModeCap];

static double ModeHz(const ModeEntry& m) { return m.den ? (double)m.num / m.den : 0.0; }
static int ModeRound(const ModeEntry& m) { return (int)(ModeHz(m) + 0.5); }
static bool ModeInteger(const ModeEntry& m) { return fabs(ModeHz(m) - ModeRound(m)) < 0.001; }

// Fills modeSelected[] for modes[0..count). Returns number of selected entries.
// minWidth/minHeight: smaller resolutions are hidden, unless the screen has nothing that
// large (e.g. a 1366x768 laptop) - then the size filter is ignored so the menu never ends up empty.
static unsigned SelectModes(const ModeEntry* modes, uint32_t count, int minRefresh, int maxModes,
                            uint32_t minWidth = 0, uint32_t minHeight = 0) {
  memset(modeSelected, 0, sizeof(modeSelected));
  if (count > kModeCap) count = kModeCap;
  if (maxModes < 1) maxModes = 1;
  // 1. One candidate per (width, height, rounded Hz); prefer the exact-integer rate.
  uint32_t cc = 0;
  bool anyLarge = false;
  for (uint32_t i = 0; i < count; i++)
    if (modes[i].w >= minWidth && modes[i].h >= minHeight && ModeRound(modes[i]) >= 1) anyLarge = true;
  if (!anyLarge) minWidth = minHeight = 0;
  for (uint32_t i = 0; i < count; i++) {
    const ModeEntry& m = modes[i];
    if (m.w < 800 || m.h < 600 || ModeRound(m) < 1) continue;
    if (m.w < minWidth || m.h < minHeight) continue;
    uint32_t j = 0;
    for (; j < cc; j++) {
      const ModeEntry& c = modes[modeCand[j]];
      if (c.w == m.w && c.h == m.h && ModeRound(c) == ModeRound(m)) break;
    }
    if (j < cc) { if (ModeInteger(m) && !ModeInteger(modes[modeCand[j]])) modeCand[j] = i; continue; }
    modeCand[cc++] = i;
  }
  unsigned picked = 0;
  // 2. Every resolution gets its highest rate (what the original game shows).
  for (uint32_t jj = cc; jj-- > 0 && (int)picked < maxModes;) {  // larger resolutions first
    uint32_t j = jj;
    const ModeEntry& c = modes[modeCand[j]];
    bool best = true;
    for (uint32_t k = 0; k < cc && best; k++) {
      const ModeEntry& o = modes[modeCand[k]];
      if (o.w == c.w && o.h == c.h && ModeRound(o) > ModeRound(c)) best = false;
    }
    if (best && !modeSelected[modeCand[j]]) { modeSelected[modeCand[j]] = 1; picked++; }
  }
  // 3. Then 60 Hz for each resolution (safe fallback for every monitor).
  for (uint32_t j = cc; j-- > 0 && (int)picked < maxModes;)
    if (ModeRound(modes[modeCand[j]]) == 60 && !modeSelected[modeCand[j]]) { modeSelected[modeCand[j]] = 1; picked++; }
  // 4. Remaining rates >= minRefresh: highest rate first, larger resolution first.
  while ((int)picked < maxModes) {
    int bestJ = -1;
    for (uint32_t j = 0; j < cc; j++) {
      uint32_t i = modeCand[j];
      if (modeSelected[i] || ModeRound(modes[i]) < minRefresh) continue;
      if (bestJ < 0) { bestJ = (int)j; continue; }
      const ModeEntry& b = modes[modeCand[bestJ]];
      int dr = ModeRound(modes[i]) - ModeRound(b);
      if (dr > 0 || (dr == 0 && (uint64_t)modes[i].w * modes[i].h > (uint64_t)b.w * b.h)) bestJ = (int)j;  // larger screens first
    }
    if (bestJ < 0) break;
    modeSelected[modeCand[bestJ]] = 1; picked++;
  }
  return picked;
}
