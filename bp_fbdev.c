/*
 * SPDX-License-Identifier: MIT
 * Copyright 2024 Oskari Rauta <oskari.rauta@gmail.com>
 */
#include <drm/drm_fbdev_generic.h>
#include <drm/drm_print.h>
#include <drm/drm_rect.h>
#include <linux/usb.h>

#include "bp_drv.h"

// TODO: this is for mpro, fix for beadapanel
static char cmd_draw[12] = {
	0x00, 0x2c, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// TODO: this is for mpro, fix for beadapanel
int bp_blit(struct bp_device *bp, struct drm_rect* rect) {

	int cmd_len = 6; // 270

	cmd_draw[2] = (char)(mpro -> block_size >> 0);
	cmd_draw[3] = (char)(mpro -> block_size >> 8);
	cmd_draw[4] = (char)(mpro -> block_size >> 16);

	struct usb_device *udev = bp_to_usb_device(bp);
	int ret;

	mutex_lock(&bp -> ctrl_lock);

	/* 0x40 USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE */
	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0xb0, 0x40,
			      0, 0, cmd_draw, cmd_len,
			      BP_MAX_DELAY);
	if ( ret < 0 ) {
		mutex_unlock(&bp -> ctrl_lock);
		return ret;
	}

	ret = usb_bulk_msg(udev, usb_sndbulkpipe(udev, 0x02), bp -> data,
			   bp -> block_size, NULL, BP_MAX_DELAY);

	mutex_unlock(&bp -> ctrl_lock);

	return ret < 0 ? ret : 0;
}
/*
int bp_fbdev_setup(struct bp_device *bp, unsigned int preferred_bpp) {

	cmd_draw[2] = (char)(bp -> block_size >> 0);
	cmd_draw[3] = (char)(bp -> block_size >> 8);
	cmd_draw[4] = (char)(bp -> block_size >> 16);

	int ret = drm_dev_register(&bp -> dev, 0);
	if ( ret )
		return ret;

	drm_fbdev_generic_setup(&bp -> dev, preferred_bpp);
	return 0;
}
*/
