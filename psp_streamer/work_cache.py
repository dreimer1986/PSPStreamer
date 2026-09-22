"""Bounded, synchronous single-flight cache; failures are never retained."""
from collections import OrderedDict
import threading
import time


class WorkCache:
    def __init__(self, entries=32, jobs=8, timeout=40):
        self.entries, self.jobs, self.timeout = entries, jobs, timeout
        self.lock = threading.Lock()
        self.values = OrderedDict()
        self.pending = {}

    def get(self, key, load, ttl=120, cache_if=lambda value: True):
        with self.lock:
            cached = self.values.get(key)
            if cached and cached[0] > time.monotonic():
                self.values.move_to_end(key)
                return cached[1]
            job = self.pending.get(key)
            owner = job is None
            if owner:
                if len(self.pending) >= self.jobs:
                    raise ValueError('Preparation busy; retry shortly')
                job = dict(event=threading.Event())
                self.pending[key] = job
        if not owner:
            if not job['event'].wait(self.timeout):
                raise ValueError('Preparation still running; retry shortly')
            if 'error' in job:
                raise job['error']
            return job['value']
        try:
            value = load()
            with self.lock:
                job['value'] = value
                if cache_if(value):
                    self.values[key] = (time.monotonic()+ttl, value)
                    self.values.move_to_end(key)
                    while len(self.values) > self.entries:
                        self.values.popitem(last=False)
            return value
        except BaseException as error:
            job['error'] = error
            raise
        finally:
            with self.lock:
                self.pending.pop(key, None)
                job['event'].set()
