/*
 * SPDX-License-Identifier: MIT
 * Copyright 2024 Oskari Rauta <oskari.rauta@gmail.com>
 */
#include <linux/usb.h>
#include <drm/drm_file.h>
#include <drm/drm_print.h>

#include "bp_drv.h"

// TODO: this file needs work, it's almost all mpro

#if IS_ENABLED(CONFIG_BACKLIGHT_CLASS_DEVICE) || IS_ENABLED(CONFIG_FB_BACKLIGHT)

#define BACKLIGHT_NAME		"beadapanel_bl"

#define MIN_BL_LEVEL		0
#define MAX_BL_LEVEL		255

static char cmd_set_brightness[8] = {
        0x00, 0x51, 0x02, 0x00, 0x00, 0x00, 0xff, 0x00
};

int bp_bl_get_brightness(struct backlight_device *bd) {

	int level = bd -> props.brightness;

	if ( level > bd -> props.max_brightness )
		level = bd -> props.max_brightness;
	else if ( level < MIN_BL_LEVEL )
		level = MIN_BL_LEVEL;

	return level;
}

int bp_bl_update_status(struct backlight_device *bd) {

	struct bp_device *bp = bl_get_data(bd);
	struct usb_device *udev = bp_to_usb_device(bp);
	int level = bd -> props.brightness;

	if ( bd -> props.power != bp -> bl.power ) {

		if ( bd -> props.power == FB_BLANK_UNBLANK )
			bd -> props.max_brightness = MAX_BL_LEVEL;
		else if (bd -> props.power == FB_BLANK_NORMAL )
			bd -> props.max_brightness = (MAX_BL_LEVEL * 0.2) * 3;
		else if (bd -> props.power == FB_BLANK_VSYNC_SUSPEND)
			bd -> props.max_brightness = (MAX_BL_LEVEL * 0.2) * 2;
		else if (bd -> props.power == FB_BLANK_HSYNC_SUSPEND)
			bd -> props.max_brightness = MAX_BL_LEVEL * 0.2;
		else if (bd -> props.power == FB_BLANK_POWERDOWN)
			bd -> props.max_brightness = 0;
		else if (bd -> props.power > FB_BLANK_POWERDOWN) {
			bd -> props.power = FB_BLANK_POWERDOWN;
			bd -> props.max_brightness = 0;
		}

		level = bd -> props.max_brightness;
		bp -> bl.power = bd -> props.power;
	}

	if ( level < MIN_BL_LEVEL )
		level = MIN_BL_LEVEL;
	else if ( level > bd -> props.max_brightness )
		return -EINVAL;
	else if ( level > MAX_BL_LEVEL )
		level = MAX_BL_LEVEL;

	cmd_set_brightness[6] = level;

	mutex_lock(&bp -> ctrl_lock);

	int ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				  0xb0, 0x40, 0, bp -> ifnum,
				  (void*)cmd_set_brightness, 8,
				  MPRO_MAX_DELAY);

	mutex_unlock(&bp -> ctrl_lock);

	if ( ret < 5 )
		return -EIO;

	bd -> props.brightness = level;
	return 0;
}

int bp_bl_suspend(struct usb_interface *interface, pm_message_t message) {

	struct drm_device *dev = usb_get_intfdata(interface);
	struct bp_device *bp = to_bp(dev);

	bp -> bl.before_suspend = bp -> bl.dev -> props.power;
	bp -> bl.before_suspend_br = bp -> bl.dev -> props.brightness;
	backlight_disable(bp -> bl.dev);
	return 0;
}

int bp_bl_resume(struct usb_interface *interface) {

	struct drm_device *dev = usb_get_intfdata(interface);
	struct bp_device *bp = to_bp(dev);

	backlight_enable(bp -> bl.dev);
	bp -> bl.dev -> props.power = bp -> bl.before_suspend;
	bp -> bl.dev -> props.brightness = bp -> bl.before_suspend_br;
	bp_bl_update_status(bp -> bl.dev);
	backlight_device_set_brightness(bp -> bl.dev, bp -> bl.before_suspend_br);
	return 0;
}


static const struct backlight_ops bp_bl_ops = {
	.update_status = bp_bl_update_status,
	.get_brightness = bp_bl_get_brightness,
};

static const struct backlight_properties bp_bl_props = {
	.type = BACKLIGHT_RAW,
	.max_brightness = MAX_BL_LEVEL,
	.brightness = MAX_BL_LEVEL,
	.fb_blank = FB_BLANK_UNBLANK,
	.power = FB_BLANK_UNBLANK,
};

void bp_bl_init(struct bp_device* bp) {

	const char *name;
	struct drm_device *dev = &bp -> dev;
	name = kasprintf(GFP_KERNEL, "beadapanel_bl%d", dev -> primary -> index);
	if ( !name ) {
		drm_err(&bp -> dev, "backlight name allocation failed, backlight registration cancelled");
		return;
	}

	bp -> bl.dev = devm_backlight_device_register(bp -> dmadev, name, bp -> dev.dev, bå, &bp_bl_ops, &bp_bl_props);
	kfree(name);

	if ( IS_ERR(bp -> bl.dev)) {
		drm_err(&bp -> dev, "unable to register backlight device\n");
		return;
	}

	bp -> bl.power = bp -> bl.dev -> props.power;
	bp -> bl.before_suspend = bp -> bl.dev -> props.power;
	drm_info(&bp -> dev, "backlight registered\n");
}

void bp_bl_deinit(struct bp_device* bp) {

	if ( bp -> bl.dev == NULL )
		return;

	backlight_disable(bp -> bl.dev);

	devm_backlight_device_unregister(bp -> dmadev, bp -> bl.dev);
	bp -> bl.dev = NULL;
}

#else
void bp_bl_init(struct bp_device *bp) {}
void bp_bl_deinit(struct bp_device *bp) {}
#endif
