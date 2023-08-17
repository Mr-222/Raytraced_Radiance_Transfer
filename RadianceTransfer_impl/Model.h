#pragma once

#include <iostream>
#include <vector>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include "Mesh.h"

using namespace std;

class Model
{
public:
    using uint16 = GeometryGenerator::uint16;
    using uint32 = GeometryGenerator::uint32;
    using MeshData = GeometryGenerator::MeshData;
    using Vertex = GeometryGenerator::Vertex;

    // model data 
    vector<Mesh> meshes;
    string directory;

    // constructor, expects a filepath to a 3D model.
    Model(string const& path)
    {
        loadModel(path);
    }

    MeshData CreateModel()
    {
        if (!meshes.empty())
        {
            MeshData data;
            int offset = 0; // index offset

            for (int i = 0; i < meshes.size(); ++i) // for every mesh...
            {
                data.Vertices.insert(data.Vertices.end(), meshes[i].vertices.cbegin(), meshes[i].vertices.cend());
                for_each(meshes[i].indices.begin(), meshes[i].indices.end(),
                    [&offset](uint32& index) {
                        index += offset;
                    });
                data.Indices32.insert(data.Indices32.end(), meshes[i].indices.cbegin(), meshes[i].indices.cend());
                offset += meshes[i].indices.size();
            }

            return data;
        }
        else
            throw std::logic_error("0 Mesh");
    }

private:
    // loads a model with supported ASSIMP extensions from file and stores the resulting meshes in the meshes vector.
    void loadModel(string const& path)
    {
        // read file via ASSIMP
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path, aiProcess_GenUVCoords | aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace | aiProcess_FixInfacingNormals);
        // check for errors
        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) // if is Not Zero
        {
            throw std::logic_error(importer.GetErrorString());
            cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << endl;
            return;
        }
        // retrieve the directory path of the filepath
        directory = path.substr(0, path.find_last_of('/'));

        // process ASSIMP's root node recursively
        processNode(scene->mRootNode, scene);
    }

    // processes a node in a recursive fashion. Processes each individual mesh located at the node and repeats this process on its children nodes (if any).
    void processNode(aiNode* node, const aiScene* scene)
    {
        // process each mesh located at the current node
        for (unsigned int i = 0; i < node->mNumMeshes; i++)
        {
            // the node object only contains indices to index the actual objects in the scene. 
            // the scene contains all the data, node is just to keep stuff organized (like relations between nodes).
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(processMesh(mesh, scene));
        }
        // after we've processed all of the meshes (if any) we then recursively process each of the children nodes
        for (unsigned int i = 0; i < node->mNumChildren; i++)
        {
            processNode(node->mChildren[i], scene);
        }

    }

    Mesh processMesh(aiMesh* mesh, const aiScene* scene)
    {
        // data to fill
        vector<Vertex> vertices;
        vector <uint32> indices;

        // walk through each of the mesh's vertices
        for (unsigned int i = 0; i < mesh->mNumVertices; i++)
        {
            Vertex vertex;
            DirectX::XMFLOAT3 vector; // we declare a placeholder vector since assimp uses its own vector class that doesn't directly convert to glm's vec3 class so we transfer the data to this placeholder glm::vec3 first.
            // positions
            vector.x = mesh->mVertices[i].x;
            vector.y = mesh->mVertices[i].y;
            vector.z = mesh->mVertices[i].z;
            vertex.Position = vector;
            // normals
            if (mesh->HasNormals())
            {
                vector.x = mesh->mNormals[i].x;
                vector.y = mesh->mNormals[i].y;
                vector.z = mesh->mNormals[i].z;
                vertex.Normal = vector;
            }
            // texture coordinates
            if (mesh->mTextureCoords[0]) // does the mesh contain texture coordinates?
            {
                DirectX::XMFLOAT2 vec;
                // a vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't 
                // use models where a vertex can have multiple texture coordinates so we always take the first set (0).
                vec.x = mesh->mTextureCoords[0][i].x;
                vec.y = mesh->mTextureCoords[0][i].y;
                vertex.TexC = vec;
                // tangent
                vector.x = mesh->mTangents[i].x;
                vector.y = mesh->mTangents[i].y;
                vector.z = mesh->mTangents[i].z;
                vertex.TangentU = vector;
                // bitangent
                //vector.x = mesh->mBitangents[i].x;
                //vector.y = mesh->mBitangents[i].y;
                //vector.z = mesh->mBitangents[i].z;
                //vertex.Bitangent = vector;
            }
            else
                vertex.TexC = DirectX::XMFLOAT2(0.0f, 0.0f);

            vertices.push_back(vertex);
        }
        // now wak through each of the mesh's faces (a face is a mesh its triangle) and retrieve the corresponding vertex indices.
        for (unsigned int i = 0; i < mesh->mNumFaces; i++)
        {
            aiFace face = mesh->mFaces[i];
            // retrieve all indices of the face and store them in the indices vector
            for (unsigned int j = 0; j < face.mNumIndices; j++)
                indices.push_back(face.mIndices[j]);
        }

        // return a mesh object created from the extracted mesh data
        return Mesh(vertices, indices);
    }
};

