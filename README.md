# Filesystem-HPFS

## 1. Purpose and format scope

High Performance File System (HPFS). This repository is the designated independent implementation home for InfiltratorOS. **Independent read-only Common V2 development provider. No released installable package yet.**

## 2. On-disk architecture and invariants

Super/spare blocks, bands and allocation bitmaps, fnodes/anodes, dnode trees, code pages, extended attributes and hotfix mapping must be interpreted together. Validate collation and redirected sector addresses before traversing metadata. The qualified development profile is narrower than the complete format.

## 3. Common V2 implementation and capabilities

The local implementation exports only `infiltrator_filesystem_api_v2`. CORE provides probe, mount, unmount, stat, enumeration and read; VOLUME and METADATA are negotiated from the descriptor. Hosted and import-free AMD64 firmware builds share the native engine. Clean standard HPFS, ASCII case-insensitive names, bitmap and directory-band validation, fnode/anode extent trees and dnode directories. Byte-authored tests passed; historical OS/2 media, code pages, extended attributes, hotfix remapping, HPFS386 and signed firmware boot remain unqualified.

## 4. Integrity, mutation, allocation and recovery

Mutation, formatting, check/repair and DEFRAG map/plan/execute/verify/recover require format-specific allocation and ordering invariants, exclusive writable host access and demonstrated interruption recovery. Until qualified, reject them before writing. Preserve native disk semantics and do not add a kernel or FUSE fallback as an alternative filesystem authority.

## 5. Build, installation and release

The target installation is `/System/Filesystems/HPFS.so` and `/System/Resources/Documentation/Filesystem-HPFS.pdf`. Neither file is currently distributed as a release. For development, check out the pinned Libraries revision shown in CI, then run `make carrier` (or the exact CI build steps for UDF). The intended release remains a portable amd64 DEB and CPU-native source-first RUN, with GitHub's automatic source archive.

## 6. Qualification and limitations

Exact tested source [d36c85b11ceb](https://github.com/Infiltrator-Projects/Filesystem-HPFS/commit/d36c85b11cebedf1ea9402ae8b18e5beb93bf5e3); [passing provider CI 45954671125](https://github.com/Infiltrator-Projects/Filesystem-HPFS/actions/runs/45954671125). Clean standard HPFS, ASCII case-insensitive names, bitmap and directory-band validation, fnode/anode extent trees and dnode directories. Byte-authored tests passed; historical OS/2 media, code pages, extended attributes, hotfix remapping, HPFS386 and signed firmware boot remain unqualified. Formatting, writes, repair, defragmentation and release remain unqualified.

## 7. Technical references and provenance

- [Technical reference (PDF)](docs/HPFS.pdf) — original-format technical reference as one integrated manual and [PDF stream source](docs/HPFS.pdfstreams); not an operational manual.

- [Pinned central source baseline](https://github.com/Infiltrator-Projects/Filesystems/tree/101026021d9088d80b7f0f472d9061f0e2c688a3/native/filesystems/hpfs)
- [Native design](native/filesystems/hpfs/DESIGN.md)
- [Migration and qualification plan](MIGRATION_STATUS.md)
- [Original-format reference source](docs/HPFS.pdfstreams) and [PDF](docs/HPFS.pdf)
- [Project philosophy](docs/PHILOSOPHY.md)
- [Common ABI](https://github.com/Infiltrator-Projects/Libraries)

Preserve inherited notices and licences on source extraction; new project-authored code uses GPL-3.0-or-later. The [technical reference](docs/HPFS-TECHNICAL-REFERENCE.md) expands original-format research. installation remains pending. Central code or test results are not evidence of an independent provider.
