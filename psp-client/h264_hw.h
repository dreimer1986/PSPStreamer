#ifndef PSP_STREAMER_H264_HW_H
#define PSP_STREAMER_H264_HW_H

/* Hardware AVC probe and decoder.  This uses the PSP Media Engine through
 * sceMpeg; it is intentionally separate from the OpenH264 fallback. */
int h264_hw_init_from_annexb(const unsigned char *access_unit, int size);
int h264_hw_decode_annexb(const unsigned char *access_unit, int size, void *framebuffer);
/* Configure before init: firmware AVC mode 5 is required for 720x480. */
void h264_hw_set_output_layout(int stride, int height, int mpeg_mode);
void h264_hw_shutdown(void);
const char *h264_hw_last_step(void);

#endif
