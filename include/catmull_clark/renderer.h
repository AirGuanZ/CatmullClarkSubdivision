#pragma once

#include <catmull_clark/common.h>

class Renderer : public agz::misc::uncopyable_t
{
public:

    Renderer();

    /**
     * @brief 设置被绘制的模型
     */
    void setMesh(const Mesh &mesh);

    /**
     * @brief 设置方向光的方向
     */
    void setLightDir(const Vec3 &lightDir);

    /**
     * @brief 设置模型的local to world变换矩阵
     */
    void setWorldTransform(const Mat4 &world);

    /**
     * @brief 设置摄像机view * proj矩阵
     */
    void setCameraViewProj(const Mat4 &viewProj);

    /**
     * @brief 绘制
     */
    void render();

    /**
     * @brief 设置是否使用线框模式
     *
     * 线框模式默认关闭
     */
    void setWireframe(bool wireframe);

    int getVertexCount() const noexcept;

    int getEdgeCount() const noexcept;

    int getTriangleCount() const noexcept;

    int getQuadCount() const noexcept;

private:

    struct SolidVertex
    {
        Vec3 position;
        Vec3 normal;
    };

    struct SolidVSTransform
    {
        Mat4 WVP;
        Mat4 world;
    };

    struct SolidPSLight
    {
        Vec3 light_dir;
        float pad = 0;
    };

    struct WireframeVSTransform
    {
        Mat4 WVP;
    };

    D3D::VertexBuffer<SolidVertex> solidBuffer_;
    D3D::VertexBuffer<Vec3>   wireframeBuffer_;

    D3D::Shader<D3D::SS_VS, D3D::SS_PS>         solidShader_;
    D3D::UniformManager<D3D::SS_VS, D3D::SS_PS> solidUniforms_;
    D3D::InputLayout                            solidInputLayout_;

    D3D::Shader<D3D::SS_VS, D3D::SS_PS>         wireframeShader_;
    D3D::UniformManager<D3D::SS_VS, D3D::SS_PS> wireframeUniforms_;
    D3D::InputLayout                            wireframeInputLayout_;

    D3D::ConstantBuffer<SolidVSTransform> solidVSTransform_;
    D3D::ConstantBuffer<SolidPSLight>     solidPSLight_;

    D3D::ConstantBuffer<WireframeVSTransform> wireframeVSTransform_;

    bool wireframe_;
    D3D::RasterizerState solidRasterizerState_;
    D3D::RasterizerState wireframeRasterizerState_;

    Mat4 world_;
    Mat4 viewProj_;

    int vertexCount_   = 0;
    int edgeCount_     = 0;
    int triangleCount_ = 0;
    int quadCount_     = 0;
};
