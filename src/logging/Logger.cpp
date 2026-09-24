#include "platform/windows/WinCommon.h"
#include "logging/Logger.h"

#include <cstdio>

namespace audioslave
{
LogLevel parseLogLevel (const juce::String& text, LogLevel fallback, bool* ok)
{
    const auto t = text.trim().toLowerCase();
    if (ok != nullptr)
        *ok = true;
    if (t == "debug")
        return LogLevel::debug;
    if (t == "info")
        return LogLevel::info;
    if (t == "warn" || t == "warning")
        return LogLevel::warn;
    if (t == "error")
        return LogLevel::error;
    if (ok != nullptr)
        *ok = false;
    return fallback;
}

juce::String logLevelName (LogLevel level)
{
    switch (level)
    {
        case LogLevel::debug: return "DEBUG";
        case LogLevel::info:  return "INFO";
        case LogLevel::warn:  return "WARN";
        case LogLevel::error: return "ERROR";
    }
    return "INFO";
}

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

Logger::~Logger()
{
    if (juce::Logger::getCurrentLogger() == this)
        juce::Logger::setCurrentLogger (nullptr);
    const juce::ScopedLock sl (lock_);
    stream_.reset();
}

void Logger::configure (const juce::File& file, LogLevel level, bool alsoConsole)
{
    {
        const juce::ScopedLock sl (lock_);
        file_ = file;
        console_ = alsoConsole;
        rotateAt_ = maxFileBytes_;
        openFileLocked();
    }
    level_.store (level);
    juce::Logger::setCurrentLogger (this);
}

void Logger::setMaxFileBytes (juce::int64 bytes)
{
    const juce::ScopedLock sl (lock_);
    maxFileBytes_ = bytes;
    rotateAt_ = bytes;
}

juce::File Logger::getFile() const
{
    const juce::ScopedLock sl (lock_);
    return file_;
}

void Logger::close()
{
    const juce::ScopedLock sl (lock_);
    stream_.reset();
    file_ = juce::File();
    console_ = false;
}

void Logger::openFileLocked()
{
    stream_.reset();
    if (file_ == juce::File())
        return;

    file_.getParentDirectory().createDirectory();
    // Buffer size 0: every write goes straight to WriteFile.
    auto stream = std::make_unique<juce::FileOutputStream> (file_, 0);
    if (stream->openedOk())
        stream_ = std::move (stream);
}

void Logger::rotateIfNeededLocked()
{
    if (stream_ == nullptr || stream_->getPosition() < rotateAt_)
        return;

    stream_.reset();

    // audioslave.log.N -> audioslave.log.N+1; the oldest copy is dropped.
    const auto path = file_.getFullPathName();
    for (int i = maxRotatedFiles - 1; i >= 1; --i)
    {
        const juce::File source (path + "." + juce::String (i));
        if (source.existsAsFile())
            source.moveFileTo (juce::File (path + "." + juce::String (i + 1)));
    }
    const bool rotated = file_.moveFileTo (juce::File (path + ".1"));

    openFileLocked();
    // If the file could not be renamed (another process holds it open), try
    // again after another megabyte instead of on every line.
    rotateAt_ = rotated || stream_ == nullptr ? maxFileBytes_ : stream_->getPosition() + 1024 * 1024;
}

juce::String Logger::formatLine (LogLevel level, const juce::String& message)
{
    const auto now = juce::Time::getCurrentTime();
    return "[" + now.formatted ("%Y-%m-%d %H:%M:%S") + "] [" + logLevelName (level) + "] [pid "
           + juce::String (static_cast<juce::int64> (::GetCurrentProcessId())) + " tid "
           + juce::String (static_cast<juce::int64> (reinterpret_cast<juce::pointer_sized_int> (juce::Thread::getCurrentThreadId())))
           + "] " + message;
}

void Logger::log (LogLevel level, const juce::String& message)
{
    if (static_cast<int> (level) < static_cast<int> (level_.load()))
        return;

    const auto line = formatLine (level, message);

    const juce::ScopedLock sl (lock_);
    if (console_)
    {
        std::FILE* out = level == LogLevel::error ? stderr : stdout;
        std::fputs (line.toRawUTF8(), out);
        std::fputc ('\n', out);
        std::fflush (out);
    }
    if (stream_ != nullptr)
    {
        *stream_ << line << "\r\n";
        rotateIfNeededLocked();
    }
}

void Logger::logMessage (const juce::String& message)
{
    log (LogLevel::info, "[JUCE] " + message);
}
} // namespace audioslave
