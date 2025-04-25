#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <vector>
#include <chrono>
#include <iostream>
#include <wrl/client.h>
#include <fstream>
#include <jni.h>
#include "screen_controller.cpp"
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#include <thread> 
using namespace Microsoft::WRL;
HWND g_hWnd = nullptr;
UINT g_dispW = 0, g_dispH = 0;                // Dimensions d'affichage du dessin
std::vector<int>* g_pDisplayBuffer = nullptr; // Pointeur vers ton tableau de couleurs

extern "C" {

    struct ScreenDevice {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<IDXGIOutputDuplication> duplication;
        ComPtr<ID3D11Texture2D> stagingTexture;
        std::vector<int> pixelBuffer;
        UINT width = 0;
        UINT height = 0;
        UINT reducedWidth = 0;
        UINT reducedHeight = 0;
        bool initialized = false;
    };

    static std::vector<ScreenDevice> g_screens;

    bool initializeScreen(int screenId, float reduction) {
        if (screenId < 0 || reduction <= 0) return false;

        if (screenId >= g_screens.size()) {
            g_screens.resize(screenId + 1);
        }

        if (g_screens[screenId].initialized) return true;

        HRESULT hr;
        ScreenDevice& screen = g_screens[screenId];

        D3D_FEATURE_LEVEL featureLevel;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &screen.device, &featureLevel, &screen.context);
        if (FAILED(hr)) return false;

        ComPtr<IDXGIDevice> dxgiDevice;
        hr = screen.device.As(&dxgiDevice);
        if (FAILED(hr)) return false;

        ComPtr<IDXGIAdapter> dxgiAdapter;
        hr = dxgiDevice->GetAdapter(&dxgiAdapter);
        if (FAILED(hr)) return false;

        ComPtr<IDXGIOutput> dxgiOutput;
        hr = dxgiAdapter->EnumOutputs(screenId, &dxgiOutput);
        if (FAILED(hr)) return false;

        ComPtr<IDXGIOutput1> dxgiOutput1;
        hr = dxgiOutput.As(&dxgiOutput1);
        if (FAILED(hr)) return false;

        hr = dxgiOutput1->DuplicateOutput(screen.device.Get(), &screen.duplication);
        if (FAILED(hr)) return false;

        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        ComPtr<IDXGIResource> desktopResource;
        hr = screen.duplication->AcquireNextFrame(100, &frameInfo, &desktopResource);
        if (FAILED(hr)) {
            return false;
        }

        ComPtr<ID3D11Texture2D> desktopTexture;
        hr = desktopResource.As(&desktopTexture);
        if (FAILED(hr)) {
            screen.duplication->ReleaseFrame();
            return false;
        }

        D3D11_TEXTURE2D_DESC desc;
        desktopTexture->GetDesc(&desc);
        screen.width = desc.Width;
        screen.height = desc.Height;
        screen.reducedWidth = static_cast<UINT>(screen.width / reduction);
        screen.reducedHeight = static_cast<UINT>(screen.height / reduction);

        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width = desc.Width;
        stagingDesc.Height = desc.Height;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        hr = screen.device->CreateTexture2D(&stagingDesc, nullptr, &screen.stagingTexture);
        if (FAILED(hr)) {
            screen.duplication->ReleaseFrame();
            return false;
        }

        screen.pixelBuffer.resize(screen.reducedWidth * screen.reducedHeight);

        screen.duplication->ReleaseFrame();
        screen.initialized = true;

        return true;
    }

    struct PixelResult {
        int* pixels;
        int size;
    };

    __declspec(dllexport)
    PixelResult getScreenPixels(char* screenIdStr, int ledX, int ledY, int keepPixels, float reduction) {
        PixelResult result = { nullptr, -1 };


        auto start = std::chrono::high_resolution_clock::now();

        int screenId = atoi(screenIdStr);
        if (!initializeScreen(screenId, reduction)) {
            return result;
        }

        ScreenDevice& screen = g_screens[screenId];

        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        ComPtr<IDXGIResource> desktopResource;
        HRESULT hr = screen.duplication->AcquireNextFrame(100, &frameInfo, &desktopResource);
        if (FAILED(hr)) {
            screen.duplication->ReleaseFrame();
            return result;
        }
        ComPtr<ID3D11Texture2D> desktopTexture;
        hr = desktopResource.As(&desktopTexture);
        if (FAILED(hr)) {
            screen.duplication->ReleaseFrame();
            return result;
        }

        screen.context->CopyResource(screen.stagingTexture.Get(), desktopTexture.Get());

        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = screen.context->Map(screen.stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            screen.duplication->ReleaseFrame();
            return result;
        }

        auto* pixels = static_cast<unsigned char*>(mapped.pData);
        float xScale = static_cast<float>(screen.width) / screen.reducedWidth;
        float yScale = static_cast<float>(screen.height) / screen.reducedHeight;
        UINT keepWidth = static_cast<UINT>(keepPixels);
        UINT keepHeight = static_cast<UINT>(keepPixels);
        UINT leftBound = keepWidth;
        UINT rightBound = screen.reducedWidth - keepWidth;
        UINT topBound = keepHeight;
        UINT bottomBound = screen.reducedHeight - keepHeight;

        // Fill pixel buffer
        for (UINT y = 0; y < screen.reducedHeight; ++y) {
            for (UINT x = 0; x < screen.reducedWidth; ++x) {
                int pixelIndex = y * screen.reducedWidth + x;
                bool keep = (x < leftBound || x >= rightBound || y < topBound || y >= bottomBound);
                if (keep) {
                    UINT srcX = static_cast<UINT>(x * xScale);
                    UINT srcY = static_cast<UINT>(y * yScale);
                    int index = (srcY * mapped.RowPitch) + (srcX * 4);
                    unsigned char b = pixels[index];
                    unsigned char g = pixels[index + 1];
                    unsigned char r = pixels[index + 2];
                    screen.pixelBuffer[pixelIndex] = (r << 16) | (g << 8) | b;
                } else {
                    screen.pixelBuffer[pixelIndex] = 0;
                }
            }
        }

        // Calculate averages for Ambilight
        std::vector<int> ledColors(ledX * 2 + ledY * 2); // top + bottom + left + right
        float topBottomZoneWidth = static_cast<float>(screen.reducedWidth) / ledX;
        float leftRightZoneHeight = static_cast<float>(screen.reducedHeight) / ledY;

        // Top edge (left to right)
        for (int i = 0; i < ledX; ++i) {
            UINT startX = static_cast<UINT>(i * topBottomZoneWidth);
            UINT endX = static_cast<UINT>((i + 1) * topBottomZoneWidth);
            long long rSum = 0, gSum = 0, bSum = 0;
            int count = 0;
            for (UINT x = startX; x < endX && x < screen.reducedWidth; ++x) {
                for (UINT y = 0; y < topBound; ++y) {
                    int pixel = screen.pixelBuffer[y * screen.reducedWidth + x];
                    rSum += (pixel >> 16) & 0xFF;
                    gSum += (pixel >> 8) & 0xFF;
                    bSum += pixel & 0xFF;
                    count++;
                }
            }
            if (count > 0) {
                ledColors[i] = ((rSum / count) << 16) | ((gSum / count) << 8) | (bSum / count);
            } else {
                ledColors[i] = 0;
            }
        }

        // Right edge (top to bottom)
        for (int i = 0; i < ledY; ++i) {
            UINT startY = static_cast<UINT>(i * leftRightZoneHeight);
            UINT endY = static_cast<UINT>((i + 1) * leftRightZoneHeight);
            long long rSum = 0, gSum = 0, bSum = 0;
            int count = 0;
            for (UINT y = startY; y < endY && y < screen.reducedHeight; ++y) {
                for (UINT x = rightBound; x < screen.reducedWidth; ++x) {
                    int pixel = screen.pixelBuffer[y * screen.reducedWidth + x];
                    rSum += (pixel >> 16) & 0xFF;
                    gSum += (pixel >> 8) & 0xFF;
                    bSum += pixel & 0xFF;
                    count++;
                }
            }
            if (count > 0) {
                ledColors[ledX + i] = ((rSum / count) << 16) | ((gSum / count) << 8) | (bSum / count);
            } else {
                ledColors[ledX + i] = 0;
            }
        }

        // Bottom edge (right to left)
        for (int i = 0; i < ledX; ++i) {
            UINT startX = static_cast<UINT>(i * topBottomZoneWidth);
            UINT endX = static_cast<UINT>((i + 1) * topBottomZoneWidth);
            long long rSum = 0, gSum = 0, bSum = 0;
            int count = 0;
            for (UINT x = startX; x < endX && x < screen.reducedWidth; ++x) {
                for (UINT y = bottomBound; y < screen.reducedHeight; ++y) {
                    int pixel = screen.pixelBuffer[y * screen.reducedWidth + x];
                    rSum += (pixel >> 16) & 0xFF;
                    gSum += (pixel >> 8) & 0xFF;
                    bSum += pixel & 0xFF;
                    count++;
                }
            }
            if (count > 0) {
                ledColors[ledX + ledY + i] = ((rSum / count) << 16) | ((gSum / count) << 8) | (bSum / count);
            } else {
                ledColors[ledX + ledY + i] = 0;
            }
        }

        // Left edge (bottom to top)
        for (int i = 0; i < ledY; ++i) {
            UINT startY = static_cast<UINT>(i * leftRightZoneHeight);
            UINT endY = static_cast<UINT>((i + 1) * leftRightZoneHeight);
            long long rSum = 0, gSum = 0, bSum = 0;
            int count = 0;
            for (UINT y = startY; y < endY && y < screen.reducedHeight; ++y) {
                for (UINT x = 0; x < leftBound; ++x) {
                    int pixel = screen.pixelBuffer[y * screen.reducedWidth + x];
                    rSum += (pixel >> 16) & 0xFF;
                    gSum += (pixel >> 8) & 0xFF;
                    bSum += pixel & 0xFF;
                    count++;
                }
            }
            if (count > 0) {
                ledColors[ledX * 2 + ledY + i] = ((rSum / count) << 16) | ((gSum / count) << 8) | (bSum / count);
            } else {
                ledColors[ledX * 2 + ledY + i] = 0;
            }
        }

        screen.context->Unmap(screen.stagingTexture.Get(), 0);
        screen.duplication->ReleaseFrame();

        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "[screen_capture]"  << "DEBUG (Screen " << screenId << "): Time taken: " << elapsed / 1000.0 << " ms" << std::endl;

        // Allocate and copy LED colors
        int* ledColorsArray = new int[ledColors.size()];
        std::copy(ledColors.begin(), ledColors.end(), ledColorsArray);
        result.pixels = ledColorsArray;
        result.size = ledColors.size();
        return result;
    }

    __declspec(dllexport)
        void freeMemory(const char* ptr) {
        delete[] reinterpret_cast<const int*>(ptr);
    }

}

void saveToBMP(const int* ledColors, int ledX, int ledY, UINT width, UINT height, const char* filename) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "[screen_capture]"  << "Failed to create BMP file" << std::endl;
        return;
    }

    const int padding = (4 - (width * 3) % 4) % 4;
    const int fileSize = 54 + (width * 3 + padding) * height;

    char bmpHeader[54] = {
        'B', 'M',
        0, 0, 0, 0,
        0, 0, 0, 0,
        54, 0, 0, 0,
        40, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        1, 0,
        24, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0
    };

    *(int*)(bmpHeader + 2) = fileSize;
    *(int*)(bmpHeader + 18) = width;
    *(int*)(bmpHeader + 22) = height;
    *(int*)(bmpHeader + 34) = (width * 3 + padding) * height;

    file.write(bmpHeader, 54);

    std::vector<char> rowData(width * 3 + padding);
    float topBottomZoneWidth = static_cast<float>(width) / ledX;
    float leftRightZoneHeight = static_cast<float>(height) / ledY;

    for (int y = height - 1; y >= 0; --y) {
        for (UINT x = 0; x < width; ++x) {
            int color;
            if (y < height / 16) {  // Top section
                int zone = static_cast<int>(x / topBottomZoneWidth);
                color = ledColors[zone];
            } else if (x >= width - width / 16) {  // Right section
                int zone = static_cast<int>((y - height / 16) / leftRightZoneHeight);
                color = ledColors[ledX + zone];
            } else if (y >= height - height / 16) {  // Bottom section
                int zone = static_cast<int>(x / topBottomZoneWidth);
                color = ledColors[ledX + ledY + zone];
            } else if (x < width / 16) {  // Left section
                int zone = static_cast<int>((y - height / 16) / leftRightZoneHeight);
                color = ledColors[ledX * 2 + ledY + zone];
            } else {
                color = 0;  // Center black
            }
            rowData[x * 3] = color & 0xFF;
            rowData[x * 3 + 1] = (color >> 8) & 0xFF;
            rowData[x * 3 + 2] = (color >> 16) & 0xFF;
        }
        if (padding > 0) {
            memset(rowData.data() + width * 3, 0, padding);
        }
        file.write(rowData.data(), width * 3 + padding);
    }

    file.close();
    std::cout << "[screen_capture]" << "Saved screenshot to " << filename << std::endl;
}

int main() {

    ScreenController controller;
    char* screenId = nullptr;
    HANDLE serialPort = INVALID_HANDLE_VALUE;

    // FPS limiter configuration
    const int TARGET_FPS = 60; //10;
    const std::chrono::microseconds FRAME_DURATION(1000000 / TARGET_FPS); // microseconds per frame

    // FPS and error tracking
    int frameCount = 0;
    int errorCount = 0;
    auto lastReportTime = std::chrono::steady_clock::now();

    while (true) {
        auto frameStart = std::chrono::steady_clock::now();

        if (screenId == nullptr) {
            auto it = controller.monitorsId.find("HKM3750");//GSM82C5
            if (it != controller.monitorsId.end()) {
                std::string str = std::to_string(it->second);
                screenId = new char[str.size() + 1];
                std::copy(str.begin(), str.end(), screenId);
                screenId[str.size()] = '\0';
                std::cout << "screenId set to: " << screenId << std::endl;
            }
            else {
                Sleep(200);
                continue;
            }
        }

        if (serialPort == INVALID_HANDLE_VALUE) {
            if (controller.serialPortNumber == 0) {
                Sleep(200);
                continue;
            }
            for (int i = 1; i <= 10; ++i) {
                if (i == controller.serialPortNumber) {
                    continue;
                }
                std::string portName = "\\\\.\\COM" + std::to_string(i);
                std::cout << "[screen_capture] Tentative de connexion au port série " << portName << std::endl;

                HANDLE tempPort = CreateFileA(portName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
                if (tempPort == INVALID_HANDLE_VALUE) {
                    continue;
                }

                DCB dcbSerialParams = { 0 };
                dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
                if (!GetCommState(tempPort, &dcbSerialParams)) {
                    CloseHandle(tempPort);
                    continue;
                }
                dcbSerialParams.BaudRate = 4000000;//4000000;
                dcbSerialParams.ByteSize = 8;
                dcbSerialParams.StopBits = ONESTOPBIT;
                dcbSerialParams.Parity = NOPARITY;
                if (!SetCommState(tempPort, &dcbSerialParams)) {
                    CloseHandle(tempPort);
                    continue;
                }

                COMMTIMEOUTS timeouts = { 0 };
                timeouts.ReadIntervalTimeout = 50;
                timeouts.ReadTotalTimeoutConstant = 1000;
                timeouts.ReadTotalTimeoutMultiplier = 10;
                if (!SetCommTimeouts(tempPort, &timeouts)) {
                    CloseHandle(tempPort);
                    continue;
                }

                std::cout << "[screen_capture] Connecté au port série: " << portName << std::endl;

                uint8_t data[2] = { 0xFF, 0xFF };
                DWORD bytesWritten;

                // Fixed: Changed 'y' to 'tempPort'
                if (WriteFile(tempPort, data, 2, &bytesWritten, NULL)) {
                    std::cout << "[screen_capture] Successfully sent FF FF" << std::endl;
                }
                else {
                    CloseHandle(tempPort);
                    continue;
                }

                auto start = std::chrono::system_clock::now();
                int tryingTime = 5000;

                while (std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now() - start).count() < tryingTime) {
                    DWORD bytesRead;
                    uint8_t received;
                    if (ReadFile(tempPort, &received, 1, &bytesRead, NULL) && bytesRead > 0) {
                        if (received == 0) {
                            std::cout << "trouvé" << std::endl;
                            serialPort = tempPort;
                            break;
                        }
                        else {
                            std::cout << "nany ?" << std::endl;
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (serialPort == INVALID_HANDLE_VALUE) {
                    std::cout << "[screen_capture] erggggg, pas le bon ...." << std::endl;
                    CloseHandle(tempPort);
                }
            }
        }
        else {
            int ledX = 161;//161;
            int ledY = 10;//93;
            int keepPixels = 40;
            float reduction = 1.0f;
            PixelResult result = getScreenPixels(screenId, ledX, ledY, keepPixels, reduction);

            for (int i = 0; i < result.size; i++) {
                DWORD bytesWritten;
                uint8_t buffer[6];
                buffer[0] = (uint8_t)0xFF;
                buffer[1] = (uint8_t)(i & 0xFF);
                buffer[2] = (uint8_t)((i >> 8) & 0xFF);
                int color = result.pixels[i];
                buffer[3] = (uint8_t)((color >> 16) & 0xFF);
                buffer[4] = (uint8_t)((color >> 8) & 0xFF);
                buffer[5] = (uint8_t)(color & 0xFF);

            
                if (buffer[1] == 0xFF) {
                    buffer[1] = 0xFE;
                }
                if (buffer[2] == 0xFF) {
                    buffer[2] = 0xFE;
                }
                if (buffer[3] == 0xFF) {
                    buffer[3] = 0xFE;
                }
                if (buffer[4] == 0xFF) {
                    buffer[4] = 0xFE;
                }
                if (buffer[5] == 0xFF) {
                    buffer[5] = 0xFE;
                }

                if (!WriteFile(serialPort, buffer, 6, &bytesWritten, NULL)) {
                    errorCount++;
                    std::cerr << "[screen_capture] Failed to send pixel " << i << std::endl;
                    CloseHandle(serialPort);
                    serialPort = INVALID_HANDLE_VALUE;
                    break;
                }
                else if (bytesWritten != 6) {
                    errorCount++;
                    std::cerr << "[screen_capture] Partial send for pixel " << i
                        << ", sent " << bytesWritten << " bytes" << std::endl;
                }
            }

            DWORD bytesWritten2;
            uint8_t data[5] = { 0xFF, 0xFF};
            if (!WriteFile(serialPort, data, 2, &bytesWritten2, NULL)) {
                errorCount++;
                std::cerr << "[screen_capture] Failed to sync pixel " << std::endl;
                CloseHandle(serialPort);
                serialPort = INVALID_HANDLE_VALUE;
                break;
            }
            if (!FlushFileBuffers(serialPort)) {
                std::cerr << "[screen_capture] Warning: FlushFileBuffers failed (err="
                    << GetLastError() << ")" << std::endl;
            }
            saveToBMP(result.pixels, ledX, ledY, 600, 600, "test.bmp");

            // Clean up PixelResult
            if (result.pixels) {
                delete[] result.pixels;
            }
        }

        // FPS limiting
        frameCount++;
        auto frameEnd = std::chrono::steady_clock::now();
        auto frameTime = frameEnd - frameStart;
        if (frameTime < FRAME_DURATION) {
            std::this_thread::sleep_for(FRAME_DURATION - frameTime);
        }

        // FPS and error reporting
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastReportTime);

        if (elapsed.count() >= 1) {
            std::cout << "[screen_capture] FPS: " << frameCount
                << " | Serial Errors: " << errorCount << std::endl;
            frameCount = 0;
            errorCount = 0;
            lastReportTime = currentTime;
        }
    }

    // Cleanup
    if (screenId) {
        delete[] screenId;
    }
    if (serialPort != INVALID_HANDLE_VALUE) {
        CloseHandle(serialPort);
    }
    return 0;
}

