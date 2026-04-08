#pragma comment(linker, "/entry:WinMainCRTStartup /subsystem:console")
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <iostream>
#include <vector>
#include <chrono>


#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// [비디오 설정 관리 구조체]
struct VideoConfig
{
    int Width = 800;
    int Height = 600;
    bool IsFullscreen = false;
    bool NeedsResize = false;
    int VSync = 1;
} g_Config;

// 전역 변수
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pImmediateContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pRenderTargetView = nullptr;
ID3D11VertexShader* g_pVertexShader = nullptr;
ID3D11PixelShader* g_pPixelShader = nullptr;
ID3D11InputLayout* g_pInputLayout = nullptr;
ID3D11Buffer* g_pVertexBuffer = nullptr;

struct Vertex
{
    float x, y, z;
    float r, g, b, a;
};

class Component
{
public:
    class GameObject* pOwner = nullptr; // 이 기능이 누구의 것인지 저장
    bool isStarted = 0;           // Start()가 실행되었는지 체크

    virtual void Start() = 0;              // 초기화
    virtual void Input() {}                // 입력 (선택사항)
    virtual void Update(float dt) = 0;     // 로직 (필수)
    virtual void Render() {}               // 그리기 (선택사항)

    virtual ~Component() {}
};


struct Color { float r, g, b, a; };

class GameObject {
public:
    std::string name;
    std::vector<class Component*> components;

    // 객체의 기본 상태 정보
    float x = 0.0f, y = 0.0f;
    Color color = { 1.0f, 1.0f, 1.0f, 1.0f }; // 기본값 흰색

    // 생성자에서 이름, 위치, 색상을 한 번에 받음
    GameObject(std::string n, float startX, float startY, Color col)
        : name(n), x(startX), y(startY), color(col) {}

    ~GameObject() {
        for (auto comp : components) delete comp;
    }
    void AddComponent(Component* pComp)
    {
        pComp->pOwner = this;
        pComp->isStarted = false;
        components.push_back(pComp);
    }
};


class Transform : public Component {
public:
    void Start() override {}
    void Update(float dt) override {}
    // Render는 필요 없으므로 기본값 사용
};

class TriangleMesh : public Component {
    ID3D11Buffer* pVB = nullptr;
    float size;

    void BuildBuffer() {
        if (pVB) { pVB->Release(); pVB = nullptr; }

        float ox = pOwner->x, oy = pOwner->y;
        Color col = pOwner->color;
        Vertex vertices[] = {
            { ox,        oy + size, 0.5f, col.r, col.g, col.b, col.a },
            { ox + size, oy - size, 0.5f, col.r, col.g, col.b, col.a },
            { ox - size, oy - size, 0.5f, col.r, col.g, col.b, col.a },
        };
        D3D11_BUFFER_DESC bd = { sizeof(vertices), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER };
        D3D11_SUBRESOURCE_DATA initData = { vertices };
        g_pd3dDevice->CreateBuffer(&bd, &initData, &pVB);
    }

public:
    TriangleMesh(float s) : size(s) {}
    void Start()  override { BuildBuffer(); }
    void Update(float dt) override { BuildBuffer(); } // 매 프레임 갱신
    void Render() override {
        if (!pVB) return;
        UINT stride = sizeof(Vertex), offset = 0;
        g_pImmediateContext->IASetVertexBuffers(0, 1, &pVB, &stride, &offset);
        g_pImmediateContext->Draw(3, 0);
    }
    ~TriangleMesh() { if (pVB) pVB->Release(); }
};

class PlayerControl : public Component {
public:
    float speed = 1.0f; // DX11 좌표계(-1.0 ~ 1.0) 기준이므로 속도를 조절하세요.
    bool moveUp, moveDown, moveLeft, moveRight;

    void Start() override {
        moveUp = moveDown = moveLeft = moveRight = false;
        printf("[%s] 컨트롤러 활성화!\n", pOwner->name.c_str());
    }

    void Input() override {
        moveUp = (GetAsyncKeyState('W') & 0x8000);
        moveDown = (GetAsyncKeyState('S') & 0x8000);
        moveLeft = (GetAsyncKeyState('A') & 0x8000);
        moveRight = (GetAsyncKeyState('D') & 0x8000);
    }

    void Update(float dt) override {
        // pOwner(GameObject)의 좌표를 직접 수정
        if (moveUp)    pOwner->y += speed * dt;
        if (moveDown)  pOwner->y -= speed * dt;
        if (moveLeft)  pOwner->x -= speed * dt;
        if (moveRight) pOwner->x += speed * dt;
    }
};

class PlayerControl1 : public Component {
public:
    float speed = 1.0f; // DX11 좌표계(-1.0 ~ 1.0) 기준이므로 속도를 조절하세요.
    bool moveUp, moveDown, moveLeft, moveRight;

    void Start() override {
        moveUp = moveDown = moveLeft = moveRight = false;
        printf("[%s] 컨트롤러 활성화!\n", pOwner->name.c_str());
    }

    void Input() override {
        moveUp = (GetAsyncKeyState(VK_UP) & 0x8000);
        moveDown = (GetAsyncKeyState(VK_DOWN) & 0x8000);
        moveLeft = (GetAsyncKeyState(VK_LEFT) & 0x8000);
        moveRight = (GetAsyncKeyState(VK_RIGHT) & 0x8000);
    }

    void Update(float dt) override {
        // pOwner(GameObject)의 좌표를 직접 수정
        if (moveUp)    pOwner->y += speed * dt;
        if (moveDown)  pOwner->y -= speed * dt;
        if (moveLeft)  pOwner->x -= speed * dt;
        if (moveRight) pOwner->x += speed * dt;
    }
};

// --- [해상도 및 리소스 재구성 함수] ---
void RebuildVideoResources(HWND hWnd)
{
    if (!g_pSwapChain) return;

    // 1. 기존 렌더 타겟 뷰 해제 (안 하면 ResizeBuffers 실패함)
    if (g_pRenderTargetView)
    {
        g_pRenderTargetView->Release();
        g_pRenderTargetView = nullptr;
    }

    // 2. 백버퍼 크기 재설정
    g_pSwapChain->ResizeBuffers(0, g_Config.Width, g_Config.Height, DXGI_FORMAT_UNKNOWN, 0);

    // 3. 새 백버퍼로부터 렌더 타겟 뷰 다시 생성
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (pBackBuffer == nullptr)
    {
        printf("GETBUFFER ERROR\n");
        return;
    }
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
    pBackBuffer->Release();

    // 4. 윈도우 창 크기 실제 조정 (전체화면이 아닐 때만)
    if (!g_Config.IsFullscreen)
    {
        RECT rc = { 0, 0, g_Config.Width, g_Config.Height };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos(hWnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);
    }

    g_Config.NeedsResize = false;
    printf("[Video] Changed: %d x %d\n", g_Config.Width, g_Config.Height);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_DESTROY)
    {
        PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

// ─────────────────────────────────────────────
// WindowManager.h
// ─────────────────────────────────────────────
class WindowManager
{
public:
    HWND hWnd = nullptr;

    bool Initialize(HINSTANCE hInstance, int width, int height)
    {
        WNDCLASSEXW wcex = { sizeof(WNDCLASSEX) };
        wcex.lpfnWndProc = WndProc;
        wcex.hInstance = hInstance;
        wcex.lpszClassName = L"DX11VideoClass";
        RegisterClassExW(&wcex);

        RECT rc = { 0, 0, width, height };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

        hWnd = CreateWindowW(
            L"DX11VideoClass",
            L"F: Fullscreen | ESC: Quit",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left, rc.bottom - rc.top,
            nullptr, nullptr, hInstance, nullptr
        );

        if (!hWnd) return false;
        return true;
    }

    void Show(int nCmdShow) { ShowWindow(hWnd, nCmdShow); }
};


// ─────────────────────────────────────────────
// DX11Renderer.h
// ─────────────────────────────────────────────
class DX11Renderer
{
public:
    bool Initialize(HWND hWnd)
    {
        if (!CreateDeviceAndSwapChain(hWnd)) return false;
        RebuildVideoResources(hWnd);
        if (!CompileShaders())              return false;
        return true;
    }

    void BeginFrame()
    {
        float clearColor[] = { 0.1f, 0.2f, 0.3f, 1.0f };
        g_pImmediateContext->ClearRenderTargetView(g_pRenderTargetView, clearColor);

        D3D11_VIEWPORT vp = { 0.0f, 0.0f,
            (float)g_Config.Width, (float)g_Config.Height, 0.0f, 1.0f };
        g_pImmediateContext->RSSetViewports(1, &vp);
        g_pImmediateContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);
    }

    void SetupPipeline()
    {
        UINT stride = sizeof(Vertex), offset = 0;
        g_pImmediateContext->IASetVertexBuffers(0, 1, &g_pVertexBuffer, &stride, &offset);
        g_pImmediateContext->IASetInputLayout(g_pInputLayout);
        g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_pImmediateContext->VSSetShader(g_pVertexShader, nullptr, 0);
        g_pImmediateContext->PSSetShader(g_pPixelShader, nullptr, 0);
    }

    void EndFrame()
    {
        g_pSwapChain->Present(g_Config.VSync, 0);
    }

    void CheckResize(HWND hWnd)
    {
        if (g_Config.NeedsResize) RebuildVideoResources(hWnd);
    }

    void Shutdown()
    {
        if (g_pVertexBuffer)    g_pVertexBuffer->Release();
        if (g_pInputLayout)     g_pInputLayout->Release();
        if (g_pVertexShader)    g_pVertexShader->Release();
        if (g_pPixelShader)     g_pPixelShader->Release();
        if (g_pRenderTargetView)g_pRenderTargetView->Release();
        if (g_pSwapChain)       g_pSwapChain->Release();
        if (g_pImmediateContext)g_pImmediateContext->Release();
        if (g_pd3dDevice)       g_pd3dDevice->Release();
    }

private:
    bool CreateDeviceAndSwapChain(HWND hWnd)
    {
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 1;
        sd.BufferDesc.Width = g_Config.Width;
        sd.BufferDesc.Height = g_Config.Height;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hWnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;

        return SUCCEEDED(D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &sd, &g_pSwapChain, &g_pd3dDevice, nullptr, &g_pImmediateContext
        ));
    }

    bool CompileShaders()
    {
        const char* shaderSource = R"(
            struct VS_IN { float3 pos : POSITION; float4 col : COLOR; };
            struct PS_IN { float4 pos : SV_POSITION; float4 col : COLOR; };
            PS_IN VS(VS_IN i) { PS_IN o; o.pos = float4(i.pos,1); o.col = i.col; return o; }
            float4 PS(PS_IN i) : SV_Target { return i.col; }
        )";

        ID3DBlob* vsBlob, * psBlob;
        D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr,
            "VS", "vs_4_0", 0, 0, &vsBlob, nullptr);
        D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr,
            "PS", "ps_4_0", 0, 0, &psBlob, nullptr);

        g_pd3dDevice->CreateVertexShader(
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_pVertexShader);
        g_pd3dDevice->CreatePixelShader(
            psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_pPixelShader);

        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        g_pd3dDevice->CreateInputLayout(
            layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_pInputLayout);

        vsBlob->Release();
        psBlob->Release();
        return true;
    }
};

// ─────────────────────────────────────────────
// GameLoop.h
// ─────────────────────────────────────────────
class GameLoop
{
public:
    bool isRunning = false;
    std::vector<GameObject*> gameWorld;

    void Initialize(HINSTANCE hInstance, int nCmdShow)
    {
        // 윈도우 생성
        window.Initialize(hInstance, g_Config.Width, g_Config.Height);
        window.Show(nCmdShow);

        // DX11 초기화
        renderer.Initialize(window.hWnd);

        isRunning = true;
        prevTime = std::chrono::high_resolution_clock::now();
    }

    void Run()
    {
        MSG msg = { 0 };
        while (isRunning && WM_QUIT != msg.message)
        {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            else
            {
                // 델타타임 계산
                std::chrono::high_resolution_clock::time_point currentTime = std::chrono::high_resolution_clock::now();
                std::chrono::duration<float> elapsed = currentTime - prevTime;
                deltaTime = elapsed.count();
                prevTime = currentTime;

                Input();
                Update();
                Render();
            }
        }
    }

    ~GameLoop()
    {
        for (auto obj : gameWorld) delete obj;
        renderer.Shutdown();
    }

private:
    WindowManager window;
    DX11Renderer  renderer;
    HWND          hWnd = nullptr;
    float         deltaTime = 0.0f;
    std::chrono::high_resolution_clock::time_point prevTime;

    // ── Input ──────────────────────────────────
    void Input()
    {
        // 전역 단축키
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
        {
            PostQuitMessage(0);
            isRunning = false;
            return;
        }
        if (GetAsyncKeyState('F') & 0x0001)
        {
            g_Config.IsFullscreen = !g_Config.IsFullscreen;
            g_pSwapChain->SetFullscreenState(g_Config.IsFullscreen, nullptr);
        }

        // 컴포넌트 입력
        for (auto obj : gameWorld)
            for (auto comp : obj->components)
                comp->Input();
    }

    // ── Update ─────────────────────────────────
    void Update()
    {
        renderer.CheckResize(hWnd);

        for (auto obj : gameWorld)
        {
            for (auto comp : obj->components)
            {
                if (!comp->isStarted) { comp->Start(); comp->isStarted = true; }
                comp->Update(deltaTime);
            }
        }
    }

    // ── Render ─────────────────────────────────
    void Render()
    {
        renderer.BeginFrame();
        renderer.SetupPipeline();

        for (auto obj : gameWorld)
            for (auto comp : obj->components)
                comp->Render();

        renderer.EndFrame();
    }
};



int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    GameLoop loop;
    loop.Initialize(hInstance, nCmdShow);
    
    // [삼각형 1]
    GameObject* tri1 = new GameObject("Left", -0.5f, 0.0f, { 1,0,0,1 });
    tri1->AddComponent(new Transform());
    tri1->AddComponent(new TriangleMesh(0.2f));
    tri1->AddComponent(new PlayerControl1());
    loop.gameWorld.push_back(tri1);

    // [삼각형 2]
    GameObject* tri2 = new GameObject("Right", 0.5f, 0.2f, { 0,1,0,1 });
    tri2->AddComponent(new Transform());
    tri2->AddComponent(new TriangleMesh(0.15f));
    tri2->AddComponent(new PlayerControl());
    loop.gameWorld.push_back(tri2);

    loop.Run();

    return 0;

}