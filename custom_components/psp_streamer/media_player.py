"""Confirmed playback state and the server's media catalogue."""
import json
from urllib.parse import quote

from homeassistant.components.media_player import (
    BrowseMedia, MediaPlayerEntity, MediaPlayerEntityFeature, MediaPlayerState, MediaType, MediaClass,
)
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity import DeviceInfo
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .api import ApiError
from .const import DOMAIN, FOLDER


async def async_setup_entry(hass, entry, async_add_entities):
    async_add_entities([Player(entry.runtime_data)])


class Player(CoordinatorEntity, MediaPlayerEntity):
    _attr_has_entity_name = True
    _attr_name = None
    _attr_supported_features = (MediaPlayerEntityFeature.PLAY | MediaPlayerEntityFeature.PAUSE |
        MediaPlayerEntityFeature.STOP | MediaPlayerEntityFeature.SEEK | MediaPlayerEntityFeature.PLAY_MEDIA |
        MediaPlayerEntityFeature.BROWSE_MEDIA | MediaPlayerEntityFeature.NEXT_TRACK |
        MediaPlayerEntityFeature.PREVIOUS_TRACK)

    def __init__(self, coordinator):
        super().__init__(coordinator)
        identity = coordinator.data['server_id']
        self._attr_unique_id = identity
        self._attr_device_info = DeviceInfo(identifiers={(DOMAIN, identity)}, name='PSP Streamer',
            manufacturer='PSPStreamer', model='PSP streaming client', configuration_url=coordinator.api.url)

    @property
    def available(self):
        return super().available and self.coordinator.data['online']

    @property
    def state(self):
        return MediaPlayerState(self.coordinator.data['state'])

    @property
    def media_content_id(self):
        return self.coordinator.data.get('id')

    @property
    def media_content_type(self):
        return MediaType.MUSIC if self.coordinator.data.get('kind') == 'audio' else MediaType.VIDEO

    @property
    def media_title(self):
        return self.coordinator.data.get('title') or None

    @property
    def media_artist(self):
        return self.coordinator.data.get('artist') or None

    @property
    def media_album_name(self):
        return self.coordinator.data.get('album') or None

    @property
    def media_duration(self):
        return self.coordinator.data.get('duration') or None

    @property
    def media_position(self):
        return self.coordinator.data.get('position')

    @property
    def media_position_updated_at(self):
        return self.coordinator.position_updated_at

    async def _command(self, action, **fields):
        if not self.available:
            raise HomeAssistantError('PSP Streamer is not connected')
        try:
            await self.coordinator.api.command(action, **fields)
        except ApiError as err:
            raise HomeAssistantError(str(err)) from err
        # Do not pretend the queued command has already executed on the PSP.
        await self.coordinator.async_request_refresh()

    async def async_media_play(self):
        await self._command('resume')

    async def async_media_pause(self):
        await self._command('pause')

    async def async_media_stop(self):
        await self._command('stop')

    async def async_media_seek(self, position):
        if self.coordinator.data.get('live'):
            raise HomeAssistantError('Live radio cannot seek')
        await self._command('seek', seconds=position)

    async def async_play_media(self, media_type, media_id, **kwargs):
        if media_type not in (DOMAIN, MediaType.MUSIC, MediaType.VIDEO, 'audio'):
            raise HomeAssistantError('Select a PSP Streamer library item')
        extra = kwargs.get('extra') or {}
        options = {key: extra[key] for key in ('audio', 'subtitle', 'audio_quality', 'video_fps', 'start') if key in extra}
        await self._command('play', id=media_id, **options)

    async def _skip(self, direction):
        if not self.available or not self.media_content_id:
            raise HomeAssistantError('No active media')
        try:
            following = await self.coordinator.api.request('/api/media-next/' + quote(self.media_content_id, safe=''),
                                                           params={'direction': direction})
            if following.get('id'):
                await self.async_play_media(DOMAIN, following['id'])
        except ApiError as err:
            raise HomeAssistantError(str(err)) from err

    async def async_media_next_track(self):
        await self._skip('next')

    async def async_media_previous_track(self):
        await self._skip('previous')

    async def async_browse_media(self, media_content_type=None, media_content_id=None):
        try:
            if media_content_id:
                if media_content_type != FOLDER:
                    raise ValueError('Not a folder')
                root, path = json.loads(media_content_id)
                if not isinstance(root, int) or not isinstance(path, str):
                    raise ValueError('Invalid folder')
            else:
                root, path = 0, ''
            listing = await self.coordinator.api.request('/api/library', params={'root': root, 'path': path}, timeout=60)
            root = listing['root']
            children = [BrowseMedia(title=f['name'], media_class=MediaClass.DIRECTORY,
                media_content_type=FOLDER, media_content_id=json.dumps([f.get('root', root), f['path']]),
                can_play=False, can_expand=True) for f in listing['folders']]
            children += [BrowseMedia(title=m['name'],
                media_class=MediaClass.MUSIC if m.get('kind') == 'audio' else MediaClass.VIDEO,
                media_content_type=DOMAIN, media_content_id=m['id'], can_play=True, can_expand=False)
                for m in listing['videos']]
            return BrowseMedia(title='PSP Streamer' if not path else path.rsplit('/', 1)[-1],
                media_class=MediaClass.DIRECTORY, media_content_type=FOLDER,
                media_content_id=json.dumps([root, path]), can_play=False, can_expand=True, children=children)
        except (ApiError, ValueError, TypeError, KeyError) as err:
            raise HomeAssistantError('Could not browse PSP Streamer') from err
