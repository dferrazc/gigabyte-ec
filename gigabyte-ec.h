#ifndef __GIGABYTE_EC_REGISTERS_CONFIG__
#define __GIGABYTE_EC_REGISTERS_CONFIG__

#include "linux/types.h"

#define GIGABYTE_EC_MODE_NULL { NULL, {0,0,0,0,0,0}}
#define GIGABYTE_EC_N_OF_ADDRESS 6
#define GIGABYTE_EC_DRIVER_NAME "gigabyte-ec"

struct gigabyte_ec_mode {
    const char *name;
    u8 value[GIGABYTE_EC_N_OF_ADDRESS];
};

struct gigabyte_ec_addr_bit{
    u8 addr;
    u8 size;
    u8 bit;
};

struct gigabyte_ec_fan_mode_conf {
    struct gigabyte_ec_addr_bit addr_bit[GIGABYTE_EC_N_OF_ADDRESS];
    struct gigabyte_ec_mode     modes[6];
};

struct gigabyte_ec_charging_mode_conf {
    struct gigabyte_ec_addr_bit addr_bit[2];
    struct gigabyte_ec_mode     modes[3]; // -> custom or standard (hard coded 1 more for null mode)
};

struct gigabyte_ec_charge_control_threshold_conf {
    u8 range_min;
    u8 range_max;
    struct gigabyte_ec_addr_bit addr_bit[1];
};

struct gigabyte_ec_conf {
    struct gigabyte_ec_fan_mode_conf      fan_mode;
    struct gigabyte_ec_charging_mode_conf charging_mode;
    struct gigabyte_ec_charge_control_threshold_conf charge_threshold;
};

#endif