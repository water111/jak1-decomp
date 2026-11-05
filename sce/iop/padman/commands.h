#ifndef SCE_IOP_PADMAN_COMMANDS_H
#define SCE_IOP_PADMAN_COMMANDS_H
#include "sce/iop/types.h"
#define PAD_COMMAND_DRIVER_MAX 16
#define PAD_ID_DIGITAL 0x41

typedef union PadPortTiming {
  u_int value;

  struct {
    u_int timing0 : 8, timing1 : 8, timing2 : 8, timing3 : 8;
  } fields;
} PadPortTiming;

typedef struct PadCommandDriver {
  u_char id;
  void (*read)(u_char* buffer);
  u_int (*port_control1)(u_int mode, u_int connected);
  u_int (*port_control2)(u_int mode);
  u_int (*transfer_control)(void);
  u_int (*input_size)(void);
  u_int (*output_size)(void);
  int (*enter_config)(u_char* buffer);
  int (*exit_config)(u_char* buffer);
  int (*query_model)(u_char* buffer);
  int (*query_actuator)(u_char* buffer);
  int (*query_combination)(u_char* buffer);
  int (*query_mode)(u_char* buffer);
  int (*query_button_mask)(u_char* buffer);
  int (*set_button_info)(u_char* buffer);
  int (*set_vref)(u_char* buffer);
  int (*set_main_mode)(u_char* buffer);
  int (*set_actuator_align)(u_char* buffer);
} PadCommandDriver;

void pad_command_reset(void);
int pad_command_register(const PadCommandDriver* driver);
int pad_command_supported(u_char id);
void pad_command_read(u_char id, u_char* buffer);
u_int pad_command_port_control1(u_char id, u_int mode, u_int connected);
u_int pad_command_port_control2(u_char id, u_int mode);
u_int pad_command_input_size(u_char id);
u_int pad_command_output_size(u_char id);
u_int pad_command_transfer_control(u_char id);
int pad_command_enter_config(u_char id, u_char* buffer);
int pad_command_exit_config(u_char id, u_char* buffer);
int pad_command_query_model(u_char id, u_char* buffer);
int pad_command_query_actuator(u_char id, u_char* buffer);
int pad_command_query_combination(u_char id, u_char* buffer);
int pad_command_query_mode(u_char id, u_char* buffer);
int pad_command_query_button_mask(u_char id, u_char* buffer);
int pad_command_set_button_info(u_char id, u_char* buffer);
int pad_command_set_vref(u_char id, u_char* buffer);
int pad_command_set_main_mode(u_char id, u_char* buffer);
int pad_command_set_actuator_align(u_char id, u_char* buffer);
#endif
