#include "surface.h"
#include "picking.h"
#include "resource.h"
#include <unordered_set>
#include <windows.h>
#include <windowsx.h>
#include <gl/GL.h>
#include <gl/GLU.h>
#include <glm/glm/gtc/matrix_transform.hpp>
#include <glm/glm/gtc/type_ptr.hpp>
#include <glm/glm/gtx/rotate_vector.hpp>

#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "Opengl32.lib")
#pragma comment(lib, "Glu32.lib")

using namespace winged;

static Surface theSurface;
static HEdge *selectedEdge, *storedEdge = nullptr;
static std::unordered_set<Vertex *>selectedVertices;
static int lastMouseX, lastMouseY;
static float rotX = 0, rotY = 0;

static glm::vec2 windowDim;
static glm::mat4 projMat, mvMat;

static Picker picker;
static GLUtesselator *tess;

void linkTwins(HEdge *edge1, HEdge *edge2) {
    edge1->twin = edge2;
    edge2->twin = edge1;
}

void linkNext(HEdge *prev, HEdge *next) {
    prev->next = next;
    next->prev = prev;
}

void makeCube(Surface *surface) {
    for (int i = 0; i < 8; i++) {
        Vertex *vertex = surface->newVertex();
        vertex->pos.x = (i & 0x1) ? 1.0f : -1.0f;
        vertex->pos.y = (i & 0x2) ? 1.0f : -1.0f;
        vertex->pos.z = (i & 0x4) ? 1.0f : -1.0f;
    }

    HEdge *edges[6][4]; // counter-clockwise order
    for (int i = 0; i < 6; i++) {
        Face *face = surface->newFace();
        for (int j = 0; j < 4; j++) {
            HEdge *edge = surface->newEdge();
            edges[i][j] = edge;
            edge->face = face;
        }
        face->edge = edges[i][0];
        for (int j = 0; j < 3; j++)
            linkNext(edges[i][j], edges[i][j + 1]);
        linkNext(edges[i][3], edges[i][0]);
    }

    // normal (-1, 0, 0)         0bZYX
    edges[0][0]->vert = surface->vertices[0b000].get();  linkTwins(edges[0][0], edges[2][3]);
    edges[0][1]->vert = surface->vertices[0b100].get();  linkTwins(edges[0][1], edges[5][3]);
    edges[0][2]->vert = surface->vertices[0b110].get();
    edges[0][3]->vert = surface->vertices[0b010].get();
    // normal (1, 0, 0)
    edges[1][0]->vert = surface->vertices[0b001].get();  linkTwins(edges[1][0], edges[4][2]);
    edges[1][1]->vert = surface->vertices[0b011].get();  linkTwins(edges[1][1], edges[3][2]);
    edges[1][2]->vert = surface->vertices[0b111].get();
    edges[1][3]->vert = surface->vertices[0b101].get();
    // normal (0, -1, 0)
    edges[2][0]->vert = surface->vertices[0b000].get();  linkTwins(edges[2][0], edges[4][3]);
    edges[2][1]->vert = surface->vertices[0b001].get();  linkTwins(edges[2][1], edges[1][3]);
    edges[2][2]->vert = surface->vertices[0b101].get();
    edges[2][3]->vert = surface->vertices[0b100].get();
    // normal (0, 1, 0)
    edges[3][0]->vert = surface->vertices[0b010].get();  linkTwins(edges[3][0], edges[0][2]);
    edges[3][1]->vert = surface->vertices[0b110].get();  linkTwins(edges[3][1], edges[5][2]);
    edges[3][2]->vert = surface->vertices[0b111].get();
    edges[3][3]->vert = surface->vertices[0b011].get();
    // normal (0, 0, -1)
    edges[4][0]->vert = surface->vertices[0b000].get();  linkTwins(edges[4][0], edges[0][3]);
    edges[4][1]->vert = surface->vertices[0b010].get();  linkTwins(edges[4][1], edges[3][3]);
    edges[4][2]->vert = surface->vertices[0b011].get();
    edges[4][3]->vert = surface->vertices[0b001].get();
    // normal (0, 0, 1)
    edges[5][0]->vert = surface->vertices[0b100].get();  linkTwins(edges[5][0], edges[2][2]);
    edges[5][1]->vert = surface->vertices[0b101].get();  linkTwins(edges[5][1], edges[1][2]);
    edges[5][2]->vert = surface->vertices[0b111].get();
    edges[5][3]->vert = surface->vertices[0b110].get();

    for (int i = 4; i < 6; i++)
        for (int j = 0; j < 4; j++)
            edges[i][j]->vert->edge = edges[i][j];

    selectedEdge = surface->edges[0].get();
}

// for debugging only!!
bool validateSurface(Surface *surface) {
    const uint32_t UNINITIALIZED = 0xCDCDCDCD; // used by MSVC debugging runtime
    const float UNINITIALIZED_FLOAT = *(float *)&UNINITIALIZED;

    bool valid = true;
    std::unordered_set<Vertex *> vertices;
    std::unordered_set<Face *> faces;
    std::unordered_set<HEdge *> edges;
    for (auto &v : surface->vertices)
        vertices.insert(v.get());
    if (vertices.size() < surface->vertices.size()) {
        wprintf(L"Surface contains duplicate vertex entries!\n");
        valid = false;
    }
    for (auto &f : surface->faces)
        faces.insert(f.get());
    if (faces.size() < surface->faces.size()) {
        wprintf(L"Surface contains duplicate face entries!\n");
        valid = false;
    }
    for (auto &e : surface->edges)
        edges.insert(e.get());
    if (edges.size() < surface->edges.size()) {
        wprintf(L"Surface contains duplicate edge entries!\n");
        valid = false;
    }

    for (auto &v : surface->vertices) {
        if (!edges.count(v->edge)) {
            wprintf(L"Vertex has invalid edge reference!\n");
            valid = false;
        } else {
            for (ITER_VERTEX_EDGES(v, vertEdge)) {
                if (vertEdge->vert != v.get()) {
                    wprintf(L"Edge attached to vertex does not reference vertex!\n");
                    valid = false;
                }
            }
        }
        if (v->pos.x == UNINITIALIZED_FLOAT || v->pos.y == UNINITIALIZED_FLOAT
                || v->pos.z == UNINITIALIZED_FLOAT) {
            wprintf(L"Vertex position is uninitialized!\n");
            valid = false;
        } else {
            glm::vec3 absPos = glm::abs(v->pos);
            if (absPos.x > 1000000 || absPos.y > 1000000 || absPos.z > 1000000)
                wprintf(L"Vertex has a very large coordinate, may be uninitialized! (%f, %f, %f)\n",
                    v->pos.x, v->pos.y, v->pos.z);
        }
    }

    for (auto &f : surface->faces) {
        if (!edges.count(f->edge)) {
            wprintf(L"Face has invalid edge reference!\n");
            valid = false;
        } else {
            for (ITER_FACE_EDGES(f, faceEdge)) {
                if (faceEdge->face != f.get()) {
                    wprintf(L"Edge attached to face does not reference face!\n");
                    valid = false;
                }
            }
            if (f->edge->next->next == f->edge) {
                wprintf(L"Face only has two edges!\n");
                valid = false;
            }
        }
    }

    for (auto &e : surface->edges) {
        if (!edges.count(e->twin)) {
            wprintf(L"Edge has invalid twin reference!\n");
            valid = false;
        } else {
            if (e->twin == e.get()) {
                wprintf(L"Edge's twin is itself!\n");
                valid = false;
            } else if (e->twin->twin != e.get()) {
                wprintf(L"Edges are not twins!\n");
                valid = false;
            }
        }
        if (!edges.count(e->next)) {
            wprintf(L"Edge has invalid next reference!\n");
            valid = false;
        } else {
            if (e->next == e.get()) {
                wprintf(L"Edge's next link is itself!\n");
                valid = false;
            } else if (e->next->prev != e.get()) {
                wprintf(L"Edges are not linked!\n");
                valid = false;
            }
        }
        if (!edges.count(e->prev)) {
            wprintf(L"Edge has invalid prev reference!\n");
            valid = false;
        } else {
            if (e->prev == e.get()) {
                wprintf(L"Edge's prev link is itself!\n");
                valid = false;
            }
        }
        if (!faces.count(e->face)) {
            wprintf(L"Edge has invalid face reference!\n");
            valid = false;
        } else {
            bool foundEdge = false;
            for (ITER_FACE_EDGES(e->face, faceEdge)) {
                if (faceEdge == e.get()) {
                    foundEdge = true;
                    break;
                }
            }
            if (!foundEdge) {
                wprintf(L"Edge cannot be reached from face!\n");
                valid = false;
            }
        }
        if (!vertices.count(e->vert)) {
            wprintf(L"Edge has invalid vertex reference!\n");
            valid = false;
        } else {
            bool foundEdge = false;
            for (ITER_VERTEX_EDGES(e->vert, vertEdge)) {
                if (vertEdge == e.get()) {
                    foundEdge = true;
                    break;
                }
            }
            if (!foundEdge) {
                wprintf(L"Edge cannot be reached from vertex!\n");
                valid = false;
            }
            if (e->vert == e->twin->vert) {
                wprintf(L"Edge between single vertex!\n");
                valid = false;
            }
        }
    }

    if (!valid)
        wprintf(L"== Surface is not valid ==\n");
    return valid;
}

bool splitEdge(Surface *surface, HEdge *edge) {
    HEdge *newEdge = surface->newEdge();
    HEdge *newTwin = surface->newEdge();
    linkTwins(newEdge, newTwin);

    // insert newEdge between edge and edge->next
    HEdge *next = edge->next, *twinPrev = edge->twin->prev;
    linkNext(newEdge, next);
    linkNext(twinPrev, newTwin);
    linkNext(edge, newEdge);
    linkNext(newTwin, edge->twin);

    Vertex *newVert = surface->newVertex();
    newVert->pos = (edge->vert->pos + edge->twin->vert->pos) / 2.0f;
    newVert->edge = newEdge;

    newEdge->vert = newVert;
    newTwin->vert = edge->twin->vert;
    newTwin->vert->edge = newTwin; // in case it was edge->twin
    edge->twin->vert = newVert;

    newEdge->face = edge->face;
    newTwin->face = edge->twin->face;
    return true;
}

bool splitFace(Surface *surface, HEdge *e1, HEdge *e2) {
    if (e1->face != e2->face) {
        wprintf(L"Edges must share a common face!\n");
        return false;
    } else if (e1->next == e2 || e2->next == e1) {
        wprintf(L"Edge already exists between these vertices!\n");
        return false;
    }

    HEdge *newEdge1 = surface->newEdge();
    HEdge *newEdge2 = surface->newEdge();
    linkTwins(newEdge1, newEdge2);

    newEdge1->vert = e1->vert;
    newEdge2->vert = e2->vert;

    HEdge *e1Prev = e1->prev, *e2Prev = e2->prev;
    linkNext(newEdge1, e2);
    linkNext(newEdge2, e1);
    linkNext(e1Prev, newEdge1);
    linkNext(e2Prev, newEdge2);

    newEdge1->face = e1->face;
    newEdge1->face->edge = newEdge1;
    Face *newFace = surface->newFace();
    newFace->edge = newEdge2;
    for (ITER_FACE_EDGES(newFace, newFaceEdge))
        newFaceEdge->face = newFace;
    return true;
}

bool addFaceVertex(Surface *surface, HEdge *edge) {
    HEdge *newEdge = surface->newEdge();
    HEdge *newTwin = surface->newEdge();
    linkTwins(newEdge, newTwin);

    linkNext(newTwin, newEdge);
    linkNext(edge->prev, newTwin);
    linkNext(newEdge, edge);

    Vertex *newVert = surface->newVertex();
    newVert->pos = edge->vert->pos;
    newVert->edge = newEdge;

    newEdge->vert = newVert;
    newTwin->vert = edge->vert;
    newTwin->vert->edge = newTwin; // in case it was edge

    newEdge->face = edge->face;
    newTwin->face = edge->face;
    return true;
}

bool removeTwoSidedFace(Surface *surface, Face *face) {
    if (face->edge->next->next != face->edge)
        return false; // face has more than two sides
    wprintf(L"Deleting face\n");
    HEdge *edge1 = face->edge, *edge2 = face->edge->next;
    edge1->vert->edge = edge1->twin->next;
    edge2->vert->edge = edge2->twin->next;
    edge1->twin->twin = edge2->twin;
    edge2->twin->twin = edge1->twin;
    surface->deleteEdge(edge1);
    surface->deleteEdge(edge2);
    surface->deleteFace(face);
    return true;
}

bool mergeVerticesAlongEdge(Surface *surface, HEdge *edge) {
    // similar structure to deleteEdge
    HEdge *twin = edge->twin;
    Vertex *keepVert = edge->vert, *oldVert = twin->vert;
    for (ITER_VERTEX_EDGES(oldVert, vertEdge))
        vertEdge->vert = keepVert;
    surface->deleteVertex(oldVert);

    if (edge->next == twin) {
        edge->face->edge = edge->prev;
        edge->vert->edge = edge->prev;
    } else {
        edge->face->edge = edge->next;
        edge->vert->edge = edge->next;
    }
    if (twin->next == edge) {
        twin->face->edge = twin->prev;
    } else {
        twin->face->edge = twin->next;
    }

    // this works even if prev or next == twin
    linkNext(edge->prev, edge->next);
    linkNext(twin->prev, twin->next);
    // TODO detect if entire solid should be deleted
    removeTwoSidedFace(surface, edge->face);
    removeTwoSidedFace(surface, twin->face);
    surface->deleteEdge(edge);
    surface->deleteEdge(twin);
    return true;
}

// TODO: mergeVerticesOnFace() -- equivalent to splitFace + mergeVerticesAlongEdge

bool deleteEdge(Surface *surface, HEdge *edge) {
    HEdge *twin = edge->twin;
    if (edge->face != twin->face) {
        Face *keepFace = edge->face, *oldFace = twin->face;
        for (ITER_FACE_EDGES(oldFace, faceEdge))
            faceEdge->face = keepFace;
        surface->deleteFace(oldFace);
    } else if (edge->next != twin && edge->prev != twin) {
        wprintf(L"Deleting this edge would create a hole in the face!\n");
        return false;
    }

    if (edge->next == twin) {
        surface->deleteVertex(twin->vert);
        edge->face->edge = edge->prev;
    } else {
        twin->vert->edge = edge->next;
        edge->face->edge = edge->next;
    }
    if (twin->next == edge) {
        surface->deleteVertex(edge->vert);
    } else {
        edge->vert->edge = twin->next;
    }

    linkNext(edge->prev, twin->next);
    linkNext(twin->prev, edge->next);
    surface->deleteEdge(edge);
    surface->deleteEdge(twin);
    // TODO detect if entire solid should be deleted
    return true;
}

bool extrudeFace(Surface *surface, Face *face) {
    // face will become the top face
    HEdge *topFirst = nullptr, *topPrev = nullptr;
    for (ITER_FACE_EDGES(face, baseEdge)) {
        HEdge *topEdge = surface->newEdge();
        HEdge *topTwin = surface->newEdge();
        linkTwins(topEdge, topTwin);
        HEdge *joinEdge = surface->newEdge();
        HEdge *joinTwin = surface->newEdge();
        linkTwins(joinEdge, joinTwin);

        // incomplete side face loop
        linkNext(topTwin, joinEdge);
        linkNext(joinEdge, baseEdge);
        // top face loop
        if (topPrev)
            linkNext(topPrev, topEdge);
        else
            topFirst = topEdge;
        topPrev = topEdge;

        Vertex *topVert = surface->newVertex();
        topVert->pos = baseEdge->vert->pos;
        topVert->edge = joinEdge;
        joinEdge->vert = topVert;
        topEdge->vert = topVert;
        joinTwin->vert = baseEdge->vert;

        Face *sideFace = surface->newFace();
        sideFace->edge = joinEdge;
        joinEdge->face = sideFace;
        topTwin->face = sideFace;
        baseEdge->face = sideFace;

        topEdge->face = face;
    }
    linkNext(topPrev, topFirst);
    face->edge = topFirst;

    for (ITER_FACE_EDGES(face, topEdge)) {
        // complete side face loop
        linkNext(topEdge->next->twin->next->twin, topEdge->twin);
        linkNext(topEdge->twin->next->next, topEdge->twin->prev);

        topEdge->twin->prev->face = topEdge->twin->face;
        topEdge->twin->vert = topEdge->next->vert;
    }
    return true;
}

void tessVertexCallback(Vertex *vertex) {
    glTexCoord2f(vertex->pos.x, vertex->pos.y);
    glVertex3fv(glm::value_ptr(vertex->pos));
}

void tessErrorCallback(GLenum errorCode) {
    wprintf(L"Tessellation error: %S\n", gluErrorString(errorCode));
}

LRESULT CALLBACK mainWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            PIXELFORMATDESCRIPTOR formatDesc = {sizeof(formatDesc)};
            formatDesc.nVersion = 1;
            formatDesc.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
            formatDesc.iPixelType = PFD_TYPE_RGBA;
            formatDesc.cColorBits = 24;
            formatDesc.cDepthBits = 32;
            formatDesc.iLayerType = PFD_MAIN_PLANE;

            HDC dc = GetDC(hwnd);
            int pixelFormat = ChoosePixelFormat(dc, &formatDesc);
            SetPixelFormat(dc, pixelFormat, &formatDesc);
            HGLRC context = wglCreateContext(dc);
            if (!context) {
                wprintf(L"Error creating context!\n");
                PostQuitMessage(0);
                return 0;
            }
            if (!wglMakeCurrent(dc, context))
                wprintf(L"Unable to make context current!\n");

            glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
            glEnable(GL_CULL_FACE);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0, 1.0);

            tess = gluNewTess();
            gluTessCallback(tess, GLU_TESS_BEGIN, (GLvoid (*) ())glBegin);
            gluTessCallback(tess, GLU_TESS_END, (GLvoid (*) ())glEnd);
            gluTessCallback(tess, GLU_TESS_VERTEX, (GLvoid (*) ())tessVertexCallback);
            gluTessCallback(tess, GLU_TESS_ERROR, (GLvoid (*) ())tessErrorCallback);
            // gluTessCallback(tess, GLU_TESS_COMBINE, (GLvoid (*) ())tessCombineCallback);
            // gluTessCallback(tess, GLU_TESS_EDGE_FLAG, (GLvoid (*) ())glEdgeFlag);

            HBITMAP textureHBitmap = LoadBitmap(GetModuleHandle(nullptr),
                MAKEINTRESOURCE(IDR_DEFAULT_TEXTURE));
            if (!textureHBitmap)
                wprintf(L"Error loading bitmap %d\n", GetLastError());
            BITMAP textureBitmap;
            GetObject(textureHBitmap, sizeof(textureBitmap), &textureBitmap);
            GLuint textureName;
            glGenTextures(1, &textureName);
            glBindTexture(GL_TEXTURE_2D, textureName);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureBitmap.bmWidth, textureBitmap.bmHeight,
                0, GL_RGB, GL_UNSIGNED_BYTE, textureBitmap.bmBits);
            // gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, textureBitmap.bmWidth, textureBitmap.bmHeight,
            //     GL_R, GL_UNSIGNED_BYTE, textureBitmap.bmBits);

            return 0;
        }
        case WM_DESTROY: {
            gluDeleteTess(tess);
            HGLRC context = wglGetCurrentContext();
            if (context) {
                HDC dc = wglGetCurrentDC();
                wglMakeCurrent(nullptr, nullptr);
                ReleaseDC(hwnd, dc);
                wglDeleteContext(context);
            }
            return 0;
        }
        case WM_NCDESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_SIZE: {
            glViewport(0, 0, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            windowDim = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

            glMatrixMode(GL_PROJECTION);
            projMat = glm::perspective(
                glm::radians(60.0f), (float)windowDim.x / windowDim.y, 0.1f, 100.0f);
            glLoadMatrixf(glm::value_ptr(projMat));
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (DragDetect(hwnd, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)})) {
                lastMouseX = GET_X_LPARAM(lParam);
                lastMouseY = GET_Y_LPARAM(lParam);
                SetCapture(hwnd);
            } else {
                if (!(GetKeyState(VK_SHIFT) < 0))
                    selectedVertices.clear();
                glm::vec2 cursor = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                auto result = picker.pickSurfaceElement(&theSurface, Surface::VERTEX,
                    cursor, windowDim, projMat * mvMat);
                if (result.type == Surface::VERTEX) {
                    selectedVertices.insert(result.vertex);
                    selectedEdge = result.vertex->edge;
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            ReleaseCapture();
            return 0;
        case WM_RBUTTONDOWN:
            lastMouseX = GET_X_LPARAM(lParam);
            lastMouseY = GET_Y_LPARAM(lParam);
            SetCapture(hwnd);
            return 0;
        case WM_RBUTTONUP:
            ReleaseCapture();
            return 0;
        case WM_MOUSEMOVE: {
            int mouseX = GET_X_LPARAM(lParam), mouseY = GET_Y_LPARAM(lParam);
            if (wParam & MK_RBUTTON) {
                rotX += glm::radians((float)(mouseY - lastMouseY)) * 0.5f;
                rotY += glm::radians((float)(mouseX - lastMouseX)) * 0.5f;
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (wParam & MK_LBUTTON) {
                glm::vec3 delta;
                if (GetKeyState(VK_SHIFT) < 0) {
                    delta = {0, -(mouseY - lastMouseY), 0};
                } else {
                    delta = {mouseX - lastMouseX, 0, mouseY - lastMouseY};
                    delta = glm::rotateY(delta, -rotY);
                }
                for (auto &vert : selectedVertices)
                    vert->pos += delta / 150.0f;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            lastMouseX = mouseX;
            lastMouseY = mouseY;
            return 0;
        }
        case WM_KEYDOWN:
            switch (wParam) {
                // selection
                case 'T':
                    selectedEdge = selectedEdge->twin;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case 'N':
                    if (GetKeyState(VK_SHIFT) < 0)
                        selectedEdge = selectedEdge->prev;
                    else
                        selectedEdge = selectedEdge->next;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case 'O':
                    if (GetKeyState(VK_SHIFT) < 0)
                        selectedEdge = selectedEdge->prev->twin;
                    else
                        selectedEdge = selectedEdge->twin->next;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case 'I':
                    if (GetKeyState(VK_SHIFT) < 0)
                        selectedEdge = selectedEdge->twin->prev;
                    else
                        selectedEdge = selectedEdge->next->twin;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case 'F':
                    if (GetKeyState(VK_SHIFT) >= 0)
                        selectedVertices.clear();
                    for (ITER_FACE_EDGES(selectedEdge->face, faceEdge))
                        selectedVertices.insert(faceEdge->vert);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case VK_RETURN:
                    wprintf(L"Store edge\n");
                    storedEdge = selectedEdge;
                    return 0;
                // operations
                case 'D':
                    if (GetKeyState(VK_SHIFT) < 0) {
                        Vertex *selectedVertex = selectedEdge->vert;
                        if (mergeVerticesAlongEdge(&theSurface, selectedEdge))
                            selectedEdge = selectedVertex->edge;
                    } else {
                        splitEdge(&theSurface, selectedEdge);
                    }
                    validateSurface(&theSurface);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case 'E':
                    if (GetKeyState(VK_SHIFT) < 0) {
                        Face *selectedFace = selectedEdge->face;
                        if (deleteEdge(&theSurface, selectedEdge))
                            selectedEdge = selectedFace->edge;
                    } else {
                        if (!storedEdge) {
                            wprintf(L"Must have an edge stored!\n");
                        } else {
                            splitFace(&theSurface, storedEdge, selectedEdge);
                        }
                    }
                    validateSurface(&theSurface);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case 'V':
                    if (addFaceVertex(&theSurface, selectedEdge)) {
                        selectedEdge = selectedEdge->prev;
                        selectedVertices.clear();
                        selectedVertices.insert(selectedEdge->vert);
                        wprintf(L"Added vertex\n");
                    }
                    validateSurface(&theSurface);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                case 'P': {
                    Face *extrudedFace = selectedEdge->face;
                    if (extrudeFace(&theSurface, extrudedFace)) {
                        selectedVertices.clear();
                        for (ITER_FACE_EDGES(extrudedFace, faceEdge))
                            selectedVertices.insert(faceEdge->vert);
                        wprintf(L"Extruded face\n");
                    }
                    validateSurface(&theSurface);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
        case WM_PAINT: {
            if (!selectedEdge)
                wprintf(L"No edge selected!!\n");

            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            mvMat = glm::mat4(1);
            mvMat = glm::translate(mvMat, glm::vec3(0, 0, -5));
            mvMat = glm::rotate(mvMat, rotX, glm::vec3(1, 0, 0));
            mvMat = glm::rotate(mvMat, rotY, glm::vec3(0, 1, 0));
            glLoadMatrixf(glm::value_ptr(mvMat));

            glColor3f(1, 1, 1);
            glBegin(GL_LINES);
            for (auto &edge : theSurface.edges) {
                if (edge.get() != selectedEdge && edge->twin != selectedEdge 
                        && edge->primary() == edge.get()) {
                    glm::vec3 v1 = edge->vert->pos, v2 = edge->twin->vert->pos;
                    glVertex3fv(glm::value_ptr(v1));
                    glVertex3fv(glm::value_ptr(v2));
                }
            }
            glEnd();

            glLineWidth(5);
            glBegin(GL_LINE_STRIP);
            {
                glm::vec3 v1 = selectedEdge->vert->pos, v2 = selectedEdge->twin->vert->pos;
                glColor3f(0.3f, 1, 0.3f);
                glVertex3fv(glm::value_ptr(v2));
                glColor3f(1, 0.3f, 0.3f);
                glVertex3fv(glm::value_ptr(v1));
                glm::vec3 normPoint = v1 + selectedEdge->face->normal() * 0.4f;
                glVertex3fv(glm::value_ptr(normPoint));
            }
            glEnd();
            glLineWidth(1);

            glColor3f(0, 1, 0);
            glPointSize(9);
            glBegin(GL_POINTS);
            for (auto &vertex : theSurface.vertices) {
                glm::vec3 v = vertex->pos;
                bool selected = selectedVertices.count(vertex.get());
                if (selected)
                    glColor3f(1, 0, 0);
                glVertex3fv(glm::value_ptr(v));
                if (selected)
                    glColor3f(0, 1, 0);
            }
            glEnd();

            glColor3f(0, 0, 1);
            glEnable(GL_TEXTURE_2D);
            // glPolygonMode(GL_FRONT, GL_LINE);
            // TODO cache faces!
            for (auto &face : theSurface.faces) {
                if (face.get() == selectedEdge->face)
                    glColor3f(0, 0.5, 1);
                // https://www.glprogramming.com/red/chapter11.html
                glm::vec3 normal = face->normal();
                gluTessNormal(tess, normal.x, normal.y, normal.z);
                gluTessBeginPolygon(tess, nullptr);
                gluTessBeginContour(tess);
                for (ITER_FACE_EDGES(face, edge)) {
                    glm::dvec3 dPos = edge->vert->pos;
                    gluTessVertex(tess, glm::value_ptr(dPos), edge->vert);
                }
                gluTessEndContour(tess);
                gluTessEndPolygon(tess);
                if (face.get() == selectedEdge->face)
                    glColor3f(0, 0, 1);
            }
            glDisable(GL_TEXTURE_2D);


            HDC dc = GetDC(hwnd);
            SwapBuffers(dc);

            EndPaint(hwnd, &ps);
            return 0;
        }
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

int main(int, char **) {
    makeCube(&theSurface);
    validateSurface(&theSurface);

    // register window class
    WNDCLASS mainWindowClass = {};
    mainWindowClass.lpszClassName = L"WingEd Main Window";
    mainWindowClass.hInstance = GetModuleHandle(nullptr);
    mainWindowClass.lpfnWndProc = mainWindowProc;
    mainWindowClass.style = CS_HREDRAW | CS_VREDRAW;
    mainWindowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClass(&mainWindowClass);

    HWND window = CreateWindow(L"WingEd Main Window", L"WingEd", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 600,
        nullptr, nullptr, GetModuleHandle(nullptr), 0);
    ShowWindow(window, SW_SHOWNORMAL);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
