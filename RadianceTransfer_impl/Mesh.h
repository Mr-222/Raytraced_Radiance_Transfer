#pragma once

#include <vector>
#include "../../Common/GeometryGenerator.h"

using namespace std;

class Mesh
{
    using uint16 = GeometryGenerator::uint16;
    using uint32 = GeometryGenerator::uint32;
    using MeshData = GeometryGenerator::MeshData;
    using Vertex = GeometryGenerator::Vertex;

public:
    Mesh(vector<Vertex> vertices, vector<uint32> indices)
    {
        this->vertices = vertices;
        this->indices = indices;
    }

    MeshData CreateMesh()
    {
        MeshData mesh;
        mesh.Vertices = vertices;
        mesh.Indices32 = indices;
        return mesh;
    }

public:
    // mesh Data
    vector<Vertex> vertices;
    vector<uint32> indices;
};

