/* Host-only hardware boundary for the real nxdk SDL window driver test. */
typedef struct {int width,height,bpp,refresh;} VIDEO_MODE;
VIDEO_MODE XVideoGetMode(void);
void *XVideoGetFB(void);
void XVideoFlushFB(void);
