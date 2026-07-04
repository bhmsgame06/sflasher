# Samsung Swift (PNX49xx) Flasher/Dumper

A simple program to flash binaries to your Samsung Swift phone.

This program has been tested only on later Samsung Swift phones, but I'm not
sure about early Swift phones.

# What to specify in "Begin block (0-258)" prompt

I am not sure if the values are the same for every model, but this is what I
know:

| Block range | Address space | Description |
| --- | --- | --- |
| 0-127 | 0x90000000-0x90ffffff | Executable image (CLA/BIN) |
| 128-247 | 0x91000000-0x91efffff | TFS region (/a/ volume) |
| 248-258 | 0x91f00000-0x91ffffff | SYSV region (/sysv/ volume) |

If you're going to flash .cla, please, always type 0.

P.S. Size of one block is 131072 bytes (65536 words).

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
