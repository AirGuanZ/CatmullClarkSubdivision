#include <stdexcept>
#include <unordered_map>

#include <catmull_clark/catmull_clark.h>

namespace
{

    struct VertexRecord
    {
        Vec3 position;
        std::vector<int> edges;
        std::vector<int> faces;
    };

    struct EdgeRecord
    {
        int low_vertex  = -1;
        int high_vertex = -1;
        int face_count  = 0;
        int faces[2]    = { -1, -1 };
    };

    struct FaceRecord
    {
        bool isQuad = false;;
        int vertices[4] = { -1, -1, -1, -1 };
        int edges   [4] = { -1, -1, -1, -1 };
    };

    /**
     * @brief 带有拓扑信息的模型
     *
     * - 位于同一位置的多个顶点被自动合并
     * - 每个顶点属于哪些边和哪些面
     * - 每条边属于哪些面，包含哪些顶点
     * - 每个面包含哪些边和哪些顶点
     * - 从顶点位置到顶点的映射
     * - 从顶点对到边的映射
     */
    struct Model
    {
        std::vector<VertexRecord> vertices;
        std::vector<EdgeRecord>   edges;
        std::vector<FaceRecord>   faces;

        std::unordered_map<Vec3, int>  positionToVertex;
        std::unordered_map<Vec2i, int> vertexPairToEdge;

        /**
         * @brief 将某个顶点挪动到指定的新位置
         */
        void moveVertex(int vertexIndex, const Vec3 &newPosition)
        {
            auto oldPosition = vertices[vertexIndex].position;
            positionToVertex.erase(oldPosition);
            if(positionToVertex.find(newPosition) != positionToVertex.end())
            {
                throw std::runtime_error(
                    "topology error in moving vertex: same position for different vertices");
            }
            positionToVertex[newPosition] = vertexIndex;
            vertices[vertexIndex].position = newPosition;
        }

        /**
         * @brief 取得position对应的顶点下标
         *
         * 若记录中没有位于此位置的顶点，将自动插入一个
         */
        int getVertexIndex(const Vec3 &position)
        {
            auto it = positionToVertex.find(position);
            if(it != positionToVertex.end())
            {
                return it->second;
            }

            int ret = static_cast<int>(vertices.size());

            VertexRecord newVertexRecord;
            newVertexRecord.position = position;
            vertices.push_back(newVertexRecord);
            positionToVertex[position] = ret;

            return ret;
        }

        /**
         * @brief 取得顶点对对应的边下标
         *
         * 若记录中没有这样一条边，将自动插入一条
         */
        int getEdgeIndex(const Vec2i &vertexPair)
        {
            auto sortedVertexPair = vertexPair.x < vertexPair.y ? vertexPair : vertexPair.yx();
            auto it = vertexPairToEdge.find(sortedVertexPair);
            if(it != vertexPairToEdge.end())
            {
                return it->second;
            }

            int ret = static_cast<int>(edges.size());

            EdgeRecord newEdgeRecord;
            newEdgeRecord.low_vertex = sortedVertexPair.x;
            newEdgeRecord.high_vertex = sortedVertexPair.y;
            edges.push_back(newEdgeRecord);
            vertexPairToEdge[sortedVertexPair] = ret;

            return ret;
        }
    };

    /**
     * @brief 将网格模型转换为带基本拓扑信息的model
     */
    Model meshToModel(const Mesh &mesh)
    {
        Model model;

        for(auto &f : mesh.faces)
        {
            // 将顶点添加到顶点表，记录其下标

            int vertexCount = f.isQuad ? 4 : 3;
            int vertexIndices[4] = { -1, -1, -1, -1 };
            for(int i = 0; i < vertexCount; ++i)
            {
                vertexIndices[i] = model.getVertexIndex(mesh.vertices[f.indices[i]].position);
            }

            // 将边添加到边表，记录其下标

            int edgeCount = vertexCount;
            int edgeIndices[4] = { -1, -1, -1, -1 };
            for(int i = 0; i < edgeCount; ++i)
            {
                int startVertex = vertexIndices[i];
                int endVertex   = vertexIndices[(i + 1) % vertexCount];
                int edgeIndex   = model.getEdgeIndex({ startVertex, endVertex });

                edgeIndices[i] = edgeIndex;
                model.vertices[startVertex].edges.push_back(edgeIndex);
                model.vertices[endVertex  ].edges.push_back(edgeIndex);
            }

            // 将面添加到面表

            FaceRecord newFaceRecord;
            newFaceRecord.isQuad = f.isQuad;

            for(int i = 0; i < vertexCount; ++i)
            {
                newFaceRecord.vertices[i] = vertexIndices[i];
            }

            for(int i = 0; i < edgeCount; ++i)
            {
                newFaceRecord.edges[i] = edgeIndices[i];
            }

            int faceIndex = static_cast<int>(model.faces.size());
            model.faces.push_back(newFaceRecord);

            // 将面的下标记入顶点和边的记录中

            for(int i = 0; i < vertexCount; ++i)
            {
                auto &vertexRecord = model.vertices[vertexIndices[i]];
                vertexRecord.faces.push_back(faceIndex);
            }

            for(int i = 0; i < edgeCount; ++i)
            {
                auto &edgeRecord = model.edges[edgeIndices[i]];
                if(edgeRecord.face_count >= 2)
                {
                    continue;
                }
                edgeRecord.faces[edgeRecord.face_count++] = faceIndex;
            }
        }

        return model;
    }

    /**
     * @brief 在oldModel上应用一次Catmull-Clark算法
     */
    Mesh applyCatmullClarkSubdivisionOnce(Model oldModel)
    {
        // 计算face points

        std::vector<Vec3> facePoints(oldModel.faces.size());
        for(size_t i = 0; i < facePoints.size(); ++i)
        {
            auto &f = oldModel.faces[i];
            int vertexCount = f.isQuad ? 4 : 3;

            Vec3 vertexSum;
            for(int j = 0; j < vertexCount; ++j)
            {
                vertexSum += oldModel.vertices[f.vertices[j]].position;
            }

            facePoints[i] = vertexSum / static_cast<float>(vertexCount);
        }

        // 计算edge points

        std::vector<Vec3> edgePoints(oldModel.edges.size());
        for(size_t i = 0; i < edgePoints.size(); ++i)
        {
            auto &e = oldModel.edges[i];
            if(e.face_count != 2)
            {
                edgePoints[i] = 0.5f * (
                    oldModel.vertices[e.low_vertex].position +
                    oldModel.vertices[e.high_vertex].position);
            }
            else
            {
                edgePoints[i] = 0.25f * (
                    oldModel.vertices[e.low_vertex].position +
                    oldModel.vertices[e.high_vertex].position +
                    facePoints[e.faces[0]] + facePoints[e.faces[1]]);
            }
        }

        // 更新vertex位置

        std::vector<Vec3> oldPositions;
        oldPositions.reserve(oldModel.vertices.size());
        for(auto &v : oldModel.vertices)
        {
            oldPositions.push_back(v.position);
        }

        for(size_t i = 0; i < oldModel.vertices.size(); ++i)
        {
            auto &v = oldModel.vertices[i];

            int n = static_cast<int>(v.faces.size());
            float m1 = static_cast<float>(n - 3) / n;
            float m2 = 1.0f / n;
            float m3 = 2.0f / n;

            Vec3 avgFacePosition;
            for(int j = 0; j < n; ++j)
            {
                avgFacePosition += facePoints[v.faces[j]];
            }
            avgFacePosition /= static_cast<float>(n);

            Vec3 avgEdgeMid;
            for(auto edgeIndex : v.edges)
            {
                auto &edge = oldModel.edges[edgeIndex];
                avgEdgeMid += 0.5f * (oldPositions[edge.low_vertex] + oldPositions[edge.high_vertex]);
            }
            avgEdgeMid /= static_cast<float>(v.edges.size());

            Vec3 newPosition = m1 * oldPositions[i] + m2 * avgFacePosition + m3 * avgEdgeMid;
            oldModel.moveVertex(static_cast<int>(i), newPosition);
        }

        // 构造新mesh

        Mesh newMesh;

        for(size_t fi = 0; fi < oldModel.faces.size(); ++fi)
        {
            auto &f = oldModel.faces[fi];
            if(f.isQuad)
            {
                Vec3 va = oldModel.vertices[f.vertices[0]].position;
                Vec3 vb = oldModel.vertices[f.vertices[1]].position;
                Vec3 vc = oldModel.vertices[f.vertices[2]].position;
                Vec3 vd = oldModel.vertices[f.vertices[3]].position;

                Vec3 eab = edgePoints[oldModel.getEdgeIndex({ f.vertices[0], f.vertices[1] })];
                Vec3 ebc = edgePoints[oldModel.getEdgeIndex({ f.vertices[1], f.vertices[2] })];
                Vec3 ecd = edgePoints[oldModel.getEdgeIndex({ f.vertices[2], f.vertices[3] })];
                Vec3 eda = edgePoints[oldModel.getEdgeIndex({ f.vertices[3], f.vertices[0] })];

                Vec3 fabcd = facePoints[fi];

                auto aIndex = static_cast<Face::Index>(newMesh.vertices.size());
                auto bIndex = aIndex + 1;
                auto cIndex = aIndex + 2;
                auto dIndex = aIndex + 3;

                auto eabIndex = aIndex + 4;
                auto ebcIndex = aIndex + 5;
                auto ecdIndex = aIndex + 6;
                auto edaIndex = aIndex + 7;

                auto fabcdIndex = aIndex + 8;

                newMesh.vertices.push_back({ va    });
                newMesh.vertices.push_back({ vb    });
                newMesh.vertices.push_back({ vc    });
                newMesh.vertices.push_back({ vd    });
                newMesh.vertices.push_back({ eab   });
                newMesh.vertices.push_back({ ebc   });
                newMesh.vertices.push_back({ ecd   });
                newMesh.vertices.push_back({ eda   });
                newMesh.vertices.push_back({ fabcd });

                newMesh.faces.push_back({ true, { edaIndex, aIndex, eabIndex, fabcdIndex } });
                newMesh.faces.push_back({ true, { eabIndex, bIndex, ebcIndex, fabcdIndex } });
                newMesh.faces.push_back({ true, { ebcIndex, cIndex, ecdIndex, fabcdIndex } });
                newMesh.faces.push_back({ true, { ecdIndex, dIndex, edaIndex, fabcdIndex } });
            }
            else
            {
                Vec3 va = oldModel.vertices[f.vertices[0]].position;
                Vec3 vb = oldModel.vertices[f.vertices[1]].position;
                Vec3 vc = oldModel.vertices[f.vertices[2]].position;

                Vec3 eab = edgePoints[oldModel.getEdgeIndex({ f.vertices[0], f.vertices[1] })];
                Vec3 ebc = edgePoints[oldModel.getEdgeIndex({ f.vertices[1], f.vertices[2] })];
                Vec3 eca = edgePoints[oldModel.getEdgeIndex({ f.vertices[2], f.vertices[0] })];

                Vec3 fabc = facePoints[fi];

                auto aIndex = static_cast<Face::Index>(newMesh.vertices.size());
                auto bIndex = aIndex + 1;
                auto cIndex = aIndex + 2;
                
                auto eabIndex = aIndex + 3;
                auto ebcIndex = aIndex + 4;
                auto ecaIndex = aIndex + 5;
                
                auto fabcIndex = aIndex + 6;

                newMesh.vertices.push_back({ va   });
                newMesh.vertices.push_back({ vb   });
                newMesh.vertices.push_back({ vc   });
                newMesh.vertices.push_back({ eab  });
                newMesh.vertices.push_back({ ebc  });
                newMesh.vertices.push_back({ eca  });
                newMesh.vertices.push_back({ fabc });

                newMesh.faces.push_back({ true, { ecaIndex, aIndex, eabIndex, fabcIndex } });
                newMesh.faces.push_back({ true, { eabIndex, bIndex, ebcIndex, fabcIndex } });
                newMesh.faces.push_back({ true, { ebcIndex, cIndex, ecaIndex, fabcIndex } });
            }
        }

        return newMesh;
    }

} // namespace anonymous

Mesh applyCatmullClarkSubdivision(const Mesh &originalMesh, int iterationCount)
{
    assert(iterationCount >= 0);

    Mesh mesh = originalMesh;
    for(int i = 0; i < iterationCount; ++i)
    {
        mesh = applyCatmullClarkSubdivisionOnce(meshToModel(mesh));
    }
    return mesh;
}
