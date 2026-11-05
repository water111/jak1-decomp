#ifndef SCE_IOP_PADMAN_EXCHANGE_H
#define SCE_IOP_PADMAN_EXCHANGE_H
#include "sce/iop/padman/state.h"
void pad_shift_response(u_char* buffer);
int pad_exchange_read(PadState* pad);
int pad_exchange_find(PadState* pad);
int pad_exchange_enter_config(int id, PadState* pad);
int pad_exchange_exit_config(PadState* pad);
int pad_exchange_main_mode(PadState* pad);
int pad_exchange_act_align(PadState* pad);
int pad_exchange_model(PadState* pad);
int pad_exchange_actuator(int index, PadState* pad);
int pad_exchange_combination(int index, PadState* pad);
int pad_exchange_mode(int index, PadState* pad);
int pad_exchange_vref(int index, PadState* pad);
int pad_exchange_button_mask(PadState* pad);
int pad_exchange_button_info(PadState* pad);
#endif
