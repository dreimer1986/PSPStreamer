#ifndef PSP_STREAMER_THEME_LAYOUT_H
#define PSP_STREAMER_THEME_LAYOUT_H
/* Measured skin pixels. Right/bottom are exclusive in drawing code. */
enum {
    LCD_LEFT_X=31, LCD_LEFT_Y=27, LCD_LEFT_R=351, LCD_LEFT_B=155,
    LCD_RIGHT_X=372, LCD_RIGHT_Y=27, LCD_RIGHT_R=450, LCD_RIGHT_B=157,
    TV_LEFT_X=25, TV_LEFT_Y=59, TV_LEFT_R=535, TV_LEFT_B=293,
    TV_RIGHT_X=557, TV_RIGHT_Y=59, TV_RIGHT_R=693, TV_RIGHT_B=294
};
static inline int theme_spectrum_x(int tv,int bin) {
    int left=tv?TV_LEFT_X:LCD_LEFT_X,width=tv?TV_LEFT_R-TV_LEFT_X:LCD_LEFT_R-LCD_LEFT_X;
    return left+3+bin*width/12;
}
static inline int theme_spectrum_width(int tv) {
    return (tv?TV_LEFT_R-TV_LEFT_X:LCD_LEFT_R-LCD_LEFT_X)/12-6;
}
static inline int theme_text_edges(int tv,int x,int y,int *right,int *bottom) {
    int lx=tv?TV_LEFT_X:LCD_LEFT_X,ly=tv?TV_LEFT_Y:LCD_LEFT_Y;
    int lr=tv?TV_LEFT_R:LCD_LEFT_R,lb=tv?TV_LEFT_B:LCD_LEFT_B;
    int rx=tv?TV_RIGHT_X:LCD_RIGHT_X,ry=tv?TV_RIGHT_Y:LCD_RIGHT_Y;
    int rr=tv?TV_RIGHT_R:LCD_RIGHT_R,rb=tv?TV_RIGHT_B:LCD_RIGHT_B;
    if(x>=lx && x<lr && y>=ly && y<lb){*right=lr-3;*bottom=lb;return 1;}
    if(x>=rx && x<rr && y>=ry && y<(tv?310:170)){*right=rr-3;*bottom=rb;return 1;}
    return 0;
}
#endif
