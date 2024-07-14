/*
 * SPDX-License-Identifier: MIT
 * Copyright 2024 Oskari Rauta <oskari.rauta@gmail.com>
 */
#ifndef _BEADAPANEL_H_
#define _BEADAPANEL_H_

#include <drm/drm_connector.h>
#include <drm/drm_drv.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_rect.h>

#include <linux/usb.h>
#include <linux/input.h>
#include <linux/usb/input.h>
#include <linux/iosys-map.h>
#include <linux/mutex.h>

#if IS_ENABLED(CONFIG_BACKLIGHT_CLASS_DEVICE) || IS_ENABLED(CONFIG_FB_BACKLIGHT)
#include <linux/backlight.h>
#endif

#define BP_BPP		16
#define BP_MAX_DELAY		100

#define BP_FORMATS \
{ \
	{ "r5g6b5", 16, {11, 5}, {5, 6}, {0, 5}, {0, 0}, DRM_FORMAT_RGB565 }, \
}

struct bp_format {
	const char *name;
	u32 bits_per_pixel;
	struct fb_bitfield red;
	struct fb_bitfield green;
	struct fb_bitfield blue;
	struct fb_bitfield transp;
	u32 fourcc;
};

struct bp_device {

	struct device			*dmadev;
	struct drm_device		dev;
/*
	struct drm_framebuffer		*fb;
	struct drm_encoder		encoder;
	struct drm_crtc			crtc;
	struct drm_plane		plane;
*/
	u8				ifnum;

	struct drm_simple_display_pipe	pipe;
	struct drm_connector		conn;
	const struct drm_format_info	*format;
	unsigned int			pitch;
	struct drm_display_mode		mode;

	unsigned char			status_cmd[20];
	unsigned char			cmd[270];

	struct iosys_map		screen_base;
	unsigned char			*data;
	unsigned int			block_size;

	struct mutex ctrl_lock;
	struct mutex damage_lock;

	struct {
		unsigned char	fw;
		unsigned char	panellink;
		unsigned char	statuslink;
	} version;

	struct {

		unsigned char		platform;
		unsigned char		model;
		unsigned char		*name;
		unsigned char		*serial;
		unsigned long		storage;

		unsigned int		width;
		unsigned int		height;
		unsigned int		width_mm;
		unsigned int		height_mm;
		unsigned int		hz;
		struct drm_rect		rect;
	} info;

#if IS_ENABLED(CONFIG_BACKLIGHT_CLASS_DEVICE) || IS_ENABLED(CONFIG_FB_BACKLIGHT)
	struct {
		struct backlight_device *dev;
		int			power;
		int			max;
		ubt			cur;
	} bl;
#endif
};

static inline struct bp_device *to_bp(struct drm_device *dev) {
	return container_of(dev, struct bp_device, dev);
}

static inline struct usb_device *bp_to_usb_device(struct bp_device *bp) {
	return interface_to_usbdev(to_usb_interface(bp -> dev.dev));
}

static inline struct usb_device *drm_to_usb_device(struct drm_device *dev) {
	return bp_to_usb_device(to_bp(dev));
}

int bp_blit(struct bp_device *bp, struct drm_rect* rect);

#if IS_ENABLED(CONFIG_BACKLIGHT_CLASS_DEVICE) || IS_ENABLED(CONFIG_FB_BACKLIGHT)
int bp_bl_suspend(struct usb_interface *interface, pm_message_t message);
int bp_bl_resume(struct usb_interface *interface);
#endif

int bp_connector_init(struct bp_device *bp);
int bp_pipe_init(struct bp_device *bp);

int bp_get_screen(struct bp_device *bp);
int bp_get_version(struct bp_device *bp);
int bp_get_id(struct bp_device *bp);
void bp_mode_config_setup(struct bp_device *bp);

void bp_bl_init(struct bp_device* bp);
void bp_bl_deinit(struct bp_device *bp);

int bp_sysfs_init(struct bp_device* bp);

void bp_fb_mark_dirty(struct iosys_map *src,
			struct drm_framebuffer *fb,
			struct drm_rect *rect);

#endif
