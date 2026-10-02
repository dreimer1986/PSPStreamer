/* SPDX-License-Identifier: MIT. Caller serializes these tiny state changes. */
#ifndef FUSA_SNAPSHOT_H
#define FUSA_SNAPSHOT_H
enum { FS_FREE, FS_WRITING, FS_READY, FS_READING };
typedef struct { int state[2]; unsigned sequence[2],format[2],serial; } FsSnapshots;
static int fs_snapshot_write(FsSnapshots *s)
{
    for(int i=0;i<2;i++)if(s->state[i]==FS_FREE){s->state[i]=FS_WRITING;return i;}
    for(int i=0;i<2;i++)if(s->state[i]==FS_READY){s->state[i]=FS_WRITING;return i;}
    return -1;
}
static void fs_snapshot_publish(FsSnapshots *s,int i,unsigned format)
{
    s->format[i]=format;s->sequence[i]=++s->serial;s->state[i]=FS_READY;
}
static int fs_snapshot_read(FsSnapshots *s)
{
    int i=s->state[0]==FS_READY?0:s->state[1]==FS_READY?1:-1;
    if(i==0&&s->state[1]==FS_READY&&s->sequence[1]>s->sequence[0])i=1;
    if(i>=0){s->state[i]=FS_READING;if(s->state[1-i]==FS_READY)s->state[1-i]=FS_FREE;}
    return i;
}
#endif
