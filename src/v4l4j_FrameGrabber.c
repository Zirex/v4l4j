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

#include <jni.h>
#include <stdio.h>
#include <jpeglib.h>
#include <stdint.h>
#include <sys/time.h>		//for struct timeval

#include "common.h"
#include "debug.h"
#include "jniutils.h"
#include "libvideo.h"
#include "libvideo-err.h"
#include "jpeg.h"
#include "libvideo-palettes.h"
#include "rgb.h"

// static variables
static jfieldID last_captured_frame_sequence_fID = NULL;
static jfieldID last_captured_frame_time_usec_fID = NULL;
static jfieldID last_captured_frame_buffer_index_fID = NULL;


/*
 * Updates the width, height, standard & format fields in a framegrabber object
 */
static void update_width_height(JNIEnv *e, jobject this, struct v4l4j_device *d) {
	LOG_FN_ENTER();

	//Updates the FrameGrabber class width, height & format fields with the
	//values returned by V4L2
	jclass this_class = (*e)->GetObjectClass(e, this);
	if(this_class == NULL) {
		info("[V4L4J] Error looking up FrameGrabber class\n");
		THROW_EXCEPTION(e, JNI_EXCP, "Error looking up FrameGrabber class");
		return;
	}

	//width
	jfieldID widthFID = (*e)->GetFieldID(e, this_class, "width", "I");
	if(widthFID == NULL) {
		THROW_EXCEPTION(e, JNI_EXCP, "Error looking up width field in FrameGrabber class");
		return;
	}
	(*e)->SetIntField(e, this, widthFID, d->vdev->capture->width);

	//height
	jfieldID heightFID = (*e)->GetFieldID(e, this_class, "height", "I");
	if(heightFID == NULL) {
		THROW_EXCEPTION(e, JNI_EXCP, "Error looking up height field in FrameGrabber class");
		return;
	}
	(*e)->SetIntField(e, this, heightFID, d->vdev->capture->height);

	//standard
	jfieldID standardFID = (*e)->GetFieldID(e, this_class, "standard", "I");
	if(standardFID == NULL) {
		THROW_EXCEPTION(e, JNI_EXCP, "Error looking up standard field in class FrameGrabber");
		return;
	}
	(*e)->SetIntField(e, this, standardFID, d->vdev->capture->std);


	//format
	if(d->output_fmt != OUTPUT_RAW) {
		jfieldID formatFID = (*e)->GetFieldID(e, this_class, "format", "I");
		if(formatFID == NULL) {
			THROW_EXCEPTION(e, JNI_EXCP, "Error looking up format field in FrameGrabber class");
			return;
		}

		int fmt;
		if(d->vdev->capture->is_native)
			fmt = d->vdev->capture->palette;
		else
			fmt = d->vdev->capture->convert->src_palette;

		dprint(LOG_V4L4J, "[V4L4J] Setting format field to '%s' image format\n", libvideo_palettes[fmt].name);
		(*e)->SetIntField(e, this, formatFID, fmt);
	}
}

static int get_buffer_length(struct v4l4j_device *d) {
	LOG_FN_ENTER();
	switch (d->output_fmt) {
		case OUTPUT_RAW:
		case OUTPUT_JPG:
			//shall we trust what the driver says ?
			dprint(LOG_V4L4J, "[V4L4J] OUTPUT: RAW / JPEG - Using byte array of size %d\n", d->vdev->capture->imagesize );
			return d->vdev->capture->imagesize;
		case OUTPUT_RGB24:
			//RGB24 means w * h * 3
			dprint(LOG_V4L4J, "[V4L4J] OUTPUT: RGB24 - Using byte array of size %d\n", d->vdev->capture->width * d->vdev->capture->height * 3);
			return d->vdev->capture->width * d->vdev->capture->height * 3;
		case OUTPUT_BGR24:
			//BGR24 means w * h * 3
			dprint(LOG_V4L4J, "[V4L4J] OUTPUT: BGR24 - Using byte array of size %d\n", d->vdev->capture->width * d->vdev->capture->height * 3);
			return d->vdev->capture->width * d->vdev->capture->height * 3;
		case OUTPUT_YUV420:
			//YUV420 means w * h * 3/2
			dprint(LOG_V4L4J, "[V4L4J] OUTPUT: YUV420 - Using byte array of size %d\n", d->vdev->capture->width * d->vdev->capture->height * 3/2);
			return d->vdev->capture->width * d->vdev->capture->height * 3/2;
		case OUTPUT_YVU420:
			//YVU420 means w * h * 3/2
			dprint(LOG_V4L4J, "[V4L4J] OUTPUT: YVU420 - Using byte array of size %d\n", d->vdev->capture->width * d->vdev->capture->height * 3/2);
			return d->vdev->capture->width * d->vdev->capture->height * 3/2;
		default:
			dprint(LOG_V4L4J,"[V4L4J] Unknown output format...");
			return 0;
	}
}

/*
 * Call init routines of RGB, JPEG or raw depending on requested
 * output image format
 */
static int init_format_converter(struct v4l4j_device *d) {
	LOG_FN_ENTER();

	if(d->need_conv) {
		int ret = 0;
		if(d->output_fmt == OUTPUT_JPG) {
			dprint(LOG_V4L4J, "[V4L4J] Initializing JPEG converter\n");
			ret = init_jpeg_compressor(d, 80);
			if(ret)
				dprint(LOG_V4L4J, "[V4L4J] Error %d initialising JPEG converter\n", ret);
		}

		if (!(d->vdev->capture->is_native)) {
			dprint(LOG_V4L4J, "[V4L4J] Setting up double conversion\n");
			XMALLOC(d->double_conversion_buffer, unsigned char *, d->vdev->capture->imagesize);
		}
		return ret;
	} else {
		dprint(LOG_LIBVIDEO, "[V4L4J] no conversion done by v4l4j - raw copy\n");
	}

	return 0;
}

static void release_format_converter(struct v4l4j_device *d){
	LOG_FN_ENTER();
	if(d->need_conv) {
		if(d->output_fmt == OUTPUT_JPG)
			destroy_jpeg_compressor(d);

		if (!d->vdev->capture->is_native)
			XFREE(d->double_conversion_buffer);
	}
}

/*
 * this function checks the output format and returns the capture image format
 * to use, depending on whether format conversion is done by v4l4j or libvideo
 * input is a libvideo palette index, output is enum output_format in common.h
 * the returned value is a libvideo palette index
 */
static int init_capture_format(struct v4l4j_device *d, int fg_out_fmt, int* src_fmt, int* dest_fmt){
	LOG_FN_ENTER();
	dprint(LOG_LIBVIDEO, "[V4L4J] Setting output to %s - input format: %s\n",
			fg_out_fmt == OUTPUT_RAW ? "RAW":
			fg_out_fmt == OUTPUT_JPG ? "JPEG":
			fg_out_fmt == OUTPUT_RGB24 ? "RGB24":
			fg_out_fmt == OUTPUT_BGR24 ? "BGR24":
			fg_out_fmt == OUTPUT_YUV420 ? "YUV420":
			fg_out_fmt == OUTPUT_YVU420 ? "YVU420" : "UNKNOWN",
			libvideo_palettes[*src_fmt].name);

	//check if libvideo does the conv
	switch(fg_out_fmt) {
	case OUTPUT_JPG:
		//for JPEG, v4l4j always does the conv
		dprint(LOG_LIBVIDEO, "[V4L4J] JPEG conversion done by v4l4j\n");
		*dest_fmt = *src_fmt;
		*src_fmt = -1;
		d->need_conv = true;
		return 0;

	case OUTPUT_RGB24:
		*dest_fmt = RGB24;
		// leave native capture format in src_fmt
		dprint(LOG_LIBVIDEO, "[V4L4J] RGB24 conversion done by libvideo\n");
		d->need_conv = false;
		return 0;

	case OUTPUT_RAW:
		*dest_fmt = *src_fmt;
		*src_fmt = -1;
		dprint(LOG_LIBVIDEO, "[V4L4J] raw format - no conversion\n");
		d->need_conv = false;
		return 0;

	case OUTPUT_BGR24:
		*dest_fmt = BGR24;
		// leave native capture format in src_fmt
		dprint(LOG_LIBVIDEO, "[V4L4J] BGR24 conversion done by libvideo\n");
		d->need_conv = false;
		return 0;
	case OUTPUT_YUV420:
		*dest_fmt = YUV420;
		// leave native capture format in src_fmt
		dprint(LOG_LIBVIDEO, "[V4L4J] YUV420 conversion done by libvideo\n");
		d->need_conv = false;
		return 0;
	case OUTPUT_YVU420:
		*dest_fmt = YVU420;
		// leave native capture format in src_fmts
		dprint(LOG_LIBVIDEO, "[V4L4J] YVU420 conversion done by libvideo\n");
		d->need_conv = false;
		return 0;
	default:
		info("[V4L4J] Error: unknown output format %d\n", fg_out_fmt);
		return -1;
	}
}


/*
 * Gets the fieldIDs for members that have to be updated every time a frame is captured
 * return 0 if an exception is thrown, 1 otherwise
 */
static int get_lastFrame_field_ids(JNIEnv *e, jobject this, struct v4l4j_device *d){
	LOG_FN_ENTER();
	
	//gets the fields of members updated every frame captured.
	jclass this_class = (*e)->GetObjectClass(e, this);
	if(this_class == NULL) {
		THROW_EXCEPTION(e, JNI_EXCP, "Error looking up FrameGrabber class");
		return 0;
	}

	//last_captured_frame_sequence_fID
	last_captured_frame_sequence_fID = (*e)->GetFieldID(e, this_class, "lastCapturedFrameSequence", "J");
	if(last_captured_frame_sequence_fID == NULL) {
		THROW_EXCEPTION(e, JNI_EXCP, "Error looking up last_captured_frame_sequence_fID field in FrameGrabber class");
		return 0;
	}

	// last_captured_frame_time_usec_fID
	last_captured_frame_time_usec_fID = (*e)->GetFieldID(e, this_class, "lastCapturedFrameTimeuSec", "J");
	if(last_captured_frame_time_usec_fID == NULL) {
		THROW_EXCEPTION(e, JNI_EXCP, "Error looking up lastCapturedFrameTimeuSec field in FrameGrabber class");
		return 0;
	}

	// last_captured_frame_buffer_index_fID
	last_captured_frame_buffer_index_fID = (*e)->GetFieldID(e, this_class, "lastCapturedFrameBufferIndex", "I");
	if(last_captured_frame_buffer_index_fID == NULL) {
		THROW_EXCEPTION(e, JNI_EXCP, "Error looking up lastCapturedFrameBufferIndex field in FrameGrabber class");
		return 0;
	}
	return 1;
}


/*
 * initialize LIBVIDEO (open, set_cap_param, init_capture)
 * creates the Java ByteBuffers
 * creates the V4L2Controls
 * initialize the JPEG compressor
 * fmt is the input format
 * output is the output format (enum output_format in common.h)
 * return the number of mmap''ed buffers
 */
JNIEXPORT jint JNICALL Java_au_edu_jcu_v4l4j_AbstractGrabber_doInit(JNIEnv *e, jobject self, jlong object, jint num_buffers, jint w, jint h, jint ch, jint std,
		jint in_fmt, jint fg_out_fmt) {
	LOG_FN_ENTER();
	int i = 0;
	struct v4l4j_device *d = (struct v4l4j_device *) (uintptr_t) object;
	int src_fmt = in_fmt, dest_fmt;

	// Get the field IDs if we dont have them already. If error getting them, return.
	if (!last_captured_frame_sequence_fID || !last_captured_frame_time_usec_fID)
		if (!get_lastFrame_field_ids(e, self, d))
			return 0;


	/*
	 * i n i t _ c a p t u r e _ d e v i c e ( )
	 */
	dprint(LOG_LIBVIDEO, "[LIBVIDEO] Calling init_capture_device()\n");
	struct capture_device *c = init_capture_device(d->vdev, w, h, ch, std, num_buffers);

	if(c == NULL) {
		dprint(LOG_V4L4J, "[V4L4J] init_capture_device failed\n");
		THROW_EXCEPTION(e, INIT_EXCP, "Error initializing device '%s'. Make sure it is a valid V4L device file and check the file permissions.", d->vdev->file);
		return 0;
	}


	/*
	 * s e t _ c a p _ p a r a m
	 */
	d->output_fmt = fg_out_fmt;
	if(init_capture_format(d, fg_out_fmt, &src_fmt, &dest_fmt) == -1){
		free_capture_device(d->vdev);
		THROW_EXCEPTION(e, INIT_EXCP, "Unknown output format %d\n", fg_out_fmt);
		return 0;
	}

	dprint(LOG_LIBVIDEO, "[V4L4J] src format: %s\n", (src_fmt != -1) ? libvideo_palettes[src_fmt].name : "'chosen by libvideo'");

	dprint(LOG_LIBVIDEO, "[V4L4J] dest format: %s\n", libvideo_palettes[dest_fmt].name);

	dprint(LOG_LIBVIDEO, "[LIBVIDEO] calling 'set_cap_param'\n");
	if((i=(*c->actions->set_cap_param)(d->vdev, src_fmt, dest_fmt)) != 0){
		dprint(LOG_V4L4J, "[V4L4J] set_cap_param failed\n");
		free_capture_device(d->vdev);
		if(i==LIBVIDEO_ERR_DIMENSIONS)
			THROW_EXCEPTION(e, DIM_EXCP, "The requested dimensions (%dx%d) are not supported", c->width, c->height);
		else if(i==LIBVIDEO_ERR_CHAN_SETUP)
			THROW_EXCEPTION(e, CHANNEL_EXCP, "The requested channel (%d) is invalid", c->channel);
		else if(i==LIBVIDEO_ERR_FORMAT)
			THROW_EXCEPTION(e, FORMAT_EXCP, "Image format %s not supported", libvideo_palettes[in_fmt].name);
		else if(i==LIBVIDEO_ERR_STD)
			THROW_EXCEPTION(e, STD_EXCP, "The requested standard (%d) is invalid", c->std);
		else
			THROW_EXCEPTION(e, GENERIC_EXCP, "Error applying capture parameters (error=%d)",i);
		return 0;
	}


	/*
	 * i n i t _ c a p t u r e ( )
	 */
	dprint(LOG_LIBVIDEO, "[LIBVIDEO] Calling 'init_capture(dev: %s)'\n", d->vdev->file);
	if((i=(*c->actions->init_capture)(d->vdev)) < 0) {
		dprint(LOG_V4L4J, "[V4L4J] init_capture failed\n");
		free_capture_device(d->vdev);
		THROW_EXCEPTION(e, GENERIC_EXCP, "Error initializing capture (error=%d)",i);
		return 0;
	}

	//setup format converter
	if(init_format_converter(d) != 0) {
		dprint(LOG_V4L4J, "[V4L4J] Error initializing the format converter\n");
		(*c->actions->free_capture)(d->vdev);
		free_capture_device(d->vdev);
		THROW_EXCEPTION(e, GENERIC_EXCP, "Error initializing the format converter");
		return 0;
	}


	//update width, height, standard & image format in FrameGrabber class
	update_width_height(e, self, d);

	return c->mmap->buffer_nr;
}

/*
 * returns an appropriate size for a byte array holding converted frames
 */
JNIEXPORT jint JNICALL Java_au_edu_jcu_v4l4j_AbstractGrabber_getBufferSize(JNIEnv *e, jclass me, jlong object) {
	LOG_FN_ENTER();
	struct v4l4j_device *d = (struct v4l4j_device *) (uintptr_t) object;

	return get_buffer_length(d);
}

/*
 * tell LIBVIDEO to start the capture
 */
JNIEXPORT void JNICALL Java_au_edu_jcu_v4l4j_AbstractGrabber_start(JNIEnv *e, jclass me, jlong object){
	LOG_FN_ENTER();
	struct v4l4j_device *d = (struct v4l4j_device *) (uintptr_t) object;

	dprint(LOG_LIBVIDEO, "[LIBVIDEO] Calling 'start_capture(dev: %s)'\n", d->vdev->file);
	if((*d->vdev->capture->actions->start_capture)(d->vdev) < 0) {
		dprint(LOG_V4L4J, "[V4L4J] start_capture failed\n");
		THROW_EXCEPTION(e, GENERIC_EXCP, "Error starting the capture");
	}
}

/*
 * tell the JPEG compressor the new compression factor
 */
JNIEXPORT void JNICALL Java_au_edu_jcu_v4l4j_AbstractGrabber_setQuality(JNIEnv *env, jclass me, jlong object, jint quality) {
	LOG_FN_ENTER();
	struct v4l4j_device *dev = (struct v4l4j_device *) (uintptr_t) object;
	if(dev->output_fmt != OUTPUT_JPG)
		return;
	dprint(LOG_V4L4J, "[V4L4J] Setting JPEG quality to %d\n", quality);
	dev->j->jpeg_quality = quality;
}

/*
 * sets the frame interval
 */
JNIEXPORT void JNICALL Java_au_edu_jcu_v4l4j_AbstractGrabber_doSetFrameIntv(JNIEnv *e, jclass me, jlong object, jint num, jint denom) {
	LOG_FN_ENTER();
	struct v4l4j_device *dev = (struct v4l4j_device *) (uintptr_t) object;

	dprint(LOG_V4L4J, "[LIBVIDEO] Setting frame interval to %d/%d\n",num, denom);
	int ret = dev->vdev->capture->actions->set_frame_interval(dev->vdev, num, denom);

	switch(ret) {
		case LIBVIDEO_ERR_FORMAT:
			dprint(LOG_V4L4J, "[V4L4J] Invalid frame interval\n");
			THROW_EXCEPTION(e, INVALID_VAL_EXCP, "Error setting frame interval: invalid values %d/%d", num, denom);
			return;
		case LIBVIDEO_ERR_IOCTL:
			THROW_EXCEPTION(e, UNSUPPORTED_METH_EXCP, "Setting frame interval not supported");
			return;
	}
}

/*
 * get the frame interval numerator (what=0) or denominator(what!=0)
 * (expects some lock to be obtained so calling this method to obtain the other
 * frac part of the frame intv does NOT interleave with doSetFrameIntv())
 */
JNIEXPORT jint JNICALL Java_au_edu_jcu_v4l4j_AbstractGrabber_doGetFrameIntv(JNIEnv *e, jclass me, jlong object, jint what) {
	LOG_FN_ENTER();

	struct v4l4j_device *dev = (struct v4l4j_device *) (uintptr_t) object;

	unsigned int num, denom;
	if(dev->vdev->capture->actions->get_frame_interval(dev->vdev, &num, &denom)) {
		THROW_EXCEPTION(e, UNSUPPORTED_METH_EXCP, "Getting frame interval not supported");
		return 0;
	}

	if(what == 0)
		return num;
	else
		return denom;
}

/*
 * sets the video input and standard
 */
JNIEXPORT void JNICALL Java_au_edu_jcu_v4l4j_AbstractGrabber_doSetVideoInputNStandard(JNIEnv *e, jclass me, jlong object, jint input_num, jint standard) {
	LOG_FN_ENTER();
	
	struct v4l4j_device *dev = (struct v4l4j_device *) (uintptr_t) object;

	dprint(LOG_V4L4J, "[LIBVIDEO] Setting input to %d and standard to %d\n", input_num, standard);
	int ret = dev->vdev->capture->actions->set_video_input_std(dev->vdev, input_num, standard);

	switch(ret) {
		case LIBVIDEO_ERR_CHANNEL:
			THROW_EXCEPTION(e, CHANNEL_EXCP, "Error setting new input %d", input_num);
			break;
		case LIBVIDEO_ERR_STD:
			dprint(LOG_V4L4J, "[V4L4J] Error setting standard to %d\n", standard);
			THROW_EXCEPTION(e, STD_EXCP, "The requested standard (%d) is invalid", standard);
			break;
	}
}

/*
 * gets the video input
 */
JNIEXPORT jint JNICALL Java_au_edu_jcu_v4l4j_AbstractGrabber_doGetVideoInput(JNIEnv *e, jclass me, jlong object) {
	LOG_FN_ENTER();
	struct v4l4j_device *dev = (struct v4l4j_device *) (uintptr_t) object;

	unsigned int input_num, standard;
	dev->vdev->capture->actions->get_video_input_std(dev->vdev, &input_num, &standard);

	return (jint) input_num;
}

/*
 * gets the video standard
 */
JNIEXPORT jint JNICALL Java_au_edu_jcu_v4l4j_AbstractGrabber_doGetVideoStandard(JNIEnv *e, jclass me, jlong object) {
	LOG_FN_ENTER();
	
	struct v4l4j_device *dev = (struct v4l4j_device *) (uintptr_t) object;

	unsigned int input_num, standard;
	dev->vdev->capture->actions->get_video_input_std(dev->vdev, &input_num, &standard);

	return (jint) standard;
}


/*
 * enqueue a buffer
 */
JNIEXPORT void JNICALL Java_au_edu_jcu_v4l4j_AbstractGrabber_enqueueBuffer(JNIEnv *e, jclass me, jlong object, jint buffer_index) {
	LOG_FN_ENTER();
	struct v4l4j_device *dev = (struct v4l4j_device *) (uintptr_t) object;

	(*dev->vdev->capture->actions->enqueue_buffer)(dev->vdev, buffer_index);
}

/*
 * dequeue a buffer, perform conversion if required and return frame
 */
JNIEXPORT jint JNICALL Java_au_edu_jcu_v4l4j_AbstractGrabber_fillBuffer(JNIEnv *env, jobject this, jlong object, jobject buffer) {
	LOG_FN_ENTER();
	struct v4l4j_device *d = (struct v4l4j_device *) (uintptr_t) object;

	//get frame from libvideo
	unsigned int buffer_index;
	struct timeval captureTime;
	unsigned long long sequence;
	void* frame = (*d->vdev->capture->actions->dequeue_buffer)(d->vdev, &d->capture_len, &buffer_index, &captureTime, &sequence);
	if(frame == NULL) {
		THROW_EXCEPTION(env, GENERIC_EXCP, "Error dequeuing buffer for capture");
		return 0;
	}

	// get a pointer to the java array
	jbyteArray arrayRef = NULL;
	unsigned int arrayLength = 0;
	void (*releaseArray)(JNIEnv* env, jbyteArray arrayRef, unsigned char* ptr);
	unsigned char* array = getBufferPointer(env, buffer, &arrayRef, &arrayLength, &releaseArray);
	// check we have a valid pointer
	if (!array) {
		(*d->vdev->capture->actions->enqueue_buffer)(d->vdev, buffer_index);
		THROW_EXCEPTION(env, GENERIC_EXCP, "Error getting the byte array");
		return 0;
	}
	
	if (arrayRef != NULL)
		dprintf(LOG_V4L4J, "[V4L4J] Slow path: Can't get a direct pointer to buffer");

	unsigned int output_len;
	START_TIMING;
	// Perform required conversion
	if(!d->vdev->capture->is_native) {
		// Check whether we can convert directly to the byte[] memory
		if(!d->need_conv) {
			// Only libv4l conversion is required
			output_len = (*d->vdev->capture->actions->convert_buffer)(d->vdev, buffer_index, d->capture_len, array);
		} else {
			// both libv4l and v4l4j conversions required
			(*d->vdev->capture->actions->convert_buffer)(d->vdev, buffer_index, d->capture_len, d->double_conversion_buffer);
			output_len = (*d->convert)(d, d->double_conversion_buffer, array);
		}
	} else {
		// No libv4l conversion required. Check if v4l4j conversion is required
		if (!d->need_conv) {
			// No v4l4j conversion required. So copy the frame to byte[] memory. This
			// is definitely NOT an optimal solution, but I cant see any other way to do it:
			// We could mmap the byte[] memory and used it as the buffer, but the JVM specs
			// clearly says the memory can go at anytime, or be moved to somewhere else.
			// And we can only hold on to it (between GetPrimitiveArrayCritical() and
			// ReleasePrimitiveArrayCritical() ) for a short amount of time. If you
			// find yourself reading this comment and you have a better idea, let me know.
			memcpy(array, frame, d->capture_len);
			output_len = d->capture_len;
		} else {
			output_len = (*d->convert)(d, frame, array);
		}
	}
	END_TIMING("JNI Conversion took ");
	
	// release pointer to java byte array
	releaseArray(env, arrayRef, array);
	
	// update class members
	(*env)->SetLongField(env, this, last_captured_frame_sequence_fID, sequence);
	//Convert timeval to int64_t (hopefully) handling overflows
	(*env)->SetLongField(env, this, last_captured_frame_time_usec_fID, (jlong) (captureTime.tv_usec) + (jlong) (captureTime.tv_sec * UINT64_C(1000000)));
	(*env)->SetIntField(env, this, last_captured_frame_buffer_index_fID, buffer_index);

	return output_len;
}

/*
 * tell LIBVIDEO to stop the capture
 */
JNIEXPORT void JNICALL Java_au_edu_jcu_v4l4j_AbstractGrabber_stop(JNIEnv *e, jclass me, jlong object) {
	LOG_FN_ENTER();
	struct v4l4j_device *d = (struct v4l4j_device *) (uintptr_t) object;

	dprint(LOG_LIBVIDEO, "[LIBVIDEO] Calling stop_capture(dev: %s)\n", d->vdev->file);
	if((*d->vdev->capture->actions->stop_capture)(d->vdev) < 0) {
		dprint(LOG_V4L4J, "Error stopping capture\n");
		//don't throw an exception here...
		//if we do, FrameGrabber wont let us call delete
		//free_capture because its state will be stuck in capture...
	}
}

/*
 * free JPEG compressor
 * free LIBVIDEO (free_capture, free_capture_device)
 */
JNIEXPORT void JNICALL Java_au_edu_jcu_v4l4j_AbstractGrabber_doRelease(JNIEnv *e, jclass me, jlong object) {
	LOG_FN_ENTER();

	struct v4l4j_device *dev = (struct v4l4j_device *) (uintptr_t) object;

	release_format_converter(dev);

	(*dev->vdev->capture->actions->free_capture)(dev->vdev);

	free_capture_device(dev->vdev);
}



