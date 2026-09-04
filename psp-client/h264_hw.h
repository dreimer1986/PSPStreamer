#ifndef PSP_STREAMER_H264_HW_H
#define PSP_STREAMER_H264_HW_H

/* Hardware AVC probe and decoder.  This uses the PSP Media Engine through
 * sceMpeg; it is intentionally separate from the OpenH264 fallback. */
int h264_hw_init_from_annexb(const unsigned char *access_unit, int size);
int h264_hw_decode_annexb(const unsigned char *access_unit, int size, void *framebuffer);
void h264_hw_shutdown(void);
const char *h264_hw_last_step(void);

#endif
