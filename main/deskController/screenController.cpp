#include <thread>
#include <fstream>
#include <codecvt>
#include <string>
#include <sstream>

class screenController {

private:
    const std::string MONITORS_FILE = "monitors.txt";
    const std::string MULTIMONITOR_TOOL_PATH = "MultiMonitorTool.exe";
    bool running = false;
    std::thread controllerThread;
    HANDLE serialPort;
public:
    bool monitor_active = false;
    screenController(HANDLE serialPort_mcu) {
        running = true;
        serialPort = serialPort_mcu;
        controllerThread = std::thread(&screenController::run, this);
    }
    ~screenController() {
        running = false; // Signal d'arrêt
        if (controllerThread.joinable())
        {
            controllerThread.join(); // Attend la fin du thread
        }
    }

private:

    void check_status_monitor() {
        boolean _monitor_active = false;
        try {
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

            std::string currentMonitorName;
            uint8_t flag = 0;
            bool flag_freq = false;

            while (std::getline(iss, line)) {
                // Nettoyer les espaces blancs au début
                line.erase(0, line.find_first_not_of(" \t\r\n"));
                //std::cout << line << std::endl;
                if (line.find("Active            :") == 0) {
                    std::string activeStatus = line.substr(line.find(":") + 1);
                    activeStatus.erase(0, activeStatus.find_first_not_of(" \t"));
                    flag = (activeStatus.find("Yes") != std::string::npos) ? 1 : 2;
                }
                else if (line.find("Name              : \\\\.\\DISPLAY3") == 0) {
                    if (flag == 1) {
                        _monitor_active = true;
                        /*if (monitor_active && !flag_freq) {
                            std::cout << "changing freq" << std::endl;
                            cmd = MULTIMONITOR_TOOL_PATH + " /SetMonitors \"Name=\\\\.\\DISPLAY3 DisplayFrequency=120\"";
                            system(cmd.c_str());
                            std::cout << cmd << std::endl;
                            //std::this_thread::sleep_for(std::chrono::milliseconds(3000));
                        }*/
                    }
                    else if (flag == 2) {
                        break;
                    }
                }
                else if (line.find("Frequency         : 120") == 0) {
                    flag_freq = true;
                }
            }
        } catch(const std::exception& e) {
            std::cerr << e.what() << std::endl;
        }
        monitor_active = _monitor_active;

    }

    void enable_monitor() {
        std::cout << "Enable screen" << std::endl;

        std::string cmd = MULTIMONITOR_TOOL_PATH + " /enable 3 /SetMonitors \"Name=\\\\.\\DISPLAY3 Width=3840 Height=2160 DisplayFrequency=120 PositionX=633 PositionY=-3760\" /SetScale \"\\\\.\\DISPLAY3\" 150";
        system(cmd.c_str());
    }

    void disable_monitor() {
        std::cout << "Disable screen" << std::endl;
        std::string cmd = MULTIMONITOR_TOOL_PATH + " /disable 3";
        system(cmd.c_str());
    }

    void run() {
        std::cout << "Starting thread screenController" << std::endl;
        while (running)
        {
            DWORD bytesRead;
            uint8_t received;
            if (ReadFile(serialPort, &received, 1, &bytesRead, NULL) && bytesRead > 0) {
                if (received == 0) {
                    std::cout << "Vérification statut écrans" << std::endl;
                    check_status_monitor();
                    uint8_t toSend;
                    if (monitor_active) {
                        toSend = 1;
                    }
                    else {
                        toSend = 4;
                    }
                    DWORD bytesWritten;
                    WriteFile(serialPort, &toSend, 1, &bytesWritten, NULL);
                    FlushFileBuffers(serialPort);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    toSend = 0;
                    WriteFile(serialPort, &toSend, 1, &bytesWritten, NULL);
                    FlushFileBuffers(serialPort);
                }
                else if (received == 1 && monitor_active) {
                    disable_monitor();
                }
                else if (received == 2 && !monitor_active) {
                    enable_monitor();
                }
                /*else if (received == 3) {
                   
                }
                else if (received == 4) {
                    
                }*/
            }
            else if (GetLastError() != ERROR_SUCCESS) {
                std::cerr << "Erreur lecture série: " << GetLastError() << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
};
