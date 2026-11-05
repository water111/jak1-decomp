#include "sce/iop/padman/commands.h"

static void pad_config_read(u_char* buffer) {
  buffer[0] = 1;
  buffer[1] = 66;
  buffer[2] = 0;
  buffer[3] = 90;
  buffer[4] = 90;
  buffer[5] = 90;
  buffer[6] = 90;
  buffer[7] = 90;
  buffer[8] = 90;
}

static u_int pad_config_port_control1(u_int mode, u_int connected) {
  PadPortTiming control;
  if (mode == 0) {
    control.value = 5;
    control.fields.timing1 = 5;
    control.fields.timing2 = 192;
    control.fields.timing3 = 255;
  } else {
    control.value = 5;
    control.fields.timing1 = 5;
    control.fields.timing2 = 6;
    control.fields.timing3 = 255;
  }
  return control.value;
}

static u_int pad_config_port_control2(u_int mode) {
  PadPortTiming control;
  if (mode == 0) {
    control.value = 20;
    control.fields.timing2 = 2;
  } else {
    control.value = 300;
    control.fields.timing2 = 2;
  }
  return control.value;
}

static u_int pad_config_transfer_control(void) {
  return 2361664;
}

static u_int pad_config_input_size(void) {
  return 9;
}

static u_int pad_config_output_size(void) {
  return 9;
}

static int pad_config_exit_config(u_char* buffer) {
  buffer[0] = 1;
  buffer[1] = 67;
  buffer[2] = 0;
  buffer[3] = 0;
  buffer[4] = 90;
  buffer[5] = 90;
  buffer[6] = 90;
  buffer[7] = 90;
  buffer[8] = 90;
  return 9;
}

static int pad_config_query_model(u_char* buffer) {
  buffer[0] = 1;
  buffer[1] = 69;
  buffer[2] = 0;
  buffer[3] = 90;
  buffer[4] = 90;
  buffer[5] = 90;
  buffer[6] = 90;
  buffer[7] = 90;
  buffer[8] = 90;
  return 9;
}

static int pad_config_query_actuator(u_char* buffer) {
  buffer[0] = 1;
  buffer[1] = 70;
  buffer[2] = 0;
  buffer[3] = 0;
  buffer[4] = 90;
  buffer[5] = 90;
  buffer[6] = 90;
  buffer[7] = 90;
  buffer[8] = 90;
  return 9;
}

static int pad_config_query_combination(u_char* buffer) {
  buffer[0] = 1;
  buffer[1] = 71;
  buffer[2] = 0;
  buffer[3] = 0;
  buffer[4] = 90;
  buffer[5] = 90;
  buffer[6] = 90;
  buffer[7] = 90;
  buffer[8] = 90;
  return 9;
}

static int pad_config_query_mode(u_char* buffer) {
  buffer[0] = 1;
  buffer[1] = 76;
  buffer[2] = 0;
  buffer[3] = 0;
  buffer[4] = 90;
  buffer[5] = 90;
  buffer[6] = 90;
  buffer[7] = 90;
  buffer[8] = 90;
  return 9;
}

static int pad_config_query_button_mask(u_char* buffer) {
  buffer[0] = 1;
  buffer[1] = 65;
  buffer[2] = 0;
  buffer[3] = 90;
  buffer[4] = 90;
  buffer[5] = 90;
  buffer[6] = 90;
  buffer[7] = 90;
  buffer[8] = 90;
  return 9;
}

static int pad_config_set_button_info(u_char* buffer) {
  buffer[0] = 1;
  buffer[1] = 79;
  buffer[2] = 0;
  buffer[3] = 0;
  buffer[4] = 0;
  buffer[5] = 0;
  buffer[6] = 0;
  buffer[7] = 0;
  buffer[8] = 0;
  return 9;
}

static int pad_config_set_vref(u_char* buffer) {
  buffer[0] = 1;
  buffer[1] = 64;
  buffer[2] = 0;
  buffer[3] = 0;
  buffer[4] = 0;
  buffer[5] = 0;
  buffer[6] = 0;
  buffer[7] = 0;
  buffer[8] = 0;
  return 9;
}

static int pad_config_set_main_mode(u_char* buffer) {
  buffer[0] = 1;
  buffer[1] = 68;
  buffer[2] = 0;
  buffer[3] = 0;
  buffer[4] = 0;
  buffer[5] = 0;
  buffer[6] = 0;
  buffer[7] = 0;
  buffer[8] = 0;
  return 9;
}

static int pad_config_set_actuator_align(u_char* buffer) {
  buffer[0] = 1;
  buffer[1] = 77;
  buffer[2] = 0;
  buffer[3] = 0;
  buffer[4] = 0;
  buffer[5] = 0;
  buffer[6] = 0;
  buffer[7] = 0;
  buffer[8] = 0;
  return 9;
}

void pad_command_init_config(void) {
  PadCommandDriver driver;
  driver.id = 15;
  driver.read = pad_config_read;
  driver.port_control1 = pad_config_port_control1;
  driver.port_control2 = pad_config_port_control2;
  driver.transfer_control = pad_config_transfer_control;
  driver.input_size = pad_config_input_size;
  driver.output_size = pad_config_output_size;
  driver.enter_config = 0;
  driver.exit_config = pad_config_exit_config;
  driver.query_model = pad_config_query_model;
  driver.query_actuator = pad_config_query_actuator;
  driver.query_combination = pad_config_query_combination;
  driver.query_mode = pad_config_query_mode;
  driver.query_button_mask = pad_config_query_button_mask;
  driver.set_button_info = pad_config_set_button_info;
  driver.set_vref = pad_config_set_vref;
  driver.set_main_mode = pad_config_set_main_mode;
  driver.set_actuator_align = pad_config_set_actuator_align;
  pad_command_register(&driver);
}
