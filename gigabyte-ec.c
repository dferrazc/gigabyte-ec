// SPDX-License-Identifier: GPL-2.0-or-later

#include "gigabyte-ec.h"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/platform_device.h>

static struct kobject *gigabyte_ec_kobj;
static bool conf_loaded = false;
static struct gigabyte_ec_conf ec_conf;

/* fan modes names */
static const char *const FM_NORMAL_NAME = "normal";
static const char *const FM_ECO_NAME    = "eco";
static const char *const FM_POWER_NAME  = "power";
static const char *const FM_TURBO_NAME  = "turbo";

/* battery modes names */
static const char *const CM_STANDARD_NAME = "standard";
static const char *const CM_CUSTOM_NAME   = "custom";

/* ec conf for supported motherboards */
static struct gigabyte_ec_conf AORUS5KE0 __initdata = {
        .fan_mode = {
                /* address, size in bits, bit */
                .addr_bit = {
                        {0x06,1,4},
                        {0x08,1,6},
                        {0x0C,1,4},
                        {0x0D,1,7},
                        {0xB0,8,0},
                        {0xB1,8,0}
                },
                .modes = {
                        { FM_NORMAL_NAME,   {0,0,0,0,0x39,0x39}},
                        { FM_ECO_NAME,      {0,1,0,0,0x39,0x39}},
                        { FM_POWER_NAME,    {0,0,1,0,0x39,0x39}},
                        { FM_TURBO_NAME,    {1,0,0,1,0xE5,0xE5}},
                        GIGABYTE_EC_MODE_NULL
                },
        },
        .charging_mode = {

                .addr_bit = {
                        {0x0F,1,2},
                        {0xA9,8,0},
                },
                .modes    = {
                        { CM_STANDARD_NAME,   {0, 0x61}},
                        { CM_CUSTOM_NAME,     {1, 0x3C}},
                        { NULL,               {0,0}},
                },
        },
        .charge_threshold = {
                .range_min = 60,
                .range_max = 100,
                .addr_bit  = {
                        {0xA9,8,0}
                        },
                },
};

// DMI Table for Supported Devices
static const struct dmi_system_id dmi_table[] = {
        {
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR,
                            "GIGABYTE"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME,
                            "AORUS 5 KE"),
		},
		.driver_data = &AORUS5KE0,
	},
    {}
};

MODULE_DEVICE_TABLE(dmi, dmi_table);

static const struct gigabyte_ec_conf *get_gigabyte_ec_conf(void)
{
    const struct dmi_system_id *dmi_entry;

    dmi_entry = dmi_first_match(dmi_table);
    return dmi_entry ? dmi_entry->driver_data : NULL;
}

// compares two strings, trimming newline at the end the second
// (from msi-ec https://github.com/BeardOverflow/msi-ec/blob/main/msi-ec.c#L1041C1-L1056C2)
static int strcmp_trim_newline2(const char *s, const char *s_nl)
{
    size_t s_nl_length = strlen(s_nl);

    if (s_nl_length - 1 > 10)
        return -1;

    if (s_nl[s_nl_length - 1] == '\n') {
        char s2[11];
        memcpy(s2, s_nl, s_nl_length - 1);
        s2[s_nl_length - 1] = '\0';
        return strcmp(s, s2);
    }

    return strcmp(s, s_nl);
}

static int ec_get_bit(u8 addr, u8 bit, u8* output)
{
    int result;
    u8 ec_result;

    result = ec_read(addr, &ec_result);
    if (result < 0) {
        printk("gigabyte-ec: Error reading from Embedded Controller.\n");
        return result;
    }

    *output = ((ec_result >> bit) & 1);
    return result;
}

static int ec_set_bit(u8 addr, u8 b, u8 bit)
{
    int ec_result;
    u8  stored;

    ec_result = ec_read(addr, &stored);
    if (ec_result < 0) {
        printk("gigabyte-ec: Error reading from Embedded Controller.\n");
        return ec_result;
    }

    if (b > 0)
        stored |= (1 << bit);
    else
        stored &= ~(1 << bit);

    ec_result = ec_write(addr, stored);
    if (ec_result < 0)
        printk("gigabyte-ec: Error writing to Embedded Controller.\n");

    return ec_result;
}

static int write_addr_bit(struct gigabyte_ec_addr_bit* addr_bit, u8 value)
{
    u8 size, addr;
    int result = 0;

    addr    = addr_bit->addr;
    size    = addr_bit->size;

    if (size == 1)
        result  = ec_set_bit(addr, value, addr_bit->bit);
    else if (size == 8)
        result = ec_write(addr, value);

    if (result < 0)
        printk("gigabyte-ec: Error writing to Embedded Controller.\n");

    return result;
}

static int read_addr_bit(struct gigabyte_ec_addr_bit* addr_bit, u8* value)
{
    u8 size, addr;
    int result = 0;

    addr    = addr_bit->addr;
    size    = addr_bit->size;

    if (size == 1)
        result  = ec_get_bit(addr, addr_bit->bit, value);
    else if (size == 8)
        result = ec_read(addr, value);

    if (result < 0)
        printk("gigabyte-ec: Error writing to Embedded Controller.\n");

    return result;
}

/* change the fan mode */
static int gigabyte_change_fan_mode(int mode)
{
    u8 value;
    int result = 0;

    for (int i = 0; i < GIGABYTE_EC_N_OF_ADDRESS; i++){
        value   = ec_conf.fan_mode.modes[mode].value[i];
        result  = write_addr_bit(&ec_conf.fan_mode.addr_bit[i], value);

        if (result < 0)
            printk("gigabyte-ec: Error changing fan mode (%d - %d).\n", mode, i);
    }
    return result;
}

static ssize_t fan_mode_show(struct device *device,
                             struct device_attribute *attr, char *buf)
{
    u8 stored, value;
    bool found = false;
    int result = 0;

    if (!conf_loaded)
        return -EINVAL;

    for (int i = 0; ec_conf.fan_mode.modes[i].name; i++) {
        found = true;
        for (int j = 0; j < GIGABYTE_EC_N_OF_ADDRESS; j++) {
            value = ec_conf.fan_mode.modes[i].value[j];
            result = read_addr_bit(&ec_conf.fan_mode.addr_bit[j], &stored);
            if (result < 0) goto error_ec;

            if (stored != value) found = false;
        }

        if (found)
            return sysfs_emit(buf, "%s\n", ec_conf.fan_mode.modes[i].name);
    }

    return sysfs_emit(buf, "unknown\n");

    error_ec:
    printk("gigabyte-ec: Error reading from embedded controller.\n");
    return sysfs_emit(buf, "error\n");
}

static ssize_t fan_mode_store(struct device *dev, struct device_attribute *attr,
                              const char *buf, size_t count)
{
    if(!conf_loaded || !count)
        return -EINVAL;

    for (int i = 0; ec_conf.fan_mode.modes[i].name; i++) {
        if (strcmp_trim_newline2(ec_conf.fan_mode.modes[i].name, buf)==0) {
            printk("gigabyte-ec: Changing fan_mode to: %s\n",buf);
            if (gigabyte_change_fan_mode(i) >= 0)
                printk("gigabyte-ec: fan_mode changed.\n");
            return count;
        }
    }

    return -EINVAL;
}


static int gigabyte_change_charging_mode(int mode)
{
    u8 value;
    int result = 0;

    for (int i = 0; i < 2; i++){
        value   = ec_conf.charging_mode.modes[mode].value[i];
        result  = write_addr_bit(&ec_conf.charging_mode.addr_bit[i], value);
    }

    if (result < 0)
        printk("gigabyte-ec: Error changing charging mode.\n");

    return result;
}

static ssize_t charging_mode_show(struct device *device,
                             struct device_attribute *attr, char *buf)
{
    u8 stored, value;
    bool found = false;
    int result = 0;

    if (!conf_loaded)
        return -EINVAL;

    for (int i = 0; ec_conf.charging_mode.modes[i].name; i++) {
        found = true;
        // The first address should be the one that defines the charging mode
        value = ec_conf.charging_mode.modes[i].value[0];
        result = read_addr_bit(&ec_conf.charging_mode.addr_bit[0], &stored);

        if (result < 0) goto error_ec;

        if (stored != value) found = false;

        if (found)
            return sysfs_emit(buf, "%s\n", ec_conf.charging_mode.modes[i].name);
    }

    return sysfs_emit(buf, "unknown\n");

    error_ec:
    printk("gigabyte-ec: Error reading from embedded controller.\n");
    return sysfs_emit(buf, "error\n");
}

static ssize_t charging_mode_store(struct device *dev, struct device_attribute *attr,
                              const char *buf, size_t count)
{
    if(!conf_loaded || !count)
        return -EINVAL;

    for (int i = 0; ec_conf.charging_mode.modes[i].name; i++) {
        if (strcmp_trim_newline2(ec_conf.charging_mode.modes[i].name, buf)==0) {
            printk("gigabyte-ec: Changing charging_mode to: %s\n",buf);
            if (gigabyte_change_charging_mode(i) >= 0)
                printk("gigabyte-ec: charging_mode changed.\n");
            return count;
        }
    }

    return -EINVAL;
}

static ssize_t charge_control_threshold_show(struct device *device,
                                  struct device_attribute *attr, char *buf)
{
    u8 stored;
    int result = 0;

    if (!conf_loaded)
        return -EINVAL;

    result = read_addr_bit(&ec_conf.charge_threshold.addr_bit[0], &stored);
    if (result < 0)
        return result;

    return sysfs_emit(buf, "%u", stored);
}

static ssize_t charge_control_threshold_store(struct device *dev, struct device_attribute *attr,
                                   const char *buf, size_t count)
{
    int result;
    u8 store;

    if(!conf_loaded || !count)
        return -EINVAL;

    result = kstrtou8(buf, 10, &store);
    if (result < 0)
        return -EINVAL;

    if (store < ec_conf.charge_threshold.range_min ||
        store > ec_conf.charge_threshold.range_max)
        return -EINVAL;

    result = write_addr_bit(&ec_conf.charge_threshold.addr_bit[0], store);
    if (result < 0)
        return result;

    return count;
}

static DEVICE_ATTR_RW(fan_mode);
static DEVICE_ATTR_RW(charging_mode);
static DEVICE_ATTR_RW(charge_control_threshold);

// Sysfs Platform driver
// Creates fan_mode in /sys/kernel/gigabyte-ec/fan_mode

static int gigabyte_platform_probe(struct platform_device *pdev)
{
    if (!conf_loaded)
        return -EINVAL;

    gigabyte_ec_kobj = kobject_create_and_add("gigabyte-ec", kernel_kobj);
    if(!gigabyte_ec_kobj){
        printk("gigabyte-ec: Error creating /sys/kernel/gigabyte-ec\n");
        return -ENOMEM;
    }

    if(sysfs_create_file(gigabyte_ec_kobj, &dev_attr_fan_mode.attr)) {
        printk("gigabyte-ec: Error creating /sys/kernel/gigabyte-ec/fan_mode\n");
        kobject_put(gigabyte_ec_kobj);
        return -ENOMEM;
    }

    if(sysfs_create_file(gigabyte_ec_kobj, &dev_attr_charging_mode.attr)) {
        printk("gigabyte-ec: Error creating /sys/kernel/gigabyte-ec/charging_mode\n");
        sysfs_remove_file(gigabyte_ec_kobj, &dev_attr_fan_mode.attr);
        kobject_put(gigabyte_ec_kobj);
        return -ENOMEM;
    }

    if(sysfs_create_file(gigabyte_ec_kobj, &dev_attr_charge_control_threshold.attr)) {
        printk("gigabyte-ec: Error creating /sys/kernel/gigabyte-ec/charge_control_threshold\n");
        sysfs_remove_file(gigabyte_ec_kobj, &dev_attr_charging_mode.attr);
        sysfs_remove_file(gigabyte_ec_kobj, &dev_attr_fan_mode.attr);
        kobject_put(gigabyte_ec_kobj);
        return -ENOMEM;
    }

    return 0;
}

static int gigabyte_platform_remove(struct platform_device *pdev)
{
    if (conf_loaded) {
        sysfs_remove_file(gigabyte_ec_kobj, &dev_attr_charge_control_threshold.attr);
        sysfs_remove_file(gigabyte_ec_kobj, &dev_attr_charging_mode.attr);
        sysfs_remove_file(gigabyte_ec_kobj, &dev_attr_fan_mode.attr);
        kobject_put(gigabyte_ec_kobj);
    }

    return 0;
}

static struct platform_device *gigabyte_platform_device;

static struct platform_driver gigabyte_platform_driver = {
        .driver = {
                .name = GIGABYTE_EC_DRIVER_NAME,
        },
        .probe  = gigabyte_platform_probe,
        .remove = gigabyte_platform_remove,
};

static int __init gigabyte_ec_init(void) {
    int result = 0;
    const struct gigabyte_ec_conf* conf;
    conf = get_gigabyte_ec_conf();

    if (!conf)
        return -ENODEV;
        
    memcpy(&ec_conf, conf, sizeof(struct gigabyte_ec_conf));
    conf_loaded = true;

    result = platform_driver_register(&gigabyte_platform_driver);
    if (result < 0)
        return result;

    gigabyte_platform_device = platform_device_alloc(GIGABYTE_EC_DRIVER_NAME, -1);
    if (gigabyte_platform_device == NULL) {
        platform_driver_unregister(&gigabyte_platform_driver);
        return -ENOMEM;
    }

    result = platform_device_add(gigabyte_platform_device);
    if (result < 0) {
        platform_device_del(gigabyte_platform_device);
        platform_driver_unregister(&gigabyte_platform_driver);
        return result;
    }

    return result;
}

static void __exit gigabyte_ec_exit(void) {
    platform_driver_unregister(&gigabyte_platform_driver);
    platform_device_del(gigabyte_platform_device);
}

/* Meta Information */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Douglas Corrêa - dferrazc@gmail.com");
MODULE_DESCRIPTION("Gigabyte Embedded Controller for AORUS laptops support.");
MODULE_VERSION("0.01");

module_init(gigabyte_ec_init);
module_exit(gigabyte_ec_exit);
