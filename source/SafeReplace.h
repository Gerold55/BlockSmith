#pragma once
// Replacing a saved file safely: write the new contents to `tmp`, then replaceFile(tmp, dest).
//
// The replacement is one step: std::filesystem::rename swaps the new file in over the old one
// (on Windows as MoveFileEx with "replace existing"; on Linux and macOS as an atomic rename).
// So at every moment `dest` is either the complete old save or the complete new one.
//
// If the rename fails - typically antivirus or OneDrive holding the file open for a moment - it
// is retried a few times, always as the same single step. The destination is NEVER removed to
// "make room": a delete followed by a failed rename would lose the old save and the new one.
// On failure the old file is left exactly as it was, the temp file is cleaned up, and the error
// is returned in `ec` (callers keep their data in memory and try again on the next save).

#include <chrono>
#include <filesystem>
#include <system_error>
#include <thread>

inline bool replaceFile(const std::filesystem::path& tmp, const std::filesystem::path& dest, std::error_code& ec) {
    constexpr int kAttempts = 4;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        ec.clear();
        std::filesystem::rename(tmp, dest, ec);              // the whole commit: old or new, never neither
        if (!ec) return true;
        if (attempt + 1 < kAttempts) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20 * (attempt + 1)));   // a brief lock: wait
        }
    }
    std::error_code ignore;
    std::filesystem::remove(tmp, ignore);                    // only the temp file; `dest` is untouched
    return false;                                            // `ec` holds the rename's error
}
