#include "picking.h"
#include <windows.h> // must include before GLU
#include <gl/GLU.h>

namespace winged {

Picker::Picker() {
    tess = gluNewTess();
}

Picker::~Picker() {
    gluDeleteTess(tess);
}

Picker::Result Picker::pickSurfaceElement(Surface *surface, Surface::ElementType types,
        glm::vec2 cursor, glm::vec2 windowDim, const glm::mat4 &project) {
    // normalized device coords
    glm::vec2 ndcCur = cursor / windowDim * 2.0f - 1.0f;
    ndcCur.y *= -1;

    Result result;
    float closestZ = 2; // range -1 to 1
    if (types & Surface::VERTEX) {
        glm::vec2 pointSize = 9.0f / windowDim;
        for (auto &vert : surface->vertices) {
            // https://stackoverflow.com/a/63084621
            glm::vec4 tv = project * glm::vec4(vert->pos, 1);
            glm::vec3 ndcVert = tv / tv.w;
            if (glm::abs(ndcVert.z) <= 1
                    && glm::abs(ndcVert.x - ndcCur.x) <= pointSize.x
                    && glm::abs(ndcVert.y - ndcCur.y) <= pointSize.y) {
                if (!result.type || ndcVert.z < closestZ) {
                    result.type = Surface::VERTEX;
                    result.vertex = vert.get();
                    result.point = vert->pos;
                    closestZ = ndcVert.z;
                }
            }
        }
    }
    return result;
}

} // namespace
