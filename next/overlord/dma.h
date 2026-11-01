#ifndef DMA_H
#define DMA_H

int DMA_SendToSPUAndSync(void *src_addr, unsigned int size, unsigned int dst_addr);

void DMA_SendToEE(void *src_addr, unsigned int size, unsigned int dst_addr);
void DMA_Sync(void);

#endif
