/* GUI input uses the same sampling path as the physical controller. Network
 * work is asynchronous; only the UI thread consumes events or edits fields. */
static void input_remote_tick(SceCtrlData *pad);
static void input_remote_stop(void);
static void input_text_begin(int capacity,int secret);
static void input_text_end(void);
static int input_text_take(char *destination,int capacity);
static int input_read(SceCtrlData *pads,int count) {
    int result=sceCtrlReadBufferPositive(pads,count);
    if(result>0)input_remote_tick(&pads[0]);
    return result;
}
static int input_peek(SceCtrlData *pads,int count) {
    int result=sceCtrlPeekBufferPositive(pads,count);
    if(result>0)input_remote_tick(&pads[0]);
    return result;
}
#define sceCtrlReadBufferPositive input_read
#define sceCtrlPeekBufferPositive input_peek
