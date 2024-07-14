/*
 * SPDX-License-Identifier: MIT
 * Copyright 2024 Oskari Rauta <oskari.rauta@gmail.com>
 */
#include <linux/usb.h>
#include <drm/drm_atomic.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_print.h>

#include "bp_drv.h"

// TODO: fix for beadapanel, this is mpro
static char cmd_quit_sleep[6] = {
	0x00, 0x29, 0x00, 0x00, 0x00, 0x00
};

static enum drm_mode_status bp_mode_valid(struct drm_crtc *crtc, const struct drm_display_mode *mode) {

	return MODE_OK;
}

static int bp_check(struct drm_crtc *crtc, struct drm_atomic_state *state) {

	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	int ret;

	if ( !crtc_state -> enable)
		return drm_atomic_add_affected_planes(state, crtc);

	ret = drm_atomic_helper_check_crtc_primary_plane(crtc_state);
	if ( ret )
		return ret;

	return drm_atomic_add_affected_planes(state, crtc);
}

static void bp_enable(struct drm_crtc *crtc, struct drm_atomic_state *state) {

	struct drm_device *dev = crtc -> dev;
	struct bp_device *bp = to_bp(dev);
	struct usb_device *udev = drm_to_usb_device(dev);
	int ret;

	mutex_lock(&bp -> ctrl_lock);

	// TODO: fix for beadapanel, this is mpro
	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0xb0, 0x40,
			      0, 0, cmd_quit_sleep, sizeof(cmd_quit_sleep),
			      BP_MAX_DELAY);

	mutex_unlock(&bp -> ctrl_lock);

	if ( ret < 0 )
		drm_warn(&bp -> dev, "failed to send quit sleep command to display");
}

static void bp_disable(struct drm_crtc *crtc, struct drm_atomic_state *state) {

	struct drm_device *dev = crtc -> dev;
	struct bp_device *bp = to_bp(dev);

	if ( !crtc.state -> active || !drm_dev_enter(dev, &idx))
		return;

	// Clear screen to black on disable
	mutex_lock(&bp -> damage_lock);

	//iosys_map_incr(&bp -> screen_base, drm_fb_clip_offset(bp -> pitch, bp -> format, &bp -> info.rect));
	iosys_map_memset(&bp -> screen_base, 0, 0, bp -> block_size);
	bp_blit(bp, &bp -> info.rect);

	mutex_unlock(&bp -> damage_lock);
	drm_dev_exit(idx);
}

static const struct drm_crtc_helper_funcs bp_crtc_helper_funcs = {
	.mode_valid = bp_mode_valid,
	.atomic_check = bp_check,
	.atomic_enable = bp_enable,
	.atomic_disable = bp_disable,
};

static int bp_crtc_enable_vblank(struct drm_crtc *crtc) {

	return 0;
}

static void bp_crtc_disable_vblank(struct drm_crtc *crtc) {
}

static const struct drm_crtc_funcs drm_simple_kms_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = bp_crtc_enable_vblank,
	.disable_vblank = bp_crtc_disable_vblank,
};

static const struct drm_encoder_funcs drm_simple_encoder_funcs_cleanup = {
	.destroy = drm_encoder_cleanup,
};

int bp_pipe_init(struct bp_device *bp) {

	int ret;

	struct drm_simple_display_pipe *pipe = &bp -> pipe;
	struct drm_encoder *encoder = &pipe -> encoder;
	struct drm_plane *plane = &pipe -> plane; //&bp->plane?
	struct drm_crtc *crtc = &pipe -> crtc; //&bp->crtc?
	struct drm_device *dev = &bp -> dev;
	struct drm_connector *connector = &bp -> conn;
	//int ret;

	//bp -> connector = connector;
	//pipe->funcs = funcs;

	pipe -> connector = connector;

	drm_plane_helper_add(plane, &drm_simple_kms_plane_helper_funcs);
	ret = drm_universal_plane_init(dev, plane, 0,
				       &drm_simple_kms_plane_funcs,
				       bp_pipe_formats, ARRAY_SIZE(bp_pipe_formats),
				       bp_pipe_modifiers,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;

	drm_plane_enable_fb_damage_clips(plane);

	drm_crtc_helper_add(crtc, &drm_simple_kms_crtc_helper_funcs);
	ret = drm_crtc_init_with_planes(dev, crtc, plane, NULL,
					&drm_simple_kms_crtc_funcs, NULL);
	if (ret)
		return ret;

	encoder -> possible_crtcs = drm_crtc_mask(crtc);
	ret = drm_encoder_init(dev, encoder, &drm_simple_encoder_funcs_cleanup, DRM_MODE_ENCODER_NONE, NULL);
//	ret = drm_simple_encoder_init(dev, encoder, DRM_MODE_ENCODER_NONE);
	if (ret || !connector)
		return ret;

	return drm_connector_attach_encoder(connector, encoder);
/*
	return 0;


	ret = drm_simple_display_pipe_init(&bp -> dev, &bp -> pipe,
					   &bp_pipe_funcs, bp_pipe_formats,
					   ARRAY_SIZE(bp_pipe_formats),
					   bp_pipe_modifiers, &bp -> conn);

	if ( ret )
		return ret;

	drm_plane_enable_fb_damage_clips(&bp -> pipe.plane);

	return 0;
*/
}
