#ifndef RAMDISK_H
#define RAMDISK_H

void InitRamdisk();
unsigned int Thread_Server();
extern unsigned char* gRamdiskRAM;
extern unsigned long gMemFreeAtStart;
#endif
