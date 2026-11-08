# Monocypher (vendored)

- Source: https://monocypher.org/download/monocypher-4.0.2.tar.gz
  (sha256 of the tarball: `38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c`)
- Version: 4.0.2
- License: BSD-2-Clause or CC0-1.0, at our choice (see `LICENCE.md`)
- Copied: `src/monocypher.c`, `src/monocypher.h`
  - monocypher.c sha256 `afe2b098c8569577a84488e0b98d276d1fba6506adea68bb9241a52111734c59`
  - monocypher.h sha256 `f78bb31255cfb7beba66afd2137f5194c8a025cf40488b6cc1e295234d43f374`
- Local changes: **none**.
- Audit: Cure53, 2020 (https://monocypher.org/quality-assurance/audit).

Used for: **Argon2id only** (`crypto_argon2`), the password hash of the microSD backups
(`core/src/backup.c`, milestone M6). Everything else in the file is unused and removed by the
linker. Checked against the RFC 9106 Argon2id test vector and against argon2-cffi (the reference
implementation) in the host tests.
