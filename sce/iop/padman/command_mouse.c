#include "sce/iop/padman/commands.h"

static void pad_mouse_read(u_char* buffer) {
  buffer[0] = 1;
  buffer[1] = 66;
  buffer[2] = 0;
  buffer[3] = 0;
  buffer[4] = 0;
  buffer[5] = 0;
  buffer[6] = 0;
}

static u_int pad_mouse_port_control1(u_int mode, u_int connected) {
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

static u_int pad_mouse_port_control2(u_int mode) {
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

static u_int pad_mouse_transfer_control(void) {
  return 1836864;
}

static u_int pad_mouse_input_size(void) {
  return 7;
}

static u_int pad_mouse_output_size(void) {
  return 7;
}

static int pad_mouse_enter_config(u_char* buffer) {
  buffer[0] = 1;
  buffer[1] = 67;
  buffer[2] = 0;
  buffer[3] = 1;
  buffer[4] = 0;
  buffer[5] = 0;
  buffer[6] = 0;
  return 7;
}

void pad_command_init_mouse(void) {
  PadCommandDriver driver;
  driver.id = 1;
  driver.read = pad_mouse_read;
  driver.port_control1 = pad_mouse_port_control1;
  driver.port_control2 = pad_mouse_port_control2;
  driver.transfer_control = pad_mouse_transfer_control;
  driver.input_size = pad_mouse_input_size;
  driver.output_size = pad_mouse_output_size;
  driver.enter_config = pad_mouse_enter_config;
  driver.exit_config = 0;
  driver.query_model = 0;
  driver.query_actuator = 0;
  driver.query_combination = 0;
  driver.query_mode = 0;
  driver.query_button_mask = 0;
  driver.set_button_info = 0;
  driver.set_vref = 0;
  driver.set_main_mode = 0;
  driver.set_actuator_align = 0;
  pad_command_register(&driver);
}
