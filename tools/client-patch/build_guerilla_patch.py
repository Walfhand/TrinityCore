#!/usr/bin/env python3
"""Refresh the Guerilla map MPQ with MOBA battleground interface entries.

The custom map patch owns the client DBCs that make map 900 appear in the
Battleground UI. This script keeps the server DBC and the client MPQ aligned.
Legacy BattlemasterList rows 12..17 are cleared, then the currently validated
MOBA archetypes are reinserted as visible entries pointing to map 900. The core
maps those aliases back to the real BG type 12.
"""

import os
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import storm

SERVER_DBC = os.path.join(ROOT, "docker", "data", "dbc", "BattlemasterList.dbc")
SOURCE_MPQ = "/home/walfhand/Documents/wow-maps/patch-guerilla.MPQ"
CLIENT_MPQ = os.path.join(
    ROOT,
    "docker",
    "client",
    "WINDOWS_World_of_Warcraft_335a",
    "WINDOWS_World of Warcraft 335a",
    "Data",
    "patch-4.MPQ",
)
ADDON_SOURCE = os.path.join(ROOT, "client-patches", "addons", "MobaLevel1PVP")
ADDON_TARGET = os.path.join(
    ROOT,
    "docker",
    "client",
    "WINDOWS_World_of_Warcraft_335a",
    "WINDOWS_World of Warcraft 335a",
    "Interface",
    "AddOns",
    "MobaLevel1PVP",
)

ARCHIVE_FILES = [
    "DBFilesClient\\Map.dbc",
    "DBFilesClient\\AreaTable.dbc",
    "DBFilesClient\\BattlemasterList.dbc",
    "DBFilesClient\\WorldSafeLocs.dbc",
    "DBFilesClient\\PvpDifficulty.dbc",
    "World\\Maps\\guerilla\\guerilla.wdt",
    "World\\Maps\\guerilla\\guerilla.wdl",
    "World\\Maps\\guerilla\\guerilla_27_25.adt",
    "World\\Maps\\guerilla\\guerilla_27_26.adt",
    "World\\Maps\\guerilla\\guerilla_28_25.adt",
    "World\\Maps\\guerilla\\guerilla_28_26.adt",
]

MOBA_BG_ID = 12
MOBA_MAP_ID = 900
MOBA_LEGACY_ALIAS_COUNT = 6
MOBA_NAMES = [
    "MOBA - Sorcier",
]

NFIELDS = 32
RECSIZE = 128


def parse_dbc(data):
    magic, rc, fc, rs, sbs = struct.unpack("<4siiii", data[:20])
    if magic != b"WDBC":
        raise RuntimeError("not a WDBC file")

    rows = []
    base = 20
    for i in range(rc):
        rows.append(bytearray(data[base + i * rs : base + (i + 1) * rs]))

    strblock = bytearray(data[base + rc * rs : base + rc * rs + sbs])
    return fc, rs, rows, strblock


def serialize_dbc(fc, rs, rows, strblock):
    out = bytearray()
    out += struct.pack("<4siiii", b"WDBC", len(rows), fc, rs, len(strblock))
    for row in rows:
        out += row
    out += strblock
    return bytes(out)


def get_field(row, idx):
    return struct.unpack_from("<i", row, idx * 4)[0]


def set_field(row, idx, value):
    struct.pack_into("<i", row, idx * 4, value)


def add_string(strblock, text):
    offset = len(strblock)
    strblock += text.encode("latin-1", "replace") + b"\x00"
    return offset


def build_moba_row(strblock, row_id, name):
    row = bytearray(RECSIZE)
    set_field(row, 0, row_id)
    set_field(row, 1, MOBA_MAP_ID)
    for idx in range(2, 9):
        set_field(row, idx, -1)
    set_field(row, 9, 3)            # MAP_BATTLEGROUND
    set_field(row, 10, 1)           # groups allowed

    name_offset = add_string(strblock, name)
    for idx in range(11, 27):
        set_field(row, idx, name_offset)

    set_field(row, 27, 0x00FFFFFF)  # locale mask
    set_field(row, 28, 5)           # max queue-as-group size shown by the client
    set_field(row, 29, 0)           # holiday world state
    set_field(row, 30, 1)           # min level for the client UI
    set_field(row, 31, 80)          # max level
    return row


def patch_battlemaster_list(data):
    fc, rs, rows, strblock = parse_dbc(data)
    if fc != NFIELDS or rs != RECSIZE:
        raise RuntimeError(f"unexpected BattlemasterList.dbc layout {fc}x{rs}")

    alias_ids = set(range(MOBA_BG_ID, MOBA_BG_ID + MOBA_LEGACY_ALIAS_COUNT))
    patched = []
    inserted = False

    for row in rows:
        row_id = get_field(row, 0)
        if row_id in alias_ids:
            if not inserted:
                for i, name in enumerate(MOBA_NAMES):
                    patched.append(build_moba_row(strblock, MOBA_BG_ID + i, name))
                inserted = True
            continue
        patched.append(row)

    if not inserted:
        for i, name in enumerate(MOBA_NAMES):
            patched.append(build_moba_row(strblock, MOBA_BG_ID + i, name))

    return serialize_dbc(fc, rs, patched, strblock)


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def main():
    print("Patching server BattlemasterList.dbc")
    write(SERVER_DBC, patch_battlemaster_list(open(SERVER_DBC, "rb").read()))

    print(f"Reading Guerilla MPQ: {SOURCE_MPQ}")
    archive_map = {}
    with tempfile.TemporaryDirectory(prefix="guerilla_patch_") as stage:
        for archived_name in ARCHIVE_FILES:
            data = storm.read_file(SOURCE_MPQ, archived_name)
            if archived_name == "DBFilesClient\\BattlemasterList.dbc":
                data = patch_battlemaster_list(data)

            local_name = archived_name.replace("\\", "__")
            local_path = os.path.join(stage, local_name)
            write(local_path, data)
            archive_map[archived_name] = local_path

        storm.build_archive(SOURCE_MPQ, archive_map)
    shutil.copy2(SOURCE_MPQ, CLIENT_MPQ)
    if os.path.isdir(ADDON_SOURCE):
        os.makedirs(ADDON_TARGET, exist_ok=True)
        for name in os.listdir(ADDON_SOURCE):
            source = os.path.join(ADDON_SOURCE, name)
            if os.path.isfile(source):
                shutil.copy2(source, os.path.join(ADDON_TARGET, name))

    print(f"Wrote {SOURCE_MPQ}")
    print(f"Installed {CLIENT_MPQ}")
    print(f"Installed addon {ADDON_TARGET}")


if __name__ == "__main__":
    main()
