#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../psp-client/flv.h"

int main(int argc, char **argv) {
    unsigned char header[13], tag[11], prev[4];
    unsigned char body[FLV_MAX_VIDEO], out[FLV_MAX_VIDEO];
    FlvAvc avc = {{0}, 0, 0};
    FILE *f, *video, *audio;
    int saw_end = 0;
    assert(pts_avsync(1000, 1200, 50) == 0);
    assert(pts_avsync(1200, 1000, 50) == 2);
    assert(pts_avsync(1000, 1050, 50) == 1);
    /* A half-hour timestamp still uses the same bounded decision, not a
     * frame-count-derived clock or accumulated fractional-frame rounding. */
    assert(pts_avsync(1800000, 1800050, 50) == 1);
    assert(pts_avsync(1800000, 1800101, 50) == 0);
    assert(pts_avsync(1800101, 1800000, 50) == 2);
    /* Extended DTS, signed composition offset, malformed config. */
    memset(tag, 0, sizeof(tag)); memset(body, 0, 5);
    tag[7] = 1; tag[6] = 10; body[2] = body[3] = 255; body[4] = 251;
    assert(flv_pts(tag, body) == 16777221);
    assert(flv_config(&avc, body, 5) < 0);
    if (argc != 4) return 2;
    f = fopen(argv[1], "rb"); video = fopen(argv[2], "wb"); audio = fopen(argv[3], "wb");
    assert(f && video && audio);
    assert(fread(header, 1, 13, f) == 13 && !memcmp(header, "FLV\1", 4));
    while (fread(tag, 1, 11, f) == 11) {
        unsigned int size = flv_u24(tag + 1);
        int n, pts = flv_u24(tag + 4) | ((unsigned int)tag[7] << 24);
        assert(size <= sizeof(body));
        assert(fread(body, 1, size, f) == size && fread(prev, 1, 4, f) == 4);
        assert(flv_u32(prev) == size + 11);
        if (tag[0] == 9 && body[1] == 0) assert(!flv_config(&avc, body + 5, size - 5));
        if (tag[0] == 9 && body[1] == 2) saw_end = 1;
        if (tag[0] == 9 && body[1] == 1) {
            n = flv_annexb(&avc, body + 5, size - 5, out, sizeof(out));
            assert(n > 0);
            assert(flv_annexb(&avc, body + 5, size - 5, out, 1) < 0);
            assert(flv_annexb(&avc, body + 5, size - 6, out, sizeof(out)) < 0);
            assert(flv_annexb(&avc, body + 5, size - 5, out, sizeof(out)) == n);
            printf("video %d\n", flv_pts(tag, body));
            fwrite(out, 1, n, video);
        }
        if (tag[0] == 8) {
            static const int rates[] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320};
            const unsigned char *mp3 = body + 1;
            assert(body[0] >> 4 == 2);
            assert((mp3[1] & 0xfe) == 0xfa && (mp3[2] & 12) == 0);
            assert(size - 1 == (unsigned int)(144000 * rates[mp3[2] >> 4] / 44100 + ((mp3[2] >> 1) & 1)));
            printf("audio %d\n", pts);
            fwrite(body + 1, 1, size - 1, audio);
        }
    }
    assert(saw_end);
    fclose(f); fclose(video); fclose(audio);
    return 0;
}
