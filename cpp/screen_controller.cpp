#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <windows.h>
#include <sstream>
#include <algorithm>
#include <codecvt>
#include <locale>

class ScreenController {
private:
    const int BAUDS = 115200;
    const std::string MONITORS_FILE = "monitors.txt";
    const std::string MULTIMONITOR_TOOL_PATH = "MultiMonitorTool.exe";

    std::vector<std::string> stat2_monitors = { "GSM82C5" };
    std::vector<std::string> stat1_monitors = { "HKM3750" };

    HANDLE serialPort = INVALID_HANDLE_VALUE;
    bool stat1_monitors_active = false;
    bool stat2_monitors_active = false;
    std::thread controllerThread;
    bool running = false;

public:
    int serialPortNumber = 0;
    std::map<std::string, int> monitorsId;
    ScreenController() {
        try {
            checkScreenStat();
        }
        catch (const std::exception& e) {
            std::cerr << e.what() << std::endl;
        }
        running = true;
        controllerThread = std::thread(&ScreenController::run, this); // Démarre le thread
    }

    ~ScreenController() {
        running = false; // Signal d'arrêt
        if (controllerThread.joinable()) {
            controllerThread.join(); // Attend la fin du thread
        }
        if (serialPort != INVALID_HANDLE_VALUE) {
            CloseHandle(serialPort);
        }
    }

private:
    void findSerialPort() {
        for (int i = 1; i <= 10; ++i) { // Teste COM1 à COM256
            std::string portName = "\\\\.\\COM" + std::to_string(i);
            std::cout << "Tentative de connexion au port série " << portName << std::endl;

            HANDLE tempPort = CreateFileA(portName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
            if (tempPort == INVALID_HANDLE_VALUE) {
                continue; // Port non disponible
            }

            // Configure les paramètres du port
            DCB dcbSerialParams = { 0 };
            dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
            if (!GetCommState(tempPort, &dcbSerialParams)) {
                CloseHandle(tempPort);
                continue;
            }
            dcbSerialParams.BaudRate = BAUDS;
            dcbSerialParams.ByteSize = 8;
            dcbSerialParams.StopBits = ONESTOPBIT;
            dcbSerialParams.Parity = NOPARITY;
            if (!SetCommState(tempPort, &dcbSerialParams)) {
                CloseHandle(tempPort);
                continue;
            }

            // Configure les timeouts
            COMMTIMEOUTS timeouts = { 0 };
            timeouts.ReadIntervalTimeout = 50;
            timeouts.ReadTotalTimeoutConstant = 1000;
            timeouts.ReadTotalTimeoutMultiplier = 10;
            if (!SetCommTimeouts(tempPort, &timeouts)) {
                CloseHandle(tempPort);
                continue;
            }
            std::cout << "Connecté au port série: " << portName << std::endl;
            auto start = std::chrono::system_clock::now();
            int tryingTime = 5000; // 5 sec

            while (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now() - start).count() < tryingTime) {
                DWORD bytesRead;
                uint8_t received;
                if (ReadFile(tempPort, &received, 1, &bytesRead, NULL) && bytesRead > 0) {
                    if (received == 0) {
                        std::cout << "trouvé" << std::endl;
                        serialPort = tempPort;
                        serialPortNumber = i;
                        return;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            std::cout << "erggggg, pas le bon ...." << std::endl;
            CloseHandle(tempPort);
        }
    }

    void checkScreenStat() {
        std::set<std::string> activeMonitors = getActiveMonitors();
        std::cout << "Moniteurs actifs: ";
        for (const auto& m : activeMonitors) std::cout << m << " ";
        std::cout << std::endl;

        stat1_monitors_active = false;
        for (const auto& monitor : stat1_monitors) {
            if (activeMonitors.count(monitor)) {
                stat1_monitors_active = true;
                break;
            }
        }

        stat2_monitors_active = false;
        for (const auto& monitor : stat2_monitors) {
            if (activeMonitors.count(monitor)) {
                stat2_monitors_active = true;
                break;
            }
        }

        std::cout << "stat1_monitors_active: " << stat1_monitors_active << std::endl;
        std::cout << "stat2_monitors_active: " << stat2_monitors_active << std::endl;
    }

    void disable(const std::vector<std::string>& ids) {
        for (const auto& id : ids) {
            std::cout << "désactivation écran " << id << std::endl;
            std::string cmd = MULTIMONITOR_TOOL_PATH + " /disable " + id;
            system(cmd.c_str());
        }
    }

    void enable(const std::vector<std::string>& ids) {
        for (const auto& id : ids) {
            std::cout << "activation écran " << id << std::endl;
            std::string cmd = MULTIMONITOR_TOOL_PATH + " /enable " + id;
            system(cmd.c_str());
        }
    }

    std::set<std::string> getActiveMonitors() {
        std::string cmd = MULTIMONITOR_TOOL_PATH + " /stext " + MONITORS_FILE;
        system(cmd.c_str());

        // Lecture correcte du fichier en UTF-16 avec gestion du BOM
        std::wifstream wifs(MONITORS_FILE, std::ios::binary);
        if (!wifs) {
            throw std::runtime_error("Impossible d'ouvrir monitors.txt");
        }

        // Gérer le BOM UTF-16 si présent
        wifs.imbue(std::locale(wifs.getloc(),
            new std::codecvt_utf16<wchar_t, 0x10ffff, std::consume_header>));

        std::wstringstream wss;
        wss << wifs.rdbuf();
        std::wstring wContent = wss.str();

        // Conversion UTF-16 ? UTF-8
        int utf8Size = WideCharToMultiByte(CP_UTF8, 0, wContent.c_str(), -1, NULL, 0, NULL, NULL);
        std::string content(utf8Size - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wContent.c_str(), -1, &content[0], utf8Size, NULL, NULL);

        std::istringstream iss(content);
        std::string line;
        std::set<std::string> activeMonitors;
        std::string currentMonitorId;
        std::string currentMonitorName;
        bool isActive = false;
        int index = 0;

        while (std::getline(iss, line)) {
            // Nettoyer les espaces blancs au début
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            std::cout << line << std::endl;
            if (line.find("Active            :") == 0) {
                std::string activeStatus = line.substr(line.find(":") + 1);
                activeStatus.erase(0, activeStatus.find_first_not_of(" \t"));
                isActive = (activeStatus.find("Yes") != std::string::npos);
                currentMonitorId.clear();
            }
            else if (line.find("Short Monitor ID  :") == 0) {
                currentMonitorId = line.substr(line.find(":") + 1);
                currentMonitorId.erase(0, currentMonitorId.find_first_not_of(" \t\r\n"));
                currentMonitorId.erase(currentMonitorId.find_last_not_of(" \t\r\n") + 1);
                if (currentMonitorId == "") {
                   // enable it
                    std::cout << "activation écran " << currentMonitorName << std::endl;
                    std::string cmd = MULTIMONITOR_TOOL_PATH + " /enable " + currentMonitorName;
                    system(cmd.c_str());
                }
                monitorsId[currentMonitorId] = index++;
                //std::cout << "found '" << currentMonitorId << "'" << std::endl;
            }

            else if (line.find("Name              : ") == 0) {
                currentMonitorName = line.substr(line.find(":") + 1);
                currentMonitorName.erase(0, currentMonitorName.find_first_not_of(" \t\r\n"));
                currentMonitorName.erase(currentMonitorName.find_last_not_of(" \t\r\n") + 1);
              
                //std::cout << "found  name '" << currentMonitorName << "'" << std::endl;
            }

            if (!currentMonitorId.empty() && isActive) {
                activeMonitors.insert(currentMonitorId);
                currentMonitorId.clear();
                currentMonitorName.clear();
                isActive = false;
            }
        }

        return activeMonitors;
    }


    void run() {
        while (running) {
            if (serialPort == INVALID_HANDLE_VALUE) {
                findSerialPort();
            }
            else {
                DWORD bytesRead;
                uint8_t received;
                if (ReadFile(serialPort, &received, 1, &bytesRead, NULL) && bytesRead > 0) {
                    std::cout << "Reçu " << (int)received << std::endl;

                    if (received == 0) {
                        std::cout << "Vérification statut écrans" << std::endl;
                        checkScreenStat();
                        uint8_t toSend;
                        if (stat1_monitors_active && !stat2_monitors_active) {
                            toSend = 1;
                        }
                        else if (!stat1_monitors_active && stat2_monitors_active) { toSend = 2; }
                        else if (stat1_monitors_active && stat2_monitors_active) { toSend = 3; }
                        else { toSend = 4;}

                        DWORD bytesWritten;
                        WriteFile(serialPort, &toSend, 1, &bytesWritten, NULL);
                        FlushFileBuffers(serialPort);
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        toSend = 0;
                        WriteFile(serialPort, &toSend, 1, &bytesWritten, NULL);
                        FlushFileBuffers(serialPort);
                    }
                    else if (received == 1) {
                        if (stat1_monitors_active) disable(stat1_monitors);
                        if (stat2_monitors_active) disable(stat2_monitors);
                    }
                    else if (received == 2) {
                        if (!stat1_monitors_active) enable(stat1_monitors);
                        if (stat2_monitors_active) disable(stat2_monitors);
                    }
                    else if (received == 3) {
                        if (stat1_monitors_active) disable(stat1_monitors);
                        if (!stat2_monitors_active) enable(stat2_monitors);
                    }
                    else if (received == 4) {
                        if (!stat1_monitors_active) enable(stat1_monitors);
                        if (!stat2_monitors_active) enable(stat2_monitors);
                    }
                }
                else if (GetLastError() != ERROR_SUCCESS) {
                    std::cerr << "Erreur lecture série: " << GetLastError() << std::endl;
                    CloseHandle(serialPort);
                    serialPort = INVALID_HANDLE_VALUE;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};

/*
int main() {
    ScreenController controller;
    std::cout << "Appuyez sur Entrée pour quitter..." << std::endl;
    std::cin.get(); // Attend une entrée utilisateur pour terminer
    return 0;
}*/