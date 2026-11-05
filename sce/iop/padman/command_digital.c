#include "sce/iop/padman/commands.h"

static void pad_digital_read(u_char* buffer) {
  buffer[0] = 1;
  buffer[1] = 66;
  buffer[2] = 0;
  buffer[3] = 0;
  buffer[4] = 0;
}

static u_int pad_digital_port_control1(u_int mode, u_int connected) {
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

static u_int pad_digital_port_control2(u_int mode) {
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

static u_int pad_digital_transfer_control(void) {
  return 1312064;
}

static u_int pad_digital_input_size(void) {
  return 5;
}

static u_int pad_digital_output_size(void) {
  return 5;
}

static int pad_digital_enter_config(u_char* buffer) {
  buffer[0] = 1;
  buffer[1] = 67;
  buffer[2] = 0;
  buffer[3] = 1;
  buffer[4] = 0;
  return 5;
}

void pad_command_init_digital(void) {
  PadCommandDriver driver;
  driver.id = 4;
  driver.read = pad_digital_read;
  driver.port_control1 = pad_digital_port_control1;
  driver.port_control2 = pad_digital_port_control2;
  driver.transfer_control = pad_digital_transfer_control;
  driver.input_size = pad_digital_input_size;
  driver.output_size = pad_digital_output_size;
  driver.enter_config = pad_digital_enter_config;
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
