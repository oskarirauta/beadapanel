/*
 * SPDX-License-Identifier: MIT
 * Copyright 2024 Oskari Rauta <oskari.rauta@gmail.com>
 */
#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_probe_helper.h>

#include "bp_drv.h"

static int bp_connector_get_modes(struct drm_connector *connector) {

	struct bp_device *bp = to_bp(connector -> dev);
	return drm_connector_helper_get_modes_fixed(connector, &bp -> mode);
}

static const struct drm_connector_helper_funcs bp_conn_helper_funcs = {
	.get_modes = bp_connector_get_modes,
};

static const struct drm_connector_funcs bp_conn_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

int bp_connector_init(struct bp_device *mpro) {

	drm_connector_helper_add(&bp -> conn, &bp_conn_helper_funcs);

	return drm_connector_init(&bp -> dev, &bp -> conn,
				  &bp_conn_funcs, DRM_MODE_CONNECTOR_USB);
}
