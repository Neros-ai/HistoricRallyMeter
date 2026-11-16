#ifndef CONFIG_FILE_H
#define CONFIG_FILE_H

#include <string>

class RallyState;

class ConfigFile {
public:
    static constexpr const char* DEFAULT_PATH = "rally_config.json";
    static void load(RallyState& state, const std::string& path = DEFAULT_PATH);
    // serialise + writeAtomic, on the calling thread. The running app saves through
    // ConfigSaver (config_saver.h) instead, so one thread owns the file.
    static void save(const RallyState& state, const std::string& path = DEFAULT_PATH);

    // The whole state as the config file's text: a complete snapshot of the state at the call.
    static std::string serialise(const RallyState& state);
    // Write bytes to path.tmp, fsync, rename over path: a power cut at any instant leaves the
    // old file or the new one, never a torn one. False if any step failed (path untouched then).
    static bool writeAtomic(const std::string& bytes, const std::string& path = DEFAULT_PATH);
};

#endif // CONFIG_FILE_H
