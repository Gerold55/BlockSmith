#pragma once
// DayTime: the time of day, shared by the game, LAN hosting and the dedicated server.
//
// One number counts the days since the world began (`total`). Its fraction is the time of day:
//     0.25 midnight   0.5 sunrise   0.75 midday   0.0 (= 1.0) sunset
// (sky.get_time / sky.set_time use this 0..1 time). The day number - for the moon's phase and
// "Day 3" - goes up at midnight. A day lasts 20 minutes by default, like Minecraft.
//
// Who decides: in singleplayer the game; on a server (or a LAN game) the server, which sends
// its clock to everyone when they join and every few seconds, so all players see the same sky.

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <string>
#include <glm/glm.hpp>

struct DayClock {
    double total = 0.55;               // a new world starts in the morning
    double secondsPerDay = 1200.0;     // real seconds in one day (server.conf: day_length, in minutes)
    double speed = 1.0;                // 1 = normal, 2 = twice as fast, 0 = time stands still

    // 0..1 through the day
    double time() const { return total - std::floor(total); }
    // Day 0 is the first day; it becomes day 1 at the first midnight
    int64_t day() const { return total < 1.25 ? 0 : static_cast<int64_t>(std::floor(total - 0.25)); }
    // Days that pass per real second (0 if the speed or day length isn't a real number)
    double rate() const {
        if (!std::isfinite(speed) || !std::isfinite(secondsPerDay) || secondsPerDay <= 0.0) return 0.0;
        return speed / secondsPerDay;
    }

    void advance(double seconds) {
        const double next = total + seconds * rate();
        if (!std::isfinite(next)) return;          // (never let the clock become NaN / infinite)
        total = next < 0.0 ? 0.0 : next;
    }

    // Jump to a time of day (0..1). forward: the next time it's that time (never back in time),
    // otherwise the same day.
    void setTimeOfDay(double t, bool forward = true) {
        t -= std::floor(t);
        const double base = std::floor(total);
        double next = base + t;
        if (forward && next < total) next += 1.0;
        total = next;
    }

    // The sun's direction: rises in the east (+x) at 0.5, highest at 0.75, sets in the west at 1.0
    glm::vec3 sunDirection() const {
        const double a = (time() - 0.5) * 6.283185307179586;
        return glm::normalize(glm::vec3(static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)), 0.3f));
    }

    // 0..7: changes every day, at midnight
    int moonPhase() const {
        const int64_t d = day();
        return static_cast<int>(((d % 8) + 8) % 8);
    }

    // "07:15" (24-hour clock: midnight 00:00, midday 12:00)
    std::string clock() const {
        const int minutes = static_cast<int>(std::floor((time() - 0.25 + 1.0) * 1440.0 + 1e-6)) % 1440;   // (+1e-6: 19:00, not 18:59)
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
        return buf;
    }

    // world.dat lines: "time=<total days> <speed>"
    std::string saveLine() const {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "time=%.6f %.4g", total, speed);
        return buf;
    }
    bool loadLine(const std::string& line) {
        if (line.rfind("time=", 0) != 0) return false;
        double t = 0.0, s = 1.0;
        const int n = std::sscanf(line.c_str() + 5, "%lf %lf", &t, &s);
        if (n >= 1 && std::isfinite(t) && t >= 0.0) total = t;
        if (n >= 2 && std::isfinite(s) && s >= 0.0 && s <= 1000.0) speed = s;
        return true;
    }

    // A time of day from text: "day" (07:00), "noon"/"midday", "sunset", "night" (19:00),
    // "midnight", "sunrise", "07:30", or a 0..1 number. -1 if it isn't one.
    static double parseTimeOfDay(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        auto fromClock = [](int h, int m) { return std::fmod((h * 60 + m) / 1440.0 + 0.25, 1.0); };
        if (s == "day" || s == "morning") return fromClock(7, 0);
        if (s == "noon" || s == "midday") return 0.75;
        if (s == "sunset" || s == "evening") return 0.0;
        if (s == "night") return fromClock(19, 0);
        if (s == "midnight") return 0.25;
        if (s == "sunrise" || s == "dawn") return 0.5;
        int h = 0, m = 0;
        char extra = 0;
        if (std::sscanf(s.c_str(), "%d:%d%c", &h, &m, &extra) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60) {
            return fromClock(h, m);
        }
        char* end = nullptr;
        const double v = std::strtod(s.c_str(), &end);
        if (end && *end == '\0' && !s.empty() && v >= 0.0 && v <= 1.0) return v;
        return -1.0;
    }
};
