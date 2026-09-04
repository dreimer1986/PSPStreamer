#ifndef PSP_STREAMER_H264_DECODER_H
#define PSP_STREAMER_H264_DECODER_H

/* C interface around the C++ OpenH264 API. Input is one complete Annex-B
 * access unit; output planes remain owned by the decoder until the next call. */
#ifdef __cplusplus
extern "C" {
#endif
int h264_decoder_init(void);
int h264_decoder_decode(const unsigned char *data, int size,
                        const unsigned char **y, const unsigned char **u,
                        const unsigned char **v, int *y_stride,
                        int *uv_stride, int *width, int *height);
void h264_decoder_shutdown(void);
#ifdef __cplusplus
}
#endif

#endif
