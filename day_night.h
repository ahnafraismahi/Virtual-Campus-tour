#ifndef DAY_NIGHT_H
#define DAY_NIGHT_H

#include <glm/glm.hpp>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum DayPhase { PHASE_MORNING, PHASE_NOON, PHASE_EVENING, PHASE_NIGHT };

struct DayNightCycle {
    float totalTime, speed;
    float morningDur, noonDur, eveningDur, nightDur, cycleLen;
    DayPhase phase;
    float phaseProgress;
    glm::vec3 skyColor, sunPos, moonPos, sunColor, sunDir;
    float sunScale, moonGlitter;
    bool dirLightActive, showMoon;
    int sunTexIdx; // 0=red, 1=yellow

    DayNightCycle() : totalTime(0), speed(1.0f),
        morningDur(20), noonDur(20), eveningDur(20), nightDur(10),
        phase(PHASE_MORNING), phaseProgress(0), sunScale(1.0f), moonGlitter(0.0f),
        dirLightActive(true), showMoon(false), sunTexIdx(0),
        skyColor(0), sunPos(0), moonPos(0), sunColor(0), sunDir(0) {
        cycleLen = morningDur + noonDur + eveningDur + nightDur;
    }

    void update(float dt) {
        totalTime += dt * speed;
        float t = fmod(totalTime, cycleLen);
        if (t < morningDur) { phase = PHASE_MORNING; phaseProgress = t / morningDur; }
        else if (t < morningDur + noonDur) { phase = PHASE_NOON; phaseProgress = (t - morningDur) / noonDur; }
        else if (t < morningDur + noonDur + eveningDur) { phase = PHASE_EVENING; phaseProgress = (t - morningDur - noonDur) / eveningDur; }
        else { phase = PHASE_NIGHT; phaseProgress = (t - morningDur - noonDur - eveningDur) / nightDur; }

        float sunAngle;
        switch (phase) {
        case PHASE_MORNING:
            sunAngle = phaseProgress * 0.333f * (float)M_PI;
            sunTexIdx = 0; dirLightActive = true; showMoon = false;
            sunColor = glm::mix(glm::vec3(0.85f, 0.35f, 0.12f), glm::vec3(0.95f, 0.75f, 0.45f), phaseProgress);
            skyColor = glm::mix(glm::vec3(0.88f, 0.48f, 0.28f), glm::vec3(0.48f, 0.68f, 0.88f), phaseProgress);
            moonGlitter = 0.0f;
            break;
        case PHASE_NOON:
            sunAngle = (0.333f + phaseProgress * 0.333f) * (float)M_PI;
            sunTexIdx = 1; dirLightActive = true; showMoon = false;
            sunColor = glm::vec3(1.0f, 0.97f, 0.88f);
            skyColor = glm::vec3(0.48f, 0.68f, 0.88f);
            moonGlitter = 0.0f;
            break;
        case PHASE_EVENING:
            sunAngle = (0.666f + phaseProgress * 0.333f) * (float)M_PI;
            sunTexIdx = 0; dirLightActive = true;
            showMoon = phaseProgress > 0.7f;
            sunColor = glm::mix(glm::vec3(0.95f, 0.75f, 0.45f), glm::vec3(0.85f, 0.32f, 0.12f), phaseProgress);
            skyColor = glm::mix(glm::vec3(0.48f, 0.68f, 0.88f), glm::vec3(0.82f, 0.38f, 0.22f), phaseProgress);
            moonGlitter = showMoon ? 0.1f : 0.0f;
            break;
        case PHASE_NIGHT:
            sunAngle = 0; sunTexIdx = 0;
            dirLightActive = false; showMoon = true;
            sunColor = glm::vec3(0);
            skyColor = glm::mix(glm::vec3(0.10f, 0.06f, 0.18f), glm::vec3(0.02f, 0.02f, 0.08f), phaseProgress);
            // Moon glitter - pulsing shimmer during night
            moonGlitter = 0.3f + 0.18f * sin(totalTime * 3.5f) + 0.08f * sin(totalTime * 7.1f);
            break;
        }
        float sd = 80.0f;
        sunPos = glm::vec3(cos(sunAngle) * sd, sin(sunAngle) * sd + 15.0f, -20.0f);
        sunDir = -glm::normalize(sunPos);
        float moonA = phaseProgress * (float)M_PI * 0.5f + (float)M_PI * 0.25f;
        moonPos = glm::vec3(-cos(moonA) * 60.0f, sin(moonA) * 50.0f + 20.0f, 10.0f);
        sunScale = (phase != PHASE_NIGHT) ? 1.0f + (1.0f - sin(sunAngle)) * 0.4f : 1.0f;
    }
};

#endif
