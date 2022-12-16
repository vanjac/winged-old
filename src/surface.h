#pragma once
#include <common.h>

#include <memory>
#include <vector>
#include <glm/glm/vec3.hpp>

namespace winged {

// https://en.wikipedia.org/wiki/Doubly_connected_edge_list
// https://cs184.eecs.berkeley.edu/sp19/article/15/the-half-edge-data-structure

struct HEdge;

struct Vertex {
    HEdge *edge; // any outgoing

    glm::vec3 pos;
};

// faces must be simple polygons, may be concave but may not contain holes
// counter-clockwise orientation
struct Face {
    HEdge *edge; // any

    glm::vec3 normalNonUnit(); // O(n)
    glm::vec3 normal(); // slower than normalNonUnit() (computes a square root)
};

// "half-edge"
struct HEdge {
    HEdge *twin, *next, *prev;
    Vertex *vert; // "from" vertex
    Face *face;

    HEdge * primary(); // O(1)
};

struct Surface {
    enum ElementType {
        NONE = 0,
        VERTEX = 1,
        FACE = 2,
        EDGE = 4
    };

    std::vector<std::unique_ptr<Vertex>> vertices;
    std::vector<std::unique_ptr<Face>> faces;
    std::vector<std::unique_ptr<HEdge>> edges;

    Vertex * newVertex(); // O(1)
    bool deleteVertex(Vertex *vertex); // O(n)
    Face * newFace();
    bool deleteFace(Face *face);
    HEdge * newEdge();
    bool deleteEdge(HEdge *edge);
};

// use in for loops:
#define ITER_FACE_EDGES(face, edgevar) \
    HEdge *edgevar = (face)->edge, *edgevar##_end_ = nullptr; \
    edgevar != edgevar##_end_; \
    edgevar##_end_ = (edgevar##_end_ ? edgevar##_end_ : edgevar), edgevar = edgevar->next
// outgoing
#define ITER_VERTEX_EDGES(vert, edgevar) \
    HEdge *edgevar = (vert)->edge, *edgevar##_end_ = nullptr; \
    edgevar != edgevar##_end_; \
    edgevar##_end_ = (edgevar##_end_ ? edgevar##_end_ : edgevar), edgevar = edgevar->twin->next

} // namespace
