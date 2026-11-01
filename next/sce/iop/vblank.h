#ifndef _VBLANK_H
#define _VBLANK_H

extern int RegisterVblankHandler(int edge, int priority, int (*handler)(void*), void* common);

#endif
