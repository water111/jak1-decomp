#ifndef SCE_IOP_SECRMAN_H
#define SCE_IOP_SECRMAN_H

struct Sio2Transfer;
void SecrSetMcCommandHandler(int (*handler)(int, int, struct Sio2Transfer*));
void SecrSetMcDevIDHandler(int (*handler)(int, int));
int SecrAuthCard(int port, int slot, int device);

#endif
