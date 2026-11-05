#ifndef SCE_IOP_DMACMAN_H
#define SCE_IOP_DMACMAN_H

#define DMA_CHANNEL_SIO2_IN 11
#define DMA_CHANNEL_SIO2_OUT 12

#define DMA_FROM_DEVICE 0
#define DMA_TO_DEVICE 1

int dmac_request(int channel, void* address, int size, int count, int direction);
void dmac_transfer(int channel);
void dmac_set_priority(int channel, int priority);
void dmac_enable(int channel);
void dmac_disable(int channel);

#endif
