# HPFS: technical reference

## Design context and compatibility

High Performance File System was introduced with OS/2 1.2 in 1989. Its principal design problems were large hard disks, directory search cost, FAT allocation overhead, long names and locality. The original HPFS and the server-oriented HPFS386 must not be treated as identical writable formats. Read and check the functional version and feature flags before permitting writes; server-specific limits, ACLs and checksums require separate validation. Sector numbers are relative to the start of the volume. The following field inventory describes the classic 512-byte sector layout.

## Volume and superblock

The boot sector is sector 0 and carries a FAT-like BPB, HPFS identification and 0xAA55 marker. Sector 16 is the superblock: little-endian 32-bit magic 0xF995E849 at byte 0, second magic 0xFA53E9C5 at 4, version and functional version at 8 and 9. Root fnode sector at 12; total sectors at 16; bad-sector count at 20; bitmap-table sector at 24; bad-block-list sector at 32; last check and optimisation times at 40 and 44; directory-band size, start, end and bitmap sector at 48, 52, 56 and 60. Reject inconsistent size and band bounds. A boot signature alone is not sufficient identification.

## Spare block and damaged media

Sector 17 contains the spare block, magic 0xF9911849 and 0xFA5229C5 at offsets 0 and 4. Status bytes begin at 8; the low bit marks an unclean volume. At 12 is the hotfix table location, followed by counts of used and total spare sectors, then free and total emergency directory nodes, codepage directory and codepage count. HPFS386 adds checksum and feature semantics; do not assume classic unused fields are universally unused. The hotfix table associates original sector addresses with preallocated replacements. Interpret mapped addresses consistently for both read and write; validate mapping ranges and uniqueness. Keep hotfix pools outside normal allocation.

Distinguish ordinary HPFS from HPFS386 checksum fields and validate the super/spare pair, hotfix mappings and codepage records before relying on dnode lookup.

## Band allocation and free space

A normal band covers 16384 sectors, or 8 MiB. Its 2048-byte bitmap records one sector per bit: 1 means available, 0 means reserved or allocated. Bit numbering starts at the least significant bit of each byte. The table referenced by the superblock supplies the bitmap locations; their common alternating end placements are not authoritative. For sector 18000 the band index is 1, local sector is 1616, bitmap byte 202 and bit 0. The directory band is already reserved in the ordinary bitmap; its separate bitmap uses one bit per four-sector directory node. Free-space counts must exclude both forms of reserved space.

## File nodes and allocation nodes

A file or directory has a 512-byte fnode, signature 0xF7E40AAE. A truncated name occupies offsets 13 through 27 with full name length at 12; parent fnode at 28. ACL information begins at 32, external EA size and sector at 44 and 48, inline EA size at 52 and flags at 54. The allocation-tree header begins at 56; its entries begin at 64. Leaf capacity is eight records; internal capacity is twelve. File size is at 160. An allocation node (anode) starts with magic 0x37E40AAE, self and parent addresses at 4 and 8, then tree header at 12. Entries start at 20; capacities are forty leaf or sixty internal entries.

## Extent tree algorithms

Each eight-byte allocation header contains flags at 0, free and used entry counts at 4 and 5 and first-free offset at 6. Flag 0x80 selects internal nodes. A leaf record consists of three little-endian 32-bit quantities: logical starting sector, run length and physical starting sector. An internal record is a logical upper boundary and child-sector address. Descend according to the exclusive upper boundary, validate each node and its parent linkage, then translate within the selected extent. For logical sector 27 in a run starting at logical 20, physical 1000, length 16, the result is physical 1007. Do not infer sparse semantics for gaps in an otherwise invalid mapping.

## Directory tree representation

A dnode is 2048 bytes, aligned on four sectors. Signature 0x77E40AAE at 0, first-free byte offset at 4, status at 8, parent address at 12 and self address at 16. Variable-sized entries begin at 20. Each entry starts with a two-byte record length, flags at 2, attributes at 3, fnode at 4, modification time at 8, byte length at 12, access and creation times at 16 and 20, EA size at 24, ACL count at 28, codepage index at 29 and name length at 30. Name begins at 31; an optional child address is at the record end. Special first and last sentinel entries are not ordinary filenames.

## Name lookup and metadata

Names use the volume's byte-based case-folding and codepage rules, not arbitrary Unicode normalisation. Directory lookup compares the desired name against sorted entries, descending through the associated child pointer where required. Validate entry lengths, alignment, name boundaries, sentinel placement, ordering and parent references. Insertion may split a dnode and promote a separator; deletion may redistribute entries or merge nodes. Root changes must be reflected in the directory fnode. Maintain the directory entry's duplicated size and timestamps consistently with the fnode. Long name support does not remove the need for correct name collision handling.

## Extended attributes

An EA begins with one-byte flags, one-byte name length and a two-byte little-endian value length. The name is followed by its terminating zero and the value. Flags distinguish required attributes and indirect storage; the indirect representation records an actual value size and sector location, which may refer to an anode tree. EA lists may themselves reside within an fnode or outside it. Bound every entry by the containing allocation. Preserve unfamiliar attributes and access controls; dropping them silently may destroy application semantics even when file data survives.

## Mutation and optimisation

For allocation, find suitably sized contiguous free runs near the owning directory, reserve them and update the allocation tree. Coalesce adjacent logical and physical runs where representable. A relocation requires copying and verifying contents before publishing the new extent; never free a live source extent prematurely. A directory split additionally requires an available aligned dnode and updates to child parents. Classic HPFS uses dirty-state tracking and repair, not a general transactional journal. Interrupted multi-sector updates must be considered explicitly, with writable support withheld until recovery ordering is validated against independent media.

## Consistency and test vectors

Traverse root and every directory child with cycle detection; collect file, dnode, anode, external EA and reserved-sector ownership. Compare that ownership against both bitmap classes; detect overlaps, missing allocations and unreachable nodes. Validate lengths independently from logical extent capacity and preserve unknown feature data. Test a directory crossing one dnode, an extent tree crossing eight runs, a file whose allocation spans two bands, non-ASCII case comparisons and a hotfixed sector. Fault-inject between allocation, copy, publication and release. Do not repair ambiguous ownership by guessing.

## Implementation and qualification boundary

The independent provider uses Common V2: https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/FILESYSTEM_COMMON_ABI_V2.md . Negotiate actual operation masks and media profiles. Distinguish unsupported, corrupt, I/O and stale-handle results. Require bounded host I/O, exclusive unmounted ownership and durable flush before mutation. Test Media and Defragmenter consume the same engine, not separate format parsers. Hosted and freestanding binaries, relocation, recovery, BOOT, kernel integration and packages require separate exact-source evidence. Current support belongs to the repository README and qualification records. The intended package paths are /System/Filesystems/ and /System/Resources/Documentation/; the reference is not an installation or release-status claim.

## Source and implementation boundary

Original context: Ray Duncan, Design Goals and Implementation of the New High Performance File System, Microsoft Systems Journal, September 1989. Independent on-disk cross-reference: Linux fs/hpfs/hpfs.h, fs/hpfs/anode.c, dnode.c and alloc.c, https://github.com/torvalds/linux/tree/v6.12/fs/hpfs . The reference above describes the classic format, not the current capabilities of the Infiltrator provider. Exact HPFS386 ACL/checksum and historical codepage differences require further primary-source and fixture validation before declaring full writable compatibility. Historical implementation status is identified separately in Implementation and qualification boundary.

The original header supplies boot signature 0xaa55, superblock 0xf995e849, spare block 0xf9911849 and dnode 0x77e40aae; compare these with the selected revision.

Retained source: https://github.com/Infiltrator-Projects/Filesystems/blob/101026021d9088d80b7f0f472d9061f0e2c688a3/native/filesystems/hpfs/linux/hpfs.h (blob 281dec8f63).

### Common V2 physical allocation queries

The provider uses the native band bitmaps, where a set bit identifies a free sector. ALLOCATION.MAP returns consecutive partition-relative byte ranges from the requested byte. The geometry beyond the filesystem is reserved. The last range ends at the end of the host partition. A complete scan of the test image reconciles with the mounted free-byte count.
