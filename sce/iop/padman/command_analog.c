#include "sce/iop/padman/commands.h"

static void pad_analog_read(u_char* buffer) {
  buffer[0] = 1;
  buffer[1] = 66;
  buffer[2] = 0;
  buffer[3] = 0;
  buffer[4] = 0;
  buffer[5] = 0;
  buffer[6] = 0;
  buffer[7] = 0;
  buffer[8] = 0;
  buffer[9] = 0;
  buffer[10] = 0;
  buffer[11] = 0;
  buffer[12] = 0;
  buffer[13] = 0;
  buffer[14] = 0;
}

static u_int pad_analog_port_control1(u_int mode, u_int connected) {
  PadPortTiming control;
  if (mode == 0) {
    control.value = 0;
    control.fields.timing0 = (connected & 2) ? 10 : 5;
    control.fields.timing1 = (connected & 2) ? 10 : 5;
    control.fields.timing2 = (connected & 2) ? 96 : 192;
    control.fields.timing3 = 255;
  } else {
    control.value = 0xff060505;
  }
  return control.value;
}

static u_int pad_analog_port_control2(u_int mode) {
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

static int pad_analog_enter_config(u_char* buffer) {
  buffer[0] = 1;
  buffer[1] = 67;
  buffer[2] = 0;
  buffer[3] = 1;
  buffer[4] = 0;
  buffer[5] = 0;
  buffer[6] = 0;
  buffer[7] = 0;
  buffer[8] = 0;
  buffer[9] = 0;
  buffer[10] = 0;
  buffer[11] = 0;
  buffer[12] = 0;
  buffer[13] = 0;
  buffer[14] = 0;
  return 21;
}

void pad_command_init_analog(void) {
  PadCommandDriver driver;
  driver.id = 7;
  driver.read = pad_analog_read;
  driver.port_control1 = pad_analog_port_control1;
  driver.port_control2 = pad_analog_port_control2;
  driver.transfer_control = 0;
  driver.input_size = 0;
  driver.output_size = 0;
  driver.enter_config = pad_analog_enter_config;
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
