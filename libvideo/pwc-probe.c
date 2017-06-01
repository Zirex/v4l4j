/*
* Copyright (C) 2007-2008 Gilles Gigan (gilles.gigan@gmail.com)
* eResearch Centre, James Cook University (eresearch.jcu.edu.au)
*
* This program was developed as part of the ARCHER project
* (Australian Research Enabling Environment) funded by a
* Systemic Infrastructure Initiative (SII) grant and supported by the Australian
* Department of Innovation, Industry, Science and Research
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public  License as published by the
* Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
* or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*/

#include <sys/ioctl.h>		//for ioctl
#include <string.h>
#include <stdlib.h>	//required for videodev2.h
#include "videodev2.h"

#include "videodev_additions.h"
#include "pwc-probe.h"
#include "libvideo-err.h"
/*
 * need pwc-ioctl.h (tested with pwc 20080311-042701)
 */
#include "pwc-ioctl.h"
#include "log.h"

#define NB_PRIV_IOCTL 3

struct pwc_probe_private {
	int isPTZ;
};


int pwc_driver_probe(struct video_device *vdev, void **data) {
	struct v4l2_capability cap;
	struct pwc_probe p;
	struct pwc_probe_private *priv;
	CLEAR(cap);
	
	dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_DEBUG, "PWC: probing PWC ...\n");
	if(ioctl(vdev->fd, VIDIOCPWCPROBE, &p) >= 0) {
		if (ioctl(vdev->fd, VIDIOC_QUERYCAP, &cap) == -1)
			goto end;

		dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_DEBUG, "PWC: card name: %s, ioctl returned: %s\n", cap.card, p.name);
		if(strncmp((char *)cap.card, p.name, sizeof(cap.card)) != 0)
			goto end;

		dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_DEBUG, "PWC: found pwc driver\n");
		XMALLOC(priv, struct pwc_probe_private *, sizeof(struct pwc_probe_private));
		*data = (void *) priv;
		int i = 3;
		if(ioctl(vdev->fd, VIDIOCPWCMPTRESET, &i) >= 0) {
			dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_DEBUG, "PWC: found PTZ-capable camera (%d controls)\n", NB_PRIV_IOCTL);
			priv->isPTZ = 1;
			return NB_PRIV_IOCTL;
		} else {
			dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_DEBUG, "PWC: no PTZ camera found\n");
			priv->isPTZ = 0;
			return 0;
		}
	}

end:
	dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_DEBUG, "PWC: pwc driver NOT found\n");
	return -1;
}

int pwc_get_ctrl(struct video_device *vdev, struct v4l2_queryctrl *qc, void *data, int *val) {
	UNUSED(data);
	
	struct pwc_mpt_angles angles;
	switch(qc->id) {
		case 0:
			//Pan/tilt reset
			dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_DEBUG, "PWC: Invoked get on Pan/Tilt reset button \n");
			break;
		case 1:
			//Pan control
			dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_DEBUG, "PWC: Invoked get on Pan\n");
			if(ioctl(vdev->fd, VIDIOCPWCMPTGANGLE, &angles) == 0) {
				*val = angles.pan;
				return LIBVIDEO_ERR_SUCCESS;
			}
			dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_ERR, "PWC: Error probing pan angle\n");
			break;
		case 2:
			//tilt control
			dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_DEBUG, "PWC: Invoked get on Tilt\n");
			if(ioctl(vdev->fd, VIDIOCPWCMPTGANGLE, &angles) == 0) {
				*val = angles.tilt;
				return LIBVIDEO_ERR_SUCCESS;
			}
			dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_ERR, "PWC: Error probing tilt angle\n");
			break;
		default:
			dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_ERR, "PWC: Cant identify control %d\n", qc->id);
			break;
	}
	return LIBVIDEO_ERR_IOCTL;
}

int pwc_set_ctrl(struct video_device *vdev, struct v4l2_queryctrl *qc, int *val, void *d) {
	UNUSED(d);
	struct pwc_mpt_angles angles;
	int prev = 0;

	switch(qc->id) {
		case 0:
			//Pan/tilt reset
			dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_DEBUG, "PWC: Invoked set on Pan/Tilt reset\n");
			*val = 0;
			int i = 3;
			if(ioctl(vdev->fd, VIDIOCPWCMPTRESET, &i) != 0) {
				dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_ERR, "PWC: Error resetting pan/tilt\n");
				return LIBVIDEO_ERR_IOCTL;
			}
			break;
		case 1:
			//Pan control
			dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_DEBUG, "PWC: Invoked set on pan\n");

			//finds the previous values for pan and tilt
			if(ioctl(vdev->fd, VIDIOCPWCMPTGANGLE, &angles) != 0)
				dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_ERR, "PWC: Error probing pan/tilt angle\n");
			else
				prev = angles.pan;
			
			angles.pan = *val;
			angles.absolute = 1;
			
			if(ioctl(vdev->fd, VIDIOCPWCMPTSANGLE, &angles) != 0) {
				dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_ERR, "PWC: Error setting pan angle\n");
				*val = prev;
				return LIBVIDEO_ERR_IOCTL;
			}
			break;
		case 2:
			//tilt control
			dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_DEBUG, "PWC: Invoked set on tiltn\n");

			//finds the previous values for pan and tilt
			if(ioctl(vdev->fd, VIDIOCPWCMPTGANGLE, &angles) != 0)
				dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_ERR, "PWC: Error probing pan/tilt angle\n");
			else
				prev = angles.tilt;

			angles.tilt = *val;
			angles.absolute = 1;

			if(ioctl(vdev->fd, VIDIOCPWCMPTSANGLE, &angles) != 0) {
				dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_ERR, "PWC: Error setting tilt angle\n");
				*val = prev;
				return LIBVIDEO_ERR_IOCTL;
			}
			break;
		default:
			dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_ERR, "PWC: Cant identify control %d\n", qc->id);
			return LIBVIDEO_ERR_OUT_OF_RANGE;
	}
	return LIBVIDEO_ERR_SUCCESS;
}

int pwc_list_ctrl(struct video_device *vdev, struct control *c, void *data) {
	unsigned int i = 0;
	struct pwc_probe_private *priv = (struct pwc_probe_private *) data;
	if(priv->isPTZ == 1) {
		//Pan/tilt reset
		dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_DEBUG, "PWC: Found pwc private ioctl Pan/Tilt reset\n");
		c[i].v4l2_ctrl->id = i;
		c[i].v4l2_ctrl->type = V4L2_CTRL_TYPE_BUTTON;
		strcpy((char *) c[i].v4l2_ctrl->name,"Pan/Tilt reset");
		c[i].v4l2_ctrl->minimum = c[i].v4l2_ctrl->maximum = c[i].v4l2_ctrl->step = c[i].v4l2_ctrl->default_value = 0;
		c[i].v4l2_ctrl->reserved[0] = V4L2_PRIV_IOCTL;
		c[i].v4l2_ctrl->reserved[1] = PWC_PROBE_INDEX;
		i++;

		//Pan/tilt control
		struct pwc_mpt_range range;
		if(ioctl(vdev->fd, VIDIOCPWCMPTGRANGE, &range) == 0) {
			//Pan control
			dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_DEBUG, "PWC: Found pwc private ioctl Pan control\n");
			c[i].v4l2_ctrl->id = i;
			c[i].v4l2_ctrl->type = V4L2_CTRL_TYPE_INTEGER;
			strcpy((char *) c[i].v4l2_ctrl->name, "Pan");
			c[i].v4l2_ctrl->minimum = range.pan_min;
			c[i].v4l2_ctrl->maximum = range.pan_max;
			c[i].v4l2_ctrl->step = 1;
			c[i].v4l2_ctrl->default_value = 0;
			c[i].v4l2_ctrl->reserved[0] = V4L2_PRIV_IOCTL;
			c[i].v4l2_ctrl->reserved[1] = PWC_PROBE_INDEX;
			i++;

			//tilt control
			dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_DEBUG, "PWC: Found pwc private ioctl Tilt control\n");
			c[i].v4l2_ctrl->id = i;
			c[i].v4l2_ctrl->type = V4L2_CTRL_TYPE_INTEGER;
			strcpy((char *) c[i].v4l2_ctrl->name, "Tilt");
			c[i].v4l2_ctrl->minimum = range.tilt_min;
			c[i].v4l2_ctrl->maximum = range.tilt_max;
			c[i].v4l2_ctrl->step = 1;
			c[i].v4l2_ctrl->default_value = 0;
			c[i].v4l2_ctrl->reserved[0] = V4L2_PRIV_IOCTL;
			c[i].v4l2_ctrl->reserved[1] = PWC_PROBE_INDEX;
		} else {
			dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_ERR, "PWC: Error probing Pan/tilt range\n");
		}
	} else{
			dprint(LIBVIDEO_SOURCE_DRV_PROBE, LIBVIDEO_LOG_DEBUG, "PWC: PTZ not supported\n");
	}
	return (signed) i;
}

