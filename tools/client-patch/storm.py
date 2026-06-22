"""Minimal StormLib (ctypes) wrapper to read and build WoW 3.3.5 MPQ patches.

Used by the MOBA custom-content pipeline to add custom spells (and other DBC rows)
to the client patch without any external GUI tool. Requires StormLib installed
(`brew install stormlib`); the shared library path is auto-detected or taken from
the STORMLIB_PATH env var.
"""
import ctypes
import os

def _find_lib():
    for p in [
        os.environ.get("STORMLIB_PATH"),
        "/home/linuxbrew/.linuxbrew/opt/stormlib/lib/libstorm.so",
        "/usr/lib/libstorm.so",
        "/usr/local/lib/libstorm.so",
        "libstorm.so",
    ]:
        if p and (p == "libstorm.so" or os.path.exists(p)):
            try:
                return ctypes.CDLL(p)
            except OSError:
                continue
    raise RuntimeError("libstorm.so not found; install StormLib (brew install stormlib) or set STORMLIB_PATH")

_s = _find_lib()

# --- flags ---------------------------------------------------------------
MPQ_OPEN_READ_ONLY        = 0x00000100
MPQ_CREATE_LISTFILE       = 0x00100000
MPQ_CREATE_ATTRIBUTES     = 0x00200000
MPQ_CREATE_ARCHIVE_V2     = 0x01000000
MPQ_FILE_COMPRESS         = 0x00000200
MPQ_FILE_REPLACEEXISTING  = 0x80000000
MPQ_COMPRESSION_ZLIB      = 0x02
SFILE_INVALID_SIZE        = 0xFFFFFFFF

_s.SFileOpenArchive.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p)]
_s.SFileOpenFileEx.argtypes  = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p)]
_s.SFileGetFileSize.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint)]
_s.SFileGetFileSize.restype  = ctypes.c_uint
_s.SFileReadFile.argtypes    = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint, ctypes.POINTER(ctypes.c_uint), ctypes.c_void_p]
_s.SFileCloseFile.argtypes   = [ctypes.c_void_p]
_s.SFileCloseArchive.argtypes= [ctypes.c_void_p]
_s.SFileCreateArchive.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p)]
_s.SFileAddFileEx.argtypes   = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint]
_s.SFileCompactArchive.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]


def read_file(mpq_path, archived_name):
    """Return the bytes of an internal file (archived_name uses backslashes)."""
    h = ctypes.c_void_p()
    if not _s.SFileOpenArchive(mpq_path.encode(), 0, MPQ_OPEN_READ_ONLY, ctypes.byref(h)):
        raise RuntimeError(f"cannot open {mpq_path}")
    try:
        fh = ctypes.c_void_p()
        if not _s.SFileOpenFileEx(h, archived_name.encode(), 0, ctypes.byref(fh)):
            raise RuntimeError(f"file not in archive: {archived_name}")
        try:
            size = _s.SFileGetFileSize(fh, None)
            if size == SFILE_INVALID_SIZE:
                raise RuntimeError("bad file size")
            buf = ctypes.create_string_buffer(size)
            read = ctypes.c_uint(0)
            if not _s.SFileReadFile(fh, buf, size, ctypes.byref(read), None) and read.value != size:
                raise RuntimeError("short read")
            return buf.raw[:read.value]
        finally:
            _s.SFileCloseFile(fh)
    finally:
        _s.SFileCloseArchive(h)


def add_file(mpq_path, archived_name, local_path):
    """Add/replace a single internal file inside an EXISTING MPQ (in place), keeping all others."""
    h = ctypes.c_void_p()
    if not _s.SFileOpenArchive(mpq_path.encode(), 0, 0, ctypes.byref(h)):
        raise RuntimeError(f"cannot open {mpq_path} for writing")
    try:
        ok = _s.SFileAddFileEx(h, local_path.encode(), archived_name.encode(),
                               MPQ_FILE_COMPRESS | MPQ_FILE_REPLACEEXISTING,
                               MPQ_COMPRESSION_ZLIB, MPQ_COMPRESSION_ZLIB)
        if not ok:
            raise RuntimeError(f"cannot add {archived_name} to {mpq_path}")
    finally:
        _s.SFileCloseArchive(h)


def build_archive(out_path, files):
    """Create an MPQ at out_path containing {archived_name: local_path}."""
    if os.path.exists(out_path):
        os.remove(out_path)
    count = max(8, 2 * len(files))
    h = ctypes.c_void_p()
    flags = MPQ_CREATE_LISTFILE | MPQ_CREATE_ATTRIBUTES | MPQ_CREATE_ARCHIVE_V2
    if not _s.SFileCreateArchive(out_path.encode(), flags, count, ctypes.byref(h)):
        raise RuntimeError(f"cannot create {out_path}")
    try:
        for archived_name, local_path in files.items():
            ok = _s.SFileAddFileEx(h, local_path.encode(), archived_name.encode(),
                                   MPQ_FILE_COMPRESS | MPQ_FILE_REPLACEEXISTING,
                                   MPQ_COMPRESSION_ZLIB, MPQ_COMPRESSION_ZLIB)
            if not ok:
                raise RuntimeError(f"cannot add {archived_name}")
    finally:
        _s.SFileCloseArchive(h)
