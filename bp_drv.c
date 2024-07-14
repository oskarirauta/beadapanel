/*
 * SPDX-License-Identifier: MIT
 * Copyright 2024 Oskari Rauta <oskari.rauta@gmail.com>
 */
#include <linux/module.h>
#include <linux/pm.h>

#include <drm/drm_gem.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_fbdev_generic.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_managed.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_print.h>

#include "bp_drv.h"

#define DRIVER_NAME		"BeadaPanel"
#define DRIVER_DESC		"NXElec's BeadaPanel screen"
#define DRIVER_DATE		"2024"
#define DRIVER_MAJOR		0
#define DRIVER_MINOR		1

// TODO: replace with beadapanel specific, this is for mpro
static char cmd_draw[12] = {
	0x00, 0x2c, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static int bp_data_alloc(struct bp_device *bp) {

	bp -> block_size = bp -> info.height * bp -> info.width * BP_BPP / 8;

	mpro -> data = drmm_kmalloc(&bp -> dev, bp -> block_size, GFP_KERNEL);
	if ( !bp -> data ) {
		bp -> block_size = 0;
		return -ENOMEM;
	}

	iosys_map_set_vaddr(&bp -> screen_base, bp -> data);

	// TODO:
	// prepare cmd and status_cmd by clearing and filling with static content (ascii string panel-link and status-link)

	// TODO: this is mpro specific..
	cmd_draw[2] = (char)(bp -> block_size >> 0);
	cmd_draw[3] = (char)(bp -> block_size >> 8);
	cmd_draw[4] = (char)(bp -> block_size >> 16);

	return 0;
}

static const struct drm_format_info *bp_get_validated_format(struct drm_device *dev, const char *format_name) {

	static const struct bp_format formats[] = BP_FORMATS;
	const struct bp_format *fmt = formats;
	const struct bp_format *end = fmt + ARRAY_SIZE(formats);
	const struct drm_format_info *info;

	if ( !format_name ) {
		drm_err(dev, "beadapanel: missing framebuffer format\n");
		return ERR_PTR(-EINVAL);
	}

	while ( fmt < end ) {
		if ( !strcmp(format_name, fmt -> name)) {
			info = drm_format_info(fmt -> fourcc);
			if ( !info )
				return ERR_PTR(-EINVAL);
			return info;
		}
		++fmt;
	}

	drm_err(dev, "beadapanel: unknown framebuffer format %s\n", format_name);
	return ERR_PTR(-EINVAL);
}

// TODO: convert for beadapanel, this is mpro
static int bp_update_frame(struct bp_device *bp) {

	struct usb_device *udev = bp_to_usb_device(bp);
	int cmd_len = 270;
	int ret;

	mutex_lock(&bp -> ctrl_lock);

	/* 0x40 USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE */
	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0xb0, 0x40,
			      0, bp -> ifnum, cmd_draw, cmd_len,
			      BP_MAX_DELAY);
	if ( ret < 0 )
		return ret;

	ret = usb_bulk_msg(udev, usb_sndbulkpipe(udev, 0x02), bp -> data,
			   bp -> block_size, NULL, BP_MAX_DELAY);

	mutex_unlock(&bp -> ctrl_lock);

	if ( ret < 0 )
		return ret;

	return 0;
}


static int bp_buf_copy(void *dst, struct iosys_map *src_map, struct drm_framebuffer *fb,
			 struct drm_rect *clip) {

	int ret;
	struct iosys_map dst_map;

	iosys_map_set_vaddr(&dst_map, dst);

	ret = drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE);
	if ( ret )
		return ret;

	drm_fb_xrgb8888_to_rgb565(&dst_map, NULL, src_map, fb, clip, false);
	drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);

	return 0;
}

void bp_fb_mark_dirty(struct iosys_map *src, struct drm_framebuffer *fb,
				struct drm_rect *rect) {

	struct bp_device *bp = to_bp(fb -> dev);
	struct drm_rect clip;
	int idx, ret;

	if ( !drm_dev_enter(fb -> dev, &idx))
		return;

	clip.x1 = 0;
	clip.x2 = fb -> width;
	clip.y1 = 0;
	clip.y2 = fb -> height;

	ret = bp_buf_copy(bp -> data, src, fb, &clip);
	if ( ret )
		goto err_msg;

	ret = bp_update_frame(bp);

err_msg:
	if (ret)
		dev_err_once(fb -> dev -> dev, "Failed to update display %d\n", ret);

	drm_dev_exit(idx);
}

static struct drm_gem_object *bp_gem_prime_import(struct drm_device *dev, struct dma_buf *dma_buf) {

	struct bp_device *bp = to_bp(dev);

	if ( !bp -> dmadev )
		return ERR_PTR(-ENODEV);

	return drm_gem_prime_import_dev(dev, dma_buf, bp -> dmadev);
}

DEFINE_DRM_GEM_FOPS(bp_fops);

static const struct drm_driver bp_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,

	.name			= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.date			= DRIVER_DATE,
	.major			= DRIVER_MAJOR,
	.minor			= DRIVER_MINOR,

	.fops			= &bp_fops,
	DRM_GEM_SHMEM_DRIVER_OPS,
	.gem_prime_import	= bp_gem_prime_import,
};

// TODO: fix for bp, this is mpro
static int bp_usb_probe(struct usb_interface *interface,
			  const struct usb_device_id *id) {

	struct bp_device *bp;
	struct drm_device *dev;
	int ret;

	bp = devm_drm_dev_alloc(&interface -> dev, &bp_drm_driver,
				      struct bp_device, dev);
	if ( IS_ERR(bp))
		return PTR_ERR(bp);

	bp -> ifnum = interface -> cur_altsetting -> desc.bInterfaceNumber;

	mutex_init(&bp -> ctrl_lock);
	mutex_init(&bp -> damage_lock);

	dev = &bp -> dev;
	dev_set_removable(dev -> dev, DEVICE_REMOVABLE);

	bp -> dmadev = usb_intf_get_dma_device(to_usb_interface(dev -> dev));
	if ( !bp -> dmadev )
		drm_warn(dev, "buffer sharing not supported"); /* not an error */

	ret = bp_get_screen(bp);
	if ( ret ) {
		drm_err(dev, "can't get screen info.\n");
		goto err_put_device;
	}

	ret = bp_get_version(bp);
	if ( ret ) {
		drm_err(dev, "can't get screen version.\n");
		goto err_put_device;
	}

	ret = bp_get_id(mpro);
	if ( ret ) {
		drm_err(dev, "can't get screen id.\n");
		goto err_put_device;
	}

	ret = drmm_mode_config_init(dev);
	if ( ret )
		goto err_put_device;

	bp_mode_config_setup(bp);

	// Forget this..
	//bp_edid_setup(bp);

	const struct drm_format_info *format = bp_get_validated_format(dev, "r5g6b5" /*"x8r8g8b8"*/);

	if ( IS_ERR(format)) {
		ret = -EINVAL;
                goto err_put_device;
		return -EINVAL;
	}

	bp -> format = format;

	unsigned int stride = drm_format_info_min_pitch(format, 0, bp -> info.width);
	if ( drm_WARN_ON(dev, !stride))
		return -EINVAL;

	drm_info(dev, "stride: %d", stride);

	ret = bp_data_alloc(bp);
	if ( ret )
		goto err_put_device;

	ret = bp_conn_init(bp);
	if ( ret )
		goto err_put_device;

	ret = bp_pipe_init(bp);
	if ( ret )
		goto err_put_device;

	drm_mode_config_reset(dev);

	usb_set_intfdata(interface, dev);

	ret = bp_sysfs_init(bp);
	if ( ret ) {
		drm_warn(dev, "failed to add sysfs entries");
	}

	ret = drm_dev_register(dev, 0);
	if ( ret )
		goto err_put_device;

	drm_kms_helper_poll_init(&bp -> dev);
	drm_fbdev_generic_setup(dev, 0);

	//bp_bl_init(bp);

	return 0;

err_put_device:
	put_device(bp -> dmadev);
	mutex_destroy(&bp -> damage_lock);
	mutex_destroy(&bp -> ctrl_lock);
	return ret;
}

static void bp_usb_disconnect(struct usb_interface *interface) {

	struct drm_device *dev = usb_get_intfdata(interface);
	struct bp_device *bp = to_bp(dev);

	bp_bl_deinit(bp);

	drm_kms_helper_poll_fini(dev);

	put_device(bp -> dmadev);
	bp -> dmadev = NULL;
	drm_dev_unplug(dev);
	drm_atomic_helper_shutdown(dev);

	mutex_destroy(&bp -> damage_lock);
	mutex_destroy(&bp -> ctrl_lock);
}

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x4e58, 0x1001) },
	{},
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver bp_usb_driver = {
	.name = "beadapanel",
	.probe = bp_usb_probe,
	.disconnect = bp_usb_disconnect,
#if IS_ENABLED(CONFIG_BACKLIGHT_CLASS_DEVICE) || IS_ENABLED(CONFIG_FB_BACKLIGHT)
	.suspend = bp_bl_suspend,
	.resume = bp_bl_resume,
	.reset_resume = bp_bl_resume,
#endif
	.id_table = id_table,
};

module_usb_driver(bp_usb_driver);
MODULE_DESCRIPTION("NXElec BeadaPanel driver");
MODULE_AUTHOR("Oskari Rauta <oskari.rauta@gmail.com>");
MODULE_LICENSE("Dual MIT/GPL");
