#include <jpeglib.h>

void
jpeg_mem_src (j_decompress_ptr cinfo, u8 * buffer,
	unsigned long bufsize);

void
jpeg_mem_dest (j_compress_ptr cinfo, u8 ** outbuffer,
	unsigned long * outsize);
