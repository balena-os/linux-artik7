/*
 * Copyright (C) 2016  Nexell Co., Ltd.
 * Author: junghyun, kim <jhkim@nexell.co.kr>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <linux/of_address.h>

#include "nx_drm_drv.h"
#include "nx_drm_crtc.h"
#include "nx_drm_encoder.h"
#include "nx_drm_connector.h"
#include "soc/s5pxx18_drm_dp.h"

#define	nx_drm_dp_encoder_set_pipe(d, p)	{	\
		struct dp_control_dev *dpc = drm_dev_get_dpc(d);	\
		dpc->module = p;	\
	}

static void nx_drm_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct nx_drm_device *display = to_nx_encoder(encoder)->display;
	struct nx_drm_panel *panel = &display->panel;
	struct nx_drm_ops *ops = display->ops;

	DRM_DEBUG_KMS("enter [ENCODER:%d] %s dpms:%d, %s\n",
		encoder->base.id,
		dp_panel_type_name(dp_panel_get_type(display)),
		mode, panel->is_connected ? "connected" : "disconnected");

	if (!panel->is_connected)
		return;

	if (to_nx_encoder(encoder)->dpms == mode) {
		DRM_DEBUG_KMS("desired dpms mode is same as previous one.\n");
		return;
	}

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		nx_drm_dp_encoder_dpms(encoder, true);
		if (ops && ops->dpms)
			ops->dpms(display->dev, mode);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		if (ops && ops->dpms)
				ops->dpms(display->dev, mode);
		nx_drm_dp_encoder_dpms(encoder, false);
		break;

	default:
		DRM_ERROR("fail : unspecified mode %d\n", mode);
		break;
	}

	to_nx_encoder(encoder)->dpms = mode;
	DRM_DEBUG_KMS("done\n");
}

static bool nx_drm_encoder_mode_fixup(struct drm_encoder *encoder,
			const struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	struct drm_connector *connector;
	struct drm_device *drm = encoder->dev;
	struct nx_drm_encoder *nx_encoder = to_nx_encoder(encoder);
	struct nx_drm_crtc *nx_crtc = to_nx_crtc(encoder->crtc);
	struct nx_drm_device *display = to_nx_encoder(encoder)->display;
	struct nx_drm_ops *ops = display->ops;
	int pipe = nx_crtc->pipe;

	DRM_DEBUG_KMS("enter, encoder id:%d crtc pipe.%d\n",
		encoder->base.id, pipe);

	/*
	 * set display controllor pipe.
	 */
	nx_encoder->pipe = pipe;
	nx_drm_dp_encoder_set_pipe(display, pipe);
	nx_drm_dp_encoder_prepare(encoder, pipe, true);

	list_for_each_entry(connector, &drm->mode_config.connector_list, head) {
		if (connector->encoder == encoder) {
			if (ops && ops->mode_fixup)
				return ops->mode_fixup(display->dev,
						connector, mode, adjusted_mode);
		}
	}

	return true;
}

static void nx_drm_encoder_mode_set(struct drm_encoder *encoder,
			struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode)
{
	struct drm_device *drm = encoder->dev;
	struct drm_connector *connector;
	struct nx_drm_device *display = to_nx_encoder(encoder)->display;
	struct nx_drm_ops *ops = display->ops;

	DRM_DEBUG_KMS("enter\n");

	list_for_each_entry(connector, &drm->mode_config.connector_list, head) {
		if (connector->encoder == encoder) {
			if (ops && ops->mode_set)
				ops->mode_set(display->dev, adjusted_mode);
		}
	}

	nx_drm_dp_display_mode_to_sync(adjusted_mode, display);
}

static void nx_drm_encoder_prepare(struct drm_encoder *encoder)
{
	DRM_DEBUG_KMS("enter\n");
}

static void nx_drm_encoder_commit(struct drm_encoder *encoder)
{
	struct nx_drm_device *display = to_nx_encoder(encoder)->display;
	struct nx_drm_ops *ops = display->ops;
	struct nx_drm_panel *panel = &display->panel;

	DRM_DEBUG_KMS("enter\n");

	if (!panel->is_connected)
		return;

	if (ops && ops->commit)
		ops->commit(display->dev);

	nx_drm_dp_encoder_commit(encoder);

	/* display output device */
	if (ops->dpms)
		ops->dpms(display->dev, DRM_MODE_DPMS_ON);
}

static struct drm_encoder_helper_funcs nx_encoder_helper_funcs = {
	.dpms = nx_drm_encoder_dpms,
	.mode_fixup = nx_drm_encoder_mode_fixup,
	.mode_set = nx_drm_encoder_mode_set,
	.prepare = nx_drm_encoder_prepare,
	.commit = nx_drm_encoder_commit,
};

static void nx_drm_encoder_destroy(struct drm_encoder *encoder)
{
	struct drm_device *drm = encoder->dev;
	struct nx_drm_encoder *nx_encoder = to_nx_encoder(encoder);

	nx_drm_dp_encoder_unprepare(encoder);

	drm_encoder_cleanup(encoder);
	devm_kfree(drm->dev, nx_encoder);
}

static struct drm_encoder_funcs nx_encoder_funcs = {
	.destroy = nx_drm_encoder_destroy,
};

struct drm_encoder *nx_drm_encoder_create(struct drm_device *drm,
			struct nx_drm_device *display, int type,
			int pipe, int possible_crtcs, void *context)
{
	struct nx_drm_encoder *nx_encoder;
	struct drm_encoder *encoder;

	DRM_DEBUG_KMS("enter possible crtcs:0x%x\n",
		possible_crtcs);

	BUG_ON(!display || 0 == possible_crtcs);

	nx_encoder =
		devm_kzalloc(drm->dev, sizeof(*nx_encoder), GFP_KERNEL);
	if (!nx_encoder)
		return ERR_PTR(-ENOMEM);

	nx_encoder->dpms = DRM_MODE_DPMS_OFF;
	nx_encoder->pipe = pipe;
	nx_encoder->display = display;
	nx_encoder->context = context;

	encoder = &nx_encoder->encoder;
	encoder->possible_crtcs = possible_crtcs;

	drm_encoder_init(drm, encoder, &nx_encoder_funcs, type);
	drm_encoder_helper_add(encoder, &nx_encoder_helper_funcs);

	DRM_DEBUG_KMS("exit, encoder id:%d\n", encoder->base.id);

	return encoder;
}
EXPORT_SYMBOL(nx_drm_encoder_create);

