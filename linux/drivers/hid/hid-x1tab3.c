/*
 *  HID driver for Lenovo:
 *  - ThinkPad X1 Tablet Gen3 Keyboard as USB
 *
 *  Copyright (c) 2019 Amos Bird <amosbird@gmail.com>
 *  Based on https://www.mail-archive.com/linux-sound@vger.kernel.org/msg00104.html
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/sysfs.h>

#include "hid-ids.h"

enum {
    HID_LENOVO_LED_MUTE,
    HID_LENOVO_LED_MICMUTE,
    HID_LENOVO_LED_FNLOCK,
    HID_LENOVO_LED_MAX,
};

struct led_table_entry {
    struct led_classdev* dev;
    uint8_t state;
};

static struct led_table_entry hid_lenovo_led_table[HID_LENOVO_LED_MAX];

struct lenovo_drvdata_tpx1cover {
    uint16_t led_state;
    uint8_t fnlock_state;
    uint8_t led_present;
    struct led_classdev led_mute;
    struct led_classdev led_micmute;
    struct led_classdev led_fnlock;
};

int hid_lenovo_led_set(int led_num, bool on) {
    struct led_classdev* dev;

    if (led_num >= HID_LENOVO_LED_MAX)
        return -EINVAL;

    dev = hid_lenovo_led_table[led_num].dev;
    hid_lenovo_led_table[led_num].state = on;

    if (!dev)
        return -ENODEV;

    if (!dev->brightness_set)
        return -ENODEV;

    dev->brightness_set(dev, on ? LED_FULL : LED_OFF);

    return 0;
}

EXPORT_SYMBOL_GPL(hid_lenovo_led_set);

#define map_key_clear(c) hid_map_usage_clear(hi, usage, bit, max, EV_KEY, (c))

static int lenovo_input_mapping_tpx1cover(struct hid_device* hdev, struct hid_input* hi,
    struct hid_field* field, struct hid_usage* usage, unsigned long** bit, int* max) {
    if ((usage->hid & HID_USAGE_PAGE) == HID_UP_CONSUMER) {
        switch (usage->hid & HID_USAGE) {
        case 0x0001: // Unknown keys -> Idenditied by usage index!
            map_key_clear(KEY_UNKNOWN);
            switch (usage->usage_index) {
            case 0x8:
                input_set_capability(hi->input, EV_KEY, KEY_FN);
                break;

            case 0x9:
                input_set_capability(hi->input, EV_KEY, KEY_MICMUTE);
                break;

            case 0xa:
                input_set_capability(hi->input, EV_KEY, KEY_CONFIG);
                break;

            case 0xb:
                input_set_capability(hi->input, EV_KEY, KEY_SEARCH);
                break;

            case 0xc:
                input_set_capability(hi->input, EV_KEY, KEY_SETUP);
                break;

            case 0xd:
                input_set_capability(hi->input, EV_KEY, KEY_SWITCHVIDEOMODE);
                break;

            case 0xe:
                input_set_capability(hi->input, EV_KEY, KEY_RFKILL);
                break;

            default:
                return -1;
            }

            return 1;

        case 0x006f: // Consumer.006f ---> Key.BrightnessUp
            map_key_clear(KEY_BRIGHTNESSUP);
            return 1;

        case 0x0070: // Consumer.0070 ---> Key.BrightnessDown
            map_key_clear(KEY_BRIGHTNESSDOWN);
            return 1;

        case 0x00b7: // Consumer.00b7 ---> Key.StopCD
            map_key_clear(KEY_STOPCD);
            return 1;

        case 0x00cd: // Consumer.00cd ---> Key.PlayPause
            map_key_clear(KEY_PLAYPAUSE);
            return 1;

        case 0x00e0: // Consumer.00e0 ---> Absolute.Volume
            return 0;
        case 0x00e2: // Consumer.00e2 ---> Key.Mute
            map_key_clear(KEY_MUTE);
            return 1;

        case 0x00e9: // Consumer.00e9 ---> Key.VolumeUp
            map_key_clear(KEY_VOLUMEUP);
            return 1;

        case 0x00ea: // Consumer.00ea ---> Key.VolumeDown
            map_key_clear(KEY_VOLUMEDOWN);
            return 1;
        }
    }

    return 0;
}

static enum led_brightness lenovo_led_brightness_get_tpx1cover(struct led_classdev* led_cdev) {
    struct device* dev = led_cdev->dev->parent;
    struct hid_device* hdev = to_hid_device(dev);
    struct lenovo_drvdata_tpx1cover* drv_data = hid_get_drvdata(hdev);
    int led_nr = 0;

    if (led_cdev == &drv_data->led_mute)
        led_nr = 0;
    else if (led_cdev == &drv_data->led_micmute)
        led_nr = 1;
    else if (led_cdev == &drv_data->led_fnlock)
        led_nr = 2;
    else
        return LED_OFF;

    return drv_data->led_state & (1 << led_nr) ? LED_FULL : LED_OFF;
}

static void lenovo_led_brightness_set_tpx1cover(
    struct led_classdev* led_cdev, enum led_brightness value) {
    struct device* dev = led_cdev->dev->parent;
    struct hid_device* hdev = to_hid_device(dev);
    struct lenovo_drvdata_tpx1cover* drv_data = hid_get_drvdata(hdev);
    struct hid_report* report;
    int led_nr = -1;
    int led_nr_hw = -1;

    if (led_cdev == &drv_data->led_mute) {
        led_nr = 0;
        led_nr_hw = 0x64;
    } else if (led_cdev == &drv_data->led_micmute) {
        led_nr = 1;
        led_nr_hw = 0x74;
    } else if (led_cdev == &drv_data->led_fnlock) {
        led_nr = 2;
        led_nr_hw = 0x54;
    } else {
        hid_warn(hdev, "Invalid LED to set.\n");
        return;
    }

    if (value == LED_OFF)
        drv_data->led_state &= ~(1 << led_nr);
    else
        drv_data->led_state |= 1 << led_nr;

    report = hdev->report_enum[HID_OUTPUT_REPORT].report_id_hash[9];
    if (report) {
        report->field[0]->value[0] = led_nr_hw;
        report->field[0]->value[1] = (drv_data->led_state & (1 << led_nr)) ? 0x02 : 0x01;
        hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
    }
}

static int lenovo_event_tpx1cover(
    struct hid_device* hdev, struct hid_field* field, struct hid_usage* usage, __s32 value) {
    int ret = 0;

    if ((usage->hid & HID_USAGE_PAGE) == HID_UP_CONSUMER && (usage->hid & HID_USAGE) == 0x0001) {
        if (usage->usage_index == 0x8 && value == 1) {
            /* struct lenovo_drvdata_tpx1cover* drv_data = hid_get_drvdata(hdev); */

            /* printk(KERN_INFO "usage->usage_index = %d, value = %d\n", usage->usage_index, value); */
            /* if (drv_data) */
            /* { */
            /*     printk(KERN_INFO "not null\n"); */
            /*     printk(KERN_INFO "drv_data->led_present = %d\n", drv_data->led_present); */
            /* } */
            /* if (drv_data && drv_data->led_present) { */
            /*     printk(KERN_INFO "inside\n"); */
            /*     drv_data->fnlock_state */
            /*         = lenovo_led_brightness_get_tpx1cover(&drv_data->led_fnlock) == LED_OFF ? 1 : 0; */
            /*     lenovo_led_brightness_set_tpx1cover( */
            /*         &drv_data->led_fnlock, drv_data->fnlock_state ? LED_FULL : LED_OFF); */
            /*     drv_data->led_present = 0; */
            /* } */
        }

        if (usage->usage_index == 0x9 && value == 1) {
            input_event(field->hidinput->input, EV_KEY, KEY_MICMUTE, 1);
            input_sync(field->hidinput->input);
            input_event(field->hidinput->input, EV_KEY, KEY_MICMUTE, 0);
            input_sync(field->hidinput->input);
            ret = 1;
        }

        if (usage->usage_index == 0xa && value == 1) {
            input_event(field->hidinput->input, EV_KEY, KEY_CONFIG, 1);
            input_sync(field->hidinput->input);
            input_event(field->hidinput->input, EV_KEY, KEY_CONFIG, 0);
            input_sync(field->hidinput->input);

            ret = 1;
        }

        if (usage->usage_index == 0xb && value == 1) {
            input_event(field->hidinput->input, EV_KEY, KEY_SEARCH, 1);
            input_sync(field->hidinput->input);
            input_event(field->hidinput->input, EV_KEY, KEY_SEARCH, 0);
            input_sync(field->hidinput->input);

            ret = 1;
        }

        if (usage->usage_index == 0xc && value == 1) {
            input_event(field->hidinput->input, EV_KEY, KEY_SETUP, 1);
            input_sync(field->hidinput->input);
            input_event(field->hidinput->input, EV_KEY, KEY_SETUP, 0);
            input_sync(field->hidinput->input);

            ret = 1;
        }

        if (usage->usage_index == 0xd && value == 1) {
            input_event(field->hidinput->input, EV_KEY, KEY_SWITCHVIDEOMODE, 1);
            input_sync(field->hidinput->input);
            input_event(field->hidinput->input, EV_KEY, KEY_SWITCHVIDEOMODE, 0);
            input_sync(field->hidinput->input);

            ret = 1;
        }

        if (usage->usage_index == 0xe && value == 1) {
            input_event(field->hidinput->input, EV_KEY, KEY_RFKILL, 1);
            input_sync(field->hidinput->input);
            input_event(field->hidinput->input, EV_KEY, KEY_RFKILL, 0);
            input_sync(field->hidinput->input);

            ret = 1;
        }
    }

    return ret;
}

static int lenovo_probe_tpx1cover_configure(struct hid_device* hdev) {
    struct hid_report* report = hdev->report_enum[HID_OUTPUT_REPORT].report_id_hash[9];
    struct lenovo_drvdata_tpx1cover* drv_data = hid_get_drvdata(hdev);

    if (!drv_data)
        return -ENODEV;

    if (!report)
        return -ENOENT;

    report->field[0]->value[0] = 0x54;
    report->field[0]->value[1] = 0x20;
    hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
    hid_hw_wait(hdev);

    report->field[0]->value[0] = 0x54;
    report->field[0]->value[1] = 0x08;
    hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
    hid_hw_wait(hdev);

    report->field[0]->value[0] = 0xA0;
    report->field[0]->value[1] = 0x02;
    hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
    hid_hw_wait(hdev);

    lenovo_led_brightness_set_tpx1cover(
        &drv_data->led_mute, hid_lenovo_led_table[HID_LENOVO_LED_MUTE].state ? LED_FULL : LED_OFF);
    hid_hw_wait(hdev);

    lenovo_led_brightness_set_tpx1cover(&drv_data->led_micmute,
        hid_lenovo_led_table[HID_LENOVO_LED_MICMUTE].state ? LED_FULL : LED_OFF);
    hid_hw_wait(hdev);

    lenovo_led_brightness_set_tpx1cover(&drv_data->led_fnlock, LED_FULL);

    return 0;
}

static int lenovo_probe_tpx1cover_special_functions(struct hid_device* hdev) {
    struct device* dev = &hdev->dev;
    struct lenovo_drvdata_tpx1cover* drv_data = NULL;

    size_t name_sz = strlen(dev_name(dev)) + 16;
    char* name_led = NULL;

    struct hid_report* report;
    bool report_match = 1;

    int ret = 0;

    report = hid_validate_values(hdev, HID_INPUT_REPORT, 2, 0, 1);
    report_match &= report ? 1 : 0;
    report = hid_validate_values(hdev, HID_INPUT_REPORT, 3, 0, 16);
    report_match &= report ? 1 : 0;
    report = hid_validate_values(hdev, HID_OUTPUT_REPORT, 9, 0, 2);
    report_match &= report ? 1 : 0;
    /* report = hid_validate_values(hdev, HID_FEATURE_REPORT, 32, 0, 1); */
    /* report_match &= report ? 1 : 0; */
    report = hid_validate_values(hdev, HID_FEATURE_REPORT, 84, 0, 1);
    report_match &= report ? 1 : 0;
    report = hid_validate_values(hdev, HID_FEATURE_REPORT, 100, 0, 1);
    report_match &= report ? 1 : 0;
    report = hid_validate_values(hdev, HID_FEATURE_REPORT, 116, 0, 1);
    report_match &= report ? 1 : 0;
    /* report = hid_validate_values(hdev, HID_FEATURE_REPORT, 132, 0, 1); */
    /* report_match &= report ? 1 : 0; */
    report = hid_validate_values(hdev, HID_FEATURE_REPORT, 144, 0, 1);
    report_match &= report ? 1 : 0;
    /* report = hid_validate_values(hdev, HID_FEATURE_REPORT, 162, 0, 1); */
    /* report_match &= report ? 1 : 0; */

    if (!report_match) {
        printk(KERN_INFO "unmatched\n");
        ret = -ENODEV;
        goto err;
    }

    drv_data = devm_kzalloc(&hdev->dev, sizeof(struct lenovo_drvdata_tpx1cover), GFP_KERNEL);

    if (!drv_data) {
        hid_err(hdev, "Could not allocate memory for tpx1cover driver data\n");
        ret = -ENOMEM;
        goto err;
    }

    printk(KERN_INFO "matched\n");
    name_led = devm_kzalloc(&hdev->dev, name_sz, GFP_KERNEL);
    if (!name_led) {
        hid_err(hdev, "Could not allocate memory for mute led data\n");
        ret = -ENOMEM;
        goto err_cleanup;
    }
    snprintf(name_led, name_sz, "%s:amber:mute", dev_name(dev));

    drv_data->led_mute.name = name_led;
    drv_data->led_mute.brightness_get = lenovo_led_brightness_get_tpx1cover;
    drv_data->led_mute.brightness_set = lenovo_led_brightness_set_tpx1cover;
    drv_data->led_mute.dev = dev;
    hid_lenovo_led_table[HID_LENOVO_LED_MUTE].dev = &drv_data->led_mute;
    led_classdev_register(dev, &drv_data->led_mute);

    name_led = devm_kzalloc(&hdev->dev, name_sz, GFP_KERNEL);
    if (!name_led) {
        hid_err(hdev, "Could not allocate memory for mic mute led data\n");
        ret = -ENOMEM;
        goto err_cleanup;
    }
    snprintf(name_led, name_sz, "%s:amber:micmute", dev_name(dev));

    drv_data->led_micmute.name = name_led;
    drv_data->led_micmute.brightness_get = lenovo_led_brightness_get_tpx1cover;
    drv_data->led_micmute.brightness_set = lenovo_led_brightness_set_tpx1cover;
    drv_data->led_micmute.dev = dev;
    hid_lenovo_led_table[HID_LENOVO_LED_MICMUTE].dev = &drv_data->led_micmute;
    led_classdev_register(dev, &drv_data->led_micmute);

    name_led = devm_kzalloc(&hdev->dev, name_sz, GFP_KERNEL);
    if (!name_led) {
        hid_err(hdev, "Could not allocate memory for FN lock led data\n");
        ret = -ENOMEM;
        goto err_cleanup;
    }

    snprintf(name_led, name_sz, "%s:amber:fnlock", dev_name(dev));

    drv_data->led_fnlock.name = name_led;
    drv_data->led_fnlock.brightness_get = lenovo_led_brightness_get_tpx1cover;
    drv_data->led_fnlock.brightness_set = lenovo_led_brightness_set_tpx1cover;
    drv_data->led_fnlock.dev = dev;
    hid_lenovo_led_table[HID_LENOVO_LED_FNLOCK].dev = &drv_data->led_fnlock;
    led_classdev_register(dev, &drv_data->led_fnlock);

    drv_data->led_state = 0;
    drv_data->fnlock_state = 1;
    drv_data->led_present = 1;

    hid_set_drvdata(hdev, drv_data);

    return lenovo_probe_tpx1cover_configure(hdev);

err_cleanup:
    if (drv_data->led_fnlock.name) {
        led_classdev_unregister(&drv_data->led_fnlock);
        devm_kfree(&hdev->dev, (void*)drv_data->led_fnlock.name);
    }

    if (drv_data->led_micmute.name) {
        led_classdev_unregister(&drv_data->led_micmute);
        devm_kfree(&hdev->dev, (void*)drv_data->led_micmute.name);
    }

    if (drv_data->led_mute.name) {
        led_classdev_unregister(&drv_data->led_mute);
        devm_kfree(&hdev->dev, (void*)drv_data->led_mute.name);
    }

    if (drv_data)
        kfree(drv_data);

err:
    return ret;
}

static int lenovo_probe_tpx1cover_touch(struct hid_device* hdev) {
    struct hid_report* report;
    bool report_match = 1;
    int ret = 0;

    report = hid_validate_values(hdev, HID_INPUT_REPORT, 2, 0, 2);
    report_match &= report ? 1 : 0;
    report = hid_validate_values(hdev, HID_INPUT_REPORT, 2, 1, 2);
    report_match &= report ? 1 : 0;
    report = hid_validate_values(hdev, HID_INPUT_REPORT, 11, 0, 61);
    report_match &= report ? 1 : 0;
    report = hid_validate_values(hdev, HID_INPUT_REPORT, 12, 0, 61);
    report_match &= report ? 1 : 0;
    report = hid_validate_values(hdev, HID_INPUT_REPORT, 16, 0, 3);
    report_match &= report ? 1 : 0;
    report = hid_validate_values(hdev, HID_INPUT_REPORT, 16, 1, 2);
    report_match &= report ? 1 : 0;
    report = hid_validate_values(hdev, HID_OUTPUT_REPORT, 9, 0, 20);
    report_match &= report ? 1 : 0;
    report = hid_validate_values(hdev, HID_OUTPUT_REPORT, 10, 0, 20);
    report_match &= report ? 1 : 0;
    report = hid_validate_values(hdev, HID_FEATURE_REPORT, 14, 0, 1);
    report_match &= report ? 1 : 0;
    report = hid_validate_values(hdev, HID_FEATURE_REPORT, 15, 0, 3);
    report_match &= report ? 1 : 0;

    if (!report_match)
        ret = -ENODEV;

    return ret;
}

static int lenovo_probe_tpx1cover(struct hid_device* hdev) {
    int ret = 0;

    printk(KERN_INFO "Hello world\n");

    /*
     * Probing for special function keys and LED control -> usb intf 1
     * Probing for touch input -> usb intf 2 (handled by rmi4 driver)
     * Other (keyboard) -> usb intf 0
     */
    if (!lenovo_probe_tpx1cover_special_functions(hdev)) {
        // special function keys and LED control
        ret = 0;
    } else if (!lenovo_probe_tpx1cover_touch(hdev)) {
        // handled by rmi
        ret = -ENODEV;
    } else {
        // keyboard
        struct lenovo_drvdata_tpx1cover* drv_data;

        drv_data = devm_kzalloc(&hdev->dev, sizeof(struct lenovo_drvdata_tpx1cover), GFP_KERNEL);

        if (!drv_data) {
            hid_err(hdev, "Could not allocate memory for tpx1cover driver data\n");
            ret = -ENOMEM;
            goto out;
        }

        drv_data->led_state = 0;
        drv_data->led_present = 0;
        drv_data->fnlock_state = 1;
        hid_set_drvdata(hdev, drv_data);
        printk(KERN_INFO "try setting fn lock\n");

        ret = 0;
    }

out:
    return ret;
}

static void lenovo_remove_tpx1cover(struct hid_device* hdev) {
    struct lenovo_drvdata_tpx1cover* drv_data = hid_get_drvdata(hdev);

    if (!drv_data)
        return;

    if (drv_data->led_present) {
        if (drv_data->led_fnlock.name) {
            hid_lenovo_led_table[HID_LENOVO_LED_FNLOCK].dev = NULL;

            led_classdev_unregister(&drv_data->led_fnlock);
            devm_kfree(&hdev->dev, (void*)drv_data->led_fnlock.name);
        }

        if (drv_data->led_micmute.name) {
            hid_lenovo_led_table[HID_LENOVO_LED_MICMUTE].dev = NULL;

            led_classdev_unregister(&drv_data->led_micmute);
            devm_kfree(&hdev->dev, (void*)drv_data->led_micmute.name);
        }

        if (drv_data->led_mute.name) {
            hid_lenovo_led_table[HID_LENOVO_LED_MUTE].dev = NULL;

            led_classdev_unregister(&drv_data->led_mute);
            devm_kfree(&hdev->dev, (void*)drv_data->led_mute.name);
        }
    }

    if (drv_data)
        devm_kfree(&hdev->dev, drv_data);

    hid_set_drvdata(hdev, NULL);
}

static int lenovo_probe(struct hid_device* hdev, const struct hid_device_id* id) {
    int ret;

    ret = hid_parse(hdev);
    if (ret) {
        hid_err(hdev, "hid_parse failed\n");
        goto err;
    }

    ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
    if (ret) {
        hid_err(hdev, "hid_hw_start failed\n");
        goto err;
    }

    switch (hdev->product) {
    case USB_DEVICE_ID_LENOVO_X1_TAB3:
        ret = lenovo_probe_tpx1cover(hdev);
        break;
    default:
        ret = 0;
        break;
    }
    if (ret)
        goto err_hid;

    return 0;
err_hid:
    hid_hw_stop(hdev);
err:
    return ret;
}

static const struct hid_device_id lenovo_devices[]
    = { { HID_USB_DEVICE(USB_VENDOR_ID_LENOVO, USB_DEVICE_ID_LENOVO_X1_TAB3) }, {} };

MODULE_DEVICE_TABLE(hid, lenovo_devices);

static struct hid_driver lenovo_driver = {
    .name = "lenovo",
    .id_table = lenovo_devices,
    .input_mapping = lenovo_input_mapping_tpx1cover,
    .probe = lenovo_probe,
    .remove = lenovo_remove_tpx1cover,
    .event = lenovo_event_tpx1cover,
};

module_hid_driver(lenovo_driver);

MODULE_LICENSE("GPL");
