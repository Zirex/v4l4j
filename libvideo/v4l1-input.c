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
#include <sys/mman.h>		//for mmap
#include <sys/time.h>		//for gettimeofday, struct timeval
#include <stdbool.h>
#include "videodev2.h"
#include "videodev.h"
#include "libvideo.h"
#include "log.h"
#include "libvideo-err.h"
#include "libvideo-palettes.h"
#include "v4l1-input.h"


bool check_v4l1(int fd, struct video_capability *vc) {
	return ioctl(fd, VIDIOCGCAP, vc) >= 0;
}

//Check whether the device is V4L1 and has capture and mmap capabilities
// get capabilities VIDIOCGCAP - check max height and width
bool check_capture_capabilities_v4l1(int fd, char *file) {
	struct video_capability vc;
	dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG, "CAP: Checking capture device\n");

	CLEAR(vc);

	if (!check_v4l1(fd, &vc)){
		dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_ERR, "CAP: Not a V4L1 device.\n");
		return false;
	}

	if (!(vc.type & VID_TYPE_CAPTURE)) {
		info("The device %s seems to be a valid V4L1 device but without capture capability\n",file);
		PRINT_REPORT_ERROR();
		info("Listing the reported capabilities:\n");
		list_cap_v4l1(fd);
		return false;
	}

	return true;
}

// set the capture parameters
// set video channel 	VIDIOCSCHAN -
// set picture format 	VIDIOCSPICT -
// set window 		VIDIOCSWIN
// get window format	VIDIOCGWIN  (to double check)
int set_cap_param_v4l1(struct video_device *vdev, unsigned int src_palette, unsigned int palette) {
	UNUSED(src_palette);
	
	dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG, "CAP: Setting capture parameters on device %s.\n", vdev->file);
	
	struct capture_device *c = vdev->capture;

	struct video_capability vc;
	CLEAR(vc);
	if (!check_v4l1(vdev->fd, &vc)) {
		dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_ERR, "CAP: Error getting capabilities (not v4l1).\n");
		return LIBVIDEO_ERR_NOCAPS;
	}

	//dont fail if requested width/height outside the allowed range
	if(c->width == MAX_WIDTH || c->width > vc.maxwidth)
		c->width = vc.maxwidth;

	if(c->height == MAX_HEIGHT || c->height > vc.maxheight)
		c->height = vc.maxheight;

	if(c->width < vc.minwidth)
		c->width = vc.minwidth;

	if(c->height < vc.minheight)
		c->height = vc.minheight;

	//Select the input channel
	struct video_channel chan;
	CLEAR(chan);
	chan.channel = c->channel;
	switch(c->std) {
		case NTSC:
			chan.norm = VIDEO_MODE_NTSC;
			break;
		case PAL:
			chan.norm = VIDEO_MODE_PAL;
			break;
		case SECAM:
			chan.norm = VIDEO_MODE_SECAM;
			break;
		default:
			chan.norm = VIDEO_MODE_AUTO;
			break;
	}
	if (ioctl(vdev->fd, VIDIOCSCHAN, &chan) == -1) {
		info("The desired input channel (%d)/standard(%d) cannot be selected.\n", c->channel, c->std);
		info("Listing the reported capabilities:\n");
		list_cap_v4l1(vdev->fd);
		return LIBVIDEO_ERR_CHAN_SETUP;
	}
	
	//Check for tuner
	chan.channel = c->channel;
	if (ioctl(vdev->fd, VIDIOCGCHAN, &chan) == -1) {
		dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_ERR, "CAP: Can't get the current channel info.\n");
		return LIBVIDEO_ERR_CHAN_SETUP;
	}
	if(chan.tuners > 1) {
		//v4l1 weirdness
		//support only 1 tuner per input
		c->tuner_nb = -1;
	} else if(chan.tuners == 1) {
		c->tuner_nb = 0;
	} else {
		c->tuner_nb = -1;
	}


	//query the current image format
	struct video_picture pict;
	CLEAR(pict);
	if(ioctl(vdev->fd, VIDIOCGPICT, &pict) == -1) {
		dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_ERR, "CAP: Can't get the current palette format\n");
		return LIBVIDEO_ERR_IOCTL;
	}

	dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG, "CAP: Applying image format\n");

	if(palette == VIDEO_PALETTE_UNDEFINED_V4L1) {
		dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_ERR, "CAP: Palette #%d (%s) isn't V4L1-compatible\n", palette, libvideo_palettes[palette].name);
		return LIBVIDEO_ERR_FORMAT;
	}

	pict.palette = (u16) libvideo_palettes[palette].v4l1_palette;
	pict.depth = (u16) libvideo_palettes[palette].depth;
	dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG1, "CAP: Trying palette %s (%d) - depth %d...\n",
			libvideo_palettes[palette].name, pict.palette, pict.depth);

	/*
	 * V4L1 weirdness
	 */
	if(palette == YUV420) {
		pict.palette = VIDEO_PALETTE_YUV420P;
		pict.depth = (u16) libvideo_palettes[palette].depth;//TODO this may be redundant
		dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG1, "CAP: Trying palette YUV420-workaround (%d) - depth %d...\n", YUV420, pict.depth);
		
		if(ioctl(vdev->fd, VIDIOCSPICT, &pict) == 0){
			c->palette = YUV420;
			c->real_v4l1_palette = YUV420P;
			c->imagesize = c->width * c->height * pict.depth / 8;
			dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG, "CAP: Palette YUV420-workaround (%d) accepted - image size: %d\n", YUV420, c->imagesize);
		} else {
			dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_ERR, "Palette not supported\n");
			return LIBVIDEO_ERR_FORMAT;
		}
	} else if(palette == YUYV) {
		pict.palette = VIDEO_PALETTE_YUV422;
		pict.depth = (u16) libvideo_palettes[palette].depth;//TODO this may be redundant
		dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG1, "CAP: trying palette %s (%d) - depth %d...\n", "YUYV-workaround", YUYV, pict.depth);
		
		if(ioctl(vdev->fd, VIDIOCSPICT, &pict) == 0) {
			c->palette = YUYV;
			c->real_v4l1_palette = YUV422;
			c->imagesize = c->width * c->height * pict.depth / 8;
			dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG, "CAP: palette %s (%d) accepted - image size: %d\n", "YUYV-workaround", YUYV, c->imagesize);
		}  else {
			dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_ERR, "Palette not supported\n");
			return LIBVIDEO_ERR_FORMAT;
		}
	} else if(palette == YUV411) {
		pict.palette = VIDEO_PALETTE_YUV411P;
		pict.depth = (u16) libvideo_palettes[palette].depth;//TODO this may be redundant
		dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG1, "CAP: trying palette %s (%d) - depth %d...\n", "YUV411-workaround", YUV411, pict.depth);
		
		if(ioctl(vdev->fd, VIDIOCSPICT, &pict) == 0) {
			c->palette = YUV411;
			c->real_v4l1_palette = YUV411P;
			c->imagesize = c->width * c->height * pict.depth / 8;
			dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG, "CAP: palette %s (%d) accepted - image size: %d\n", "YUV411-workaround", YUYV, c->imagesize);
		} else {
			dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_ERR, "Palette not supported\n");
			return LIBVIDEO_ERR_FORMAT;
		}
	} else {
		if(ioctl(vdev->fd, VIDIOCSPICT, &pict) == 0) {
			c->palette = palette;
			c->real_v4l1_palette = (int) palette;
			c->imagesize = c->width * c->height * pict.depth / 8;
			dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG, "CAP: palette %s (%d) accepted - image size: %d\n", libvideo_palettes[palette].name, palette, c->imagesize);
		}  else {
			dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_ERR, "Palette not supported\n");
			return LIBVIDEO_ERR_FORMAT;
		}
	}
	
	c->needs_conversion = false;
	
	struct video_window win = {
		.x = 0,
		.y = 0,
		.width = c->width,
		.height = c->height,
		.chromakey = 0,
		.flags = 0,
		.clips = NULL,
		.clipcount = 0
	};
	
	if(ioctl(vdev->fd, VIDIOCSWIN, &win) == -1) {
		info("libvideo was unable to set the requested capture size (%dx%d).\n", c->width, c->height);
		info("Maybe the device doesnt support this combination of width and height.\n");
		return LIBVIDEO_ERR_DIMENSIONS;
	}
	
	CLEAR(win);
	
	if(ioctl(vdev->fd, VIDIOCGWIN, &win) == -1) {
		dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_ERR, "CAP: Can't verify the image size\n");
		return LIBVIDEO_ERR_DIMENSIONS;
	}
	
	if(win.width != c->width || win.height != c->height){
		dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG, "CAP: V4L1 resized image from %dx%d to %dx%d\n", c->width, c->height,win.width, win.height);
		c->width = win.width;
		c->height = win.height;
	}
	
	dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG, "CAP: capture resolution: %dx%d\n", c->width, c->height);
	
	return 0;
}

int set_frame_intv_v4l1(struct video_device *vdev, unsigned int num, unsigned int denom) {
	UNUSED(vdev);
	UNUSED(num);
	UNUSED(denom);
	// TODO: implement me if anyone wants this feature
	info("This function (set_frame_intv_v4l1) is not implemented.\n");
	return LIBVIDEO_ERR_IOCTL;
}

int get_frame_intv_v4l1(struct video_device *vdev, unsigned int *num, unsigned int *denom) {
	UNUSED(vdev);
	UNUSED(num);
	UNUSED(denom);
	// TODO: implement me if anyone wants this feature
	info("This function (get_frame_intv_v4l1) is not implemented.\n");
	return LIBVIDEO_ERR_IOCTL;
}

int set_video_input_std_v4l1(struct video_device *vdev, unsigned int input_num, unsigned int std) {
	UNUSED(vdev);
	UNUSED(input_num);
	UNUSED(std);
	// TODO: implement me if anyone wants this feature
	info("This function (set_video_input_std_v4l1) is not implemented.\n");
	return LIBVIDEO_ERR_IOCTL;
}

void get_video_input_std_v4l1(struct video_device *vdev, unsigned int *input_num, unsigned int *std) {
	UNUSED(vdev);
	UNUSED(input_num);
	UNUSED(std);
	// TODO: implement me if anyone wants this feature
	info("This function (get_video_input_std_v4l1) is not implemented.\n");
}

// get streaming cap details VIDIOCGMBUF, initialise streaming and
// create mmap'ed buffers
int init_capture_v4l1(struct video_device *vdev) {
	dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG, "CAP: Initializing capture on device %s.\n", vdev->file);

	struct video_mbuf vm = {0};
	if(ioctl(vdev->fd, VIDIOCGMBUF, &vm) == -1) {
		dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_ERR, "CAP: Error getting mmap information from driver.\n");
		return LIBVIDEO_ERR_REQ_MMAP;
	}

	dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG, "CAP: Driver allocated %d simultaneous buffers\n", vm.frames);
	dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG, "CAP:  - First offset [0]: %d\n", vm.offsets[0]);
	dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG, "CAP:  - Second offset [1]: %d\n", vm.offsets[1]);
	dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG, "CAP:  - Total size: %zu\n", vm.size);
	
	
	struct capture_device *c = vdev->capture;
	/*
	 * We only use two buffers, regardless of what the driver returned,
	 * unless it said 1, in which case we abort. For info, the QC driver
	 * returns vm.offset[0]=vm.offset[1]=0 ... gspca doesnt... because of
	 * this, we will store vm.size in c->mmap->v4l1_mmap_size so we can
	 * re-use it when unmmap'ing and we set
	 * c->mmap->buffers[0] == c->mmap->buffers[1] = vm.offset[1] - 1,
	 * so we have sensible values in the length fields, and we can still
	 * unmmap the area with the right value
	 */

	if(vm.frames > 2) {
		dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG, "CAP: Using only 2 buffers (of %d)\n", vm.frames);
	} else if (vm.frames < 2) {
		//Although it wont require much fixing...
		//do drivers allocate only 1 buffer anyway ?
		info("The video driver returned an unsupported number of MMAP buffers(%d).\n", vm.frames);
		PRINT_REPORT_ERROR();
		return LIBVIDEO_ERR_INVALID_BUF_NB;
	}

	c->mmap->buffer_nr = 2;

	XMALLOC( c->mmap->buffers, struct mmap_buffer *, (long unsigned int)(c->mmap->buffer_nr*sizeof(struct mmap_buffer)));

	c->mmap->buffers[0].start = mmap(NULL, vm.size, PROT_READ, MAP_SHARED, vdev->fd, 0);

	if(MAP_FAILED == c->mmap->buffers[0].start) {
		dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_ERR, "CAP: Can't allocate mmap'ed memory\n");
		return LIBVIDEO_ERR_MMAP_BUF;
	}
	dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG, "CAP: mmap'ed %zu bytes at %p\n", vm.size, c->mmap->buffers[0].start);

	c->mmap->v4l1_mmap_size = vm.size;
	c->mmap->buffers[1].start =( void *)c->mmap->buffers[0].start + vm.offsets[1];
	c->mmap->buffers[0].length = c->mmap->buffers[1].length = vm.size - 1;

	c->mmap->tmp = 0;

	return 0;
}

// start the capture of first buffer VIDIOCMCAPTURE(0)
int start_capture_v4l1(struct video_device *vdev) {
	struct capture_device *c = vdev->capture;
	dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG, "CAP: starting capture on device %s.\n", vdev->file);
	struct video_mmap mm = {
		.frame = 0,
		.width = c->width,
		.height = c->height,
		.format = libvideo_palettes[c->real_v4l1_palette].v4l1_palette
	};
	
	if(ioctl(vdev->fd, VIDIOCMCAPTURE, &mm) == -1) {
		dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_ERR, "CAP: Can't start the capture\n");
		return LIBVIDEO_ERR_IOCTL;
	}
	
	c->mmap->tmp = 0;
	
	return LIBVIDEO_ERR_SUCCESS;
}

//dequeue the next buffer with available frame
// start the capture of next buffer VIDIOCMCAPTURE(x)
void *dequeue_buffer_v4l1(struct video_device *vdev, unsigned int *len, unsigned int *index, struct timeval *capture_time, unsigned long long *sequence) {
	UNUSED(index);
	dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG2, "CAP: dequeuing buffer on device %s.\n", vdev->file);
	struct capture_device *c = vdev->capture;
	//TODO fix for when sizeof(unsigned int) != sizeof(void*), which might happen on 64-bit machines
	unsigned int curr_frame = (unsigned int) c->mmap->tmp;
	unsigned int next_frame = curr_frame ^ 1;
	*len = c->imagesize;
	
	//Capture time is when we send the capture command
	//We do this because v4l1 doesn't provide a capture timestamp
	if (capture_time != NULL)
		gettimeofday(capture_time, NULL);

	struct video_mmap mm;
	CLEAR(mm);
	mm.frame = next_frame;
	mm.width = c->width;
	mm.height = c->height;
	mm.format = libvideo_palettes[c->real_v4l1_palette].v4l1_palette;
	
	dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG2, "CAP: Starting capture of next frame (%d)\n", next_frame);
	if(ioctl(vdev->fd, VIDIOCMCAPTURE, &mm) == -1) {
		dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_ERR, "CAP: Can't initiate the capture of next frame\n");
		*len = 0;
		return NULL;
	}

	dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG2, "CAP: Waiting for frame (%d)\n", curr_frame);
	if(ioctl(vdev->fd, VIDIOCSYNC, &curr_frame) == -1) {
		dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_ERR, "CAP: Error waiting for next frame (%d)\n", curr_frame);
		*len = 0;
		return NULL;
	}
	
	c->mmap->tmp = (void *)next_frame;
	if (sequence)
		*sequence = 0;
	return c->mmap->buffers[curr_frame].start;
}

//enqueue the buffer when done using the frame
void enqueue_buffer_v4l1(struct video_device *device, unsigned int i) {
	UNUSED(device);
	UNUSED(i);
}

//counterpart of start_capture, must be called it start_capture was successful
int stop_capture_v4l1(struct video_device *vdev) {
	UNUSED(vdev);
	return 0;
}

//counterpart of init_capture, must be called it init_capture was successful
void free_capture_v4l1(struct video_device *vdev) {
	dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG, "CAP: freeing capture structures on device %s.\n", vdev->file);
	dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG, "CAP: unmmap %d bytes at %p\n", vdev->capture->mmap->v4l1_mmap_size, vdev->capture->mmap->buffers[0].start);
	
	if (munmap(vdev->capture->mmap->buffers[0].start,
			(size_t) vdev->capture->mmap->v4l1_mmap_size) == -1)
			dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_ERR, "CAP: Error unmapping mmap'ed buffer\n");
	
	XFREE(vdev->capture->mmap->buffers);
}


 /*
 * Control related functions
 */
 //returns the number of controls (standard and private V4L1 controls only)
unsigned int count_v4l1_controls(struct video_device *vdev) {
	UNUSED(vdev);
	//4 basic controls in V4L1
	dprint(LIBVIDEO_SOURCE_CTRL, LIBVIDEO_LOG_DEBUG, "CTRL: Found 4 controls\n");
	return 4;
}

//Populate the control_list with fake V4L2 controls matching V4L1 video
//controls and returns how many fake controls were created
unsigned int create_v4l1_controls(struct video_device *vdev, struct control *controls, unsigned int max) {
	UNUSED(vdev);
	unsigned int count = 0;
	if (max <= count)
		return count;
	
	//list standard V4L controls
	//brightness
	struct v4l2_queryctrl *brightnessControl = controls[0].v4l2_ctrl;
	brightnessControl->id = V4L2_CID_BRIGHTNESS;
	brightnessControl->type = V4L2_CTRL_TYPE_INTEGER;
	strcpy((char *)brightnessControl->name, "Brightness");
	brightnessControl->minimum = 0;
	brightnessControl->maximum = 65535;
	brightnessControl->step = 1;
	brightnessControl->default_value = 32768;
	brightnessControl->flags = 0;
	dprint(LIBVIDEO_SOURCE_CTRL, LIBVIDEO_LOG_DEBUG, "CTRL: found control(id: %d - name: %s - min: %d - max: %d - step: %d)\n",
			brightnessControl->id,
			(char *) &brightnessControl->name,
			brightnessControl->minimum,
			brightnessControl->maximum,
			brightnessControl->step);
	if (max <= ++count)
		return count;

	//hue
	controls[count].v4l2_ctrl->id = V4L2_CID_HUE;
	controls[count].v4l2_ctrl->type = V4L2_CTRL_TYPE_INTEGER;
	strcpy((char *)controls[count].v4l2_ctrl->name, "Hue");
	controls[count].v4l2_ctrl->minimum = 0;
	controls[count].v4l2_ctrl->maximum = 65535;
	controls[count].v4l2_ctrl->step = 1;
	controls[count].v4l2_ctrl->default_value = 32768;
	controls[count].v4l2_ctrl->flags = 0;
	dprint(LIBVIDEO_SOURCE_CTRL, LIBVIDEO_LOG_DEBUG, "CTRL: found control(id: %d - name: %s - min: %d -max: %d - step: %d)\n",
			controls[count].v4l2_ctrl->id,
			(char *) &controls[count].v4l2_ctrl->name,
			controls[count].v4l2_ctrl->minimum,
			controls[count].v4l2_ctrl->maximum,
			controls[count].v4l2_ctrl->step);
	if (max <= ++count)
		return count;

	//color
	controls[count].v4l2_ctrl->id = V4L2_CID_SATURATION;
	controls[count].v4l2_ctrl->type = V4L2_CTRL_TYPE_INTEGER;
	strcpy((char *)controls[count].v4l2_ctrl->name, "Saturation");
	controls[count].v4l2_ctrl->minimum = 0;
	controls[count].v4l2_ctrl->maximum = 65535;
	controls[count].v4l2_ctrl->step = 1;
	controls[count].v4l2_ctrl->default_value = 32768;
	controls[count].v4l2_ctrl->flags = 0;
	dprint(LIBVIDEO_SOURCE_CTRL, LIBVIDEO_LOG_DEBUG, "CTRL: found control(id: %d - name: %s - min: %d -max: %d - step: %d)\n",
			controls[count].v4l2_ctrl->id,
			(char *) &controls[count].v4l2_ctrl->name,
			controls[count].v4l2_ctrl->minimum,
			controls[count].v4l2_ctrl->maximum,
			controls[count].v4l2_ctrl->step);
	if (max <= ++count)
		return count;

	//contrast
	controls[count].v4l2_ctrl->id = V4L2_CID_CONTRAST;
	controls[count].v4l2_ctrl->type = V4L2_CTRL_TYPE_INTEGER;
	strcpy((char *)controls[count].v4l2_ctrl->name, "Contrast");
	controls[count].v4l2_ctrl->minimum = 0;
	controls[count].v4l2_ctrl->maximum = 65535;
	controls[count].v4l2_ctrl->step = 1;
	controls[count].v4l2_ctrl->default_value = 32768;
	controls[count].v4l2_ctrl->flags = 0;
	dprint(LIBVIDEO_SOURCE_CTRL, LIBVIDEO_LOG_DEBUG, "CTRL: found control(id: %d - name: %s - min: %d -max: %d - step: %d)\n",
			controls[count].v4l2_ctrl->id,
			(char *) &controls[count].v4l2_ctrl->name,
			controls[count].v4l2_ctrl->minimum,
			controls[count].v4l2_ctrl->maximum,
			controls[count].v4l2_ctrl->step);
	count++;

	return count;
}

//returns the value of a control
int get_control_value_v4l1(struct video_device *vdev, struct v4l2_queryctrl *ctrl, int *val) {
	struct video_picture pict = {0};
	//query the current image format
	if(ioctl(vdev->fd, VIDIOCGPICT, &pict) == -1) {
		dprint(LIBVIDEO_SOURCE_CTRL, LIBVIDEO_LOG_ERR, "CTRL: Can't get the value for control %s\n", (char *) &ctrl->name);
		return LIBVIDEO_ERR_IOCTL;
	}
	
	switch(ctrl->id) {
		case V4L2_CID_BRIGHTNESS:
			*val = pict.brightness;
			break;
		case V4L2_CID_HUE:
			*val = pict.hue;
			break;
		case V4L2_CID_SATURATION:
			*val = pict.colour;
			break;
		case V4L2_CID_CONTRAST:
			*val = pict.contrast;
			break;
		default:
			dprint(LIBVIDEO_SOURCE_CTRL, LIBVIDEO_LOG_ERR, "CTRL: unknown control %s (id: %d)\n", (char *) &ctrl->name, ctrl->id);
			return LIBVIDEO_ERR_IOCTL;
	}
	return 0;
}

//sets the value of a control
int set_control_value_v4l1(struct video_device *vdev, struct v4l2_queryctrl *ctrl, int *v) {
	struct video_picture pict;
	CLEAR(pict);
	//query the current image format
	if(ioctl(vdev->fd, VIDIOCGPICT, &pict) == -1) {
		dprint(LIBVIDEO_SOURCE_CTRL, LIBVIDEO_LOG_ERR, "CTRL: cannot get the current value for control %s\n", (char *) &ctrl->name);
		return LIBVIDEO_ERR_IOCTL;
	}
	
	int prev;
	switch(ctrl->id) {
		case V4L2_CID_BRIGHTNESS:
			prev = pict.brightness;
			pict.brightness = (uint16_t) *v;
			break;
		case V4L2_CID_HUE:
			prev = pict.hue;
			pict.hue = (uint16_t) *v;
			break;
		case V4L2_CID_SATURATION:
			prev = pict.colour;
			pict.colour = (uint16_t) *v;
			break;
		case V4L2_CID_CONTRAST:
			prev = pict.contrast;
			pict.contrast = (uint16_t) *v;
			break;
		default:
			dprint(LIBVIDEO_SOURCE_CTRL, LIBVIDEO_LOG_ERR, "CTRL: unknown control %s (id: %d)\n", (char *) &ctrl->name, ctrl->id);
			return LIBVIDEO_ERR_IOCTL;
	}

	//set the new value
	if(ioctl(vdev->fd, VIDIOCSPICT, &pict) == -1) {
		dprint(LIBVIDEO_SOURCE_CTRL, LIBVIDEO_LOG_ERR, "CTRL: Error setting the new value (%d) for control %s\n", *v, (char *) &ctrl->name);
		*v = prev;
		return LIBVIDEO_ERR_IOCTL;
	}
	
	return 0;
}



// ****************************************
// List caps functions
// ****************************************

static void enum_image_fmt_v4l1(int fd) {
	printf("============================================\n"
		"Querying image format\n\n");
	
	struct video_picture pic;
	CLEAR(pic);
	
	if(ioctl(fd, VIDIOCGPICT, &pic) < 0) {
		printf("Not supported ...\n");
		return;
	}
	
	printf("brightness: %d - hue: %d - colour: %d - contrast: %d - depth: %d (palette %d)\n",
			pic.brightness, pic.hue, pic.colour,
			pic.contrast, pic.depth, pic.palette);
	const int i = pic.palette;
#define PRINT_PALETTE_SUPPORT(palName) \
		do {\
			CLEAR(pic);\
			pic.palette = VIDEO_PALETTE_ ## palName; \
			printf("Palette %s:%s supported (%d%s)\n", \
				#palName, \
				ioctl(fd, VIDIOCSPICT, &pic) < 0 ? " NOT" : "", \
				pic.palette, \
				pic.palette == i ? "; current setting" : ""); \
		} while (0);
	PRINT_PALETTE_SUPPORT(GREY);
	PRINT_PALETTE_SUPPORT(HI240);
	PRINT_PALETTE_SUPPORT(RGB565);
	PRINT_PALETTE_SUPPORT(RGB555);
	PRINT_PALETTE_SUPPORT(RGB24);
	PRINT_PALETTE_SUPPORT(RGB32);
	PRINT_PALETTE_SUPPORT(YUV422);
	PRINT_PALETTE_SUPPORT(YUYV);
	PRINT_PALETTE_SUPPORT(UYVY);
	PRINT_PALETTE_SUPPORT(YUV420);
	PRINT_PALETTE_SUPPORT(YUV411);
	PRINT_PALETTE_SUPPORT(RAW);
	PRINT_PALETTE_SUPPORT(YUV422P);
	PRINT_PALETTE_SUPPORT(YUV411P);
	PRINT_PALETTE_SUPPORT(YUV420P);
	PRINT_PALETTE_SUPPORT(YUV410P);
	printf("\n");
#undef PRINT_PALETTE_SUPPORT
}

static void query_current_image_fmt_v4l1(int fd) {
	printf("============================================\n"
			"Querying current image size\n");
	
	struct video_window win = {0};
	if(ioctl(fd, VIDIOCGWIN, &win) == -1) {
		printf("Cannot get the image size\n");
		return;
	}
	
	printf("Current width: %d\n", win.width);
	printf("Current height: %d\n", win.height);
	printf("\n");
}

static void query_capture_intf_v4l1(int fd) {
	struct video_capability vc;
	CLEAR(vc);
	
	if (ioctl( fd, VIDIOCGCAP, &vc) == -1) {
		printf("Failed to get capabilities.\n");
		return;
	}


	printf("============================================\n"
			"Querying capture interfaces\n");
	for (unsigned int i = 0; i < vc.channels; i++) {
		struct video_channel chan = {0};
		chan.channel=i;
		if (ioctl(fd, VIDIOCGCHAN, &chan) == -1) {
			printf("Failed to get input details.");
			return;
		}
		printf("Input number: %d\n", chan.channel);
		printf("Name: %s\n", chan.name);
		if(chan.flags & VIDEO_VC_TUNER) {
			printf("Has tuners\n");
			printf("\tNumber of tuners: (%d) ", chan.tuners);
			//TODO: list tuner using struct video_tuner and VIDIOCGTUNER
		} else {
			printf("Doesn't have tuners\n");
		}
		if(chan.flags & VIDEO_VC_AUDIO)
			printf("Has audio\n");

		printf("Type: ");
		if(chan.type & VIDEO_TYPE_TV)
			printf("TV\n");
		if(chan.type & VIDEO_TYPE_CAMERA)
			printf("Camera\n");
		printf("\n");
	}
	printf("\n");
}

static void query_frame_sizes_v4l1(int fd) {
	struct video_capability vc;
	CLEAR(vc);

	if (ioctl(fd, VIDIOCGCAP, &vc) == -1) {
		printf("Failed to get capabilities.");
		return;
	}

	printf("============================================\n"
			"Querying supported frame sizes\n\n");
	printf("Min width: %d - Min height %d\n", vc.minwidth, vc.minheight);
	printf("Max width: %d - Max height %d\n", vc.maxwidth, vc.maxheight);
	printf("\n");
}


//void query_control(struct capture_device *);
void list_cap_v4l1(int fd) {
	struct video_capability vc;
	CLEAR(vc);

	if (ioctl( fd, VIDIOCGCAP, &vc) == -1) {
		printf("Failed to get capabilities.");
		return;
	}

	printf("============================================\n"
			"Querying general capabilities\n\n");

	//print capabilities
	printf("Driver name: %s\n",vc.name);
	#define PRINT_CAP(capability, name) printf("%s " name " capability\n", (vc.type & capability) ? "Has" : "Does NOT have")
	PRINT_CAP(VID_TYPE_CAPTURE, "capture");
	PRINT_CAP(VID_TYPE_TUNER, "tuner");
	PRINT_CAP(VID_TYPE_TELETEXT, "teletext");
	PRINT_CAP(VID_TYPE_OVERLAY, "overlay");
	PRINT_CAP(VID_TYPE_CHROMAKEY, "overlay chromakey");
	PRINT_CAP(VID_TYPE_CLIPPING, "clipping");
	PRINT_CAP(VID_TYPE_FRAMERAM, "frame buffer overlay");
	PRINT_CAP(VID_TYPE_SCALES, "scaling");
	PRINT_CAP(VID_TYPE_MONOCHROME, "monochrome only capture");
	PRINT_CAP(VID_TYPE_SUBCAPTURE, "sub capture");
	#undef PRINT_CAP
	
	query_capture_intf_v4l1(fd);
	enum_image_fmt_v4l1(fd);
	query_current_image_fmt_v4l1(fd);
	query_frame_sizes_v4l1(fd);
	//query_control(c);
}
