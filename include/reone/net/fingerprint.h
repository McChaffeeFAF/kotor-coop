/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <filesystem>
#include <string>

#include "protocol.h"

namespace reone {

namespace net {

/**
 * Fingerprint of a game install, compared in the co-op handshake.
 *
 * Both players bring their own files; nothing is ever transferred. The
 * handshake only proves the two installs hold the same content:
 *  - key: FNV-1a 64 over chitin.key (the BIF index, so every BIF entry)
 *  - modules: the sorted, lower-cased module file names with their sizes
 *  - tlk: FNV-1a 64 over dialog.tlk
 *
 * A missing file hashes as absent rather than failing, so a mismatch is
 * reported instead of the join crashing.
 */
Fingerprint fingerprintInstall(const std::filesystem::path &gamePath);

/** Hash one file's bytes; 0 when it cannot be read. */
uint64_t hashFile(const std::filesystem::path &path);

/** "key=... modules=... tlk=..." in hex, for logs and netstatus. */
std::string describeFingerprint(const Fingerprint &fingerprint);

/**
 * Whether two installs may play together: both present, with the same chitin.key and the
 * same module files. dialog.tlk is left out on purpose: it only holds the strings, which
 * differ between the game's languages (French, German, Italian, Spanish, Polish, ...) while
 * the modules, scripts and rules are the same. Callers warn about a tlk difference
 * (see tlkDiffers) instead of refusing the join.
 */
bool installsCompatible(const Fingerprint &a, const Fingerprint &b);

/** Whether dialog.tlk differs between two compatible installs (another language, or a text mod). */
bool tlkDiffers(const Fingerprint &a, const Fingerprint &b);

/** Which component differs first: "key", "modules", "tlk", "presence" or "". */
std::string fingerprintDifference(const Fingerprint &a, const Fingerprint &b);

} // namespace net

} // namespace reone
