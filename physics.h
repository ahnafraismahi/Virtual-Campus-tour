#ifndef PHYSICS_H
#define PHYSICS_H

#include <glm/glm.hpp>
#include <vector>

struct AABB {
    glm::vec3 mn, mx;
    AABB() : mn(0), mx(0) {}
    AABB(glm::vec3 center, glm::vec3 halfSize) : mn(center - halfSize), mx(center + halfSize) {}
};

inline AABB makeAABB(float cx, float cy, float cz, float hw, float hh, float hd) {
    AABB b;
    b.mn = glm::vec3(cx - hw, cy, cz - hd);
    b.mx = glm::vec3(cx + hw, cy + hh, cz + hd);
    return b;
}

inline bool pointInAABB(glm::vec3 p, const AABB& b, float r = 0.8f) {
    return p.x + r > b.mn.x && p.x - r < b.mx.x &&
           p.z + r > b.mn.z && p.z - r < b.mx.z &&
           p.y < b.mx.y;
}

inline bool checkCollision(glm::vec3 pos, const std::vector<AABB>& colliders, float r = 0.8f) {
    for (auto& b : colliders)
        if (pointInAABB(pos, b, r)) return true;
    return false;
}

inline glm::vec3 resolveCollision(glm::vec3 oldPos, glm::vec3 newPos, const std::vector<AABB>& colliders, float r = 0.8f) {
    if (!checkCollision(newPos, colliders, r)) return newPos;
    // Try sliding along X
    glm::vec3 tryX(newPos.x, oldPos.y, oldPos.z);
    if (!checkCollision(tryX, colliders, r)) return tryX;
    // Try sliding along Z
    glm::vec3 tryZ(oldPos.x, oldPos.y, newPos.z);
    if (!checkCollision(tryZ, colliders, r)) return tryZ;
    return oldPos;
}

struct Projectile {
    glm::vec3 position, velocity;
    bool active;
    int bounces;
    float radius;
    Projectile() : position(0), velocity(0), active(false), bounces(0), radius(0.15f) {}
    void launch(glm::vec3 pos, glm::vec3 vel) { position = pos; velocity = vel; active = true; bounces = 0; }
    void update(float dt) {
        if (!active) return;
        velocity.y -= 9.81f * dt;
        position += velocity * dt;
        if (position.y - radius <= 0.0f) {
            position.y = radius;
            velocity.y = -velocity.y * 0.6f;
            velocity.x *= 0.9f; velocity.z *= 0.9f;
            bounces++;
            if (bounces >= 4 || glm::length(velocity) < 0.3f) { active = false; velocity = glm::vec3(0); }
        }
    }
};

#endif
