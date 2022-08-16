#include "arlib.h"

// this api sucks, but it works
extern bool xz_active;
extern function<bool()> xz_repeat;
extern function<void()> xz_finish;
extern function<void(int pos, int durat)> xz_progress;

bool xz_play(cstrnul fn);
void xz_stop();
