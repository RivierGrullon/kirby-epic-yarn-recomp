# SPDX-License-Identifier: GPL-3.0-only
"""Small host-specific filesystem and executable conventions for setup."""
import os
from pathlib import Path
import stat

WINDOWS = os.name == 'nt'


def executable_name(target=None):
    target = target or ('windows' if WINDOWS else 'linux')
    if target not in ('windows', 'linux'):
        raise ValueError(f'Unsupported native target: {target}')
    return 'WiiCompiled.exe' if target == 'windows' else 'WiiCompiled'


def is_link(path):
    """Reject junctions and other Windows reparse points as well as symlinks."""
    try:
        info = Path(path).lstat()
    except FileNotFoundError:
        return False
    return stat.S_ISLNK(info.st_mode) or bool(getattr(info, 'st_file_attributes', 0) & 0x400)


def open_read(path, *, directory=False):
    if not WINDOWS:
        flags = os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC
        if directory:
            flags |= os.O_DIRECTORY
        return os.open(path, flags)
    return _windows_open(path, directory=directory)


def open_lock(path):
    if WINDOWS:
        return _windows_open(path, write=True, create=True)
    return os.open(path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600)


def _windows_open(path, *, directory=False, write=False, create=False):
    import ctypes
    from ctypes import wintypes
    import msvcrt
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                  ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.GetFileInformationByHandleEx.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
    class Attributes(ctypes.Structure):
        _fields_ = [('attributes', wintypes.DWORD), ('tag', wintypes.DWORD)]
    path = Path(os.path.abspath(path))
    handles = []
    try:
        # Pin every ancestor against rename while opening the child. OPEN_REPARSE_POINT
        # plus the handle attribute check rejects junctions without following them.
        for current in [*reversed(path.parents), path]:
            access = 0xc0000000 if write and current == path else 0x80000000
            disposition = 4 if create and current == path else 3
            handle = kernel.CreateFileW(str(current), access, 3, None, disposition, 0x02200000, None)
            if handle == ctypes.c_void_p(-1).value:
                raise ctypes.WinError(ctypes.get_last_error())
            handles.append(handle)
            info = Attributes()
            if not kernel.GetFileInformationByHandleEx(handle, 9, ctypes.byref(info), ctypes.sizeof(info)):
                raise ctypes.WinError(ctypes.get_last_error())
            if info.attributes & 0x400:
                raise ValueError(f'Setup paths must not contain reparse points: {current}')
            expected_directory = current != path or directory
            if bool(info.attributes & 0x10) != expected_directory:
                raise ValueError(f'Unexpected setup file type: {current}')
        fd = msvcrt.open_osfhandle(handles[-1], (os.O_RDWR if write else os.O_RDONLY) | os.O_BINARY)
        handles.pop()  # The CRT descriptor owns this handle now.
        return fd
    finally:
        for handle in reversed(handles):
            kernel.CloseHandle(handle)


def fsync_directory(path):
    # Windows does not allow CRT directory descriptors to be fsynced. File data
    # is flushed before the atomic os.replace; directory fsync remains on POSIX.
    if not WINDOWS:
        fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)


def readable(fd, timeout=0.1):
    """Poll a worker pipe without a blocked reader thread at shutdown."""
    if not WINDOWS:
        import select
        return bool(select.select([fd], [], [], timeout)[0])
    import ctypes
    from ctypes import wintypes
    import msvcrt
    import time
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    handle = msvcrt.get_osfhandle(fd)
    kernel.GetFileType.argtypes = [wintypes.HANDLE]
    kind = kernel.GetFileType(handle)
    if kind == 1:  # Redirected disk file, including EOF.
        return True
    if kind != 3:
        raise ValueError('The JSON setup worker requires redirected stdin; use --install for a console')
    kernel.PeekNamedPipe.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                                   ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
    available = wintypes.DWORD()
    if not kernel.PeekNamedPipe(handle, None, 0, None, ctypes.byref(available), None):
        error = ctypes.get_last_error()
        if error in (109, 232):  # Broken pipe / no data: os.read observes EOF.
            return True
        raise ctypes.WinError(error)
    if available.value:
        return True
    time.sleep(timeout)
    return False
