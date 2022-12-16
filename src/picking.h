#pragma once
#include <common.h>

#include "surface.h"
#include <glm/glm/vec2.hpp>
#include <glm/glm/vec3.hpp>
#include <glm/glm/mat4x4.hpp>

class GLUtesselator; // gl/GLU.h

namespace winged {

class Picker {
public:
    struct Result {
        Surface::ElementType type = Surface::NONE;
        union {
            Vertex *vertex = nullptr;
            Face *face;
            HEdge *edge;
        };
        glm::vec3 point;
    };

    Picker();
    ~Picker();

    Result pickSurfaceElement(Surface *surface, Surface::ElementType types,
        glm::vec2 cursor, glm::vec2 windowDim, const glm::mat4 &project);

private:
    GLUtesselator *tess;
};

} // namespace
