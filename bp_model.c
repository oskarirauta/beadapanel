/*
 * SPDX-License-Identifier: MIT
 * Copyright 2024 Oskari Rauta <oskari.rauta@gmail.com>
 */
#include <linux/module.h>
#include <linux/usb.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modes.h>
#include <drm/drm_rect.h>

#include "bp_drv.h"

// TODO: re-create file, all this is for mpro

#define MODEL_DEFAULT		"MPRO\n"
#define MODEL_5IN		"MPRO-5\n"
#define MODEL_5IN_OLED		"MPRO-5H\n"
#define MODEL_4IN3		"MPRO-4IN3\n"
#define MODEL_4IN		"MPRO-4\n"
#define MODEL_6IN8		"MPRO-6IN8\n"
#define MODEL_3IN4		"MPRO-3IN4\n"

static const char cmd_get_screen[5] = {
	0x51, 0x02, 0x04, 0x1f, 0xfc
};

static const char cmd_get_version[5] = {
	0x51, 0x02, 0x04, 0x1f, 0xf8
};

static const char cmd_get_id[5] = {
	0x51, 0x02, 0x08, 0x1f, 0xf0
};

int bp_get_screen(struct bp_device *bp) {

	struct usb_device *udev = bp_to_usb_device(bp);
	void *cmd = bp -> cmd;
	int ret;

	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				0xb5, 0x40, 0, 0,
				(void*)cmd_get_screen, 5,
				MPRO_MAX_DELAY);

	if ( ret < 5 )
		return -EIO;

	ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				0xb6, 0xc0, 0, 0,
				cmd, 1, MPRO_MAX_DELAY);

	if ( ret < 1 )
		return -EIO;

	ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				0xb7, 0xc0, 0, 0,
				cmd, 5, MPRO_MAX_DELAY);

	if ( ret < 5 )
		return -EIO;

	bp -> screen = ((unsigned int*)(cmd + 1))[0];
	return 0;
}

int bp_get_version(struct bp_device *bp) {

	struct usb_device *udev = bp_to_usb_device(bp);
	void *cmd = bp -> cmd;
	int ret;

	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				0xb5, 0x40, 0, 0,
				(void*)cmd_get_version, 5,
				BP_MAX_DELAY);

	if ( ret < 5 )
		return -EIO;

	ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				0xb6, 0xc0, 0, 0,
				cmd, 1, BP_MAX_DELAY);

	if ( ret < 1 )
		return -EIO;

	ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				0xb7, 0xc0, 0, 0,
				cmd, 5, BP_MAX_DELAY);

	if ( ret < 5 )
		return -EIO;

	bp -> version = ((unsigned int *)(cmd + 1))[0];
	return 0;
}

int bp_get_id(struct bp_device *bp) {

	struct usb_device *udev = bp_to_usb_device(bp);
	void *cmd = bp -> cmd;
	int ret;

	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				0xb5, 0x40, 0, 0,
				(void*)cmd_get_id, 5,
				BP_MAX_DELAY);

	if ( ret < 5 )
		return -EIO;

	ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				0xb6, 0xc0, 0, 0,
				cmd, 1, BP_MAX_DELAY);

	if ( ret < 1 )
		return -EIO;

	ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				0xb7, 0xc0, 0, 0,
				cmd, 9, BP_MAX_DELAY);

	if ( ret < 5 )
		return -EIO;

	memcpy(bp -> id, cmd + 1, 8);
	return 0;
}

static const struct drm_mode_config_funcs bp_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static void bp_create_info(struct bp_device *bp, unsigned int width, unsigned int height, unsigned int width_mm, unsigned int height_mm, unsigned int margin) {

	struct drm_rect r = { .x1 = 0, .y1 = 0, .x2 = width - 1, .y2 = height - 1 };
	bp -> info.width = width;
	bp -> info.height = height;
	bp -> info.width_mm = width_mm;
	bp -> info.height_mm = height_mm;
	bp -> info.margin = margin;
	bp -> info.hz = 60;
	bp -> info.rect = r;

	const struct drm_display_mode mode = {
		DRM_MODE_INIT(bp -> info.hz, bp -> info.width, bp -> info.height, bp -> info.width_mm, bp -> info.height_mm)
	};

	bp -> mode = mode;
}

void bp_mode_config_setup(struct bp_device *bp) {

	struct drm_device *dev = &bp -> dev;

	switch ( bp -> screen ) {
	case 0x00000005:
		bp_create_info(bp, 480, 854, 62, 110, bp -> version != 0x00000003 ? 320 : 0);
		bp -> info.name = MODEL_5IN;
		break;

	case 0x00001005:
		bp_create_info(bp, 720, 1280, 62, 110, 0);
		bp -> info.name = MODEL_5IN_OLED;
		break;

	case 0x00000304:
		bp_create_info(bp, 480, 800, 56, 94, 0);
		bp -> info.name = MODEL_4IN3;
		break;

	case 0x00000004:
	case 0x00000b04:
	case 0x00000104:
		bp_create_info(bp, 480, 800, 53, 86, 0);
		bp -> info.name = MODEL_4IN;
		break;

	case 0x00000007:
		bp_create_info(bp, 800, 480, 89, 148, 0);
		bp -> info.name = MODEL_6IN8;
		break;

	case 0x00000403:
		bp_create_info(bp, 800, 800, 88, 88, 0);
		bp -> info.name = MODEL_3IN4;
		break;

	default:
		bp_create_info(bp, 480, 800, 0, 0, 0);
		bp -> info.name = MODEL_DEFAULT;
	}

	dev -> mode_config.funcs = &bp_mode_config_funcs;
	dev -> mode_config.min_width = bp -> info.width;
	dev -> mode_config.max_width = bp -> info.width;
	dev -> mode_config.min_height = bp -> info.height;
	dev -> mode_config.max_height = bp -> info.height;
}
