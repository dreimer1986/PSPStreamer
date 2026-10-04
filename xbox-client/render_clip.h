/* The pinned SDL software backend needs a viewport in each clip batch.
 * Requires SDL_HINT_RENDER_BATCHING=1 before renderer creation. */
static void xbox_render_clip(SDL_Renderer *renderer,const SDL_Rect *rect){
    SDL_RenderSetViewport(renderer,NULL);
    SDL_RenderSetClipRect(renderer,rect);
}
