#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera
{
  public:
    glm::vec3 Position = {0.0f, 0.0f, 0.0f};
    glm::vec3 Front = {0.0f, 0.0f, -1.0f};
    glm::vec3 WorldUp = {0.0f, 1.0f, 0.0f};
    glm::vec3 Right = {}, Up = {};

    float Yaw = -90.0f, Pitch = 0.0f;
    float Fov = 45.0f, Znear = 0.1f, Zfar = 1000.0f;

    Camera(glm::vec3 pos = {0.0f, 0.0f, 0.0f}) : Position(pos)
    {
        UpdateVectors();
    }

    glm::mat4 ViewProjection(float aspect) const
    {
        glm::mat4 proj =
            glm::perspective(glm::radians(Fov), aspect, Znear, Zfar);
        glm::mat4 view = glm::lookAt(Position, Position + Front, Up);
        return proj * view;
    }

  private:
    void UpdateVectors()
    {
        glm::vec3 front = {cos(glm::radians(Yaw)) * cos(glm::radians(Pitch)),
                           sin(glm::radians(Pitch)),
                           sin(glm::radians(Yaw)) * cos(glm::radians(Pitch))};
        Front = glm::normalize(front);
        Right = glm::normalize(glm::cross(Front, WorldUp));
        Up = glm::normalize(glm::cross(Right, Front));
    }

    friend class FlyController;
};

struct FlyController
{
    bool Forward = false, Backward = false;
    bool Up = false, Down = false;
    bool Right = false, Left = false;

    float Speed = 2.5f, Sensitivity = 0.1f;

    void Update(Camera* cam, float dt)
    {
        float v = Speed * dt;
        cam->Position += cam->Front * (Forward ? v : Backward ? -v : 0.0f);
        cam->Position += cam->Up * (Up ? v : Down ? -v : 0.0f);
        cam->Position += cam->Right * (Right ? v : Left ? -v : 0.0f);
    }

    void MoveMouse(Camera* cam, glm::vec2 delta)
    {
        delta *= Sensitivity;
        cam->Yaw += delta.x;
        cam->Pitch += delta.y;
        cam->Pitch = glm::clamp(cam->Pitch, -89.0f, 89.0f);
        cam->UpdateVectors();
    }
};
