#include <windows.h>
#include <d3d11.h>
#include <vector>
#include <cmath>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// ==================== OFFSETS (актуальные для CS2) ====================
namespace offsets {
    // client.dll
    constexpr uintptr_t dwLocalPlayerPawn = 0x180FE88;
    constexpr uintptr_t dwEntityList = 0x1949E58;
    constexpr uintptr_t dwViewMatrix = 0x1985B60;
    
    // C_CSPlayerPawn
    constexpr uintptr_t m_iHealth = 0x344;
    constexpr uintptr_t m_iTeamNum = 0x3E3;
    constexpr uintptr_t m_bIsAlive = 0xEB0;
    constexpr uintptr_t m_pGameSceneNode = 0x328;
}

// ==================== MATH ====================
struct Vector2 {
    float x, y;
    Vector2() : x(0), y(0) {}
    Vector2(float x, float y) : x(x), y(y) {}
};

struct Vector3 {
    float x, y, z;
    Vector3() : x(0), y(0), z(0) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
};

bool WorldToScreen(const Vector3& worldPos, Vector2& screenPos, float* viewMatrix) {
    float w = viewMatrix[12] * worldPos.x + viewMatrix[13] * worldPos.y + 
              viewMatrix[14] * worldPos.z + viewMatrix[15];
    
    if (w < 0.01f) return false;
    
    float invW = 1.0f / w;
    float x = (viewMatrix[0] * worldPos.x + viewMatrix[1] * worldPos.y + 
               viewMatrix[2] * worldPos.z + viewMatrix[3]) * invW;
    float y = (viewMatrix[4] * worldPos.x + viewMatrix[5] * worldPos.y + 
               viewMatrix[6] * worldPos.z + viewMatrix[7]) * invW;
    
    screenPos.x = (x + 1.0f) * 0.5f * GetSystemMetrics(SM_CXSCREEN);
    screenPos.y = (1.0f - y) * 0.5f * GetSystemMetrics(SM_CYSCREEN);
    
    return true;
}

// ==================== MEMORY ====================
uintptr_t GetModuleBase(const wchar_t* moduleName) {
    return (uintptr_t)GetModuleHandleW(moduleName);
}

template<typename T>
T Read(uintptr_t address) {
    return *(T*)address;
}

uintptr_t GetEntity(uintptr_t entityList, int index) {
    uintptr_t listEntry = Read<uintptr_t>(entityList + (8 * (index & 0x7F) + 16));
    if (!listEntry) return 0;
    return Read<uintptr_t>(listEntry + 0x80 * (index >> 7) + 0x78);
}

Vector3 GetBonePosition(uintptr_t playerPawn, int boneId) {
    uintptr_t gameSceneNode = Read<uintptr_t>(playerPawn + offsets::m_pGameSceneNode);
    if (!gameSceneNode) return Vector3(0, 0, 0);
    
    uintptr_t boneArray = Read<uintptr_t>(gameSceneNode + 0x180);
    if (!boneArray) return Vector3(0, 0, 0);
    
    Vector3 pos;
    pos.x = Read<float>(boneArray + boneId * 32 + 12);
    pos.y = Read<float>(boneArray + boneId * 32 + 16);
    pos.z = Read<float>(boneArray + boneId * 32 + 20);
    
    return pos;
}

// ==================== ESP ====================
bool g_ESPEnabled = true;
ID3D11Device* g_pDevice = nullptr;
ID3D11DeviceContext* g_pContext = nullptr;
WNDPROC g_oWndProc = nullptr;
HANDLE g_hThread = nullptr;
bool g_running = true;

void DrawBox(float x, float y, float w, float h, float r, float g, float b, float a) {
    if (!g_pContext) return;
    
    // Простой прямоугольник через буфер (упрощённо для демонстрации)
    // В реальном проекте здесь был бы полноценный рендер через DirectX
    // Для теста выводим в DebugView
    static int frameCount = 0;
    if (frameCount++ % 100 == 0) {
        char buf[256];
        sprintf_s(buf, "ESP Active - Enemy at: %.0f, %.0f (size: %.0f)", x, y, w);
        OutputDebugStringA(buf);
    }
}

void RenderESP() {
    if (!g_ESPEnabled) return;
    
    uintptr_t client = GetModuleBase(L"client.dll");
    if (!client) return;
    
    float* viewMatrix = (float*)(client + offsets::dwViewMatrix);
    if (!viewMatrix) return;
    
    uintptr_t localPlayer = Read<uintptr_t>(client + offsets::dwLocalPlayerPawn);
    if (!localPlayer) return;
    
    int localTeam = Read<int>(localPlayer + offsets::m_iTeamNum);
    if (localTeam != 2 && localTeam != 3) return;
    
    uintptr_t entityList = Read<uintptr_t>(client + offsets::dwEntityList);
    if (!entityList) return;
    
    int enemiesFound = 0;
    
    for (int i = 1; i <= 64; i++) {
        uintptr_t playerPawn = GetEntity(entityList, i);
        if (!playerPawn) continue;
        
        bool isAlive = Read<bool>(playerPawn + offsets::m_bIsAlive);
        if (!isAlive) continue;
        
        int health = Read<int>(playerPawn + offsets::m_iHealth);
        if (health <= 0) continue;
        
        int team = Read<int>(playerPawn + offsets::m_iTeamNum);
        if (team == localTeam) continue;
        
        Vector3 headPos = GetBonePosition(playerPawn, 6);
        Vector3 feetPos = GetBonePosition(playerPawn, 0);
        
        Vector2 screenHead, screenFeet;
        if (!WorldToScreen(headPos, screenHead, viewMatrix)) continue;
        if (!WorldToScreen(feetPos, screenFeet, viewMatrix)) continue;
        
        float height = screenFeet.y - screenHead.y;
        float width = height * 0.5f;
        
        enemiesFound++;
        
        // Рисуем белый бокс
        DrawBox(screenHead.x - width/2, screenHead.y, width, height, 1, 1, 1, 1);
    }
    
    // Выводим количество врагов в консоль для отладки
    static int lastEnemies = 0;
    if (enemiesFound != lastEnemies) {
        char buf[100];
        sprintf_s(buf, "[ESP] Enemies found: %d", enemiesFound);
        OutputDebugStringA(buf);
        lastEnemies = enemiesFound;
    }
}

// ==================== HOOKS ====================
LRESULT CALLBACK hkWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN && wParam == VK_INSERT) {
        g_ESPEnabled = !g_ESPEnabled;
        
        // Выводим статус в DebugView
        if (g_ESPEnabled) {
            OutputDebugStringA("[ESP] ENABLED");
        } else {
            OutputDebugStringA("[ESP] DISABLED");
        }
    }
    return CallWindowProcW(g_oWndProc, hWnd, uMsg, wParam, lParam);
}

// ==================== MAIN THREAD ====================
DWORD WINAPI MainThread(LPVOID lpParam) {
    // Ждём загрузки client.dll
    while (!GetModuleHandleW(L"client.dll")) {
        Sleep(100);
        if (!g_running) return 0;
    }
    
    // Находим окно игры
    HWND hGameWnd = nullptr;
    while (!hGameWnd) {
        hGameWnd = FindWindowW(nullptr, L"Counter-Strike 2");
        if (!hGameWnd) {
            hGameWnd = FindWindowW(nullptr, L"Counter-Strike: Global Offensive");
        }
        Sleep(100);
        if (!g_running) return 0;
    }
    
    // Устанавливаем хук окна
    g_oWndProc = (WNDPROC)SetWindowLongPtrW(hGameWnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
    
    OutputDebugStringA("[CS2] DLL injected successfully! Press INSERT to toggle ESP");
    
    // Основной цикл рендера
    while (g_running) {
        RenderESP();
        Sleep(16); // ~60 FPS
    }
    
    // Восстанавливаем оригинальный WndProc
    if (g_oWndProc) {
        SetWindowLongPtrW(hGameWnd, GWLP_WNDPROC, (LONG_PTR)g_oWndProc);
    }
    
    return 0;
}

// ==================== DLL ENTRY POINT ====================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        g_running = true;
        g_hThread = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
        break;
        
    case DLL_PROCESS_DETACH:
        g_running = false;
        if (g_hThread) {
            WaitForSingleObject(g_hThread, 1000);
            CloseHandle(g_hThread);
        }
        break;
    }
    return TRUE;
}