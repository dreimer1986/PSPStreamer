"""UI setup, reconfiguration and password recovery."""
import voluptuous as vol

from homeassistant import config_entries
from homeassistant.const import CONF_PASSWORD
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers import selector

from .api import Api, ApiError, AuthError, normalize_url
from .const import CONF_URL, DOMAIN


class ConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

    async def _form(self, step, user_input):
        entry = (self._get_reauth_entry() if step == 'reauth_confirm' else
                 self._get_reconfigure_entry() if step == 'reconfigure' else None)
        errors = {}
        if user_input is not None:
            data = dict(user_input)
            data.setdefault(CONF_PASSWORD, '')
            try:
                data[CONF_URL] = normalize_url(data[CONF_URL])
                api = Api(async_get_clientsession(self.hass), data[CONF_URL], data.get(CONF_PASSWORD, ''))
                status = await api.status()
            except AuthError:
                errors['base'] = 'invalid_auth'
            except (ApiError, ValueError):
                errors['base'] = 'cannot_connect'
            else:
                await self.async_set_unique_id(status['server_id'])
                if entry:
                    self._abort_if_unique_id_mismatch()
                    return self.async_update_reload_and_abort(entry, data_updates=data)
                self._abort_if_unique_id_configured()
                return self.async_create_entry(title='PSP Streamer', data=data)
        default_url = (user_input or {}).get(CONF_URL, entry.data[CONF_URL] if entry else 'http://homeassistant.local:8091')
        schema = vol.Schema({vol.Required(CONF_URL, default=default_url): str,
            vol.Optional(CONF_PASSWORD, default=''): selector.TextSelector(
                selector.TextSelectorConfig(type=selector.TextSelectorType.PASSWORD))})
        return self.async_show_form(step_id=step, data_schema=schema, errors=errors)

    async def async_step_user(self, user_input=None):
        return await self._form('user', user_input)

    async def async_step_reconfigure(self, user_input=None):
        return await self._form('reconfigure', user_input)

    async def async_step_reauth(self, entry_data):
        return await self.async_step_reauth_confirm()

    async def async_step_reauth_confirm(self, user_input=None):
        return await self._form('reauth_confirm', user_input)
