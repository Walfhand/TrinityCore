#!/usr/bin/env python3
"""Rebuild the frFR client spell patch with the MOBA custom spells.

Reads the current Spell.dbc + SkillLineAbility.dbc out of the existing patch (so the
vanilla rows, frFR names, and previously-added custom spells are preserved), appends/refreshes
our custom spell rows by cloning a reference spell, and repackages the MPQ via StormLib.

Idempotent: re-running removes our custom IDs first, then re-adds them.

Usage:  python3 tools/client-patch/build_patch.py
See tools/client-patch/README.md for the field map and the full pipeline.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import storm

PATCH = os.path.join(ROOT, "client-patches", "frFR", "patch-frFR-4.MPQ")

# --- Spell.dbc field indices (3.3.5a, 0-based; see README) ---------------
F_ID = 0
F_CASTTIME = 28
F_NAME = 136           # 16 locale columns; frFR = column 2
F_NAME_FRFR = 138
F_RANK = 153           # 16 locale columns
F_DESC = 170           # Description, 16 locale columns (frFR = F_DESC + FRFR)
F_POWERTYPE = 41
F_MANACOST = 42
F_MANACOSTPCT = 204
F_RECOVERY = 29        # cooldown shown by the client (ms); keep == the server SpellScript cooldown
FRFR = 2
RECSIZE = 936
NFIELDS = 234

# --- Custom spells to inject. Clone `ref` for visuals/range/target, then override. ---
# cooldown_ms MUST match the server SpellScript cooldown so the client sablier matches the server.
CUSTOM_SPELLS = [
    # id,     ref,  name (frFR),         instant?, cooldown_ms, description (frFR, no $ vars so it never lies)
    (900200,  686,  "Decharge instable", True,     3000,
     "Lance une decharge d'ombre instable. Degats accrus par votre Instabilite."),
]


def parse_dbc(data):
    magic, rc, fc, rs, sbs = struct.unpack("<4siiii", data[:20])
    assert magic == b"WDBC", "not a WDBC file"
    rows = []
    base = 20
    for i in range(rc):
        rows.append(bytearray(data[base + i * rs:base + i * rs + rs]))
    strblock = bytearray(data[base + rc * rs: base + rc * rs + sbs])
    return rc, fc, rs, rows, strblock


def serialize_dbc(fc, rs, rows, strblock):
    out = bytearray()
    out += struct.pack("<4siiii", b"WDBC", len(rows), fc, rs, len(strblock))
    for r in rows:
        out += r
    out += strblock
    return bytes(out)


def get_field(row, idx):
    return struct.unpack_from("<i", row, idx * 4)[0]


def set_field(row, idx, val):
    struct.pack_into("<i", row, idx * 4, val)


def add_string(strblock, text):
    off = len(strblock)
    strblock += text.encode("latin-1", "replace") + b"\x00"
    return off


def build_spell_dbc(base_bytes):
    rc, fc, rs, rows, strblock = parse_dbc(base_bytes)
    assert rs == RECSIZE and fc == NFIELDS, f"unexpected Spell.dbc layout {fc}x{rs}"

    custom_ids = {c[0] for c in CUSTOM_SPELLS}
    rows = [r for r in rows if get_field(r, F_ID) not in custom_ids]
    by_id = {get_field(r, F_ID): r for r in rows}

    for spell_id, ref, name, instant, cooldown_ms, desc in CUSTOM_SPELLS:
        ref_row = by_id.get(ref)
        if ref_row is None:
            raise RuntimeError(f"reference spell {ref} not found in Spell.dbc")
        row = bytearray(ref_row)
        set_field(row, F_ID, spell_id)
        if instant:
            set_field(row, F_CASTTIME, 1)  # CastTimes.dbc index 1 = instant
        set_field(row, F_RECOVERY, cooldown_ms)  # client cooldown sablier (match the server script)
        # No resource cost client-side: MOBA spells are gated by script cooldowns / the Instability gauge,
        # not by mana. Leaving the cloned mana cost makes the client block the cast ("not enough mana").
        set_field(row, F_POWERTYPE, 0)
        set_field(row, F_MANACOST, 0)
        set_field(row, F_MANACOSTPCT, 0)
        name_off = add_string(strblock, name)
        set_field(row, F_NAME, name_off)            # enUS
        set_field(row, F_NAME + FRFR, name_off)      # frFR (the column the client reads)
        set_field(row, F_RANK, 0)
        set_field(row, F_RANK + FRFR, 0)
        desc_off = add_string(strblock, desc)
        set_field(row, F_DESC, desc_off)             # enUS
        set_field(row, F_DESC + FRFR, desc_off)       # frFR
        rows.append(row)
        print(f"  Spell.dbc: +{spell_id} '{name}' (cloned {ref}, no mana cost)")

    return serialize_dbc(fc, rs, rows, strblock)


def build_skilllineability_dbc(base_bytes):
    """Clone the existing custom SLA entry (rage guard 900100) for each new spell so it is learnable."""
    rc, fc, rs, rows, strblock = parse_dbc(base_bytes)
    F_SLA_ID = 0
    F_SLA_SPELL = 2  # SkillLineAbility: [0]=Id [1]=SkillLine [2]=Spell ...

    custom_ids = {c[0] for c in CUSTOM_SPELLS}
    rows = [r for r in rows if get_field(r, F_SLA_SPELL) not in custom_ids]
    max_id = max((get_field(r, F_SLA_ID) for r in rows), default=0)

    # template = any existing custom MOBA spell SLA (rage guard), else any row
    template = next((r for r in rows if get_field(r, F_SLA_SPELL) == 900100), None)
    if template is None:
        template = rows[-1] if rows else None
    if template is None:
        return serialize_dbc(fc, rs, rows, strblock)

    for spell_id, *_ in CUSTOM_SPELLS:
        max_id += 1
        row = bytearray(template)
        set_field(row, F_SLA_ID, max_id)
        set_field(row, F_SLA_SPELL, spell_id)
        rows.append(row)
        print(f"  SkillLineAbility.dbc: +spell {spell_id} (id {max_id})")

    return serialize_dbc(fc, rs, rows, strblock)


def main():
    print(f"Reading base DBCs from {PATCH}")
    spell = storm.read_file(PATCH, "DBFilesClient\\Spell.dbc")
    sla = storm.read_file(PATCH, "DBFilesClient\\SkillLineAbility.dbc")

    new_spell = build_spell_dbc(spell)
    new_sla = build_skilllineability_dbc(sla)

    stage = os.path.join(HERE, "_stage")
    os.makedirs(stage, exist_ok=True)
    spath = os.path.join(stage, "Spell.dbc")
    slapath = os.path.join(stage, "SkillLineAbility.dbc")
    open(spath, "wb").write(new_spell)
    open(slapath, "wb").write(new_sla)

    storm.build_archive(PATCH, {
        "DBFilesClient\\Spell.dbc": spath,
        "DBFilesClient\\SkillLineAbility.dbc": slapath,
    })
    print(f"Wrote {PATCH}  ({os.path.getsize(PATCH)} bytes)")
    print("Install: copy it to the client Data/frFR/ folder, clear the client Cache, relog.")


if __name__ == "__main__":
    main()
