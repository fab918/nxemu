"""Temporary per-executable NVIDIA disk-cache policy for this POC only.

ABI/constants: NVIDIA public nvapi.h, nvapi_interface.h and NvApiDriverSettings.h.
https://github.com/NVIDIA/nvapi
Does not change the global profile or delete any driver cache files.
"""
import ctypes as c
import json
from pathlib import Path
import sys

U32, Handle, Text = c.c_uint32, c.c_void_p, c.c_wchar * 2048
NAME = 'NXEmu Shader POC 20261022'
APP = 'nxemu-shader-poc.exe'
SETTING = 0x00198FFF  # PS_SHADERDISKCACHE_ID, OFF=0


class Value(c.Union):
    _pack_ = 4
    _fields_ = [('dword', U32), ('storage', c.c_ubyte * 4100)]


class Setting(c.Structure):
    _pack_ = 4
    _fields_ = [('version', U32), ('name', Text), ('id', U32), ('type', U32),
                ('location', U32), ('current_predefined', U32), ('predefined_valid', U32),
                ('predefined', Value), ('current', Value)]


class Profile(c.Structure):
    _fields_ = [('version', U32), ('name', Text), ('gpu_support', U32),
                ('predefined', U32), ('apps', U32), ('settings', U32)]


class Application(c.Structure):
    _fields_ = [('version', U32), ('predefined', U32), ('name', Text),
                ('friendly', Text), ('launcher', Text)]


def versioned(cls):
    value = cls()
    value.version = c.sizeof(cls) | (1 << 16)
    return value


class Driver:
    def __init__(self):
        self.dll = c.WinDLL('nvapi64.dll')
        self.dll.nvapi_QueryInterface.argtypes = [U32]
        self.dll.nvapi_QueryInterface.restype = c.c_void_p
        self.call(0x0150e828, [])
        self.session = Handle()
        self.call(0x0694d52e, [c.POINTER(Handle)], c.byref(self.session))
        self.call(0x375dbd6b, [Handle], self.session)

    def call(self, identifier, types, *args, check=True):
        address = self.dll.nvapi_QueryInterface(identifier)
        if not address:
            raise RuntimeError(f'NVAPI entry unavailable: {identifier:x}')
        result = c.CFUNCTYPE(c.c_int, *types)(address)(*args)
        if result and check:
            raise RuntimeError(f'NVAPI {identifier:x}: status {result}')
        return result

    def find(self):
        handle = Handle()
        result = self.call(0x7e4a9a0b, [Handle, c.c_wchar_p, c.POINTER(Handle)],
                           self.session, NAME, c.byref(handle), check=False)
        return result, handle

    def save(self):
        self.call(0xfcbc7e14, [Handle], self.session)

    def close(self):
        self.call(0xdad9cff8, [Handle], self.session)
        self.call(0xd22bdd7e, [])


def run(action, marker):
    driver = Driver()
    try:
        status, handle = driver.find()
        if action == 'create':
            if status == 0 or marker.exists():
                raise RuntimeError('test profile/marker already exists; refusing to overwrite')
            if status != -163:  # NVAPI_PROFILE_NOT_FOUND
                raise RuntimeError(f'unexpected profile lookup status {status}')
            profile = versioned(Profile)
            profile.name = NAME
            driver.call(0xcc176068, [Handle, c.POINTER(Profile), c.POINTER(Handle)],
                        driver.session, c.byref(profile), c.byref(handle))
            app = versioned(Application)
            app.name, app.friendly = APP, NAME
            driver.call(0x4347a9de, [Handle, Handle, c.POINTER(Application)],
                        driver.session, handle, c.byref(app))
            setting = versioned(Setting)
            setting.id, setting.current.dword = SETTING, 0
            driver.call(0x577dd202, [Handle, Handle, c.POINTER(Setting)],
                        driver.session, handle, c.byref(setting))
            driver.save()
            marker.write_text(json.dumps(dict(profile=NAME, application=APP, shader_disk_cache=0), indent=2))
        elif status != 0:
            raise RuntimeError(f'profile lookup failed {status}')
        if action in ('create', 'verify'):
            setting = versioned(Setting)
            driver.call(0x73bf8338, [Handle, Handle, U32, c.POINTER(Setting)],
                        driver.session, handle, SETTING, c.byref(setting))
            app, found = versioned(Application), Handle()
            driver.call(0xeee566b2, [Handle, c.c_wchar_p, c.POINTER(Handle), c.POINTER(Application)],
                        driver.session, APP, c.byref(found), c.byref(app))
            if found.value != handle.value or setting.current.dword != 0 or setting.location != 0:
                raise RuntimeError('per-executable disk-cache override did not verify')
            print(json.dumps(dict(profile=NAME, application=app.name, shader_disk_cache=setting.current.dword,
                                  setting_location=setting.location, verified=True)))
        elif action == 'remove':
            evidence = json.loads(marker.read_text())
            if evidence['profile'] != NAME or evidence['application'] != APP:
                raise RuntimeError('wrong cleanup marker')
            profile = versioned(Profile)
            driver.call(0x61cd6fd6, [Handle, Handle, c.POINTER(Profile)], driver.session, handle, c.byref(profile))
            if profile.name != NAME or profile.predefined or profile.apps != 1:
                raise RuntimeError('profile changed; refusing cleanup')
            driver.call(0x17093206, [Handle, Handle], driver.session, handle)
            driver.save()
            print(json.dumps(dict(profile=NAME, removed=True)))
    finally:
        driver.close()


if __name__ == '__main__':
    if sys.argv[1] not in ('create', 'verify', 'remove'):
        raise SystemExit('action must be create, verify or remove')
    run(sys.argv[1], Path(sys.argv[2]))
