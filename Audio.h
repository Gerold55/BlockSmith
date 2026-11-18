#pragma once
// Audio (OpenAL Soft): sound effects in 3D, music streamed from disk, volume groups.
//
// The engine plays; Lua decides what (the `sound` table, see docs/MODDING.md). Blocks can
// name sounds for footsteps, digging, breaking and placing (register_block's `sounds`),
// which the engine plays by itself.
//
// Sounds are found by name, Luanti style: "default_dig" plays default_dig.ogg/.wav, or a
// random one of default_dig.1.ogg, default_dig.2.ogg... if there are several. They're
// looked for in every mod's folder and its sounds/ folder, the game's sounds/ and ./sounds/.
//
// With no sound card / OpenAL, everything here quietly does nothing.

#include <glm/glm.hpp>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

struct SoundParams {
    std::string group = "effects";   // "effects", "ambient", "music", "ui" (each has a volume)
    float gain = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool positional = false;         // false: plays at the listener (UI, music, own actions)
    glm::vec3 position{0.0f};
    float maxDistance = 24.0f;       // silent beyond this (blocks)
    float refDistance = 2.0f;        // full volume within this
    bool sandboxed = false;          // client mods: only files inside the sound folders (see findFiles)
};

class AudioSystem {
public:
    ~AudioSystem();
    bool init();          // opens the default device; false = no audio (game runs silently)
    void shutdown();
    bool available() const { return m_device != nullptr; }

    // Folders to look for sounds in (each also gets /sounds tried). Set before playing.
    void setSearchFolders(const std::vector<std::string>& folders) {
        m_folders = folders;
        m_resolved.clear();
        m_resolvedSandboxed.clear();
    }

    // Returns a handle (0 = nothing played: unknown name, no audio...)
    int play(const std::string& name, const SoundParams& params = {});
    void stop(int handle);
    void setGain(int handle, float gain);
    void setPitch(int handle, float pitch);
    void setPosition(int handle, const glm::vec3& pos);
    void fade(int handle, float targetGain, float seconds, bool stopAtZero = true);
    bool isPlaying(int handle) const;
    void stopGroup(const std::string& group);

    // Music: one track at a time, streamed. Crossfades when a new one starts.
    int playMusic(const std::string& name, float fadeSeconds = 2.0f, bool loop = false, float gain = 1.0f);
    void stopMusic(float fadeSeconds = 2.0f);
    const std::string& currentMusic() const { return m_musicName; }

    // 0..1. "master" scales everything.
    void setVolume(const std::string& group, float volume);
    float volume(const std::string& group) const;

    void setListener(const glm::vec3& pos, const glm::vec3& forward, const glm::vec3& up);
    void update(float dt);   // every frame: fades, streaming, finished sounds

    bool soundExists(const std::string& name, bool sandboxed = false);   // any file for this name?

private:
    struct Buffer { unsigned id = 0; int channels = 0; float seconds = 0.0f; };
    struct Stream;           // an .ogg being streamed (music)
    struct Voice {
        unsigned source = 0;
        int handle = 0;
        std::string group;
        float gain = 1.0f;           // before volumes
        float fadeTarget = -1.0f;    // < 0: not fading
        float fadeRate = 0.0f;       // gain per second
        bool stopAtZero = false;
        bool loop = false;
        bool music = false;
        Stream* stream = nullptr;    // music being streamed
        double started = 0.0;
    };

    // The files a sound name can play (variants name.1.ogg ... included). sandboxed = true (client
    // mods): no raw paths, only the sound folders, and each file's real location - symlinks
    // followed - must be inside one of them; else as for game mods.
    std::vector<std::string> findFiles(const std::string& name, bool sandboxed = false);
    const Buffer* loadBuffer(const std::string& path, bool mono);
    Voice* newVoice(const std::string& group);
    Voice* findVoice(int handle);
    const Voice* findVoice(int handle) const;
    void applyGain(Voice& v);
    void freeVoice(Voice& v);
    bool refillStream(Voice& v, unsigned buffer);

    void* m_device = nullptr;
    void* m_context = nullptr;
    std::vector<std::string> m_folders;
    std::unordered_map<std::string, std::vector<std::string>> m_resolved;   // name -> files
    std::unordered_map<std::string, std::vector<std::string>> m_resolvedSandboxed;   // same, for client mods
    std::unordered_map<std::string, Buffer> m_buffers;                      // path(+#mono) -> buffer
    // The format (".ogg" / ".wav") of each file findFiles matched, by the path it returns. A
    // sandboxed match is returned as its canonical target, whose own name may not end in the
    // matched extension (a symlink "step.ogg" -> "blob"), so the decoder is chosen from this.
    std::unordered_map<std::string, std::string> m_formats;
    // ".ogg" or ".wav" for a path findFiles returned (else from the path's own extension)
    std::string formatOf(const std::string& path) const;
    std::vector<Voice> m_voices;
    std::map<std::string, float> m_volumes{ {"master", 1.0f}, {"effects", 1.0f}, {"ambient", 1.0f},
                                            {"music", 0.6f}, {"ui", 1.0f} };
    int m_nextHandle = 1;
    int m_musicHandle = 0;
    std::string m_musicName;
    double m_time = 0.0;
    std::mt19937 m_rng{std::random_device{}()};
};
