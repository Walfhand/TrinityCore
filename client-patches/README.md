# Client patches

This folder stores generated client MPQ patches that must be kept with the repo.

## Current custom spell patch

The current frFR patch contains the client-side DBC data needed for custom MOBA
spells, notably:

- `900100` - `Garde rageuse`
- `900101` - `Garde rageuse - bouclier`

The server-side definition is tracked separately in:

```text
sql/custom/world/0002_moba_spells.sql
src/server/scripts/Custom/Moba/moba_spells.cpp
```

The SQL makes TrinityCore aware of the spell IDs through `spell_dbc` and
`spell_script_names`. The MPQ makes the 3.3.5a client aware of the same spell
IDs through `DBFilesClient\Spell.dbc`, so the spell can appear in the spellbook,
action bars, icon/tooltips, and client spell cache.

## How the MPQ/DBC spell patch is built

This repo currently keeps the generated MPQ, not a fully automated DBC build
pipeline. The practical workflow used for the current patch is:

1. Pick custom spell IDs outside normal Blizzard ranges.

   Current MOBA range:

   ```text
   900100+
   ```

2. Add the server rows in `sql/custom/world`.

   For `Garde rageuse`, the base spell is a dummy spell scripted in C++, and the
   aura spell is an absorb aura:

   ```text
   900100 -> spell_moba_rage_guard
   900101 -> absorb aura used by the scripted spell
   ```

3. Implement server behavior in C++.

   The current script handles:

   - rage cost
   - cooldown
   - MOBA level scaling
   - absorb amount calculation
   - triggered aura cast

4. Update client `Spell.dbc`.

   Starting from the extracted client DBC:

   ```text
   docker/data/dbc/Spell.dbc
   ```

   copy or clone nearby spell rows, assign the custom IDs, then set at minimum:

   - spell ID
   - display name
   - description/tooltips if needed
   - icon/visual fields
   - cast/range/duration indexes
   - effect fields aligned with server SQL

   The edited client DBC must be packed in the MPQ under this exact internal
   path:

   ```text
   DBFilesClient\Spell.dbc
   ```

5. Build the locale MPQ patch.

   The output kept by the repo is:

   ```text
   client-patches/frFR/patch-frFR-4.MPQ
   ```

   The archive must preserve Blizzard MPQ paths. For spell DBC work, the needed
   file path inside the archive is:

   ```text
   DBFilesClient\Spell.dbc
   ```

6. Install the patch into the local client.

For the frFR 3.3.5a client, copy:

```text
client-patches/frFR/patch-frFR-4.MPQ
```

to:

```text
docker/client/WINDOWS_World_of_Warcraft_335a/WINDOWS_World of Warcraft 335a/Data/frFR/patch-frFR-4.MPQ
```

Restart the client after copying a patch. Delete the client `Cache` folder if DBC changes do not appear immediately.

## Validation checklist

After changing a custom spell DBC/MPQ:

1. Apply or re-apply the matching SQL in `sql/custom/world`.
2. Rebuild/restart the server if C++ changed.
3. Copy the MPQ to the client locale folder.
4. Close the WoW client completely.
5. Delete the client `Cache` folder.
6. Start the client and select the archetype that learns the spell.
7. Confirm that the spell appears in the spellbook and can be placed on an
   action bar.

## Known gap

The final MPQ is tracked, but the DBC editing/export tooling is not yet
committed to the repo. We should add a small reproducible tool later, ideally:

```text
tools/dbc/
tools/mpq/
make client-patch-frFR
```

That should generate `client-patches/frFR/patch-frFR-4.MPQ` from tracked source
data instead of relying on a manual DBC editor step.
