/* Native vector controller diagram; scales with the theme at every output mode. */
static int help_page,help_return,help_resume;
static void help_draw(SDL_Color normal,SDL_Color selected){
    SDL_Point outline[]={{44,135},{64,104},{177,104},{197,135},{211,217},{189,226},{165,191},{77,191},{53,226},{31,217},{44,135}};
    color(115,155,180);SDL_RenderDrawLines(renderer,outline,sizeof(outline)/sizeof(*outline));
    SDL_Rect cross_h={51,150,35,11},cross_v={63,138,11,35};SDL_RenderFillRect(renderer,&cross_h);SDL_RenderFillRect(renderer,&cross_v);
    text_at("Y",158,123,25,selected);text_at("X",142,143,25,normal);
    text_at("B",177,143,25,normal);text_at("A",159,164,25,selected);
    text_at("LT",52,80,30,normal);text_at("RT",173,80,30,normal);
    text_at("B / S",99,115,53,normal);text_at("RS",137,193,35,selected);
    const char *titles[]={XL(HELP_LIBRARY_TITLE),XL(HELP_VIDEO_TITLE),XL(HELP_MUSIC_TITLE),XL(HELP_EDIT_TITLE)};
    const char *rows[4][5]={
        {XL(HELP_OPEN_SHORT),XL(HELP_MENU_SHORT),XL(HELP_ACTION_SHORT),XL(HELP_SEARCH_HINT),XL(HELP_EXIT_HINT)},
        {XL(HELP_VIDEO_SHORT),XL(HELP_SEEK_HINT),XL(HELP_CHAPTER_HINT),XL(HELP_FULL_HINT),XL(HELP_STOP_HINT)},
        {XL(HELP_MUSIC_X),XL(HELP_FULL_HINT),XL(HELP_VOLUME_SHORT),XL(HELP_SKIP_SHORT),XL(HELP_FLIGHT_HINT)},
        {XL(HELP_EDIT_MOVE),XL(HELP_EDIT_ACCEPT),XL(HELP_EDIT_ERASE),XL(HELP_EDIT_DONE),XL(HELP_EDIT_CANCEL)}};
    text_at(titles[help_page],227,65,298,selected);
    for(int i=0;i<5;i++)text_at(rows[help_page][i],227,104+i*29,298,normal);
    text_at(XL(HELP_PAGES),35,264,492,normal);
}
