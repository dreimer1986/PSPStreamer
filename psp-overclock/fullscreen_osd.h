/* SPDX-License-Identifier: MIT
 * Optional kernel-only text mailbox. No imported callbacks or pixel buffers. */
#ifndef FS_OSD_API_H
#define FS_OSD_API_H
#define FS_OSD_DEVICE "fusafullscreen:"
#define FS_OSD_STATUS 0x46530001u
#define FS_OSD_TEXT 0x46530002u
typedef struct { unsigned version,slot,visible;char lines[3][40]; } FsOsdText;
static inline int fs_osd_message_valid(const FsOsdText *text)
{return text->version==1&&text->slot<2&&text->visible<=1;}
#ifndef FS_OSD_SERVER
static inline int fs_osd_active(void)
{return sceIoDevctl(FS_OSD_DEVICE,FS_OSD_STATUS,NULL,0,NULL,0)==1;}
static inline void fs_osd_publish(unsigned slot,const char lines[3][40],int visible)
{
    FsOsdText text={.version=1,.slot=slot,.visible=visible!=0};
    if(lines)memcpy(text.lines,lines,sizeof(text.lines));
    sceIoDevctl(FS_OSD_DEVICE,FS_OSD_TEXT,&text,sizeof(text),NULL,0);
}
#endif
#endif
