#include <windows.h>
#include <iostream>
#include <fstream>
#include <thread> 
#include <wrl/client.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <vector>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#include "serialHelper.cpp"
#include "screenController.cpp"

using namespace Microsoft::WRL;

HANDLE serialPort_led = INVALID_HANDLE_VALUE;
HANDLE serialPort_mcu = INVALID_HANDLE_VALUE;

std::vector<int> previousLedColors;

#define USE_PARALLEL 1

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
        PixelResult getScreenPixels(int screenId, int ledX, int ledY, int keepPixels, float reduction) {
        PixelResult result = { nullptr, -1 };

        // Timestamp global pour la fonction entière
        auto startTotal = std::chrono::high_resolution_clock::now();

        // Timing pour l'initialisation
        auto startInit = std::chrono::high_resolution_clock::now();
        if (!initializeScreen(screenId, reduction)) {
            std::cout << "Failed to initialize screen" << std::endl;
            return result;
        }
        auto endInit = std::chrono::high_resolution_clock::now();
        auto microsInit = std::chrono::duration_cast<std::chrono::microseconds>(endInit - startInit).count();
        //std::cout << "Initialization time: " << microsInit << " μs" << std::endl;

        ScreenDevice& screen = g_screens[screenId];

        // Timing pour l'acquisition du frame
        auto startAcquire = std::chrono::high_resolution_clock::now();
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        ComPtr<IDXGIResource> desktopResource;
        HRESULT hr = screen.duplication->AcquireNextFrame(10, &frameInfo, &desktopResource);
        if (FAILED(hr)) {
            screen.duplication->ReleaseFrame();
            if (hr == 0x887a0026) {
                std::cout << "error reload" << std::endl;
                screen.duplication.Reset();
                screen.stagingTexture.Reset();
                screen.initialized = false;
            }
            return result;
        }
        auto endAcquire = std::chrono::high_resolution_clock::now();
        auto microsAcquire = std::chrono::duration_cast<std::chrono::microseconds>(endAcquire - startAcquire).count();
        //std::cout << "Frame acquisition time: " << microsAcquire << " μs" << std::endl;

        // Timing pour la conversion de ressource
        auto startConvert = std::chrono::high_resolution_clock::now();
        ComPtr<ID3D11Texture2D> desktopTexture;
        hr = desktopResource.As(&desktopTexture);
        if (FAILED(hr)) {
            screen.duplication->ReleaseFrame();
            return result;
        }
        auto endConvert = std::chrono::high_resolution_clock::now();
        auto microsConvert = std::chrono::duration_cast<std::chrono::microseconds>(endConvert - startConvert).count();
       // std::cout << "Resource conversion time: " << microsConvert << " μs" << std::endl;

        // Timing pour la copie de ressource
        auto startCopy = std::chrono::high_resolution_clock::now();
        screen.context->CopyResource(screen.stagingTexture.Get(), desktopTexture.Get());
        auto endCopy = std::chrono::high_resolution_clock::now();
        auto microsCopy = std::chrono::duration_cast<std::chrono::microseconds>(endCopy - startCopy).count();
        //std::cout << "Resource copy time: " << microsCopy << " μs" << std::endl;

        // Timing pour le mapping de texture
        auto startMap = std::chrono::high_resolution_clock::now();
        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = screen.context->Map(screen.stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            screen.duplication->ReleaseFrame();
            return result;
        }
        auto endMap = std::chrono::high_resolution_clock::now();
        auto microsMap = std::chrono::duration_cast<std::chrono::microseconds>(endMap - startMap).count();
        //std::cout << "Texture mapping time: " << microsMap << " μs" << std::endl;

        auto* pixels = static_cast<unsigned char*>(mapped.pData);

        // Préparation des variables
        auto startPrepare = std::chrono::high_resolution_clock::now();
        float xScale = static_cast<float>(screen.width) / screen.reducedWidth;
        float yScale = static_cast<float>(screen.height) / screen.reducedHeight;
        UINT keepWidth = static_cast<UINT>(keepPixels);
        UINT keepHeight = static_cast<UINT>(keepPixels);
        UINT leftBound = keepWidth;
        UINT rightBound = screen.reducedWidth - keepWidth;
        UINT topBound = keepHeight;
        UINT bottomBound = screen.reducedHeight - keepHeight;
        auto endPrepare = std::chrono::high_resolution_clock::now();
        auto microsPrepare = std::chrono::duration_cast<std::chrono::microseconds>(endPrepare - startPrepare).count();
        //std::cout << "Variables preparation time: " << microsPrepare << " μs" << std::endl;

        // Optimisation combinée: accès mémoire optimisés + élimination des branches conditionnelles
        auto startFill = std::chrono::high_resolution_clock::now();

        // 1. Remplir tout le buffer avec des zéros d'un coup (plus rapide qu'une boucle)
        std::memset(screen.pixelBuffer.data(), 0, screen.pixelBuffer.size() * sizeof(int));

        // 2. Traiter les bordures par segments contigus (meilleure localité de cache)
        // Bord supérieur
        for (UINT y = 0; y < topBound; ++y) {
            const UINT baseY = static_cast<UINT>(y * yScale);
            const unsigned char* const rowPtr = pixels + (baseY * mapped.RowPitch);
            int* const destRowPtr = screen.pixelBuffer.data() + (y * screen.reducedWidth);

            for (UINT x = 0; x < screen.reducedWidth; ++x) {
                const UINT srcX = static_cast<UINT>(x * xScale);
                const unsigned char* const pixelPtr = rowPtr + (srcX * 4);

                // Lecture séquentielle pour une meilleure performance de cache
                const unsigned char b = pixelPtr[0];
                const unsigned char g = pixelPtr[1];
                const unsigned char r = pixelPtr[2];

                destRowPtr[x] = (r << 16) | (g << 8) | b;
            }
        }

        // Bord inférieur (y compris la partie droite/gauche qui se recoupe avec le haut/bas)
        for (UINT y = bottomBound; y < screen.reducedHeight; ++y) {
            const UINT baseY = static_cast<UINT>(y * yScale);
            const unsigned char* const rowPtr = pixels + (baseY * mapped.RowPitch);
            int* const destRowPtr = screen.pixelBuffer.data() + (y * screen.reducedWidth);

            for (UINT x = 0; x < screen.reducedWidth; ++x) {
                const UINT srcX = static_cast<UINT>(x * xScale);
                const unsigned char* const pixelPtr = rowPtr + (srcX * 4);

                const unsigned char b = pixelPtr[0];
                const unsigned char g = pixelPtr[1];
                const unsigned char r = pixelPtr[2];

                destRowPtr[x] = (r << 16) | (g << 8) | b;
            }
        }

        // Bords gauche et droit (sans les rangées déjà traitées)
        for (UINT y = topBound; y < bottomBound; ++y) {
            const UINT baseY = static_cast<UINT>(y * yScale);
            const unsigned char* const rowPtr = pixels + (baseY * mapped.RowPitch);
            int* const destRowPtr = screen.pixelBuffer.data() + (y * screen.reducedWidth);

            // Bord gauche
            for (UINT x = 0; x < leftBound; ++x) {
                const UINT srcX = static_cast<UINT>(x * xScale);
                const unsigned char* const pixelPtr = rowPtr + (srcX * 4);

                const unsigned char b = pixelPtr[0];
                const unsigned char g = pixelPtr[1];
                const unsigned char r = pixelPtr[2];

                destRowPtr[x] = (r << 16) | (g << 8) | b;
            }

            // Bord droit
            for (UINT x = rightBound; x < screen.reducedWidth; ++x) {
                const UINT srcX = static_cast<UINT>(x * xScale);
                const unsigned char* const pixelPtr = rowPtr + (srcX * 4);

                const unsigned char b = pixelPtr[0];
                const unsigned char g = pixelPtr[1];
                const unsigned char r = pixelPtr[2];

                destRowPtr[x] = (r << 16) | (g << 8) | b;
            }
        }
        auto endFill = std::chrono::high_resolution_clock::now();
        auto microsFill = std::chrono::duration_cast<std::chrono::microseconds>(endFill - startFill).count();
        //std::cout << "Pixel buffer filling time: " << microsFill << " μs" << std::endl;

        // Initialisation pour calcul des moyennes
        auto startAvgInit = std::chrono::high_resolution_clock::now();
        
        float topBottomZoneWidth = static_cast<float>(screen.reducedWidth) / ledX;
        float leftRightZoneHeight = static_cast<float>(screen.reducedHeight) / ledY;
        auto endAvgInit = std::chrono::high_resolution_clock::now();
        auto microsAvgInit = std::chrono::duration_cast<std::chrono::microseconds>(endAvgInit - startAvgInit).count();
        //std::cout << "Average calculation init time: " << microsAvgInit << " μs" << std::endl;

        // Version optimisée des calculs de bords
        auto startEdgeCalc = std::chrono::high_resolution_clock::now();

        // Pré-allouer le tableau de couleurs des LEDs
        std::vector<int> ledColors(ledX * 2 + ledY * 2, 0);

        // 1. Utiliser un accès séquentiel optimisé pour les mémoires
        // 2. Précomputer les limites de zones une seule fois
        // 3. Réduire les opérations de division en utilisant des incréments

        // Structure pour précalculer les zones
        struct ZoneInfo {
            UINT startX, endX, startY, endY;
        };

        // Pré-calculer toutes les zones pour chaque LED
        std::vector<ZoneInfo> topZones(ledX);
        std::vector<ZoneInfo> rightZones(ledY);
        std::vector<ZoneInfo> bottomZones(ledX);
        std::vector<ZoneInfo> leftZones(ledY);

        // Pré-calcul des zones pour les bords
        for (int i = 0; i < ledX; ++i) {
            // Zones supérieures
            topZones[i].startX = static_cast<UINT>(i * topBottomZoneWidth);
            topZones[i].endX = static_cast<UINT>((i + 1) * topBottomZoneWidth);
            topZones[i].startY = 0;
            topZones[i].endY = topBound;

            // Zones inférieures (de droite à gauche)
            bottomZones[i].startX = screen.reducedWidth - static_cast<UINT>((i + 1) * topBottomZoneWidth);
            bottomZones[i].endX = screen.reducedWidth - static_cast<UINT>(i * topBottomZoneWidth);
            bottomZones[i].startY = bottomBound;
            bottomZones[i].endY = screen.reducedHeight;
        }

        for (int i = 0; i < ledY; ++i) {
            // Zones droites
            rightZones[i].startX = rightBound;
            rightZones[i].endX = screen.reducedWidth;
            rightZones[i].startY = static_cast<UINT>(i * leftRightZoneHeight);
            rightZones[i].endY = static_cast<UINT>((i + 1) * leftRightZoneHeight);

            // Zones gauches (de bas en haut)
            leftZones[i].startX = 0;
            leftZones[i].endX = leftBound;
            leftZones[i].startY = screen.reducedHeight - static_cast<UINT>((i + 1) * leftRightZoneHeight);
            leftZones[i].endY = screen.reducedHeight - static_cast<UINT>(i * leftRightZoneHeight);
        }

        // Traitement parallèle des 4 bords en utilisant std::thread si disponible
        #ifdef USE_PARALLEL
        // Version parallèle avec 4 threads (un par bord)
        std::thread topThread, rightThread, bottomThread, leftThread;

        auto calcZoneAverage = [&screen](const std::vector<ZoneInfo>& zones, std::vector<int>& ledColors, int offset) {
        #else
        auto calcZoneAverage = [&screen](const std::vector<ZoneInfo>& zones, std::vector<int>& ledColors, int offset) {
        #endif
            for (size_t i = 0; i < zones.size(); ++i) {
                const auto& zone = zones[i];

                // Utiliser des variables accumulateurs 32 bits si possible pour éviter les conversions
                int rSum = 0, gSum = 0, bSum = 0;
                int count = 0;

                // Optimisation: calculer directement l'adresse de début de ligne
                const int* bufferStart = screen.pixelBuffer.data();

                // Traiter ligne par ligne pour une meilleure localité de cache
                for (UINT y = zone.startY; y < zone.endY && y < screen.reducedHeight; ++y) {
                    const int* rowStart = bufferStart + (y * screen.reducedWidth);

                    for (UINT x = zone.startX; x < zone.endX && x < screen.reducedWidth; ++x) {
                        // Accès direct au pixel sans multiplication dans la boucle intérieure
                        int pixel = rowStart[x];

                        // Extraction des composantes en une seule passe avec masques
                        rSum += (pixel >> 16) & 0xFF;
                        gSum += (pixel >> 8) & 0xFF;
                        bSum += pixel & 0xFF;
                        ++count;
                    }
                }

                // Éviter la division si count est 0
                if (count > 0) {
                    // Division une seule fois à la fin
                    rSum /= count;
                    gSum /= count;
                    bSum /= count;
                    ledColors[offset + i] = (rSum << 16) | (gSum << 8) | bSum;
                }
            }
            };

        #ifdef USE_PARALLEL
        // Lancer les threads parallèles
        topThread = std::thread(calcZoneAverage, std::ref(topZones), std::ref(ledColors), 0);
        rightThread = std::thread(calcZoneAverage, std::ref(rightZones), std::ref(ledColors), ledX);
        bottomThread = std::thread(calcZoneAverage, std::ref(bottomZones), std::ref(ledColors), ledX + ledY);
        leftThread = std::thread(calcZoneAverage, std::ref(leftZones), std::ref(ledColors), ledX * 2 + ledY);

        // Attendre tous les threads
        topThread.join();
        rightThread.join();
        bottomThread.join();
        leftThread.join();
        #else
        // Version séquentielle optimisée
        calcZoneAverage(topZones, ledColors, 0);
        calcZoneAverage(rightZones, ledColors, ledX);
        calcZoneAverage(bottomZones, ledColors, ledX + ledY);
        calcZoneAverage(leftZones, ledColors, ledX * 2 + ledY);
        #endif

        auto endEdgeCalc = std::chrono::high_resolution_clock::now();
        auto microsEdgeCalc = std::chrono::duration_cast<std::chrono::microseconds>(endEdgeCalc - startEdgeCalc).count();


        // Timing pour le nettoyage et libération des ressources
        auto startCleanup = std::chrono::high_resolution_clock::now();
        screen.context->Unmap(screen.stagingTexture.Get(), 0);
        screen.duplication->ReleaseFrame();
        auto endCleanup = std::chrono::high_resolution_clock::now();
        auto microsCleanup = std::chrono::duration_cast<std::chrono::microseconds>(endCleanup - startCleanup).count();
        //std::cout << "Resource cleanup time: " << microsCleanup << " μs" << std::endl;

        // Timing pour l'allocation du tableau de résultats
        auto startAlloc = std::chrono::high_resolution_clock::now();
        int* ledColorsArray = new int[ledColors.size()];
        std::copy(ledColors.begin(), ledColors.end(), ledColorsArray);
        result.pixels = ledColorsArray;
        result.size = ledColors.size();
        auto endAlloc = std::chrono::high_resolution_clock::now();
        auto microsAlloc = std::chrono::duration_cast<std::chrono::microseconds>(endAlloc - startAlloc).count();
        //std::cout << "Result allocation time: " << microsAlloc << " μs" << std::endl;

        // Affichage du temps total
        auto endTotal = std::chrono::high_resolution_clock::now();
        auto microsTotal = std::chrono::duration_cast<std::chrono::microseconds>(endTotal - startTotal).count();
        //std::cout << "Total function time: " << microsTotal << " μs (" << microsTotal / 1000.0 << " ms)" << std::endl;

        // Résumé des pourcentages de temps
        //std::cout << "Time breakdown (%):" << std::endl;
        //std::cout << "  Initialization: " << (microsInit * 100.0 / microsTotal) << "%" << std::endl;
        //std::cout << "  Frame acquisition: " << (microsAcquire * 100.0 / microsTotal) << "%" << std::endl;
        //std::cout << "  Resource conversion: " << (microsConvert * 100.0 / microsTotal) << "%" << std::endl;
        //std::cout << "  Resource copy: " << (microsCopy * 100.0 / microsTotal) << "%" << std::endl;
        //std::cout << "  Texture mapping: " << (microsMap * 100.0 / microsTotal) << "%" << std::endl;
        //std::cout << "  Pixel buffer filling: " << (microsFill * 100.0 / microsTotal) << "%" << std::endl;
        //std::cout << "  Edge calculations time: " << (microsEdgeCalc * 100.0 / microsTotal) << "%" << std::endl;

        return result;
    }

    __declspec(dllexport)
        void freeMemory(const char* ptr) {
        delete[] reinterpret_cast<const int*>(ptr);
    }

}

int main()
{
    std::cout << "Starting program, looking for serial port..." << std::endl;

    while (serialPort_mcu == INVALID_HANDLE_VALUE || serialPort_led == INVALID_HANDLE_VALUE) {
        if (serialPort_mcu == INVALID_HANDLE_VALUE) {
            serialPort_mcu = findSerial_mcu();
        }
        if (serialPort_led == INVALID_HANDLE_VALUE) {
            serialPort_led = findSerial_led();
        }
    }

    screenController controller(serialPort_mcu);

    int ledX = 169;
    int ledY = 90;

    const int TARGET_FPS = 60;
    const std::chrono::microseconds FRAME_DURATION(1000000 / TARGET_FPS); // microseconds per frame


    int frameCount = 0;
    int errorCount = 0;
    auto lastReportTime = std::chrono::steady_clock::now();

    while (true) {
        if (controller.monitor_active) {

            auto frameStart = std::chrono::steady_clock::now();

            int keepPixels = 140;
            float reduction = 1.0f;
            //auto start = std::chrono::high_resolution_clock::now();
            PixelResult result = getScreenPixels(2, ledX, ledY, keepPixels, reduction);
            //auto end = std::chrono::high_resolution_clock::now();
            //auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            //std::cout << "Frame time taken: " << milliseconds << " milliseconds, size:" << result.size << std::endl;

            if (result.size == -1) {
                continue;
            }

            int offset = 460;           // décalage voulu
            int total = result.size;    // nombre total de pixels

            std::vector<int> correctedColors(total);
            constexpr float gamma = 0.3f;             // plus gamma est grand, plus c'est sombre
            constexpr float inv_gamma = 1.0f / gamma;
            for (int i = 0; i < total; ++i) {
                int raw = result.pixels[i];
                float r = ((raw >> 16) & 0xFF) / 255.0f;
                float g = ((raw >> 8) & 0xFF) / 255.0f;
                float b = ((raw) & 0xFF) / 255.0f;
                // correction gamma
                uint8_t rc = uint8_t(std::pow(r, inv_gamma) * 255.0f + 0.5f);
                uint8_t gc = uint8_t(std::pow(g, inv_gamma) * 255.0f + 0.5f);
                uint8_t bc = uint8_t(std::pow(b, inv_gamma) * 255.0f + 0.5f);
                correctedColors[i] = (rc << 16) | (gc << 8) | bc;
            }

            std::vector<bool> pixelChanged(total, false);
            bool anyChange = false;
            if (previousLedColors.size() != correctedColors.size()) {
                // Premier frame ou changement de taille
                previousLedColors.resize(correctedColors.size());
                std::fill(pixelChanged.begin(), pixelChanged.end(), true);
                anyChange = true;
            }
            else {
                // Détecter les changements
                for (int j = 0; j < total; j++) {
                    if (correctedColors[j] != previousLedColors[j]) {
                        pixelChanged[j] = true;
                        anyChange = true;
                    }
                }
            }
            // Copier pour la prochaine comparaison
            std::copy(correctedColors.begin(), correctedColors.end(), previousLedColors.begin());

            //auto start = std::chrono::high_resolution_clock::now();
            for (int j = 0; j < total; j++) {
                if (!pixelChanged[j]) {
                    continue;
                }
                int i = (offset + j) % total;
                DWORD bytesWritten;
                uint8_t buffer[6];
                buffer[0] = (uint8_t)0xFF;
                buffer[1] = (uint8_t)(i & 0xFF);
                buffer[2] = (uint8_t)((i >> 8) & 0xFF);
                int color = correctedColors[j];
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
                if (!WriteFile(serialPort_led, buffer, 6, &bytesWritten, NULL)) {
                    std::cerr << "[screen_capture] Failed to send pixel " << j << std::endl;
                    errorCount++;
                }
                else if (bytesWritten != 6) {
                    std::cerr << "[screen_capture] Partial send for pixel " << j
                        << ", sent " << bytesWritten << " bytes" << std::endl;
                    errorCount++;
                }
            }
            DWORD bytesWritten2;
            uint8_t data[5] = { 0xFF, 0xFF };
            if (!WriteFile(serialPort_led, data, 2, &bytesWritten2, NULL)) {
                std::cerr << "[screen_capture] Failed to sync pixel " << std::endl;
                errorCount++;
            }
            if (!FlushFileBuffers(serialPort_led)) {
                std::cerr << "[screen_capture] Warning: FlushFileBuffers failed (err="
                    << GetLastError() << ")" << std::endl;
                errorCount++;
            }

            //auto end = std::chrono::high_resolution_clock::now();
            //auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            //saveToBMP(result.pixels, ledX, ledY, 600, 600, "test.bmp");

            // Clean up PixelResult
            if (result.pixels) {
                delete[] result.pixels;
            }

            frameCount++;
            auto frameEnd = std::chrono::steady_clock::now();
            auto frameTime = frameEnd - frameStart;
            if (frameTime < FRAME_DURATION) {
                std::this_thread::sleep_for(FRAME_DURATION - frameTime);
            }

            auto currentTime = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastReportTime);

            if (elapsed.count() >= 1) {
                std::cout << "[screen_capture] FPS: " << frameCount << " | Serial Errors: " << errorCount << std::endl;
                frameCount = 0;
                errorCount = 0;
                lastReportTime = currentTime;
            }
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }

    CloseHandle(serialPort_mcu);
    CloseHandle(serialPort_led);
    // first find serial port
    return 0;
}
