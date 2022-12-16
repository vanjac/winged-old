#include "surface.h"
#include <glm/glm/geometric.hpp>

namespace winged {

glm::vec3 Face::normalNonUnit() {
    // Newell's method
    // https://web.archive.org/web/20070507025303/http://www.acm.org/tog/GraphicsGems/gemsiii/newell.c
    // an extension to 3D of https://stackoverflow.com/a/1165943
    glm::vec3 normal(0);
    for (ITER_FACE_EDGES(this, e)) {
        glm::vec3 sum = e->vert->pos + e->next->vert->pos;
        glm::vec3 diff = e->vert->pos - e->next->vert->pos;
        normal += glm::vec3(diff.y * sum.z, diff.z * sum.x, diff.x * sum.y);
    }
    return normal;
}

glm::vec3 Face::normal() {
    return glm::normalize(normalNonUnit());
}

HEdge * HEdge::primary() {
    return this < twin ? this : twin; // use pointer order
}

template<typename T>
bool removeUnordered(std::vector<std::unique_ptr<T>> &vec, T *item) {
    for (auto it = vec.begin(); it != vec.end(); it++) {
        if (it->get() == item) {
            *it = std::move(vec.back());
            vec.pop_back();
            return true;
        }
    }
    wprintf(L"Item could not be removed!\n");
    return false;
}

Vertex * Surface::newVertex() {
    return vertices.emplace_back(new Vertex).get();
}

bool Surface::deleteVertex(Vertex *vertex) {
    return removeUnordered(vertices, vertex);
}

Face * Surface::newFace() {
    return faces.emplace_back(new Face).get();
}

bool Surface::deleteFace(Face *face) {
    return removeUnordered(faces, face);
}

HEdge * Surface::newEdge() {
    return edges.emplace_back(new HEdge).get();
}

bool Surface::deleteEdge(HEdge *edge) {
    return removeUnordered(edges, edge);
}

} // namespace