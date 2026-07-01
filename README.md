# Samsung Swift (PNX49xx) Flasher/Dumper

A simple program to flash binaries to your Samsung Swift phone.

This program has been tested only on later Samsung Swift phones, but I'm not
sure about early Swift phones.

# Compatibility chart

| Model | State | Description |
| --- | :-: | --- |
| [GT-E2121B](https://lpcwiki.miraheze.org/wiki/Samsung_GT-E2121B) | ✅ | Compatible |

# Current project state

On 01.07.2026 (Jule 1st) I implemented TFS/CSC flashing support, which is
highly experimental.

I tested TFS flashing into an empty erased flash memory, and CSC flashing into
an existing file system in the flash memory. Both work without any bug as well,
but be careful when the existing file system barely has free space (<100K).

# If you want to contribute

Please, don't shy to contribute to this project. Even filling out the
compatibility chart already is a some kind of a contribution! But remember,
always create a backup dump before flashing! :3
