#include <catmull_clark/renderer.h>

namespace
{
    const char *SOLID_VERTEX_SHADER_SOURCE = R"___(
cbuffer Transform
{
    float4x4 WVP;
    float4x4 World;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal   : NORMAL;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput)0;
    output.position = mul(float4(input.position, 1), WVP);
    output.normal   = mul(float4(input.normal,   0), World);
    return output;
}
)___";

    const char *SOLID_PIXEL_SHADER_SOURCE = R"___(
cbuffer Light
{
    float3 LightDir;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal   : NORMAL;
};

float4 main(PSInput input) : SV_TARGET
{
    float lightFactor = 0.1 + 0.75 * max(0, dot(normalize(input.normal), -LightDir));
    return float4(lightFactor, lightFactor, lightFactor, 1);
};
)___";

    const char *WIREFRAME_VERTEX_SHADER_SOURCE = R"___(
cbuffer Transform
{
    float4x4 WVP;
};

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_POSITION;
};

VSOutput main(VSInput input)
{
    VSOutput output = (VSOutput)0;
    output.position = mul(float4(input.position, 1), WVP);
    return output;
}
)___";

    const char *WIREFRAME_PIXEL_SHADER_SOURCE = R"___(
struct PSInput
{
    float4 position : SV_POSITION;
};

float4 main(PSInput input) : SV_TARGET
{
    return float4(1, 1, 1, 1);
};
)___";

}

Renderer::Renderer()
{
    solidShader_.InitializeStage<D3D::SS_VS>(SOLID_VERTEX_SHADER_SOURCE);
    solidShader_.InitializeStage<D3D::SS_PS>(SOLID_PIXEL_SHADER_SOURCE);
    if(!solidShader_.IsAllStagesAvailable())
    {
        throw std::runtime_error("failed to initialize renderer solid shader");
    }

    wireframeShader_.InitializeStage<D3D::SS_VS>(WIREFRAME_VERTEX_SHADER_SOURCE);
    wireframeShader_.InitializeStage<D3D::SS_PS>(WIREFRAME_PIXEL_SHADER_SOURCE);
    if(!wireframeShader_.IsAllStagesAvailable())
    {
        throw std::runtime_error("failed to initialize renderer wireframe shader");
    }

    solidUniforms_     = solidShader_.CreateUniformManager();
    wireframeUniforms_ = wireframeShader_.CreateUniformManager();

    solidInputLayout_ = D3D::InputLayoutBuilder
        ("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, offsetof(SolidVertex, position))
        ("NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, offsetof(SolidVertex, normal))
        .Build(solidShader_);
    wireframeInputLayout_ = D3D::InputLayoutBuilder
        ("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0)
        .Build(wireframeShader_);

    solidVSTransform_.Initialize(true, nullptr);
    solidPSLight_    .Initialize(true, nullptr);
    solidUniforms_.GetConstantBufferSlot<D3D::SS_VS>("Transform")->SetBuffer(solidVSTransform_);
    solidUniforms_.GetConstantBufferSlot<D3D::SS_PS>("Light")->SetBuffer(solidPSLight_);

    wireframeVSTransform_.Initialize(true, nullptr);
    wireframeUniforms_.GetConstantBufferSlot<D3D::SS_VS>("Transform")->SetBuffer(wireframeVSTransform_);

    wireframe_ = false;
    solidRasterizerState_    .Initialize(D3D11_FILL_SOLID, D3D11_CULL_BACK,     false);
    wireframeRasterizerState_.Initialize(D3D11_FILL_WIREFRAME, D3D11_CULL_NONE, false);
}

void Renderer::setMesh(const Mesh &mesh)
{
    vertexCount_   = 0;
    edgeCount_     = 0;
    triangleCount_ = 0;
    quadCount_     = 0;

    // triangles

    std::vector<SolidVertex> vertex_data;
    for(auto &f : mesh.faces)
    {
        if(f.isQuad)
        {
            auto &v0 = mesh.vertices[f.indices[0]].position;
            auto &v1 = mesh.vertices[f.indices[1]].position;
            auto &v2 = mesh.vertices[f.indices[2]].position;
            auto &v3 = mesh.vertices[f.indices[3]].position;

            Vec3 nor0 = cross(v1 - v0, v2 - v1).normalize();
            Vec3 nor1 = cross(v2 - v0, v3 - v2).normalize();
            Vec3 nor = (nor0 + nor1).normalize();

            vertex_data.push_back({ v0, nor });
            vertex_data.push_back({ v1, nor });
            vertex_data.push_back({ v2, nor });

            vertex_data.push_back({ v0, nor });
            vertex_data.push_back({ v2, nor });
            vertex_data.push_back({ v3, nor });

            ++quadCount_;
        }
        else
        {
            auto &v0 = mesh.vertices[f.indices[0]].position;
            auto &v1 = mesh.vertices[f.indices[1]].position;
            auto &v2 = mesh.vertices[f.indices[2]].position;

            Vec3 nor = cross(v1 - v0, v2 - v1).normalize();

            vertex_data.push_back({ v0, nor });
            vertex_data.push_back({ v1, nor });
            vertex_data.push_back({ v2, nor });

            ++triangleCount_;
        }
    }

    solidBuffer_.Destroy();
    solidBuffer_.Initialize(UINT(vertex_data.size()), false, vertex_data.data());

    // wireframe

    std::vector<Vec3> vertices;
    std::unordered_map<Vec3, int> positionToVertex;
    std::unordered_set<Vec2i> isEdgeAdded;

    std::vector<Vec3> wireframeData;

    auto getVertexIndex = [&](const Vec3 &pos)
    {
        auto it = positionToVertex.find(pos);
        if(it != positionToVertex.end())
        {
            return it->second;
        }

        int ret = static_cast<int>(vertices.size());
        vertices.push_back(pos);
        positionToVertex[pos] = ret;

        return ret;
    };

    auto addEdge = [&](int v0, int v1)
    {
        if(v0 > v1)
        {
            std::swap(v0, v1);
        }

        if(isEdgeAdded.find({ v0, v1 }) != isEdgeAdded.end())
            return;

        isEdgeAdded.insert({ v0, v1 });
        wireframeData.push_back(vertices[v0]);
        wireframeData.push_back(vertices[v1]);
    };

    for(auto &f : mesh.faces)
    {
        if(f.isQuad)
        {
            int a = getVertexIndex(mesh.vertices[f.indices[0]].position);
            int b = getVertexIndex(mesh.vertices[f.indices[1]].position);
            int c = getVertexIndex(mesh.vertices[f.indices[2]].position);
            int d = getVertexIndex(mesh.vertices[f.indices[3]].position);

            addEdge(a, b);
            addEdge(b, c);
            addEdge(c, d);
            addEdge(d, a);
        }
        else
        {
            int a = getVertexIndex(mesh.vertices[f.indices[0]].position);
            int b = getVertexIndex(mesh.vertices[f.indices[1]].position);
            int c = getVertexIndex(mesh.vertices[f.indices[2]].position);

            addEdge(a, b);
            addEdge(b, c);
            addEdge(c, a);
        }
    }

    wireframeBuffer_.Destroy();
    wireframeBuffer_.Initialize(UINT(wireframeData.size()), false, wireframeData.data());

    vertexCount_ = static_cast<int>(vertices.size());
    edgeCount_   = static_cast<int>(isEdgeAdded.size());
}

void Renderer::setLightDir(const Vec3 &lightDir)
{
    solidPSLight_.SetValue({ lightDir.normalize(), 0 });
}

void Renderer::setWorldTransform(const Mat4 &world)
{
    Mat4 wvp = world * viewProj_;

    world_ = world;
    solidVSTransform_.SetValue({ wvp, world_ });
    wireframeVSTransform_.SetValue({ wvp });
}

void Renderer::setCameraViewProj(const Mat4 &viewProj)
{
    Mat4 wvp = world_ * viewProj;

    viewProj_ = viewProj;
    solidVSTransform_.SetValue({ wvp, world_ });
    wireframeVSTransform_.SetValue({ wvp });
}

void Renderer::setWireframe(bool wireframe)
{
    wireframe_ = wireframe;
}

void Renderer::render()
{
    if(wireframe_)
    {
        if(!wireframeBuffer_.IsAvailable())
        {
            return;
        }

        wireframeShader_         .Bind();
        wireframeUniforms_       .Bind();
        wireframeInputLayout_    .Bind();
        wireframeRasterizerState_.Bind();
        wireframeBuffer_         .Bind(0);

        D3D::RenderState::Draw(D3D11_PRIMITIVE_TOPOLOGY_LINELIST, wireframeBuffer_.GetVertexCount());

        wireframeBuffer_         .Unbind(0);
        wireframeRasterizerState_.Unbind();
        wireframeInputLayout_    .Unbind();
        wireframeUniforms_       .Unbind();
        wireframeShader_         .Unbind();
    }
    else
    {
        if(!solidBuffer_.IsAvailable())
        {
            return;
        }

        solidShader_         .Bind();
        solidUniforms_       .Bind();
        solidInputLayout_    .Bind();
        solidRasterizerState_.Bind();
        solidBuffer_         .Bind(0);

        D3D::RenderState::Draw(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, solidBuffer_.GetVertexCount());

        solidBuffer_         .Unbind(0);
        solidRasterizerState_.Unbind();
        solidInputLayout_    .Unbind();
        solidUniforms_       .Unbind();
        solidShader_         .Unbind();
    }
}

int Renderer::getVertexCount() const noexcept
{
    return vertexCount_;
}

int Renderer::getEdgeCount() const noexcept
{
    return edgeCount_;
}

int Renderer::getTriangleCount() const noexcept
{
    return triangleCount_;
}

int Renderer::getQuadCount() const noexcept
{
    return quadCount_;
}
