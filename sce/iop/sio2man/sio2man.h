#ifndef SCE_IOP_SIO2MAN_H
#define SCE_IOP_SIO2MAN_H

struct Sio2Dma {
  void* address;
  int size;
  int count;
};

struct Sio2Transfer {
  unsigned int recv1;
  unsigned int send1[4];
  unsigned int send2[4];
  unsigned int recv2;
  unsigned int send3[16];
  unsigned int recv3;
  int input_size;
  int output_size;
  unsigned char* input;
  unsigned char* output;
  struct Sio2Dma input_dma;
  struct Sio2Dma output_dma;
};

void sio2_ctrl_set(unsigned int value);
unsigned int sio2_ctrl_get(void);
unsigned int sio2_recv1_get(void);
void sio2_send1_set(int port, unsigned int value);
unsigned int sio2_send1_get(int port);
void sio2_send2_set(int port, unsigned int value);
unsigned int sio2_send2_get(int port);
unsigned int sio2_recv2_get(void);
void sio2_send3_set(int index, unsigned int value);
unsigned int sio2_send3_get(int index);
unsigned int sio2_recv3_get(void);
void sio2_unk78_set(unsigned int value);
unsigned int sio2_unk78_get(void);
void sio2_unk7c_set(unsigned int value);
unsigned int sio2_unk7c_get(void);
void sio2_data_out(unsigned char value);
unsigned char sio2_data_in(void);
void sio2_stat_set(unsigned int value);
unsigned int sio2_stat_get(void);
void sio2_pad_init_legacy(void);
unsigned int sio2_recv2_get_legacy(void);
int sio2_transfer_legacy(struct Sio2Transfer* transfer);

void sio2_pad_init(void);
void sio2_mc_init(void);
void sio2_mtap_init(void);
void sio2_init_40(void);
void sio2_init_100(void);
int sio2_transfer(struct Sio2Transfer* transfer);
void sio2_transfer_reset(void);
void sio2_set_slot_callback(int (*callback)(int*));
void sio2_set_callback2(int (*callback)(void));
void sio2_set_callback3(int (*callback)(void));
void sio2_set_callback4(void (*callback)(void));
int sio2_change_slot(int* slots);
int sio2_call_callback2(void);
int sio2_call_callback3(void);
void sio2_call_callback4(void);

#endif
