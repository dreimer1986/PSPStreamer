"""Authenticated, asynchronous server API; never poll the command mailbox."""
import asyncio
import base64
from urllib.parse import urlsplit

import aiohttp


class ApiError(Exception):
    """Unavailable, incompatible or failed request."""


class AuthError(ApiError):
    """Password rejected."""


def normalize_url(value):
    value = value.strip().rstrip('/')
    parsed = urlsplit(value)
    if (parsed.scheme not in {'http', 'https'} or not parsed.hostname or
            parsed.username or parsed.password or parsed.query or parsed.fragment):
        raise ValueError('Invalid server URL')
    _ = parsed.port  # Reject invalid ports before saving a config entry.
    return value


class Api:
    def __init__(self, session, url, password):
        self.session = session
        self.url = normalize_url(url)
        self.headers = {'Authorization': 'Basic ' + base64.b64encode(('psp:'+password).encode()).decode('ascii')}

    async def request(self, path, *, params=None, command=None, timeout=30):
        try:
            async with self.session.request('POST' if command is not None else 'GET',
                    self.url + path, params=params, json=command, headers=self.headers,
                    allow_redirects=False, timeout=aiohttp.ClientTimeout(total=timeout)) as response:
                if response.status in (401, 403):
                    raise AuthError('Authentication failed')
                if response.status != 200:
                    raise ApiError(f'Server returned HTTP {response.status}')
                result = await response.json()
                if not isinstance(result, dict):
                    raise ApiError('Invalid server response')
                return result
        except (aiohttp.ClientError, asyncio.TimeoutError, ValueError) as err:
            # Do not expose URLs, credentials or server response bodies in logs.
            raise ApiError('Could not communicate with PSP Streamer') from err

    async def status(self):
        data = await self.request('/api/player', timeout=10)
        if (data.get('api') != 1 or not isinstance(data.get('server_id'), str) or
                not isinstance(data.get('online'), bool) or
                data.get('state') not in {'idle', 'playing', 'paused', 'buffering'}):
            raise ApiError('Update the PSP Streamer server')
        return data

    async def command(self, action, **fields):
        return await self.request('/api/remote/command', command={'action': action, **fields}, timeout=60)
