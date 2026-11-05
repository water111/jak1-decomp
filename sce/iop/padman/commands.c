#include "sce/iop/padman/commands.h"
static PadCommandDriver command_drivers[PAD_COMMAND_DRIVER_MAX];
static int command_driver_count;

void pad_command_reset(void) {
  int i;
  for (i = PAD_COMMAND_DRIVER_MAX - 1; i >= 0; --i)
    command_drivers[i].id = 0;
  command_driver_count = 0;
}

int pad_command_register(const PadCommandDriver* driver) {
  int i;
  if (command_driver_count + 1 >= PAD_COMMAND_DRIVER_MAX || !driver)
    return 0;
  {
    for (i = 0; i < command_driver_count; ++i) {
      if (driver->id == command_drivers[i].id)
        return 0;
    }
    command_drivers[command_driver_count].id = driver->id;
    command_drivers[command_driver_count].read = driver->read;
    command_drivers[command_driver_count].port_control1 = driver->port_control1;
    command_drivers[command_driver_count].port_control2 = driver->port_control2;
    command_drivers[command_driver_count].transfer_control = driver->transfer_control;
    command_drivers[command_driver_count].input_size = driver->input_size;
    command_drivers[command_driver_count].output_size = driver->output_size;
    command_drivers[command_driver_count].enter_config = driver->enter_config;
    command_drivers[command_driver_count].exit_config = driver->exit_config;
    command_drivers[command_driver_count].query_model = driver->query_model;
    command_drivers[command_driver_count].query_actuator = driver->query_actuator;
    command_drivers[command_driver_count].query_combination = driver->query_combination;
    command_drivers[command_driver_count].query_mode = driver->query_mode;
    command_drivers[command_driver_count].query_button_mask = driver->query_button_mask;
    command_drivers[command_driver_count].set_button_info = driver->set_button_info;
    command_drivers[command_driver_count].set_vref = driver->set_vref;
    command_drivers[command_driver_count].set_main_mode = driver->set_main_mode;
    command_drivers[command_driver_count].set_actuator_align = driver->set_actuator_align;
    ++command_driver_count;
    return 1;
  }
  return 0;
}

int pad_command_supported(u_char id) {
  int i;
  id >>= 4;
  for (i = 0; i < command_driver_count; ++i) {
    if (id == command_drivers[i].id)
      return 1;
  }
  return 0;
}

void pad_command_read(u_char id, u_char* buffer) {
  int i;
  id >>= 4;
  for (i = 0; i < command_driver_count; ++i) {
    if (id == command_drivers[i].id && command_drivers[i].read) {
      command_drivers[i].read(buffer);
      return;
    }
  }
}

u_int pad_command_port_control1(u_char id, u_int mode, u_int connected) {
  int i;
  id >>= 4;
  for (i = 0; i < command_driver_count; ++i) {
    if (id == command_drivers[i].id && command_drivers[i].port_control1)
      return command_drivers[i].port_control1(mode, connected);
  }
  return 0;
}

u_int pad_command_port_control2(u_char id, u_int mode) {
  int i;
  id >>= 4;
  for (i = 0; i < command_driver_count; ++i) {
    if (id == command_drivers[i].id && command_drivers[i].port_control2)
      return command_drivers[i].port_control2(mode);
  }
  return 0;
}

u_int pad_command_input_size(u_char id) {
  if (!id)
    id = PAD_ID_DIGITAL;
  return 2 * (id & 15) + 3;
}

u_int pad_command_output_size(u_char id) {
  if (!id)
    id = PAD_ID_DIGITAL;
  return 2 * (id & 15) + 3;
}

u_int pad_command_transfer_control(u_char id) {
  union {
    u_int value;

    struct {
      u_int flags : 8, input_size : 9, reserved : 1, output_size : 9, padding : 5;
    } fields;
  } control;

  control.value = 0;
  control.fields.input_size = pad_command_input_size(id);
  control.fields.flags = 0x40;
  control.fields.output_size = pad_command_output_size(id);
  return control.value;
}

int pad_command_enter_config(u_char id, u_char* buffer) {
  int i;
  id >>= 4;
  for (i = 0; i < command_driver_count; ++i) {
    if (id == command_drivers[i].id && command_drivers[i].enter_config)
      return command_drivers[i].enter_config(buffer);
  }
  return 0;
}

int pad_command_exit_config(u_char id, u_char* buffer) {
  int i;
  id >>= 4;
  for (i = 0; i < command_driver_count; ++i) {
    if (id == command_drivers[i].id && command_drivers[i].exit_config)
      return command_drivers[i].exit_config(buffer);
  }
  return 0;
}

int pad_command_query_model(u_char id, u_char* buffer) {
  int i;
  id >>= 4;
  for (i = 0; i < command_driver_count; ++i) {
    if (id == command_drivers[i].id && command_drivers[i].query_model)
      return command_drivers[i].query_model(buffer);
  }
  return 0;
}

int pad_command_query_actuator(u_char id, u_char* buffer) {
  int i;
  id >>= 4;
  for (i = 0; i < command_driver_count; ++i) {
    if (id == command_drivers[i].id && command_drivers[i].query_actuator)
      return command_drivers[i].query_actuator(buffer);
  }
  return 0;
}

int pad_command_query_combination(u_char id, u_char* buffer) {
  int i;
  id >>= 4;
  for (i = 0; i < command_driver_count; ++i) {
    if (id == command_drivers[i].id && command_drivers[i].query_combination)
      return command_drivers[i].query_combination(buffer);
  }
  return 0;
}

int pad_command_query_mode(u_char id, u_char* buffer) {
  int i;
  id >>= 4;
  for (i = 0; i < command_driver_count; ++i) {
    if (id == command_drivers[i].id && command_drivers[i].query_mode)
      return command_drivers[i].query_mode(buffer);
  }
  return 0;
}

int pad_command_query_button_mask(u_char id, u_char* buffer) {
  int i;
  id >>= 4;
  for (i = 0; i < command_driver_count; ++i) {
    if (id == command_drivers[i].id && command_drivers[i].query_button_mask)
      return command_drivers[i].query_button_mask(buffer);
  }
  return 0;
}

int pad_command_set_button_info(u_char id, u_char* buffer) {
  int i;
  id >>= 4;
  for (i = 0; i < command_driver_count; ++i) {
    if (id == command_drivers[i].id && command_drivers[i].set_button_info)
      return command_drivers[i].set_button_info(buffer);
  }
  return 0;
}

int pad_command_set_vref(u_char id, u_char* buffer) {
  int i;
  id >>= 4;
  for (i = 0; i < command_driver_count; ++i) {
    if (id == command_drivers[i].id && command_drivers[i].set_vref)
      return command_drivers[i].set_vref(buffer);
  }
  return 0;
}

int pad_command_set_main_mode(u_char id, u_char* buffer) {
  int i;
  id >>= 4;
  for (i = 0; i < command_driver_count; ++i) {
    if (id == command_drivers[i].id && command_drivers[i].set_main_mode)
      return command_drivers[i].set_main_mode(buffer);
  }
  return 0;
}

int pad_command_set_actuator_align(u_char id, u_char* buffer) {
  int i;
  id >>= 4;
  for (i = 0; i < command_driver_count; ++i) {
    if (id == command_drivers[i].id && command_drivers[i].set_actuator_align)
      return command_drivers[i].set_actuator_align(buffer);
  }
  return 0;
}
