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

#ifndef H_LIBVIDEO_LOG
#define H_LIBVIDEO_LOG

#include <stdio.h>		//for fprintf
#include <stdlib.h>		//for malloc
#include <string.h>		//for memset
#include <sys/time.h>

#include "libvideo.h"
#include "utils.h"

#define info(format, ...) do {\
				fprintf (stderr, "[ %s:%d ] " format, __FILE__, __LINE__, ## __VA_ARGS__);\
				fflush(stderr);\
				} while(0)

//debug stuff
#ifdef DEBUG

#define LIBVIDEO_LOG_INFO 			(1 << 0)
#define LIBVIDEO_LOG_DEBUG	 		(1 << 1)	//once / twice per call
#define LIBVIDEO_LOG_DEBUG1 		(1 << 2)	//for / while loops
#define LIBVIDEO_LOG_DEBUG2 		(1 << 3)	//many times per seconds
#define LIBVIDEO_LOG_ERR 			(1 << 4)	//error / quirks
#define LIBVIDEO_LOG_MEMORY			(1 << 5)	// Memory allocation/etc.
#define LIBVIDEO_LOG_ALL			(LIBVIDEO_LOG_INFO | LIBVIDEO_LOG_DEBUG | LIBVIDEO_LOG_DEBUG1 | LIBVIDEO_LOG_DEBUG2 | LIBVIDEO_LOG_ERR | LIBVIDEO_LOG_MEMORY)

#define LIBVIDEO_SOURCE_VIDDEV		(1 << 0)
#define LIBVIDEO_SOURCE_QRY			(1 << 1)
#define LIBVIDEO_SOURCE_CAP			(1 << 2)
#define LIBVIDEO_SOURCE_CTRL		(1 << 3)
#define LIBVIDEO_SOURCE_MALLOC		(1 << 4)
#define LIBVIDEO_SOURCE_DRV_PROBE	(1 << 5)
#define LIBVIDEO_SOURCE_TUNER		(1 << 6)
#define LIBVIDEO_SOURCE_PROCESSING	(1 << 7)
#define LIBVIDEO_SOURCE_CONTROL		(1 << 8)
#define LIBVIDEO_SOURCE_CONVERT		(1 << 9)
#define LIBVIDEO_SOURCE_ALL 		(LIBVIDEO_SOURCE_VIDDEV | LIBVIDEO_SOURCE_QRY | LIBVIDEO_SOURCE_CAP |\
							 		LIBVIDEO_SOURCE_CTRL | LIBVIDEO_SOURCE_MALLOC | LIBVIDEO_SOURCE_DRV_PROBE | \
									LIBVIDEO_SOURCE_TUNER | LIBVIDEO_SOURCE_PROCESSING | LIBVIDEO_SOURCE_CONTROL | LIBVIDEO_SOURCE_CONVERT)




//default log levels and sources
#ifndef LIBVIDEO_LOG_LEVEL
#define LIBVIDEO_LOG_LEVEL 			LIBVIDEO_LOG_ALL
#endif

#ifndef LIBVIDEO_LOG_SOURCE
#define LIBVIDEO_LOG_SOURCE 		LIBVIDEO_SOURCE_ALL
#endif

//Print with origin information
#define dprint(source, level, format, ...) \
	do {\
		if(((source) & LIBVIDEO_LOG_SOURCE) && ((level) & LIBVIDEO_LOG_LEVEL)){\
			fprintf (stderr, "[%s:%d %s] " format,\
				__FILE__, __LINE__, __PRETTY_FUNCTION__, ## __VA_ARGS__\
				);\
			fflush(stderr);\
		}\
	} while(0)

#define dprint_v4l2_control(ctrl)\
	do { \
		dprint(LIBVIDEO_SOURCE_CTRL, LIBVIDEO_LOG_DEBUG1, \
				"CTRL: control id: 0x%x - name: %s - min: %d - max: %d - step: %d - type: %d (%s) - flags: %d (%s%s%s%s%s%s%s)\n", \
				ctrl.v4l2_ctrl->id, (char *) &(ctrl.v4l2_ctrl->name), ctrl.v4l2_ctrl->minimum, ctrl.v4l2_ctrl->maximum,\
				ctrl.v4l2_ctrl->step, ctrl.v4l2_ctrl->type, \
				ctrl.v4l2_ctrl->type == V4L2_CTRL_TYPE_INTEGER ? "Integer" :  \
				ctrl.v4l2_ctrl->type == V4L2_CTRL_TYPE_BOOLEAN ? "Boolean" :  \
				ctrl.v4l2_ctrl->type == V4L2_CTRL_TYPE_MENU ? "Menu" :  \
				ctrl.v4l2_ctrl->type == V4L2_CTRL_TYPE_BUTTON ? "Button" : \
				ctrl.v4l2_ctrl->type == V4L2_CTRL_TYPE_INTEGER64 ? "Integer64" :  \
				ctrl.v4l2_ctrl->type == V4L2_CTRL_TYPE_CTRL_CLASS ? "Class" :\
				ctrl.v4l2_ctrl->type == V4L2_CTRL_TYPE_STRING ? "String" :\
				ctrl.v4l2_ctrl->type == V4L2_CTRL_TYPE_BITMASK ? "Bitmask" :\
				ctrl.v4l2_ctrl->type == V4L2_CTRL_TYPE_INTEGER_MENU ? "IntMenu" : "UNKNOWN TYPE",\
				ctrl.v4l2_ctrl->flags, \
				ctrl.v4l2_ctrl->flags & V4L2_CTRL_FLAG_DISABLED ? "Disabled " : "", \
				ctrl.v4l2_ctrl->flags & V4L2_CTRL_FLAG_GRABBED ? "Grabbed " : "", \
				ctrl.v4l2_ctrl->flags & V4L2_CTRL_FLAG_READ_ONLY ? "ReadOnly " : "", \
				ctrl.v4l2_ctrl->flags & V4L2_CTRL_FLAG_UPDATE ? "Update " : "", \
				ctrl.v4l2_ctrl->flags & V4L2_CTRL_FLAG_INACTIVE ? "Inactive " : "", \
				ctrl.v4l2_ctrl->flags & V4L2_CTRL_FLAG_SLIDER ? "slider " : "",\
				ctrl.v4l2_ctrl->flags & V4L2_CTRL_FLAG_WRITE_ONLY ? "write-only" : ""); \
		for(unsigned int i=0; i<ctrl.count_menu; i++) {\
			if (ctrl.v4l2_ctrl->type==V4L2_CTRL_TYPE_MENU)\
				dprint(LIBVIDEO_SOURCE_CTRL, LIBVIDEO_LOG_DEBUG1, "   Menu %d - index: %d - '%s'\n", i, ctrl.v4l2_menu[i].index, ctrl.v4l2_menu[i].name);\
			else\
				dprint(LIBVIDEO_SOURCE_CTRL, LIBVIDEO_LOG_DEBUG1, "   Menu %d - index: %d - '%lld'\n", i, ctrl.v4l2_menu[i].index, ctrl.v4l2_menu[i].value);\
		}\
	} while(0);
#else  //if not DEBUG

#define dprint(source, level, format, ...) UNUSED(0)
#define dprint_v4l2_control(ctrl) UNUSED(ctrl)
#endif // if DEBUG

//#define SHOW_CONVERSION_TIMING
#if defined(DEBUG) || defined(SHOW_CONVERSION_TIMING)
#define START_TIMING	\
	struct timeval start, end;\
	gettimeofday(&start, NULL);

#ifdef DEBUG
	#define PRINT(...) dprint(LIBVIDEO_SOURCE_CAP, LIBVIDEO_LOG_DEBUG2, ## __VA_ARGS__)
#else
	#define PRINT(...) fprintf(stdout, ## __VA_ARGS__)
#endif
#define END_TIMING(str_prefix)	\
	gettimeofday(&end, NULL);\
	timersub(&end, &start, &start);\
	PRINT(str_prefix " %llu us\n", (unsigned long long) (start.tv_sec * 1000000 + start.tv_usec));\
	fflush(stdout);

#else
#define START_TIMING UNUSED(0)
#define END_TIMING(...) UNUSED(0)
#endif



#define XMALLOC(var, type, size)	\
	do { \
		(var) = (type) malloc((size));\
		if (!(var)) {\
			fprintf(stderr, "[%s:%d %s] MEMALLOC: OUT OF MEMORY Cant allocate %lu bytes\n", __FILE__, __LINE__, __PRETTY_FUNCTION__, (long unsigned int) (size));\
			fflush(stderr);\
		} else {\
			CLEAR_ARR((var), (size)); \
			dprint(LIBVIDEO_SOURCE_MALLOC, LIBVIDEO_LOG_MEMORY, "[MEMALLOC]: allocating %lu bytes of type %s for var %s (%p)\n", (long unsigned int) (size), #type, #var, (var));\
		}\
	} while (0)

#define XREALLOC(var, type, size)	\
	do { \
		int should_clear = var == NULL ? 1 : 0;\
		var = (type) realloc(var, (size)); \
		if (!var) {\
			fprintf(stderr, "[%s:%d %s] REALLOC: OUT OF MEMORY Cant reallocate %lu bytes\n", __FILE__, __LINE__, __PRETTY_FUNCTION__, (long unsigned int)  (size));\
			fflush(stderr);\
		} else { \
			if (should_clear) {\
				CLEAR_ARR((var), (size)); \
				dprint(LIBVIDEO_SOURCE_MALLOC, LIBVIDEO_LOG_MEMORY, "[REALLOC]: Allocating %lu bytes of type %s for var %s (%p)\n", (long unsigned int) (size), #type, #var, (var));\
			} else {\
				dprint(LIBVIDEO_SOURCE_MALLOC, LIBVIDEO_LOG_ALL, "REALLOC: re-allocating %lu bytes of type %s for var %s (%p).\n", (long unsigned int) (size), #type, #var, (var));\
			} \
		}\
	} while (0)

#define XFREE(var)	\
	do {\
		dprint(LIBVIDEO_SOURCE_MALLOC, LIBVIDEO_LOG_MEMORY, "MEMALLOC: freeing memory for var %s (%p).\n", #var, (var)); \
		if ((var)) { \
			free((var)); \
		} else { \
			dprint(LIBVIDEO_SOURCE_MALLOC, LIBVIDEO_LOG_ALL, "MEMALLOC: Trying to free a NULL pointer.\n");\
		} \
		var = NULL;\
	} while (0)

#define PRINT_MEA_CULPA() info("This is most likely a bug in v4l4j. Please let the author know about this issue. See README file or create an issue at github.com/mailmindlin/v4l4j.\n");
#define PRINT_REPORT_ERROR() info("Please let the author know about this error.\nSee the ISSUES section in the libvideo README file or create an issue at github.com/mailmindlin/v4l4j.\n");
#endif
