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
#include <string.h>
#include <sys/ioctl.h>		//for ioctl

#include "libvideo.h"
#include "v4l2-input.h"
#include "libvideo-err.h"
#include "log.h"
#include "palettes.h"


/*
 * this function takes an image format int returned by v4l2
 * and finds the matching libv4l id
 */
static int find_v4l2_palette(int v4l2_fmt){
	int i = 0;

	while(i<ARRAY_SIZE(libv4l_palettes)) {
		if(libv4l_palettes[i].v4l2_palette == v4l2_fmt)
			return i;
		i++;
	}

	return UNSUPPORTED_PALETTE;
}

//this function adds the given palette fmt to the list of
//supported palettes in struct device_info. It also
//check with libv4l_convert if it is converted from another palette
//it returns 0 if everything went fine, LIBV4L_ERR_IOCTL otherwise
static int add_supported_palette(struct device_info *di, int fmt,
		struct v4lconvert_data *conv){
	struct v4l2_format dst, src;
	CLEAR(dst);
	CLEAR(src);

	di->nb_palettes++;
	XREALLOC(di->palettes, struct palette_info *,
			di->nb_palettes * sizeof(struct palette_info));

	di->palettes[(di->nb_palettes - 1)].index = fmt;


	//check if this format is the result of a conversion form another format
	//by libv4l_convert
	dst.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst.fmt.pix.pixelformat = libv4l_palettes[fmt].v4l2_palette;
	dst.fmt.pix.width=640;
	dst.fmt.pix.height=480;
	if(v4lconvert_try_format(conv,&dst,&src)!=0){
		dprint(LIBV4L_LOG_SOURCE_QUERY, LIBV4L_LOG_LEVEL_ERR,
				"QRY: Error checking palette %s (libv4l convert says: %s)\n",
				libv4l_palettes[fmt].name,
				v4lconvert_get_error_message(conv));
		return LIBV4L_ERR_IOCTL;
	}

	if(v4lconvert_needs_conversion(conv,&src,&dst)==1){
		//it is converted form another format
		di->palettes[(di->nb_palettes - 1)].raw_palette =
			find_v4l2_palette(src.fmt.pix.pixelformat);
		dprint(LIBV4L_LOG_SOURCE_QUERY, LIBV4L_LOG_LEVEL_DEBUG,
				"QRY: converted from %d (%s)\n",
				di->palettes[(di->nb_palettes - 1)].raw_palette,
				libv4l_palettes[di->palettes[(di->nb_palettes - 1)].raw_palette].name
				);
	} else
		di->palettes[(di->nb_palettes - 1)].raw_palette = UNSUPPORTED_PALETTE;

	return 0;
}

//this function checks the supporte palettes
//it returns how many supported palettes there are, or LIBV4L_ERR_IOCTL
static int check_palettes_v4l2(struct video_device *vdev){
	struct v4lconvert_data *convert = v4lconvert_create(vdev->fd);
	struct v4l2_fmtdesc fmtd;
	CLEAR(fmtd);
	struct device_info *di = vdev->info;
	di->palettes = NULL;
	int p;

	dprint(LIBV4L_LOG_SOURCE_QUERY, LIBV4L_LOG_LEVEL_DEBUG, "QRY: Checking supported palettes.\n");

	fmtd.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmtd.index = 0;

	//while(ioctl(vdev->fd, VIDIOC_ENUM_FMT, &fmtd) >= 0) {
	while(v4lconvert_enum_fmt(convert, &fmtd)>=0){
		dprint(LIBV4L_LOG_SOURCE_QUERY, LIBV4L_LOG_LEVEL_DEBUG1, "QRY: looking for palette %d\n", fmtd.pixelformat);
		if ((p=find_v4l2_palette(fmtd.pixelformat)) == UNSUPPORTED_PALETTE) {
			info("libv4l has encountered an unsupported image format:\n");
			info("%s (%d)\n", fmtd.description, fmtd.pixelformat);
			info("Please let the author know about this error.\n");
			info("See the ISSUES section in the libv4l README file.\n");
		} else {
			dprint(LIBV4L_LOG_SOURCE_QUERY, LIBV4L_LOG_LEVEL_DEBUG,
					"QRY: %s supported (%d)\n", libv4l_palettes[p].name, p);
			if(add_supported_palette(di, p, convert)!=0){
				if(di->palettes)
					XFREE(di->palettes);
				return LIBV4L_ERR_IOCTL;
			}
		}
		fmtd.index++;
	}
	v4lconvert_destroy(convert);
	return fmtd.index;
}
static int query_tuner(struct video_input_info *vi, int fd, int index){
	struct v4l2_tuner t;
	CLEAR(t);
	t.index = index;

	if (ioctl (fd, VIDIOC_G_TUNER, &t) != 0)
		return -1;

	dprint(LIBV4L_LOG_SOURCE_QUERY, LIBV4L_LOG_LEVEL_DEBUG,
			"QRY: Tuner: %s - low: %u - high: %u - unit: %s\n",
			t.name, t.rangelow, t.rangehigh,
			t.capability & V4L2_TUNER_CAP_LOW ? "kHz": "MHz");

	XMALLOC(vi->tuner, struct tuner_info *, sizeof(struct tuner_info));
	strncpy(vi->tuner->name, (char *) t.name, NAME_FIELD_LENGTH);
	vi->tuner->index = index;
	vi->tuner->unit = t.capability & VIDEO_TUNER_LOW ? KHZ_UNIT : MHZ_UNIT;
	vi->tuner->rssi = t.signal;
	vi->tuner->type =  t.type == V4L2_TUNER_RADIO ? RADIO_TYPE : TV_TYPE;
	vi->tuner->rangehigh = t.rangehigh;
	vi->tuner->rangelow = t.rangelow;

	dprint(LIBV4L_LOG_SOURCE_QUERY, LIBV4L_LOG_LEVEL_DEBUG,
				"QRY: Tuner: %s - low: %lu - high: %lu - unit: %d\n",
				vi->tuner->name, vi->tuner->rangelow, vi->tuner->rangehigh,
				vi->tuner->unit);

	return 0;
}

static void free_tuner(struct tuner_info *t){
	if (t)
		XFREE(t);
}

static void free_video_inputs(struct video_input_info *vi, int nb){
	int i;
	for(i=0;i<nb;i++) {
		free_tuner(vi[i].tuner);
		if (vi[i].nb_stds) XFREE(vi[i].supported_stds);
	}
	XFREE(vi);
}

static void add_supported_std(struct video_input_info *vi, int std){
	dprint(LIBV4L_LOG_SOURCE_QUERY, LIBV4L_LOG_LEVEL_DEBUG, "QRY: Adding standard %d\n", std);
	vi->nb_stds++;
	XREALLOC(vi->supported_stds, int *, vi->nb_stds * sizeof(int));
	vi->supported_stds[(vi->nb_stds - 1)] = std;
}

int check_inputs_v4l2(struct video_device *vdev){
	struct v4l2_input vi;
	int i, ret = 0;
	struct device_info *di = vdev->info;
	CLEAR(vi);
	di->inputs = NULL;

	dprint(LIBV4L_LOG_SOURCE_QUERY, LIBV4L_LOG_LEVEL_DEBUG, "QRY: querying inputs\n");

	//Check how many inputs there are
	while (-1 != ioctl(vdev->fd, VIDIOC_ENUMINPUT, &vi))
		vi.index++;

	di->nb_inputs = vi.index;

	dprint(LIBV4L_LOG_SOURCE_QUERY, LIBV4L_LOG_LEVEL_DEBUG, "QRY: found %d inputs\n", di->nb_inputs );

	XMALLOC(di->inputs, struct video_input_info *, di->nb_inputs * sizeof(struct video_input_info ));

	for (i=0; i<di->nb_inputs; i++) {
		CLEAR(vi);
		CLEAR(di->inputs[i]);
		vi.index = i;
		if (-1 == ioctl(vdev->fd, VIDIOC_ENUMINPUT, &vi)) {
			info("Failed to get details of input %d on device %s\n", i, vdev->file);
			ret = LIBV4L_ERR_IOCTL;
			free_video_inputs(di->inputs,i);
			goto end;
		}

		dprint(LIBV4L_LOG_SOURCE_QUERY, LIBV4L_LOG_LEVEL_DEBUG, "QRY: input %d - %s - %s - tuner: %d\n", i, vi.name, (vi.type == V4L2_INPUT_TYPE_TUNER) ? "Tuner" : "Camera",vi.tuner);

		strncpy(di->inputs[i].name, (char *) vi.name, NAME_FIELD_LENGTH);
		di->inputs[i].index = i;
		di->inputs[i].type = (vi.type == V4L2_INPUT_TYPE_TUNER) ? INPUT_TYPE_TUNER : INPUT_TYPE_CAMERA;

		if (vi.type & V4L2_INPUT_TYPE_TUNER) {
			dprint(LIBV4L_LOG_SOURCE_QUERY, LIBV4L_LOG_LEVEL_DEBUG, "QRY: Querying tuner\n");
			if (-1 == query_tuner(&di->inputs[i], vdev->fd, vi.tuner)) {
				info("Failed to get details of tuner on input %d of device %s\n", i, vdev->file);
				ret = LIBV4L_ERR_IOCTL;
				free_video_inputs(di->inputs,i);
				goto end;
			}
		} else {
			dprint(LIBV4L_LOG_SOURCE_QUERY, LIBV4L_LOG_LEVEL_DEBUG, "QRY: No tuner\n");
			di->inputs[i].tuner = NULL;
		}

		if(vi.std & V4L2_STD_PAL) add_supported_std(&di->inputs[i], PAL);
		if(vi.std & V4L2_STD_NTSC) add_supported_std(&di->inputs[i], NTSC);
		if(vi.std & V4L2_STD_SECAM) add_supported_std(&di->inputs[i], SECAM);
		if(vi.std == V4L2_STD_UNKNOWN) add_supported_std(&di->inputs[i], WEBCAM);

	}
	end:
	return ret;
}

int query_device_v4l2(struct video_device *vdev){
	int ret = 0;
	struct v4l2_capability caps;

	dprint(LIBV4L_LOG_SOURCE_QUERY, LIBV4L_LOG_LEVEL_DEBUG, "QRY: Querying V4L2 device.\n");

	if (check_v4l2(vdev->fd, &caps)==-1) {
		info("Error checking capabilities of V4L2 video device %s", vdev->file);
		ret = LIBV4L_ERR_NOCAPS;
		goto end;
	}
	//fill name field
	strncpy(vdev->info->name, (char *) caps.card, NAME_FIELD_LENGTH);

	//fill input field
	if(check_inputs_v4l2(vdev)==-1){
		info("Error checking available inputs on V4L2 video device %s", vdev->file);
		ret = LIBV4L_ERR_NOCAPS;
		goto end;
	}

	//fill palettes field
	if((vdev->info->nb_palettes = check_palettes_v4l2(vdev))==LIBV4L_ERR_IOCTL){
		free_video_inputs(vdev->info->inputs, vdev->info->nb_inputs);
		info("Error checking supported palettes on V4L2 video device %s", vdev->file);
		ret = LIBV4L_ERR_NOCAPS;
	}

	end:
	return ret;
}

void free_video_device_v4l2(struct video_device *vd){
	XFREE(vd->info->palettes);
	free_video_inputs(vd->info->inputs, vd->info->nb_inputs);
}