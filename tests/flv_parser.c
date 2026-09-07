#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../psp-client/flv.h"
#include "../psp-client/audio_lease.h"

static void test_output_lifecycle(void) {
    AudioLease lease = {-1};
    int i, released;
    /* Full ring under producer pressure, with an occasional DAC drain.
     * Never recycle the buffer just submitted; release each slot once. */
    for (i = 0; i < 10000; ++i) {
        int slot = i % 8, previous = lease.held;
        assert(previous != slot);
        released = audio_lease_submit(&lease, slot);
        assert(released == previous && lease.held == slot && released != slot);
        if (i % 13 == 0) {
            assert(audio_lease_drain(&lease) == slot);
            assert(audio_lease_drain(&lease) == -1);
        }
    }
    assert(audio_lease_drain(&lease) >= 0); /* final EOF without next packet */
    assert(audio_lease_drain(&lease) == -1);
    /* Startup cannot deadlock: prepare, show first, release DAC, then sync. */
    assert(pts_presentation_status(0, 0, 1, 0, 0, 25, 50, 0) == 0);
    assert(pts_presentation_status(1, 0, 1, 0, 0, 25, 50, 0) == 1);
    assert(pts_presentation_status(1, 1, 1, 0, 0, 75, 50, 0) == 0);
    assert(pts_presentation_status(1, 1, 1, 1, 0, 75, 50, 0) == 1);
    /* A 600-ms prepare stall makes a formerly timely picture late. Re-read
     * audio at presentation, discard the picture, never rewind audio. */
    assert(pts_presentation_status(1, 1, 1, 1, 1000, 1000, 50, 0) == 1);
    assert(pts_presentation_status(1, 1, 1, 1, 1600, 1000, 50, 0) == 2);
    assert(pts_presentation_status(1, 1, 1, 1, 1000, 1600, 50, 0) == 0);
    assert(pts_presentation_status(1, 1, 0, 0, 0, 500, 50, 499) == 0);
    assert(pts_presentation_status(1, 1, 0, 0, 0, 500, 50, 500) == 1);
}

int main(int argc, char **argv) {
    test_output_lifecycle();
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
