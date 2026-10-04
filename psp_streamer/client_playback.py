"""Playback telemetry for non-PSP clients; never consumes the PSP command queue."""
import re
import threading
import time


class PlaybackReports:
    def __init__(self):
        self.lock=threading.Lock()
        self.clients={}

    def accept(self, client, sequence):
        now=time.monotonic()
        self.clients={k:v for k,v in self.clients.items() if now-v[1]<3600}
        if sequence<=self.clients.get(client,(-1,0))[0]:return False
        if client not in self.clients and len(self.clients)>=128:
            del self.clients[min(self.clients,key=lambda k:self.clients[k][1])]
        self.clients[client]=(sequence,now)
        return True


def report(server, data):
    if not isinstance(data, dict):
        raise ValueError('Invalid playback report')
    token = data.get('id', '')
    if 'id_hex' in data:
        if not isinstance(data['id_hex'], str) or len(data['id_hex']) > 12288:
            raise ValueError('Invalid media identifier')
        token = bytes.fromhex(data['id_hex']).decode('utf-8')
    client = data.get('client', '')
    if not isinstance(client, str) or not re.fullmatch(r'(browser|xbox)-[a-zA-Z0-9-]{1,64}', client):
        raise ValueError('Invalid playback client')
    if not isinstance(token, str) or not token or len(token)>1536:
        raise ValueError('Invalid media identifier')
    state = data.get('state')
    if state not in ('playing', 'paused', 'stopped'):
        raise ValueError('Invalid playback state')
    sequence=data.get('sequence')
    if type(sequence) is not int or not 0<sequence<2**53:raise ValueError('Invalid playback sequence')
    position, duration = data.get('position'), data.get('duration')
    if any(type(x) is not int or not 0<=x<=604800000 for x in (position,duration)):
        raise ValueError('Invalid playback position')
    if token.startswith('radio.'):
        server.radio.get(token)
    else:
        server.library.decode(token)
    provider = server.plex if token.startswith('plex.') else server.jellyfin if token.startswith('jellyfin.') else None
    name=data.get('name','')
    if 'name_hex' in data:
        if not isinstance(data['name_hex'], str) or len(data['name_hex']) > 4096:
            raise ValueError('Invalid media name')
        name=bytes.fromhex(data['name_hex']).decode('utf-8',errors='replace')
    # Deliberately do not change player_status, stream_pauses, playlist.observe,
    # or remote_after: those belong to the PSP and may be in use concurrently.
    with server.client_reports.lock:
        if not server.client_reports.accept(client,sequence):return {'ok':True,'stale':True}
        if provider:provider.report(token, state, position, duration, client=client)
        server.comfort.report({'media':[token], 'state':[state], 'position':[str(position)],
            'duration':[str(duration)]}, {'title':str(name)[:128],
            'kind':'audio' if data.get('kind')=='audio' else 'video'})
    return {'ok':True}
