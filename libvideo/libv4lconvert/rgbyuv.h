/*
 * All rights reserved, this is provided as-is, etc. See the lisence.
 * This file contains macros for YUV/YVU conversion to/from RGB/BGR.
 * Example (YUV => RGB):
 *     int u1 = UV2U1(u, v)
 *     int rg = UV2RG(u, v)
 *     int v1 = UV2V1(u, v)
 *     int _y = FIX_Y(y)
 * 
 *     int r = CLIP(_y + u1)
 *     int g = CLIP(_y - rg)
 *     int b = CLIP(_y + v1)
 * 
 * Example (RGB => YUV):
 *     int y = RGB2Y(r, g, b)
 *     int u = RGB2U(r, g, b)
 *     int v = RGB2V(r, g, b)
 * The utility of this file include:
 *  1) These conversions are done all over. These macros will replace all the conversion.
 *  2) Depending on the CPU/etc., the user can decide how to deal with the speed/accuracy tradeoff
 */
#ifndef __LIBV4LCONVERT_RGBYUV_H
#define __LIBV4LCONVERT_RGBYUV_H

//If the conversion quality is out of bounds, let's fall back to full float
//This should be pretty safe.
#if defined(CONVERSION_QUALITY) && (CONVERSION_QUALITY < 0 || CONVERSION_QUALITY > 3)
	#warning "Conversion quality out of bounds."
	#undef CONVERSION_QUALITY
#endif

#ifndef CONVERSION_QUALITY
	#pragma message "Defaulting CONVERSION_QUALITY to 3 (full float)"
	#define CONVERSION_QUALITY 3
#endif


//Clip the value to between 0 and 255
#define CLIP256(color)	(u8)({int _color = (color);(_color > 0xFF) ? 0xFF : ((_color < 0) ? 0 : _color);})
// Final in conversion, can be overwritten (for fixed-point)
#define CLIP_RGB(color)		CLIP256(color)
// Identity transformation, unless fixed point
#define FIX_Y(x)			(x)

// Default RGB => YUV macros
#define RGB2Y(r, g, b) ((u8) ((8453 * (r) + 16594 * (g) + 3223 * (b) + 524288) >> 15))
#define RGB2U(r, g, b) ((u8) ((-4878 * (signed) (r) - 9578 * (signed) (g) + 14456 * (signed) (b) + 4210688) >> 15))
#define RGB2V(r, g, b) ((u8) ((14456 * (r) - 12105 * (g) - 2351 * (b) + 4210688) >> 15))


#if CONVERSION_QUALITY == 0
	// Optimized for speed, at the expense of precision.
	// Might not be worth it nowdays if you have a co-processor or FPU
	//YUV => RGB
	#define UV2V1(u, v)		((((v) << 1) + (v)) >> 1)
	#define UV2RG(u, v)		(((u << 1) + (u) + ((v) << 2) + ((v) << 1)) >> 3)
	#define UV2U1(u, v)		((((u) << 7) + (u)) >> 6)
#elif CONVERSION_QUALITY == 1
	// Integer approximation. Middling speed
	//YUV => RGB
	#define UV2V1(u, v)		(((v) * 1436) >> 10)
	#define UV2RG(u, v)		(((u) * 352 + ((v) * 731)) >> 10)
	#define UV2U1(u, v)		(((u) * 1814) >> 10)
	
	//RGB => YUV
	#define RGB2Y(r, g, b) ((u8) ((8453 * (r) + 16594 * (g) + 3223 * (b) + 524288) >> 15))
	#define RGB2U(r, g, b) ((u8) ((-4878 * (signed) (r) - 9578 * (signed) (g) + 14456 * (signed) (b) + 4210688) >> 15))
	#define RGB2V(r, g, b) ((u8) ((14456 * (r) - 12105 * (g) - 2351 * (b) + 4210688) >> 15))
#elif CONVERSION_QUALITY == 2
	// Fixed-point arithmetic. Kinda fast, and should get you the precision you need
	// Can still be beat out by some FPU's, just because it requires more instructions
	//Number of bits to scale fixed-point by
	#define SCALEBITS 		10
	// 1/2, scaled appropriately
	//NOTE: I am not sure what standard this follows/approximates (definately not BT.601)
	//TODO: find out
	#define ONE_HALF		(1UL << (SCALEBITS - 1))
	//YUV => RGB
	#define FIX_Y(x)		((int)((x) * (1UL << SCALEBITS) + 0.5))
	#define UV2V1(u, v)		(FIX(1.40200) * v + ONE_HALF)
	#define UV2RG(u, v)		(FIX(0.34414) * u + FIX(0.71414) * v - ONE_HALF)
	#define UV2U1(u, v)		(FIX(1.77200) * u + ONE_HALF)
	#define CLIP_RGB(color)		CLIP256((color) >> SCALEBITS)
#elif CONVERSION_QUALITY == 3
	// Relatively slow conversion, but nice and accurate
	// If the processor has a fpu, these shouldn't be much worse
	// BT.601 Standard
	//YUV => RGB
	#define UV2V1(u, v)		((int) (0.00000f * (float) (u) + 1.13983f * (float) (v) + 0.5f))
	#define UV2RG(u, v)		((int) (0.39465f * (float) (u) + 0.58060f * (float) (v) - 0.5f))
	#define UV2U1(u, v)		((int) (2.03211f * (float) (u) + 0.00000f * (float) (v) + 0.5f))
	
	//RGB => YUV
	#define RGB2Y(r, g, b) ((int) (0.29900f * (float) (r) + 0.58700f * (float) (g) + 0.1140f * (float) (b)))
	#define RGB2U(r, g, b) ((int) (-.14713f * (float) (r) - 0.28886f * (float) (g) + 0.4360f * (float) (b)))
	#define RGB2V(r, g, b) ((int) (0.61500f * (float) (r) - 0.51499f * (float) (g) - 0.1001f * (float) (b)))
#endif

#endif