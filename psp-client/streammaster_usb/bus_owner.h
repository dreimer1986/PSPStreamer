/* SPDX-License-Identifier: GPL-2.0-or-later
 * The XMB can have an idle bus running without an active USB function.
 * Borrow that bus, never stop a bus started by another component. */
static int sm_bus_owned;
static int sm_bus_start(void) {
    if(sceUsbGetDrvState(PSP_USBBUS_DRIVERNAME)==1)return 0;
    int rc=sceUsbStart(PSP_USBBUS_DRIVERNAME,0,NULL);
    if(rc>=0)sm_bus_owned=1;
    else if((unsigned)rc==0x80243001U &&
            sceUsbGetDrvState(PSP_USBBUS_DRIVERNAME)==1)return 0;
    return rc;
}
static void sm_bus_stop(void) {
    if(sm_bus_owned && sceUsbStop(PSP_USBBUS_DRIVERNAME,0,NULL)>=0)sm_bus_owned=0;
}
