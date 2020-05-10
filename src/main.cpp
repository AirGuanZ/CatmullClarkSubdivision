#include <iostream>

#include <agz/utility/d3d11/ImGui/imgui.h>
#include <agz/utility/d3d11/ImGui/imfilebrowser.h>
#include <agz/utility/mesh.h>
#include <agz/utility/time.h>

#include <catmull_clark/catmull_clark.h>
#include <catmull_clark/renderer.h>

/**
 * @brief 从指定obj文件中加载网格模型
 */
Mesh loadMesh(const std::string &filename)
{
    Mesh mesh;

    auto faces = agz::mesh::load_from_obj(filename);
    for(auto &f : faces)
    {
        auto index = static_cast<Face::Index>(mesh.vertices.size());

        mesh.vertices.push_back({ f.vertices[0].position });
        mesh.vertices.push_back({ f.vertices[1].position });
        mesh.vertices.push_back({ f.vertices[2].position });

        if(f.is_quad)
        {
            mesh.vertices.push_back({ f.vertices[3].position });
            mesh.faces.push_back({ true, { index, index + 1, index + 2, index + 3 } });
        }
        else
        {
            mesh.faces.push_back({ false, { index, index + 1, index + 2 } });
        }
    }

    return mesh;
}

/**
 * @brief 计算将模型从本地变换到[-0.5, +0.5]^3中的变换矩阵
 */
Mat4 localToUnitCube(const Mesh &mesh)
{
    Vec3 low((std::numeric_limits<float>::max)());
    Vec3 high((std::numeric_limits<float>::lowest)());

    for(auto &f : mesh.faces)
    {
        int vertexCount = f.isQuad ? 4 : 3;

        for(int i = 0; i < vertexCount; ++i)
        {
            auto &v = mesh.vertices[f.indices[i]];

            low.x = (std::min)(low.x, v.position.x);
            low.y = (std::min)(low.y, v.position.y);
            low.z = (std::min)(low.z, v.position.z);

            high.x = (std::max)(high.x, v.position.x);
            high.y = (std::max)(high.y, v.position.y);
            high.z = (std::max)(high.z, v.position.z);
        }
    }

    float maxExtent = (high - low).max_elem();
    float scale = 1 / maxExtent;
    Vec3 trans  = -0.5f * (low + high);
    return Trans4::translate(trans) * Trans4::scale(scale, scale, scale);
}

void run()
{
    D3D::WindowDesc windowDesc;
    windowDesc.clientWidth  = 1200;
    windowDesc.clientHeight = 600;
    windowDesc.windowTitle  = L"Catmull-Clark Subdivision Demo";
    windowDesc.sampleCount  = 4;

    D3D::Window window;
    window.Initialize(windowDesc);

    auto mouse    = window.GetMouse();
    auto keyboard = window.GetKeyboard();

    // 投影矩阵

    Mat4 proj = Trans4::perspective(
        agz::math::deg2rad(30.0f), window.GetClientAspectRatio(), 0.1f, 100.0f);

    D3D::WindowResizeHandler windowResizeHandler(
        [&](const D3D::WindowResizeEvent &e)
    {
        proj = Trans4::perspective(
            agz::math::deg2rad(30.0f), window.GetClientAspectRatio(), 0.1f, 100.0f);
    });
    window.Attach(&windowResizeHandler);

    // obj browser

    ImGui::FileBrowser fileBrowser;
    fileBrowser.SetTitle("select obj");
    fileBrowser.SetTypeFilters({ ".obj" });

    // 加载初始模型

    int subdivisionCount = 0;
    Mesh originalMesh   = loadMesh("./asset/cube.obj");
    Mesh subdividedMesh = applyCatmullClarkSubdivision(originalMesh, subdivisionCount);

    Renderer renderer;
    renderer.setWorldTransform(localToUnitCube(originalMesh));
    renderer.setMesh(subdividedMesh);

    // 绘制状态

    bool wireframe = false;
    float cameraVertRad = 0.5f;
    float cameraHoriRad = 0.2f;
    float cameraDistance = 5;

    // 注册鼠标滚轮回调函数

    D3D::WheelScrollHandler wheelScrollHandler(
        [&](const D3D::WheelScrollEvent &e)
    {
        cameraDistance -= 0.002f * e.offset;
        cameraDistance = agz::math::clamp(cameraDistance, 1.0f, 10.0f);
    });
    mouse->Attach(&wheelScrollHandler);

    bool exitMainloop = false;
    while(!exitMainloop && !window.GetCloseFlag())
    {
        window.DoEvents();
        window.WaitForFocus();
        window.ImGuiNewFrame();

        if(keyboard->IsKeyDown(D3D::KEY_ESCAPE))
        {
            exitMainloop = true;
        }

        // 摄像机调整

        if(mouse->IsMouseButtonPressed(D3D::MouseButton::Middle))
        {
            cameraHoriRad -= 0.01f * mouse->GetRelativeCursorPositionX();
            cameraVertRad += 0.01f * mouse->GetRelativeCursorPositionY();
            cameraVertRad = agz::math::clamp(cameraVertRad, -agz::math::PI_f / 2 + 0.01f, agz::math::PI_f / 2 - 0.01f);
        }
        Vec3 cameraPos = cameraDistance * Vec3(
            std::cos(cameraVertRad) * std::cos(cameraHoriRad),
            std::sin(cameraVertRad),
            std::cos(cameraVertRad) * std::sin(cameraHoriRad));
        Vec3 lightDir = -Vec3(
            std::cos(cameraVertRad + 0.3f) * std::cos(cameraHoriRad - 0.2f),
            std::sin(cameraVertRad + 0.3f),
            std::cos(cameraVertRad + 0.3f) * std::sin(cameraHoriRad - 0.2f)).normalize();
        Mat4 view = Trans4::look_at(cameraPos, { 0, 0, 0 }, { 0, 1, 0 });
        renderer.setLightDir(lightDir);
        renderer.setCameraViewProj(view * proj);

        // GUI

        if(ImGui::Begin("debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if(ImGui::Checkbox("wireframe", &wireframe))
            {
                renderer.setWireframe(wireframe);
            }

            ImGui::PushItemWidth(200);
            if(ImGui::SliderInt("subdivision", &subdivisionCount, 0, 5))
            {
                agz::time::clock_t clock;
                subdividedMesh = applyCatmullClarkSubdivision(originalMesh, subdivisionCount);
                std::cout << "time: " << clock.us() / 1000.0f / 100 << "ms" << std::endl;
                renderer.setMesh(subdividedMesh);
            }
            ImGui::PopItemWidth();

            if(ImGui::Button("select obj"))
            {
                fileBrowser.Open();
            }

            ImGui::Text("vertex:   %d", renderer.getVertexCount());
            ImGui::Text("edge:     %d", renderer.getEdgeCount());
            ImGui::Text("quad:     %d", renderer.getQuadCount());
            ImGui::Text("triangle: %d", renderer.getTriangleCount());
        }
        ImGui::End();

        fileBrowser.Display();

        if(fileBrowser.HasSelected())
        {
            AGZ_SCOPE_GUARD({ fileBrowser.ClearSelected(); });

            subdivisionCount = 0;
            originalMesh = loadMesh(fileBrowser.GetSelected().string());
            subdividedMesh = originalMesh;

            renderer.setWorldTransform(localToUnitCube(originalMesh));
            renderer.setMesh(subdividedMesh);
        }

        // rendering

        window.ClearDefaultDepthStencil();
        window.ClearDefaultRenderTarget();

        renderer.render();

        window.ImGuiRender();
        window.SwapBuffers();
    }
}

int main()
{
    try
    {
        run();
    }
    catch(const std::exception &err)
    {
        std::cout << err.what() << std::endl;
        return -1;
    }
}
