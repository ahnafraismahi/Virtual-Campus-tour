#ifndef CAMERA_H
#define CAMERA_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

enum Camera_Movement { CAM_FORWARD, CAM_BACKWARD, CAM_LEFT, CAM_RIGHT };

class Camera {
public:
    glm::vec3 Position;
    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;
    float Yaw;
    float Pitch;
    float MovementSpeed;
    float MouseSensitivity;
    float Zoom;
    bool groundClamped;
    float groundY;

    Camera(glm::vec3 position = glm::vec3(0.0f, 2.5f, 0.0f),
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f),
        float yaw = -90.0f, float pitch = 0.0f)
        : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(15.0f),
        MouseSensitivity(0.1f), Zoom(45.0f), groundClamped(false), groundY(2.5f)
    {
        Position = position;
        WorldUp = up;
        Yaw = yaw;
        Pitch = pitch;
        updateCameraVectors();
    }

    glm::mat4 GetViewMatrix() {
        return glm::lookAt(Position, Position + Front, Up);
    }

    void ProcessKeyboard(Camera_Movement direction, float deltaTime) {
        float velocity = MovementSpeed * deltaTime;

        if (groundClamped) {
            // Car mode: only move in XZ plane
            glm::vec3 flatFront = glm::normalize(glm::vec3(Front.x, 0.0f, Front.z));
            glm::vec3 flatRight = glm::normalize(glm::cross(flatFront, WorldUp));
            if (direction == CAM_FORWARD)  Position += flatFront * velocity;
            if (direction == CAM_BACKWARD) Position -= flatFront * velocity;
            if (direction == CAM_LEFT)     Position -= flatRight * velocity;
            if (direction == CAM_RIGHT)    Position += flatRight * velocity;
            Position.y = groundY;
        } else {
            if (direction == CAM_FORWARD)  Position += Front * velocity;
            if (direction == CAM_BACKWARD) Position -= Front * velocity;
            if (direction == CAM_LEFT)     Position -= Right * velocity;
            if (direction == CAM_RIGHT)    Position += Right * velocity;
        }
    }

    void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true) {
        xoffset *= MouseSensitivity;
        yoffset *= MouseSensitivity;
        Yaw += xoffset;
        Pitch += yoffset;
        if (constrainPitch) {
            if (Pitch > 89.0f) Pitch = 89.0f;
            if (Pitch < -89.0f) Pitch = -89.0f;
        }
        updateCameraVectors();
    }

    void ProcessMouseScroll(float yoffset) {
        Zoom -= yoffset;
        if (Zoom < 1.0f) Zoom = 1.0f;
        if (Zoom > 45.0f) Zoom = 45.0f;
    }

private:
    void updateCameraVectors() {
        glm::vec3 front;
        front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        front.y = sin(glm::radians(Pitch));
        front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        Front = glm::normalize(front);
        Right = glm::normalize(glm::cross(Front, WorldUp));
        Up = glm::normalize(glm::cross(Right, Front));
    }
};

#endif