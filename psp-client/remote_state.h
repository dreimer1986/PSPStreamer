/* A new server process starts a new sequence space. Leave pending Play for
 * the idle dispatcher, including when recovering from a server restart. */
static char remote_session[40];
static int remote_state_reset(const char *reply, int *sequence) {
    char session[40];
    int changed=0, next=json_integer(reply,"seq",*sequence);
    if(json_value(reply,"session",session,sizeof(session))) {
        changed=remote_session[0] && strcmp(remote_session,session);
        strcpy(remote_session,session);
    }
    if(changed || next<*sequence) {
        *sequence=remote_control_sequence=0;
        return 1;
    }
    return 0;
}
