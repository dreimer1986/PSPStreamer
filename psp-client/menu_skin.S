    .section .rodata
    .align 6
    .global receiver_skin
    .global receiver_skin_end
receiver_skin:
    .incbin "assets/menu_skin.raw"
receiver_skin_end:
    .align 6
    .global receiver_tv_skin
    .global receiver_tv_skin_end
receiver_tv_skin:
    .incbin "assets/menu_skin_tv.raw"
receiver_tv_skin_end:
