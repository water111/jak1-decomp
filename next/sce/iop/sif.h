#ifndef _SIF_H_DEFS
#define _SIF_H_DEFS

typedef struct {
	unsigned int	data;
	unsigned int	addr;
	unsigned int	size;
	unsigned int	mode;
} sceSifDmaData;

extern unsigned int sceSifSetDma(sceSifDmaData *sdd, int len);
extern int sceSifDmaStat(unsigned int id);

#endif
