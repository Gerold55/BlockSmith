#include "Audio.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"   // the decoder itself is compiled in StbVorbis.cpp

namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxVoices = 64;          // sounds playing at once (the oldest effect makes room)
constexpr int kStreamBuffers = 4;
constexpr int kStreamFrames = 16384;       // per streaming buffer (~0.37 s at 44.1 kHz)

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !out.empty();
}

// PCM .wav (8 or 16 bit, mono or stereo) -> 16-bit samples
bool decodeWav(const std::vector<uint8_t>& d, std::vector<int16_t>& pcm, int& channels, int& rate) {
    auto u32 = [&d](size_t i) { return static_cast<uint32_t>(d[i] | d[i + 1] << 8 | d[i + 2] << 16 | d[i + 3] << 24); };
    auto u16 = [&d](size_t i) { return static_cast<uint16_t>(d[i] | d[i + 1] << 8); };
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) return false;
    int bits = 0, format = 0;
    channels = 0;
    size_t pos = 12;
    while (pos + 8 <= d.size()) {
        const uint32_t size = u32(pos + 4);
        const size_t body = pos + 8;
        if (body + size > d.size()) break;
        if (std::memcmp(d.data() + pos, "fmt ", 4) == 0 && size >= 16) {
            format = u16(body);
            channels = u16(body + 2);
            rate = static_cast<int>(u32(body + 4));
            bits = u16(body + 14);
        } else if (std::memcmp(d.data() + pos, "data", 4) == 0) {
            if (format != 1 || (channels != 1 && channels != 2) || (bits != 8 && bits != 16)) return false;
            if (bits == 16) {
                pcm.resize(size / 2);
                std::memcpy(pcm.data(), d.data() + body, pcm.size() * 2);
            } else {
                pcm.resize(size);
                for (size_t i = 0; i < size; ++i) pcm[i] = static_cast<int16_t>((static_cast<int>(d[body + i]) - 128) << 8);
            }
            return true;
        }
        pos = body + size + (size & 1);
    }
    return false;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

struct AudioSystem::Stream {
    stb_vorbis* file = nullptr;
    int channels = 0;
    int rate = 0;
    bool loop = false;
    bool ended = false;
    unsigned buffers[kStreamBuffers] = {};
    std::vector<int16_t> scratch;
};

AudioSystem::~AudioSystem() { shutdown(); }

bool AudioSystem::init() {
    ALCdevice* dev = alcOpenDevice(nullptr);
    if (!dev) {
        std::cerr << "[Audio] No sound device - the game will be silent" << std::endl;
        return false;
    }
    ALCcontext* ctx = alcCreateContext(dev, nullptr);
    if (!ctx || !alcMakeContextCurrent(ctx)) {
        std::cerr << "[Audio] Couldn't start OpenAL - the game will be silent" << std::endl;
        if (ctx) alcDestroyContext(ctx);
        alcCloseDevice(dev);
        return false;
    }
    m_device = dev;
    m_context = ctx;
    alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);   // fades to silence at a sound's max distance
    const char* name = alcGetString(dev, ALC_DEVICE_SPECIFIER);
    std::cout << "[Audio] OpenAL ready: " << (name ? name : "default device") << std::endl;
    return true;
}

void AudioSystem::shutdown() {
    if (!m_device) return;
    for (auto& v : m_voices) freeVoice(v);
    m_voices.clear();
    for (auto& [path, b] : m_buffers) if (b.id) alDeleteBuffers(1, &b.id);
    m_buffers.clear();
    alcMakeContextCurrent(nullptr);
    alcDestroyContext(static_cast<ALCcontext*>(m_context));
    alcCloseDevice(static_cast<ALCdevice*>(m_device));
    m_context = m_device = nullptr;
}

// ---------------------------------------------------------------------------
// Finding and loading sounds
// ---------------------------------------------------------------------------

namespace {
// Is `path` (canonical) inside `root` (canonical)? Compared part by part, so "sounds2" isn't
// inside "sounds" and ".." can't be in either (canonical paths have none).
bool insideRoot(const fs::path& path, const fs::path& root) {
    auto r = root.begin(), p = path.begin();
    for (; r != root.end(); ++r, ++p) {
        if (r->empty()) continue;                      // a trailing separator on the root
        if (p == path.end() || *p != *r) return false;
    }
    return p != path.end();                            // something below the root, not the root itself
}
}

std::vector<std::string> AudioSystem::findFiles(const std::string& name, bool sandboxed) {
    auto& cache = sandboxed ? m_resolvedSandboxed : m_resolved;
    const auto cached = cache.find(name);
    if (cached != cache.end()) return cached->second;

    std::vector<std::string> found;
    std::error_code ec;
    const std::string ext = lower(fs::path(name).extension().string());
    // Where to look, in order: (game mods only) the name as given; each sound folder's sounds/
    // and the folder itself; then ./sounds. The first folder that has the sound wins.
    std::vector<std::string> dirs;
    if (!sandboxed) dirs.push_back("");
    for (const auto& f : m_folders) { dirs.push_back(f + "/sounds/"); dirs.push_back(f + "/"); }
    dirs.push_back("sounds/");

    // Sandboxed (client mods): the folders a sound may come from, resolved to where they really
    // are. A match counts only if its own real location (symlinks followed, ".." resolved) is
    // inside one of them - so neither "../../secret", an absolute path nor a symlink pointing out
    // of the folders can reach other files. The checked, canonical path is what's returned.
    std::vector<fs::path> roots;
    if (sandboxed) {
        std::vector<std::string> rootDirs = m_folders;
        rootDirs.push_back("sounds");
        for (const auto& d : rootDirs) {
            const fs::path c = fs::canonical(d, ec);
            if (!ec && fs::is_directory(c, ec)) roots.push_back(c);
        }
        if (name.empty() || fs::path(name).is_absolute() || fs::path(name).has_root_name()) roots.clear();
    }
    // `format`: the extension the candidate was matched with - kept with the returned path, so
    // the decoder never depends on the (canonical) target's own name
    auto accept = [&](const std::string& candidate, const char* format) {
        if (!fs::is_regular_file(candidate, ec)) return;
        if (!sandboxed) { found.push_back(candidate); m_formats[candidate] = format; return; }
        const fs::path real = fs::canonical(candidate, ec);
        if (ec) return;
        for (const auto& root : roots) {
            if (insideRoot(real, root)) {
                found.push_back(real.string());          // decoded directly: the link isn't reopened
                m_formats[real.string()] = format;
                return;
            }
        }
        std::cerr << "[Audio] Refused sound file outside the sound folders: " << candidate << std::endl;
    };

    if (!sandboxed || !roots.empty()) {
        for (const auto& dir : dirs) {
            if (ext == ".ogg" || ext == ".wav") {
                accept(dir + name, ext == ".ogg" ? ".ogg" : ".wav");
            } else {
                for (const char* e : { ".ogg", ".wav" }) {
                    accept(dir + name + e, e);
                    for (int i = 1; i <= 16; ++i) {   // Luanti-style variants: name.1.ogg, name.2.ogg...
                        accept(dir + name + "." + std::to_string(i) + e, e);
                    }
                }
            }
            if (!found.empty()) break;   // the first folder that has it wins
        }
    }
    if (found.empty()) std::cerr << "[Audio] Sound not found: " << name << std::endl;
    cache[name] = found;
    return found;
}

std::string AudioSystem::formatOf(const std::string& path) const {
    const auto it = m_formats.find(path);
    if (it != m_formats.end()) return it->second;
    return lower(fs::path(path).extension().string());
}

bool AudioSystem::soundExists(const std::string& name, bool sandboxed) {
    return !findFiles(name, sandboxed).empty();
}

const AudioSystem::Buffer* AudioSystem::loadBuffer(const std::string& path, bool mono) {
    const std::string key = mono ? path + "#mono" : path;
    const auto it = m_buffers.find(key);
    if (it != m_buffers.end()) return it->second.id ? &it->second : nullptr;

    Buffer b;
    std::vector<int16_t> pcm;
    int channels = 0, rate = 0;
    if (formatOf(path) == ".ogg") {
        short* out = nullptr;
        const int frames = stb_vorbis_decode_filename(path.c_str(), &channels, &rate, &out);
        if (frames > 0 && out) {
            pcm.assign(out, out + static_cast<size_t>(frames) * channels);
            free(out);
        }
    } else {
        std::vector<uint8_t> bytes;
        if (readFile(path, bytes)) decodeWav(bytes, pcm, channels, rate);
    }
    if (pcm.empty() || channels < 1 || channels > 2 || rate <= 0) {
        std::cerr << "[Audio] Couldn't read " << path << " (use .ogg or 16-bit .wav)" << std::endl;
        m_buffers[key] = Buffer{};
        return nullptr;
    }
    // 3D sounds must be mono (OpenAL only places mono sounds in space)
    if (mono && channels == 2) {
        std::vector<int16_t> m(pcm.size() / 2);
        for (size_t i = 0; i < m.size(); ++i) m[i] = static_cast<int16_t>((pcm[i * 2] + pcm[i * 2 + 1]) / 2);
        pcm.swap(m);
        channels = 1;
    }
    alGenBuffers(1, &b.id);
    alBufferData(b.id, channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, pcm.data(),
                 static_cast<ALsizei>(pcm.size() * sizeof(int16_t)), rate);
    b.channels = channels;
    b.seconds = static_cast<float>(pcm.size() / channels) / static_cast<float>(rate);
    auto& stored = m_buffers[key];
    stored = b;
    return &stored;
}

// ---------------------------------------------------------------------------
// Voices
// ---------------------------------------------------------------------------

AudioSystem::Voice* AudioSystem::newVoice(const std::string& group) {
    if (m_voices.size() >= kMaxVoices) {
        // Make room: the oldest one-shot effect stops
        auto oldest = m_voices.end();
        for (auto it = m_voices.begin(); it != m_voices.end(); ++it) {
            if (it->music || it->loop) continue;
            if (oldest == m_voices.end() || it->started < oldest->started) oldest = it;
        }
        if (oldest == m_voices.end()) return nullptr;
        freeVoice(*oldest);
        m_voices.erase(oldest);
    }
    Voice v;
    alGenSources(1, &v.source);
    if (alGetError() != AL_NO_ERROR) return nullptr;
    v.handle = m_nextHandle++;
    v.group = group;
    v.started = m_time;
    m_voices.push_back(v);
    return &m_voices.back();
}

AudioSystem::Voice* AudioSystem::findVoice(int handle) {
    for (auto& v : m_voices) if (v.handle == handle) return &v;
    return nullptr;
}
const AudioSystem::Voice* AudioSystem::findVoice(int handle) const {
    for (const auto& v : m_voices) if (v.handle == handle) return &v;
    return nullptr;
}

void AudioSystem::applyGain(Voice& v) {
    alSourcef(v.source, AL_GAIN, std::max(0.0f, v.gain) * volume(v.group) * volume("master"));
}

void AudioSystem::freeVoice(Voice& v) {
    if (!v.source) return;
    alSourceStop(v.source);
    alSourcei(v.source, AL_BUFFER, 0);
    alDeleteSources(1, &v.source);
    v.source = 0;
    if (v.stream) {
        alDeleteBuffers(kStreamBuffers, v.stream->buffers);
        if (v.stream->file) stb_vorbis_close(v.stream->file);
        delete v.stream;
        v.stream = nullptr;
    }
}

int AudioSystem::play(const std::string& name, const SoundParams& p) {
    if (!m_device || name.empty()) return 0;
    const auto files = findFiles(name, p.sandboxed);
    if (files.empty()) return 0;
    const std::string& file = files[std::uniform_int_distribution<size_t>(0, files.size() - 1)(m_rng)];
    const Buffer* b = loadBuffer(file, p.positional);
    if (!b) return 0;
    Voice* v = newVoice(p.group);
    if (!v) return 0;
    v->gain = p.gain;
    v->loop = p.loop;
    alSourcei(v->source, AL_BUFFER, static_cast<ALint>(b->id));
    alSourcef(v->source, AL_PITCH, std::clamp(p.pitch, 0.1f, 4.0f));
    alSourcei(v->source, AL_LOOPING, p.loop ? AL_TRUE : AL_FALSE);
    if (p.positional) {
        alSourcei(v->source, AL_SOURCE_RELATIVE, AL_FALSE);
        alSource3f(v->source, AL_POSITION, p.position.x, p.position.y, p.position.z);
        alSourcef(v->source, AL_REFERENCE_DISTANCE, std::max(0.1f, p.refDistance));
        alSourcef(v->source, AL_MAX_DISTANCE, std::max(p.refDistance + 0.1f, p.maxDistance));
        alSourcef(v->source, AL_ROLLOFF_FACTOR, 1.0f);
    } else {
        alSourcei(v->source, AL_SOURCE_RELATIVE, AL_TRUE);   // follows the listener: "in your head"
        alSource3f(v->source, AL_POSITION, 0.0f, 0.0f, 0.0f);
        alSourcef(v->source, AL_ROLLOFF_FACTOR, 0.0f);
    }
    applyGain(*v);
    alSourcePlay(v->source);
    return v->handle;
}

void AudioSystem::stop(int handle) {
    for (auto it = m_voices.begin(); it != m_voices.end(); ++it) {
        if (it->handle != handle) continue;
        if (it->handle == m_musicHandle) { m_musicHandle = 0; m_musicName.clear(); }
        freeVoice(*it);
        m_voices.erase(it);
        return;
    }
}

void AudioSystem::stopGroup(const std::string& group) {
    std::vector<int> handles;
    for (const auto& v : m_voices) if (v.group == group) handles.push_back(v.handle);
    for (int h : handles) stop(h);
}

void AudioSystem::setGain(int handle, float gain) {
    if (Voice* v = findVoice(handle)) { v->gain = gain; v->fadeTarget = -1.0f; applyGain(*v); }
}
void AudioSystem::setPitch(int handle, float pitch) {
    if (Voice* v = findVoice(handle)) alSourcef(v->source, AL_PITCH, std::clamp(pitch, 0.1f, 4.0f));
}
void AudioSystem::setPosition(int handle, const glm::vec3& pos) {
    if (Voice* v = findVoice(handle)) alSource3f(v->source, AL_POSITION, pos.x, pos.y, pos.z);
}
void AudioSystem::fade(int handle, float target, float seconds, bool stopAtZero) {
    Voice* v = findVoice(handle);
    if (!v) return;
    target = std::max(0.0f, target);
    if (seconds <= 0.0f) {
        v->gain = target;
        applyGain(*v);
        if (target <= 0.0f && stopAtZero) stop(handle);
        return;
    }
    v->fadeTarget = target;
    v->fadeRate = std::abs(target - v->gain) / seconds;
    v->stopAtZero = stopAtZero;
}
bool AudioSystem::isPlaying(int handle) const { return findVoice(handle) != nullptr; }

void AudioSystem::setVolume(const std::string& group, float vol) {
    m_volumes[group] = std::clamp(vol, 0.0f, 1.0f);
    for (auto& v : m_voices) applyGain(v);
}
float AudioSystem::volume(const std::string& group) const {
    const auto it = m_volumes.find(group);
    return it == m_volumes.end() ? 1.0f : it->second;
}

void AudioSystem::setListener(const glm::vec3& pos, const glm::vec3& forward, const glm::vec3& up) {
    if (!m_device) return;
    alListener3f(AL_POSITION, pos.x, pos.y, pos.z);
    const float orientation[6] = { forward.x, forward.y, forward.z, up.x, up.y, up.z };
    alListenerfv(AL_ORIENTATION, orientation);
}

// ---------------------------------------------------------------------------
// Music (streamed .ogg; a .wav plays as a normal sound)
// ---------------------------------------------------------------------------

bool AudioSystem::refillStream(Voice& v, unsigned buffer) {
    Stream& s = *v.stream;
    s.scratch.resize(static_cast<size_t>(kStreamFrames) * s.channels);
    int frames = stb_vorbis_get_samples_short_interleaved(s.file, s.channels, s.scratch.data(),
                                                           static_cast<int>(s.scratch.size()));
    if (frames <= 0 && s.loop) {
        stb_vorbis_seek_start(s.file);
        frames = stb_vorbis_get_samples_short_interleaved(s.file, s.channels, s.scratch.data(),
                                                           static_cast<int>(s.scratch.size()));
    }
    if (frames <= 0) { s.ended = true; return false; }
    alBufferData(buffer, s.channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, s.scratch.data(),
                 static_cast<ALsizei>(static_cast<size_t>(frames) * s.channels * sizeof(int16_t)), s.rate);
    return true;
}

int AudioSystem::playMusic(const std::string& name, float fadeSeconds, bool loop, float gain) {
    if (!m_device) return 0;
    if (name == m_musicName && findVoice(m_musicHandle)) return m_musicHandle;   // already playing
    stopMusic(fadeSeconds);
    const auto files = findFiles(name);
    if (files.empty()) return 0;
    const std::string& file = files[std::uniform_int_distribution<size_t>(0, files.size() - 1)(m_rng)];

    int handle = 0;
    if (formatOf(file) != ".ogg") {
        SoundParams p;
        p.group = "music"; p.loop = loop; p.gain = gain;
        handle = play(file, p);
        if (Voice* v = findVoice(handle)) v->music = true;
    } else {
        int error = 0;
        stb_vorbis* vf = stb_vorbis_open_filename(file.c_str(), &error, nullptr);
        if (!vf) { std::cerr << "[Audio] Couldn't open music " << file << std::endl; return 0; }
        Voice* v = newVoice("music");
        if (!v) { stb_vorbis_close(vf); return 0; }
        const stb_vorbis_info info = stb_vorbis_get_info(vf);
        v->music = true;
        v->stream = new Stream();
        v->stream->file = vf;
        v->stream->channels = std::min(info.channels, 2);
        v->stream->rate = static_cast<int>(info.sample_rate);
        v->stream->loop = loop;
        alGenBuffers(kStreamBuffers, v->stream->buffers);
        int queued = 0;
        for (unsigned b : v->stream->buffers) {
            if (!refillStream(*v, b)) break;
            alSourceQueueBuffers(v->source, 1, &b);
            ++queued;
        }
        alSourcei(v->source, AL_SOURCE_RELATIVE, AL_TRUE);
        alSourcef(v->source, AL_ROLLOFF_FACTOR, 0.0f);
        v->gain = gain;
        applyGain(*v);
        if (queued > 0) alSourcePlay(v->source);
        handle = v->handle;
    }
    if (handle && fadeSeconds > 0.0f) {
        if (Voice* v = findVoice(handle)) { v->gain = 0.0f; applyGain(*v); fade(handle, gain, fadeSeconds, false); }
    }
    m_musicHandle = handle;
    m_musicName = handle ? name : "";
    return handle;
}

void AudioSystem::stopMusic(float fadeSeconds) {
    if (!m_musicHandle) return;
    const int h = m_musicHandle;
    m_musicHandle = 0;
    m_musicName.clear();
    if (Voice* v = findVoice(h)) {
        if (fadeSeconds > 0.0f) fade(h, 0.0f, fadeSeconds, true);
        else stop(h);
        (void)v;
    }
}

// ---------------------------------------------------------------------------
// Every frame
// ---------------------------------------------------------------------------

void AudioSystem::update(float dt) {
    if (!m_device) return;
    m_time += dt;
    std::vector<int> finished;
    for (auto& v : m_voices) {
        // Fades
        if (v.fadeTarget >= 0.0f) {
            const float step = v.fadeRate * dt;
            if (std::abs(v.fadeTarget - v.gain) <= step) {
                v.gain = v.fadeTarget;
                v.fadeTarget = -1.0f;
                if (v.gain <= 0.0f && v.stopAtZero) finished.push_back(v.handle);
            } else {
                v.gain += (v.fadeTarget > v.gain) ? step : -step;
            }
            applyGain(v);
        }
        // Streaming: refill buffers the source has finished with
        if (v.stream) {
            ALint processed = 0;
            alGetSourcei(v.source, AL_BUFFERS_PROCESSED, &processed);
            while (processed-- > 0) {
                ALuint b = 0;
                alSourceUnqueueBuffers(v.source, 1, &b);
                if (!v.stream->ended && refillStream(v, b)) alSourceQueueBuffers(v.source, 1, &b);
            }
            ALint state = 0, queued = 0;
            alGetSourcei(v.source, AL_SOURCE_STATE, &state);
            alGetSourcei(v.source, AL_BUFFERS_QUEUED, &queued);
            if (state != AL_PLAYING && queued > 0) alSourcePlay(v.source);   // ran dry: carry on
            if (queued == 0 && v.stream->ended) finished.push_back(v.handle);
            continue;
        }
        // One-shot sounds that have ended
        ALint state = 0;
        alGetSourcei(v.source, AL_SOURCE_STATE, &state);
        if (state == AL_STOPPED) finished.push_back(v.handle);
    }
    for (int h : finished) stop(h);
}
