"""One lightweight status poll per server, independent of the PSP workers."""
from datetime import timedelta
import logging

from homeassistant.exceptions import ConfigEntryAuthFailed
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed
from homeassistant.util import dt as dt_util

from .api import ApiError, AuthError
from .const import DOMAIN


class PlayerCoordinator(DataUpdateCoordinator):
    def __init__(self, hass, entry, api):
        super().__init__(hass, logging.getLogger(__name__), name=DOMAIN,
                         config_entry=entry, update_interval=timedelta(seconds=2))
        self.api = api
        self.position_updated_at = None

    async def _async_update_data(self):
        try:
            data = await self.api.status()
        except AuthError as err:
            raise ConfigEntryAuthFailed('PSP Streamer password rejected') from err
        except ApiError as err:
            raise UpdateFailed(str(err)) from err
        age = data.get('age')
        self.position_updated_at = dt_util.utcnow() - timedelta(seconds=age or 0)
        return data
