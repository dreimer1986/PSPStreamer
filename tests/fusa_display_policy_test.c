#include <assert.h>
#include "../psp-fusa-probe/display_policy.h"
int main(void)
{
    assert(fs_display_layer(0)==0&&fs_display_layer(1)==2);
    assert(fs_game_tv_layout(0x2d2,480,272));
    assert(fs_game_tv_layout(0x1d2,480,272)); /* Metal Slug scene transition. */
    assert(fs_repair_tv_layout(0x1d2,480,272,0x400000,2)); /* First transition. */
    assert(fs_repair_tv_layout(0x2d2,480,272,0x400000,2));
    assert(!fs_repair_tv_layout(0,480,272,0x400000,2));
    assert(!fs_repair_tv_layout(0x1d2,480,272,0x200000,2));
    assert(!fs_repair_tv_layout(0x1d2,480,272,0x400000,0));
    assert(!fs_game_tv_layout(0,480,272)); /* Never force LCD back to TV. */
    assert(!fs_game_tv_layout(0x1d2,720,480)); /* Already scaled, not a source. */
    assert(!fs_game_tv_layout(0x1d1,480,272)); /* No guessed interlace mode. */
    assert(fs_relocatable(0x27bdffe0)); /* addiu sp,sp,-32 */
    assert(fs_relocatable(0xafbf001c)); /* sw ra,28(sp) */
    assert(fs_relocatable(0x3c028800)); /* lui v0,... */
    assert(fs_relocatable(0x00801021)); /* move v0,a0 */
    assert(fs_relocatable(0));
    assert(!fs_relocatable(0x08000000)); /* j: existing hook */
    assert(!fs_relocatable(0x0c000000)); /* jal */
    assert(!fs_relocatable(0x10000000)); /* beq */
    assert(!fs_relocatable(0x04000000)); /* REGIMM */
    assert(!fs_relocatable(0x03e00008)); /* jr ra */
    assert(!fs_relocatable(0x0320f809)); /* jalr */
    assert(!fs_relocatable(0x0000000c)); /* syscall */
    assert(!fs_relocatable(0x40026000)); /* mfc0 */
    return 0;
}
