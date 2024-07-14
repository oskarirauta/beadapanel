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

// TODO: This file needs work...

// TODO: this is for mpro....
static char cmd_quit_sleep[6] = {
	0x00, 0x29, 0x00, 0x00, 0x00, 0x00
};
/*
static void bp_atomic_update(struct drm_plane *plane, struct drm_atomic_state *state) {

        struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(state, plane);
        struct drm_plane_state *old_plane_state = drm_atomic_get_old_plane_state(state, plane);
        struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
        struct drm_framebuffer *fb = plane_state -> fb;
        struct drm_device *dev = plane -> dev;
        struct bp_device *bp = to_bp(dev);
        struct drm_atomic_helper_damage_iter iter;
        struct drm_rect damage;
        int idx;

        if ( drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE))
                return;

        if ( !drm_dev_enter(dev, &idx))
                goto out_drm_gem_fb_end_cpu_access;

        mutex_lock(&bp -> damage_lock);

        drm_atomic_helper_damage_iter_init(&iter, old_plane_state, plane_state);
        drm_atomic_for_each_plane_damage(&iter, &damage) {

                struct drm_rect dst_clip = plane_state -> dst;
                struct iosys_map dst = bp -> screen_base;

                if ( !drm_rect_intersect(&dst_clip, &damage))
                        continue;

                iosys_map_incr(&dst, drm_fb_clip_offset(bp -> pitch, bp -> format, &dst_clip));

                drm_fb_xrgb8888_to_rgb565(&dst, &bp -> pitch, shadow_plane_state -> data, fb, &damage, false);
        }

        mpro_blit(bp, &bp -> info.rect);
        mutex_unlock(&bp -> damage_lock);

        drm_dev_exit(idx);

out_drm_gem_fb_end_cpu_access:
        drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);
}

static int bp_atomic_check(struct drm_plane *plane, struct drm_atomic_state *state) {

        struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(state, plane);
        struct drm_device *dev = plane -> dev;
        struct bp_device *bp = to_bp(dev);
        struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, &bp -> crtc);
        int ret;

        ret = drm_atomic_helper_check_plane_state(plane_state, crtc_state,
                                                  DRM_PLANE_NO_SCALING,
                                                  DRM_PLANE_NO_SCALING,
                                                  false, false);
        if ( ret )
                return ret;

        if ( !plane_state -> visible )
                return 0;

        return 0;
}
*/

static void bp_pipe_enable(struct drm_simple_display_pipe *pipe,
				 struct drm_crtc_state *crtc_state,
				 struct drm_plane_state *plane_state) {

	struct bp_device *bp = to_bp(pipe -> crtc.dev);
	struct usb_device *udev = bp_to_usb_device(bp);
	//struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	//struct drm_framebuffer *fb = plane_state -> fb;

	mutex_lock(&bp -> ctrl_lock);

	if ( usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0xb0, 0x40,
			     0, 0, cmd_quit_sleep, sizeof(cmd_quit_sleep),
			     BP_MAX_DELAY) < 0 ) drm_warn(&bp -> dev, "failed to send quit sleep command to display");

	mutex_unlock(&bp -> ctrl_lock);

	//bp_fb_mark_dirty(&shadow_plane_state -> data[0], fb, &bp -> info.rect);
}

static void bp_pipe_disable(struct drm_simple_display_pipe *pipe) {

	//struct bp_device *bp = to_bp(pipe -> crtc.dev);
	//struct drm_plane_state *plane_state = pipe -> plane.state;
	//struct drm_framebuffer *fb = plane_state -> fb;
	struct drm_device *dev = pipe -> crtc.dev;
	struct bp_device *bp = to_bp(dev);
	int idx;

	if ( !pipe -> crtc.state -> active || !drm_dev_enter(dev, &idx))
		return;

	mutex_lock(&bp -> damage_lock);

	// Clear screen to black on disable
	iosys_map_incr(&bp -> screen_base, drm_fb_clip_offset(bp -> pitch, bp -> format, &bp -> info.rect));
	iosys_map_memset(&bp -> screen_base, 0, 0, bp -> block_size);
	bp_blit(bp, &bp -> info.rect);

	mutex_unlock(&bp -> damage_lock);
	drm_dev_exit(idx);
}

static void bp_pipe_update(struct drm_simple_display_pipe *pipe,
				 struct drm_plane_state *old_state) {

	struct drm_plane_state *plane_state = pipe -> plane.state;
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state -> fb;
	struct drm_device *dev = pipe -> crtc.dev;
	struct bp_device *bp = to_bp(dev);
	struct drm_atomic_helper_damage_iter iter;
	struct drm_rect damage;
	int idx;

	if ( !pipe -> crtc.state -> active || !drm_dev_enter(dev, &idx))
		return;

	if ( drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE)) {
		drm_dev_exit(idx);
		return;
	}

	mutex_lock(&bp -> damage_lock);

	drm_atomic_helper_damage_iter_init(&iter, old_state, plane_state);
	drm_atomic_for_each_plane_damage(&iter, &damage) {

		struct drm_rect dst_clip = plane_state -> dst;
		struct iosys_map dst = bp -> screen_base;

		if ( !pipe -> crtc.state -> active )
			break;

		if ( !drm_rect_intersect(&dst_clip, &damage))
			continue;

		iosys_map_incr(&dst, drm_fb_clip_offset(bp -> pitch, bp -> format, &dst_clip));
		drm_fb_xrgb8888_to_rgb565(&dst, &bp -> pitch, shadow_plane_state -> data, fb, &damage, false);
	}

	bp_blit(bp, &bp -> info.rect);
	mutex_unlock(&bp -> damage_lock);

	drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);
	drm_dev_exit(idx);
}

static const struct drm_simple_display_pipe_funcs bp_pipe_funcs = {
	.enable		= bp_pipe_enable,
	.disable	= bp_pipe_disable,
	.update		= bp_pipe_update,
	DRM_GEM_SIMPLE_DISPLAY_PIPE_SHADOW_PLANE_FUNCS,
};

static const uint32_t bp_pipe_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const uint64_t bp_pipe_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};


static int drm_simple_kms_plane_prepare_fb(struct drm_plane *plane,
					   struct drm_plane_state *state) {

/*
	struct drm_simple_display_pipe *pipe = container_of(plane, struct drm_simple_display_pipe, plane);

	if ( !pipe -> funcs || !pipe -> funcs -> prepare_fb) {
*/
		if (WARN_ON_ONCE(!drm_core_check_feature(plane -> dev, DRIVER_GEM)))
			return 0;

//		WARN_ON_ONCE(pipe -> funcs && pipe -> funcs -> cleanup_fb);

		return drm_gem_plane_helper_prepare_fb(plane, state);
/*
	}

	return pipe -> funcs -> prepare_fb(pipe, state);
*/
}

static void drm_simple_kms_plane_cleanup_fb(struct drm_plane *plane,
					    struct drm_plane_state *state) {}

static int drm_simple_kms_plane_begin_fb_access(struct drm_plane *plane,
						struct drm_plane_state *new_plane_state) {

	struct drm_simple_display_pipe *pipe;
	pipe = container_of(plane, struct drm_simple_display_pipe, plane);

	drm_gem_simple_kms_begin_shadow_fb_access(pipe, new_plane_state);
	return 0;
}

static void drm_simple_kms_plane_end_fb_access(struct drm_plane *plane,
					       struct drm_plane_state *new_plane_state) {

	struct drm_simple_display_pipe *pipe;
	pipe = container_of(plane, struct drm_simple_display_pipe, plane);

	drm_gem_simple_kms_end_shadow_fb_access(pipe, new_plane_state);
}

static int drm_simple_kms_plane_atomic_check(struct drm_plane *plane,
					struct drm_atomic_state *state) {

	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct drm_simple_display_pipe *pipe;
	struct drm_crtc_state *crtc_state;
	int ret;

	pipe = container_of(plane, struct drm_simple_display_pipe, plane);
	crtc_state = drm_atomic_get_new_crtc_state(state, &pipe -> crtc);

	ret = drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  false, false);
	if (ret)
		return ret;

	if ( !plane_state -> visible )
		return 0;

	return 0;
//	return bp_pipe_update(plane, state);
/*
	if ( !pipe -> funcs || !pipe -> funcs -> check )
		return 0;

	return pipe -> funcs -> check(pipe, plane_state, crtc_state);
*/
}

static void drm_simple_kms_plane_atomic_update(struct drm_plane *plane,
					struct drm_atomic_state *state) {

	struct drm_plane_state *old_pstate = drm_atomic_get_old_plane_state(state, plane);
	struct drm_simple_display_pipe *pipe;

	pipe = container_of(plane, struct drm_simple_display_pipe, plane);
/*
	if ( !pipe -> funcs || !pipe -> funcs -> update)
		return;

	pipe -> funcs -> update(pipe, old_pstate);
*/
	bp_pipe_update(pipe, old_pstate);
}

static const struct drm_plane_helper_funcs drm_simple_kms_plane_helper_funcs = {
	.prepare_fb = drm_simple_kms_plane_prepare_fb,
	.cleanup_fb = drm_simple_kms_plane_cleanup_fb,
	.begin_fb_access = drm_simple_kms_plane_begin_fb_access,
	.end_fb_access = drm_simple_kms_plane_end_fb_access,
	.atomic_check = drm_simple_kms_plane_atomic_check,
	.atomic_update = drm_simple_kms_plane_atomic_update,
};

static void drm_simple_kms_plane_reset(struct drm_plane *plane) {

	struct drm_simple_display_pipe *pipe;
	pipe = container_of(plane, struct drm_simple_display_pipe, plane);
	return drm_gem_simple_kms_reset_shadow_plane(pipe);

	//return drm_atomic_helper_plane_reset(plane);
}

static struct drm_plane_state *drm_simple_kms_plane_duplicate_state(struct drm_plane *plane) {

	struct drm_simple_display_pipe *pipe;
	pipe = container_of(plane, struct drm_simple_display_pipe, plane);
	return drm_gem_simple_kms_duplicate_shadow_plane_state(pipe);

	//return drm_atomic_helper_plane_duplicate_state(plane);
}

static void drm_simple_kms_plane_destroy_state(struct drm_plane *plane, struct drm_plane_state *state) {

	struct drm_simple_display_pipe *pipe;
	pipe = container_of(plane, struct drm_simple_display_pipe, plane);
	drm_gem_simple_kms_destroy_shadow_plane_state(pipe, state);

	//drm_atomic_helper_plane_destroy_state(plane, state);
}

static bool drm_simple_kms_format_mod_supported(struct drm_plane *plane, uint32_t format, uint64_t modifier) {

	return modifier == DRM_FORMAT_MOD_LINEAR;
}

static const struct drm_plane_funcs drm_simple_kms_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_simple_kms_plane_reset,
	.atomic_duplicate_state	= drm_simple_kms_plane_duplicate_state,
	.atomic_destroy_state	= drm_simple_kms_plane_destroy_state,
	.format_mod_supported   = drm_simple_kms_format_mod_supported,
};

//

static enum drm_mode_status
drm_simple_kms_crtc_mode_valid(struct drm_crtc *crtc, const struct drm_display_mode *mode) {

	return MODE_OK;
}

static int drm_simple_kms_crtc_check(struct drm_crtc *crtc, struct drm_atomic_state *state) {

	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	int ret;

	if ( !crtc_state -> enable )
		goto out;

	ret = drm_atomic_helper_check_crtc_primary_plane(crtc_state);
	if ( ret )
		return ret;

out:
	return drm_atomic_add_affected_planes(state, crtc);
}

static void drm_simple_kms_crtc_enable(struct drm_crtc *crtc, struct drm_atomic_state *state) {

	struct drm_plane *plane;
	struct drm_simple_display_pipe *pipe;

	pipe = container_of(crtc, struct drm_simple_display_pipe, crtc);
	plane = &pipe -> plane;
	mpro_pipe_enable(pipe, crtc -> state, plane -> state);
}

static void drm_simple_kms_crtc_disable(struct drm_crtc *crtc, struct drm_atomic_state *state) {

	struct drm_simple_display_pipe *pipe;
	pipe = container_of(crtc, struct drm_simple_display_pipe, crtc);
	mpro_pipe_disable(pipe);
}

static const struct drm_crtc_helper_funcs drm_simple_kms_crtc_helper_funcs = {
	.mode_valid = drm_simple_kms_crtc_mode_valid,
	.atomic_check = drm_simple_kms_crtc_check,
	.atomic_enable = drm_simple_kms_crtc_enable,
	.atomic_disable = drm_simple_kms_crtc_disable,
};

static void drm_simple_kms_crtc_reset(struct drm_crtc *crtc) {

	return drm_atomic_helper_crtc_reset(crtc);
}

static struct drm_crtc_state *drm_simple_kms_crtc_duplicate_state(struct drm_crtc *crtc) {

	return drm_atomic_helper_crtc_duplicate_state(crtc);
}

static void drm_simple_kms_crtc_destroy_state(struct drm_crtc *crtc, struct drm_crtc_state *state) {

	drm_atomic_helper_crtc_destroy_state(crtc, state);
}

static int drm_simple_kms_crtc_enable_vblank(struct drm_crtc *crtc) {

	return 0;
}

static void drm_simple_kms_crtc_disable_vblank(struct drm_crtc *crtc)
{}

static const struct drm_crtc_funcs drm_simple_kms_crtc_funcs = {
	.reset = drm_simple_kms_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_simple_kms_crtc_duplicate_state,
	.atomic_destroy_state = drm_simple_kms_crtc_destroy_state,
	.enable_vblank = drm_simple_kms_crtc_enable_vblank,
	.disable_vblank = drm_simple_kms_crtc_disable_vblank,
};

static const struct drm_encoder_funcs drm_simple_encoder_funcs_cleanup = {
	.destroy = drm_encoder_cleanup,
};

int bp_pipe_init(struct bp_device *bp) {

	int ret;

	struct drm_simple_display_pipe *pipe = &bp -> pipe;
	struct drm_encoder *encoder = &pipe -> encoder;
	struct drm_plane *plane = &pipe -> plane; //&bp -> plane
	struct drm_crtc *crtc = &pipe -> crtc; //&bp -> crtc
	struct drm_device *dev = &bp -> dev;
	struct drm_connector *connector = &bp -> conn;

	// int ret;
	// bp -> connector = connector;
	// pipe -> funcs = funcs;

	pipe -> connector = connector;

	drm_plane_helper_add(plane, &drm_simple_kms_plane_helper_funcs);
	ret = drm_universal_plane_init(dev, plane, 0,
				       &drm_simple_kms_plane_funcs,
				       bp_pipe_formats, ARRAY_SIZE(bp_pipe_formats),
				       bp_pipe_modifiers,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if ( ret )
		return ret;

	drm_plane_enable_fb_damage_clips(plane);

	drm_crtc_helper_add(crtc, &drm_simple_kms_crtc_helper_funcs);
	ret = drm_crtc_init_with_planes(dev, crtc, plane, NULL,
					&drm_simple_kms_crtc_funcs, NULL);
	if ( ret )
		return ret;

	encoder -> possible_crtcs = drm_crtc_mask(crtc);
	ret = drm_encoder_init(dev, encoder, &drm_simple_encoder_funcs_cleanup, DRM_MODE_ENCODER_NONE, NULL);
//	ret = drm_simple_encoder_init(dev, encoder, DRM_MODE_ENCODER_NONE);
	if ( ret || !connector )
		return ret;

	return drm_connector_attach_encoder(connector, encoder);

	return 0;

/*
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
