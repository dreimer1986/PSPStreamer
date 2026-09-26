/* Visibility only; never changes clocks, hooks or suspend policy. */
static unsigned long long oc_overlay_deadline(unsigned long long until,
        unsigned long long now,int toggle,int always,int active) {
    if(always && active)return ~0ULL;
    if(toggle)until=until?0:now+5000000ULL;
    return until && now>=until?0:until;
}
