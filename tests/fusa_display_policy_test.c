#include <assert.h>
#include "../psp-fusa-probe/display_policy.h"
int main(void)
{
    assert(fs_display_layer(0)==0&&fs_display_layer(1)==2);
    assert(fs_game_tv_layout(0x2d2,480,272));
    assert(fs_game_tv_layout(0x1d2,480,272)); /* Metal Slug scene transition. */
    assert(fs_redirect_mode(1,0,0,0,480,272)); /* Game asks for LCD-shaped geometry. */
    assert(fs_redirect_mode(1,0,0,0x1d2,480,272));
    assert(fs_redirect_mode(1,0,0,0x2d2,480,272));
    assert(!fs_redirect_mode(1,0,1,0,480,272)); /* Deliberate Screen key. */
    assert(!fs_redirect_mode(1,1,0,0,480,272)); /* Plugin restore/DVE calls. */
    assert(!fs_redirect_mode(0,0,0,0,480,272));
    assert(!fs_redirect_mode(1,0,0,0x1d1,480,272));
    assert(!fs_redirect_mode(1,0,0,0x1d2,720,480));
    assert(fs_report_game_mode(1,0,0));
    assert(!fs_report_game_mode(0,0,0)); /* Restore/suspend/inactive. */
    assert(!fs_report_game_mode(1,1,0)); /* Worker sees physical output. */
    assert(!fs_report_game_mode(1,0,-1)); /* Pointer/error result preserved. */
    assert(fs_layer_route(1,0,0)==FS_SYSTEM);
    assert(fs_layer_route(1,0,2)==FS_GAME);
    assert(fs_layer_route(1,0,1)==FS_FORWARD);
    assert(fs_layer_route(1,1,0)==FS_FORWARD);
    assert(fs_layer_route(1,1,2)==FS_FORWARD);
    assert(fs_layer_route(0,0,0)==FS_FORWARD);
    assert(fs_layer_route(0,0,2)==FS_FORWARD);
    assert(fs_snapshot_layout_valid(5,5)); /* Normal swaps keep layout. */
    assert(!fs_snapshot_layout_valid(5,6)); /* Mode/format/stride changed. */
    assert(!fs_snapshot_layout_valid(5,7)); /* Changed and changed back. */
    assert(fs_output_due(0,0,0));
    assert(!fs_output_due(1,10,10));
    assert(!fs_output_due(1,10,11));
    assert(fs_output_due(1,10,12));
    assert(fs_output_due(1,10,15)); /* Missed slots do not require catch-up. */
    assert(!fs_output_due(1,0xffffffffU,0));
    assert(fs_output_due(1,0xffffffffU,1));
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
