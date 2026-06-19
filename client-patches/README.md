# Client patches

This folder stores generated client MPQ patches that must be kept with the repo.

For the frFR 3.3.5a client, copy:

```text
client-patches/frFR/patch-frFR-4.MPQ
```

to:

```text
docker/client/WINDOWS_World_of_Warcraft_335a/WINDOWS_World of Warcraft 335a/Data/frFR/patch-frFR-4.MPQ
```

Restart the client after copying a patch. Delete the client `Cache` folder if DBC changes do not appear immediately.
