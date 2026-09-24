#pragma once
// File logger with level filtering and size-based rotation (5 MB x 5 copies).
//
// It is a juce::Logger and installs itself as JUCE's current logger, so
// messages JUCE writes through juce::Logger::writeToLog() end up in the same
// file. Lines are written straight to the OS (no user-space buffer and no
// FlushFileBuffers per line), which keeps the log live without a disk sync
// per message.

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>

namespace audioslave
{
enum class LogLevel
{
    debug = 0,
    info = 1,
    warn = 2,
    error = 3
};

// Parses DEBUG / INFO / WARN / WARNING / ERROR (case-insensitive).
// Unknown text returns `fallback` and sets *ok to false.
LogLevel parseLogLevel (const juce::String& text, LogLevel fallback = LogLevel::info, bool* ok = nullptr);
juce::String logLevelName (LogLevel level);

class Logger final : public juce::Logger
{
public:
    static Logger& instance();

    // Opens `file` for appending (an empty File disables the file sink) and
    // optionally echoes every line to stdout/stderr.
    void configure (const juce::File& file, LogLevel level, bool alsoConsole);
    void setLevel (LogLevel level) noexcept { level_.store (level); }
    [[nodiscard]] LogLevel getLevel() const noexcept { return level_.load(); }
    [[nodiscard]] juce::File getFile() const;

    void log (LogLevel level, const juce::String& message);
    void debug (const juce::String& message) { log (LogLevel::debug, message); }
    void info (const juce::String& message)  { log (LogLevel::info, message); }
    void warn (const juce::String& message)  { log (LogLevel::warn, message); }
    void error (const juce::String& message) { log (LogLevel::error, message); }

    // Closes the file (used at shutdown and by tests).
    void close();

    // "[2026-09-24 10:15:02] [INFO] [pid 123 tid 456] message"
    static juce::String formatLine (LogLevel level, const juce::String& message);

    static constexpr juce::int64 defaultMaxFileBytes = 5 * 1024 * 1024;
    static constexpr int maxRotatedFiles = 5;

    // Size at which the file is rotated (tests use a small value).
    void setMaxFileBytes (juce::int64 bytes);

private:
    Logger() = default;
    ~Logger() override;

    void logMessage (const juce::String& message) override; // juce::Logger::writeToLog
    void openFileLocked();
    void rotateIfNeededLocked();

    mutable juce::CriticalSection lock_;
    juce::File file_;
    std::unique_ptr<juce::FileOutputStream> stream_;
    std::atomic<LogLevel> level_ { LogLevel::info };
    bool console_ = false;
    juce::int64 maxFileBytes_ = defaultMaxFileBytes;
    juce::int64 rotateAt_ = defaultMaxFileBytes; // raised when a rotation fails
};
} // namespace audioslave
