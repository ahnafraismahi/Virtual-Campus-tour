#ifndef ENTITIES_H
#define ENTITIES_H

#include <glm/glm.hpp>
#include <vector>
#include <cstdlib>
#include <cmath>
#include "day_night.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Bird {
    glm::vec3 position, velocity, target;
    float wingAngle, wingSpeed, size;

    Bird() : position(0, 15, 0), velocity(0), wingAngle(0), wingSpeed(8.0f), size(0.4f) { randomTarget(); }

    void randomTarget() {
        target = glm::vec3((rand() % 120) - 60.0f, 10.0f + (rand() % 15), (rand() % 120) - 60.0f);
    }

    void update(float dt, float time, glm::vec3 wind) {
        glm::vec3 toT = target - position;
        float dist = glm::length(toT);
        if (dist < 3.0f) randomTarget();

        glm::vec3 desired = glm::normalize(toT) * 5.0f;
        velocity += (desired - velocity) * 2.0f * dt + wind * 0.5f * dt;
        float spd = glm::length(velocity);
        if (spd > 8.0f) velocity = glm::normalize(velocity) * 8.0f;
        position += velocity * dt;
        position.y = glm::clamp(position.y, 8.0f, 30.0f);
        position.x = glm::clamp(position.x, -80.0f, 80.0f);
        position.z = glm::clamp(position.z, -80.0f, 80.0f);
        wingSpeed = 6.0f + spd * 0.5f;
        wingAngle = sin(time * wingSpeed + size * 10.0f) * 0.6f;
    }

    float getYaw() const {
        if (glm::length(velocity) < 0.1f) return 0;
        return atan2(velocity.x, velocity.z);
    }
};

enum StudentState { GOING_TO_CLASS, IN_CLASS, GOING_TO_HALL, IN_HALL };

struct StudentNPC {
    glm::vec3 position;
    int wpIdx;
    float walkPhase, speed;
    std::vector<glm::vec3> waypoints;
    StudentState state;
    float stateTimer;

    // Building positions for scheduling
    static constexpr float academicX = 0.0f, academicZ = -57.0f;
    static constexpr float hallX = 30.0f, hallZ = -52.0f;

    StudentNPC() : position(0), wpIdx(0), walkPhase(0), speed(2.0f), state(GOING_TO_CLASS), stateTimer(0) {}

    void setWaypoints(const std::vector<glm::vec3>& wp) {
        waypoints = wp;
        if (!waypoints.empty()) position = waypoints[0];
    }

    void updateSchedule(DayPhase phase, float dt) {
        stateTimer += dt;
        // Morning to before evening: go to academic building
        if (phase == PHASE_MORNING || phase == PHASE_NOON) {
            if (state != IN_CLASS && state != GOING_TO_CLASS) {
                state = GOING_TO_CLASS;
                setWaypointsToAcademic();
            }
            // Arrive at academic -> stay in class
            glm::vec3 academicPos(academicX, 0, academicZ);
            if (state == GOING_TO_CLASS && glm::length(position - academicPos) < 3.0f) {
                state = IN_CLASS;
                stateTimer = 0;
            }
        }
        // Evening/Night: go to hall
        else if (phase == PHASE_EVENING || phase == PHASE_NIGHT) {
            if (state != IN_HALL && state != GOING_TO_HALL) {
                state = GOING_TO_HALL;
                setWaypointsToHall();
            }
            glm::vec3 hallPos(hallX, 0, hallZ);
            if (state == GOING_TO_HALL && glm::length(position - hallPos) < 3.0f) {
                state = IN_HALL;
                stateTimer = 0;
            }
        }
    }

    void setWaypointsToAcademic() {
        waypoints.clear();
        glm::vec3 academicDoor(0.0f, 0.0f, -53.85f);
        waypoints.push_back(position);
        waypoints.push_back(glm::vec3(position.x, 0, -30));
        waypoints.push_back(glm::vec3(0, 0, -30));
        waypoints.push_back(academicDoor);
        waypoints.push_back(glm::vec3(academicX, 0, academicZ));
        wpIdx = 0;
    }

    void setWaypointsToHall() {
        waypoints.clear();
        glm::vec3 hallDoor(hallX, 0.0f, -49.85f);
        waypoints.push_back(position);
        waypoints.push_back(glm::vec3(position.x, 0, -30));
        waypoints.push_back(glm::vec3(hallX, 0, -30));
        waypoints.push_back(hallDoor);
        waypoints.push_back(glm::vec3(hallX, 0, hallZ));
        wpIdx = 0;
    }

    void update(float dt) {
        if (waypoints.size() < 2) return;
        // Don't move if in class or hall
        if (state == IN_CLASS || state == IN_HALL) return;

        glm::vec3 tgt = waypoints[wpIdx];
        glm::vec3 dir = tgt - position;
        float dist = glm::length(dir);
        if (dist < 0.5f) {
            wpIdx = (wpIdx + 1) % (int)waypoints.size();
        }
        else {
            glm::vec3 nextPos = position + glm::normalize(dir) * speed * dt;
            // Keep pedestrians from walking across the lake region.
            glm::vec2 lakeCenter(26.0f, -8.0f);
            float lakeRadius = 13.5f;
            glm::vec2 nextXZ(nextPos.x, nextPos.z);
            glm::vec2 off = nextXZ - lakeCenter;
            float dLake = glm::length(off);
            if (dLake < lakeRadius) {
                glm::vec2 pushDir = dLake > 0.001f ? glm::normalize(off) : glm::vec2(1, 0);
                nextXZ = lakeCenter + pushDir * lakeRadius;
                nextPos.x = nextXZ.x;
                nextPos.z = nextXZ.y;
            }
            position = nextPos;
        }
        walkPhase += speed * dt * 3.0f;
    }

    bool isSitting() const {
        return state == IN_CLASS || state == IN_HALL;
    }

    float getYaw() const {
        if (waypoints.size() < 2) return 0;
        glm::vec3 dir = waypoints[wpIdx] - position;
        if (glm::length(dir) < 0.01f) return 0;
        return atan2(dir.x, dir.z);
    }
};

enum PropType { PROP_FAN, PROP_LIGHT, PROP_TAP, PROP_DOOR };

struct InteractiveProp {
    PropType type;
    glm::vec3 position;
    bool active;
    float value;
    float targetValue;
    float speed;
    
    InteractiveProp(PropType t, glm::vec3 pos) : type(t), position(pos), active(false), value(0), targetValue(0), speed(5.0f) {
        if(t == PROP_FAN || t == PROP_LIGHT) active = true; // Default on for now
    }

    void update(float dt) {
        if (type == PROP_DOOR) {
            targetValue = active ? 90.0f : 0.0f;
            value += (targetValue - value) * dt * speed;
        } else if (type == PROP_FAN) {
            if (active) value += dt * 10.0f;
        } else if (type == PROP_TAP) {
            if (active) value += dt;
        }
    }
};

#endif
