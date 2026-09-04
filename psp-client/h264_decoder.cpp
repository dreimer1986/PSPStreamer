#include <string.h>

#include "codec/api/wels/codec_api.h"

#include "h264_decoder.h"

static ISVCDecoder *decoder;

extern "C" int h264_decoder_init(void) {
    SDecodingParam params;
    long result;
    if (decoder) return 0;
    result = WelsCreateDecoder(&decoder);
    if (result != 0 || !decoder) return -1;
    memset(&params, 0, sizeof(params));
    params.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
    params.eEcActiveIdc = ERROR_CON_DISABLE;
    params.bParseOnly = false;
    result = decoder->Initialize(&params);
    if (result != 0) {
        WelsDestroyDecoder(decoder);
        decoder = NULL;
        return -2;
    }
    {
        int threads = 0;
        int quiet = 0;
        decoder->SetOption(DECODER_OPTION_NUM_OF_THREADS, &threads);
        decoder->SetOption(DECODER_OPTION_TRACE_LEVEL, &quiet);
    }
    return 0;
}

extern "C" int h264_decoder_decode(const unsigned char *data, int size,
                        const unsigned char **y, const unsigned char **u,
                        const unsigned char **v, int *y_stride,
                        int *uv_stride, int *width, int *height) {
    unsigned char *planes[3] = {0, 0, 0};
    SBufferInfo info;
    DECODING_STATE state;
    if (!decoder || !data || size <= 0) return -1;
    memset(&info, 0, sizeof(info));
    state = decoder->DecodeFrameNoDelay(data, size, planes, &info);
    if (state != dsErrorFree && state != dsFramePending) return -2;
    if (info.iBufferStatus != 1 || !planes[0] || !planes[1] || !planes[2]) return 0;
    *y = planes[0]; *u = planes[1]; *v = planes[2];
    *y_stride = info.UsrData.sSystemBuffer.iStride[0];
    *uv_stride = info.UsrData.sSystemBuffer.iStride[1];
    *width = info.UsrData.sSystemBuffer.iWidth;
    *height = info.UsrData.sSystemBuffer.iHeight;
    return 1;
}

extern "C" void h264_decoder_shutdown(void) {
    if (!decoder) return;
    decoder->Uninitialize();
    WelsDestroyDecoder(decoder);
    decoder = NULL;
}
