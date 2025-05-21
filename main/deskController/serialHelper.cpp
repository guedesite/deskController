#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thread> 


HANDLE findSerial_led() {
    std::cout << "Looking for LED Serial" << std::endl;
    for (int i = 1; i <= 10; ++i) { // Teste COM1 à COM256
        std::string portName = "\\\\.\\COM" + std::to_string(i);
        std::cout << "Trying COM" << portName << std::endl;

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
        dcbSerialParams.BaudRate = 4000000;
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
        std::cout << "Connected to: " << portName << std::endl;

        uint8_t data[2] = { 0xFF, 0xFF };
        DWORD bytesWritten;

        // Fixed: Changed 'y' to 'tempPort'
        if (WriteFile(tempPort, data, 2, &bytesWritten, NULL)) {
            std::cout << "Sent FF FF" << std::endl;
        }
        else {
            CloseHandle(tempPort);
            continue;
        }

        auto start = std::chrono::system_clock::now();
        int tryingTime = 500; // 5 sec

        while (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now() - start).count() < tryingTime) {
            DWORD bytesRead;
            uint8_t received;
            if (ReadFile(tempPort, &received, 1, &bytesRead, NULL) && bytesRead > 0) {
                std::cout << "recieve " << portName << std::endl;
                if (received == 0) {
                    std::cout << "found on " << portName << std::endl;
                    return tempPort;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        CloseHandle(tempPort);
    }
    std::cout << "Serial MCU unfound." << std::endl;
    return INVALID_HANDLE_VALUE;
}

HANDLE findSerial_mcu() {
    std::cout << "Looking for MCU Serial" << std::endl;
    for (int i = 1; i <= 10; ++i) { // Teste COM1 à COM256
        std::string portName = "\\\\.\\COM" + std::to_string(i);
        std::cout << "Trying COM" << portName << std::endl;

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
        dcbSerialParams.BaudRate = 115200;
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
        std::cout << "Connected to: " << portName << std::endl;
        auto start = std::chrono::system_clock::now();
        int tryingTime = 5000; // 5 sec

        while (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now() - start).count() < tryingTime) {
            DWORD bytesRead;
            uint8_t received;
            if (ReadFile(tempPort, &received, 1, &bytesRead, NULL) && bytesRead > 0) {
                if (received == 0) {
                    std::cout << "found on " << portName << std::endl;
                    return tempPort;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        CloseHandle(tempPort);
    }
    std::cout << "Serial MCU unfound." << std::endl;
    return INVALID_HANDLE_VALUE;
}
