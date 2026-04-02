#ifndef HADES_ENGINE_COMPONENTS_CAMERA_COMPONENT_HPP
#define HADES_ENGINE_COMPONENTS_CAMERA_COMPONENT_HPP

namespace hades
{
  struct CameraComponent
  {
    bool isMainCamera = false;
    float fovY = 60.0f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
  };
}

#endif
