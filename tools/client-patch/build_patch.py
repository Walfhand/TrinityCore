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

# Base creature DBCs are read from the server-extracted copy (same data the client ships). We only
# need them to silence the MOBA minions client-side (sounds are client-only; the server ignores them).
DBC_SRC = os.path.join(ROOT, "docker", "data", "dbc")
SILENT_SOUND_ID = 9000        # custom CreatureSoundData row: all sound refs stay 0 -> no aggro/wound/death sound
CDI_SOUND_FIELD = 2           # CreatureDisplayInfo.SoundID: overrides the model's combat sounds for THIS display
# Why the display and not the model: a model (e.g. CreatureModelData HumanMale) is shared with champions,
# so muting it would silence players too. The display-level override only affects NPCs using that display.

# Minion silencing is data-driven: we read the minions' modelid1 (= display id) straight from the SQL and
# mute every display they use. Change a minion's model in the SQL, rerun this script, and it stays silent.
MINION_SQL = os.path.join(ROOT, "sql", "custom", "world", "0001_moba_lobby.sql")
MINION_ENTRIES = {900003, 900004, 900005, 900006, 900007, 900008}   # melee/caster/siege, blue+red

# Generic SOUND files to silence by shipping a zero-byte file at the same MPQ path (the client then plays
# nothing). 3.3.5 has no per-creature sound mute (and no addon API for it), so we mute the specific files
# the minions trigger. These are shared files, but no current champion uses them, so in practice it only
# silences the minions. To mute a different minion spell later: find its SoundEntries files (SpellVisual ->
# SpellVisualKit field 15 = SoundID -> SoundEntries DirectoryBase + File) and add their paths here.
MUTE_SOUND_FILES = [
    # Caster minion Wrath (5176): every sound its SpellVisual (3860) kits reference (kit field 15 = SoundID).
    "Sound\\Spells\\Cast\\NatureCast.wav",      # cast sound
    "Sound\\Spells\\LightningBoltImpact.wav",   # impact sound
    "Sound\\Spells\\LifeDrainLoop.wav",         # precast loop ("the spell leaving")
    "Sound\\Spells\\Cast\\HolyCast.wav",        # secondary kit sound
]

# Custom FrameXML UI shipped in the patch (always-on, requires the interface-edit exe patch). We append
# our module to the stock FrameXML.toc (read from locale-frFR.MPQ) and ship our .lua alongside it.
CLIENT_FRFR = os.path.join(ROOT, "docker", "client", "WINDOWS_World_of_Warcraft_335a",
                           "WINDOWS_World of Warcraft 335a", "Data", "frFR")
# The CURRENT 3.3.5 FrameXML.toc (Interface 30300, full file list) lives in the latest base patch, NOT in
# locale-frFR.MPQ (that one is the stale 3.0.0 toc — using it would drop ~18 UI files and crash on login).
FRAMEXML_TOC_MPQ = os.path.join(CLIENT_FRFR, "patch-frFR-3.MPQ")
FRAMEXML_TOC = "Interface\\FrameXML\\FrameXML.toc"
FRAMEXML_DIR = os.path.join(ROOT, "client-patches", "framexml")
# Custom FrameXML modules, in LOAD ORDER: the shared MobaUI core must load before the modules that use it.
FRAMEXML_FILES = ["MobaUI.lua", "MobaLevel1PVP.lua", "MobaInstability.lua"]

# Character creation: disable ONLY the Death Knight (heroic class -> starts level 55 with runes, which breaks
# the level-1 MOBA bracket). All other classes stay creatable. CharBaseInfo.dbc is a special byte-record DBC:
# each record is (raceId u8, classId u8).
CHARBASEINFO_DBC = "DBFilesClient\\CharBaseInfo.dbc"
DEATH_KNIGHT_CLASS_ID = 6

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
F_RANGE = 46           # RangeIndex
F_DURATION = 40        # DurationIndex
F_EFFECT1 = 71         # Effect[0]
F_TARGETA = 86         # EffectImplicitTargetA[0]
F_CATEGORY = 1         # spell Category; shared categories link cooldowns (must be 0 for our spells)
F_CATRECOVERY = 30     # CategoryRecoveryTime
F_ICON = 133           # SpellIconID
F_ATTR = 4             # Attributes (SPELL_ATTR0_*)
F_EFFECT2 = 72         # Effect[1]; cleared on cloned spells whose 2nd effect we don't want predicted
F_EFFECTMECHANIC1 = 83 # EffectMechanic[0]
F_EFFECTMECHANIC2 = 84 # EffectMechanic[1] (e.g. Shadowfury's STUN); cleared with Effect2
F_AURANAME1 = 95       # EffectApplyAuraName[0]
F_AURANAME2 = 96       # EffectApplyAuraName[1] (e.g. MOD_STUN); cleared with Effect2
F_BASEPOINTS1 = 80     # EffectBasePoints[0]
F_BASEPOINTS2 = 81     # EffectBasePoints[1]
F_RADIUS1 = 92         # EffectRadiusIndex[0]
F_TARGETA2 = 87        # EffectImplicitTargetA[1]
F_RADIUS2 = 93         # EffectRadiusIndex[1]
F_MAXLEVEL = 37
F_BASELEVEL = 38
F_SPELLLEVEL = 39      # drives the "Niveau X requis" tooltip; zero it (unlock is gated by the archetype)
FRFR = 2
RECSIZE = 936
NFIELDS = 234

# --- Custom spells to inject. Clone `ref` so the CLIENT inherits its targeting / animation / sound,
# then override id/name/cost/cooldown and (optionally) icon/target/range/attr. Distinct icons per
# ability. Keep IDs == MobaSorcier.h / SQL; cooldown_ms == the server SpellScript cooldown.
CUSTOM_SPELLS = [
    {"id": 900200, "ref": 686,   "name": "Decharge instable", "cd": 3000,   # Shadow Bolt: shadow nuke
     "desc": "Lance une decharge d'ombre instable. Degats accrus par votre Instabilite."},
    {"id": 900208, "ref": 6603, "name": "Trait d'entropie", "cd": 0, "icon": 3376,
     "target": 6, "range": 11, "effect1": 3,   # RangeIndex 11 = 15y, just under the server check (Sorcier::BasicAttackRange ~16.5y) so the client never lets you cast where the server replies "out of range"
     "desc": "Commande l'attaque de base a distance du Sorcier."},
    {"id": 900209, "ref": 44425, "name": "Trait d'entropie visuel", "cd": 0, "icon": 3376,
     "range": 3, "learn": False,
     "desc": "Projectile visuel de l'attaque de base du Sorcier."},
    {"id": 900201, "ref": 30283, "name": "Faille d'entropie", "cd": 8000, "duration": 39,  # Shadowfury clone; clear its stun aura
     "effect2": 0, "effect_mechanic2": 0, "aura2": 0, "basepoints2": 0, "target2": 0, "radius2": 0,
     "desc": "Skillshot au sol : degats de zone + ralentissement. Degats accrus par l'Instabilite."},
    {"id": 900202, "ref": 1953,  "name": "Pas du neant", "cd": 14000, "icon": 87,  # Blink: real leap (anim+sound); shadow-teleport icon
     "desc": "Saut dimensionnel : tu bondis en avant et ralentis les ennemis a l'arrivee."},
    {"id": 900203, "ref": 11113, "name": "Cataclysme", "cd": 90000, "icon": 45,  # Blast Wave: instant fire PBAoE; MeteorStorm icon
     "desc": "Consomme toute ton Instabilite pour une explosion de feu autour de toi. Plus la jauge est haute, plus ca tape."},
    {"id": 900204, "ref": 686,   "name": "Marque d'entropie", "cd": 0, "icon": 2311,
     "desc": "Marque d'entropie : a 3 charges, elle detone pour des degats magiques."},
    {"id": 900205, "ref": 686,   "name": "Entropie - ralentissement", "cd": 0, "icon": 596, "duration": 39,
     "effect1": 6, "effect_mechanic1": 11, "aura1": 33, "basepoints1": -30, "target": 6, "radius1": 0,
     "desc": "Ralenti par l'energie d'entropie."},
    {"id": 900206, "ref": 172,   "name": "Marque d'entropie", "cd": 0, "target": 1, "attr": 0x40, "icon": 1979,
     "desc": "Passif : vos sorts appliquent une Marque d'entropie. A 3 charges, elle detone pour des degats magiques."},
    {"id": 900207, "ref": 172,   "name": "Instabilite", "cd": 0, "target": 1, "attr": 0x40, "icon": 164,
     "desc": "Passif : vos sorts genèrent de l'Instabilite (jauge sous votre vie). Plus elle est haute, "
             "plus vos sorts infligent de degats (jusqu'a +80% a 100%). A 100%, surcharge : vous subissez "
             "des degats et la jauge se vide. Elle decroit hors combat. Cataclysme (R) consomme toute la jauge."},
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

    custom_ids = {c["id"] for c in CUSTOM_SPELLS}
    rows = [r for r in rows if get_field(r, F_ID) not in custom_ids]
    by_id = {get_field(r, F_ID): r for r in rows}

    for c in CUSTOM_SPELLS:
        ref_row = by_id.get(c["ref"])
        if ref_row is None:
            raise RuntimeError(f"reference spell {c['ref']} not found in Spell.dbc")
        row = bytearray(ref_row)
        set_field(row, F_ID, c["id"])
        set_field(row, F_CASTTIME, 1)            # instant (CastTimes.dbc index 1)
        set_field(row, F_RECOVERY, c["cd"])       # client cooldown sablier (match the server script)
        # No resource cost client-side: MOBA spells are gated by script cooldowns / the Instability gauge,
        # not by mana. Leaving the cloned mana cost makes the client block the cast ("not enough mana").
        set_field(row, F_POWERTYPE, 0)
        set_field(row, F_MANACOST, 0)
        set_field(row, F_MANACOSTPCT, 0)
        # No level requirement: the cloned ref carries its own level (e.g. Shadowfury 60), which the
        # client shows as "Niveau X requis". MOBA spells are unlocked by the archetype, not the DBC.
        set_field(row, F_MAXLEVEL, 0)
        set_field(row, F_BASELEVEL, 0)
        set_field(row, F_SPELLLEVEL, 0)
        # Clear the cloned spell Category: refs like Shadowfury/Blast Wave share category 250, whose
        # category cooldown would link our W and R. Each MOBA spell must own an independent cooldown.
        set_field(row, F_CATEGORY, 0)
        set_field(row, F_CATRECOVERY, 0)
        if "target" in c:
            set_field(row, F_TARGETA, c["target"])  # match the server effect target (self vs enemy)
        if "range" in c:
            set_field(row, F_RANGE, c["range"])
        if "duration" in c:
            set_field(row, F_DURATION, c["duration"])
        if "icon" in c:
            set_field(row, F_ICON, c["icon"])        # distinct icon per ability
        if "attr" in c:
            set_field(row, F_ATTR, get_field(row, F_ATTR) | c["attr"])  # e.g. SPELL_ATTR0_PASSIVE
        if "effect1" in c:
            set_field(row, F_EFFECT1, c["effect1"])
            set_field(row, F_EFFECTMECHANIC1, c.get("effect_mechanic1", 0))
            set_field(row, F_AURANAME1, c.get("aura1", 0))
            set_field(row, F_BASEPOINTS1, c.get("basepoints1", 0))
            set_field(row, F_RADIUS1, c.get("radius1", 0))
        if "effect2" in c:
            # Replace the whole 2nd-effect group. Shadowfury's clone carries a STUN here; Faille
            # d'entropie clears it and the server script applies the dedicated slow aura instead.
            set_field(row, F_EFFECT2, c["effect2"])
            set_field(row, F_EFFECTMECHANIC2, c.get("effect_mechanic2", 0))
            set_field(row, F_AURANAME2, c.get("aura2", 0))
            set_field(row, F_BASEPOINTS2, c.get("basepoints2", 0))
            set_field(row, F_TARGETA2, c.get("target2", 0))
            set_field(row, F_RADIUS2, c.get("radius2", 0))
        name_off = add_string(strblock, c["name"])
        set_field(row, F_NAME, name_off)            # enUS
        set_field(row, F_NAME + FRFR, name_off)      # frFR (the column the client reads)
        set_field(row, F_RANK, 0)
        set_field(row, F_RANK + FRFR, 0)
        desc_off = add_string(strblock, c["desc"])
        set_field(row, F_DESC, desc_off)             # enUS
        set_field(row, F_DESC + FRFR, desc_off)       # frFR
        rows.append(row)
        print(f"  Spell.dbc: +{c['id']} '{c['name']}' (cloned {c['ref']}, no mana cost)")

    return serialize_dbc(fc, rs, rows, strblock)


def build_skilllineability_dbc(base_bytes):
    """Clone the existing custom SLA entry (rage guard 900100) for each new spell so it is learnable."""
    rc, fc, rs, rows, strblock = parse_dbc(base_bytes)
    F_SLA_ID = 0
    F_SLA_SPELL = 2  # SkillLineAbility: [0]=Id [1]=SkillLine [2]=Spell ...

    custom_ids = {c["id"] for c in CUSTOM_SPELLS}
    rows = [r for r in rows if get_field(r, F_SLA_SPELL) not in custom_ids]
    max_id = max((get_field(r, F_SLA_ID) for r in rows), default=0)

    # template = any existing custom MOBA spell SLA (rage guard), else any row
    template = next((r for r in rows if get_field(r, F_SLA_SPELL) == 900100), None)
    if template is None:
        template = rows[-1] if rows else None
    if template is None:
        return serialize_dbc(fc, rs, rows, strblock)

    for c in CUSTOM_SPELLS:
        if c.get("learn") is False:
            continue
        max_id += 1
        row = bytearray(template)
        set_field(row, F_SLA_ID, max_id)
        set_field(row, F_SLA_SPELL, c["id"])
        rows.append(row)
        print(f"  SkillLineAbility.dbc: +spell {c['id']} (id {max_id})")

    return serialize_dbc(fc, rs, rows, strblock)


def build_creature_sound_dbc(base_bytes):
    """Append one fully-zeroed (silent) CreatureSoundData row; idempotent on re-run."""
    rc, fc, rs, rows, strblock = parse_dbc(base_bytes)
    rows = [r for r in rows if get_field(r, F_ID) != SILENT_SOUND_ID]
    silent = bytearray(rs)               # all fields 0 = no sound references at all
    set_field(silent, F_ID, SILENT_SOUND_ID)
    rows.append(silent)
    print(f"  CreatureSoundData.dbc: +{SILENT_SOUND_ID} (silent)")
    return serialize_dbc(fc, rs, rows, strblock)


def minion_display_ids():
    """Read modelid1 (the CreatureDisplayInfo id) for each minion entry straight from the SQL, so model
    changes are picked up automatically on the next build. The leading creature_template columns are all
    numeric, so a plain comma split is safe for reading `entry` and `modelid1`."""
    import re
    sql = open(MINION_SQL, encoding="utf-8").read()
    ids = set()
    for m in re.finditer(r"INSERT\s+INTO\s+`creature_template`\s*\(([^)]*)\)\s*VALUES\s*\((.*?)\)\s*;",
                         sql, re.DOTALL | re.IGNORECASE):
        cols = [c.strip().strip('`') for c in m.group(1).split(',')]
        vals = [v.strip() for v in m.group(2).split(',')]
        if len(cols) != len(vals):
            continue
        row = dict(zip(cols, vals))
        try:
            entry = int(row.get('entry', '0'))
            model = int(row.get('modelid1', '0'))
        except ValueError:
            continue
        if entry in MINION_ENTRIES and model > 0:
            ids.add(model)
    return ids


def build_creature_display_dbc(base_bytes, display_ids):
    """Point each minion display's SoundID at the silent row so its NPCs make no combat sounds."""
    rc, fc, rs, rows, strblock = parse_dbc(base_bytes)
    wanted = set(display_ids)
    for r in rows:
        if get_field(r, F_ID) in wanted:
            set_field(r, CDI_SOUND_FIELD, SILENT_SOUND_ID)
            print(f"  CreatureDisplayInfo.dbc: {get_field(r, F_ID)} SoundID -> {SILENT_SOUND_ID}")
    return serialize_dbc(fc, rs, rows, strblock)


def build_charbaseinfo_no_deathknight(base_bytes):
    """Drop the (race, Death Knight) rows so character creation offers every class EXCEPT Death Knight."""
    magic, rc, fc, rs, sbs = struct.unpack("<4siiii", base_bytes[:20])
    assert magic == b"WDBC" and rs == 2, "unexpected CharBaseInfo.dbc layout"
    body = base_bytes[20:20 + rc * rs]
    strblock = base_bytes[20 + rc * rs:20 + rc * rs + sbs]
    kept = b"".join(body[i * rs:i * rs + rs] for i in range(rc) if body[i * rs + 1] != DEATH_KNIGHT_CLASS_ID)
    print(f"  CharBaseInfo.dbc: dropped Death Knight rows, {len(kept) // rs} kept (of {rc})")
    return struct.pack("<4siiii", b"WDBC", len(kept) // rs, fc, rs, sbs) + kept + strblock


def build_framexml_toc():
    """Append our modules to the stock FrameXML.toc (in load order), preserving the original bytes."""
    raw = storm.read_file(FRAMEXML_TOC_MPQ, FRAMEXML_TOC)
    if not raw.endswith(b"\n"):
        raw += b"\r\n"
    for name in FRAMEXML_FILES:
        if name.encode() not in raw:
            raw += (name + "\r\n").encode()
            print("  FrameXML.toc: +" + name)
    return raw


def main():
    print(f"Reading base DBCs from {PATCH}")
    spell = storm.read_file(PATCH, "DBFilesClient\\Spell.dbc")
    sla = storm.read_file(PATCH, "DBFilesClient\\SkillLineAbility.dbc")

    new_spell = build_spell_dbc(spell)
    new_sla = build_skilllineability_dbc(sla)

    # Silence the MOBA minions on whatever display(s) the SQL gives them: client-only, players unaffected.
    displays = minion_display_ids()
    print(f"  minion display ids from SQL: {sorted(displays)}")
    new_csd = build_creature_sound_dbc(open(os.path.join(DBC_SRC, "CreatureSoundData.dbc"), "rb").read())
    new_cdi = build_creature_display_dbc(open(os.path.join(DBC_SRC, "CreatureDisplayInfo.dbc"), "rb").read(), displays)

    stage = os.path.join(HERE, "_stage")
    os.makedirs(stage, exist_ok=True)
    spath = os.path.join(stage, "Spell.dbc")
    slapath = os.path.join(stage, "SkillLineAbility.dbc")
    csdpath = os.path.join(stage, "CreatureSoundData.dbc")
    cdipath = os.path.join(stage, "CreatureDisplayInfo.dbc")
    open(spath, "wb").write(new_spell)
    open(slapath, "wb").write(new_sla)
    open(csdpath, "wb").write(new_csd)
    open(cdipath, "wb").write(new_cdi)

    # Character creation -> every class except Death Knight.
    cbipath = os.path.join(stage, "CharBaseInfo.dbc")
    open(cbipath, "wb").write(build_charbaseinfo_no_deathknight(open(os.path.join(DBC_SRC, "CharBaseInfo.dbc"), "rb").read()))

    # Custom FrameXML modules (shared MobaUI core + features): patched toc + each lua.
    tocpath = os.path.join(stage, "FrameXML.toc")
    open(tocpath, "wb").write(build_framexml_toc())
    framexml_staged = {}
    for name in FRAMEXML_FILES:
        dst = os.path.join(stage, name)
        open(dst, "wb").write(open(os.path.join(FRAMEXML_DIR, name), "rb").read())
        framexml_staged[name] = dst

    # Zero-byte file used to silence each muted sound path (the client plays an empty wav = no sound).
    emptypath = os.path.join(stage, "silent.empty")
    open(emptypath, "wb").close()

    archive = {
        "DBFilesClient\\Spell.dbc": spath,
        "DBFilesClient\\SkillLineAbility.dbc": slapath,
        "DBFilesClient\\CreatureSoundData.dbc": csdpath,
        "DBFilesClient\\CreatureDisplayInfo.dbc": cdipath,
        CHARBASEINFO_DBC: cbipath,
    }
    for sound_path in MUTE_SOUND_FILES:
        archive[sound_path] = emptypath
        print(f"  mute sound: {sound_path}")

    archive[FRAMEXML_TOC] = tocpath
    for name, dst in framexml_staged.items():
        archive["Interface\\FrameXML\\" + name] = dst

    storm.build_archive(PATCH, archive)
    print(f"Wrote {PATCH}  ({os.path.getsize(PATCH)} bytes)")
    print("Install: copy it to the client Data/frFR/ folder, clear the client Cache, relog.")


if __name__ == "__main__":
    main()
