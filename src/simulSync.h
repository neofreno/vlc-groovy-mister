#pragma once
#include "msvc-compat/poll.h"
#include "msvc-compat/types.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>

#define FPS 60

typedef struct {
    std::thread thread;
    std::atomic<std::chrono::steady_clock::time_point> nextFrameTime;
    std::atomic<bool> running;
} fps_sync_t;

static fps_sync_t* fpsSync = NULL; // Estructura global para manejar el hilo de sincronización

class simulSync {

public:
    static void init() {
        fpsSync = new fps_sync_t();
        // Inicializar la estructura de sincronización
        fpsSync->running = true;

        // Crear el hilo de sincronización de 60FPS
        fpsSync->thread = std::thread(WaitFor60FPS);
    }

    // Hilo que corre a 60FPS
    static void WaitFor60FPS()
    {
        const auto frame_time = std::chrono::microseconds(1000000 / FPS); // 60 FPS = 16.67ms
        fpsSync->nextFrameTime.store(std::chrono::steady_clock::now()); // Tiempo base del primer ciclo

        while (fpsSync->running.load())
        {
            fpsSync->nextFrameTime.store(fpsSync->nextFrameTime.load() + frame_time); // Calcular el siguiente frame

            auto currentTime = std::chrono::steady_clock::now();
            auto sleepTime = fpsSync->nextFrameTime.load() - currentTime;

            if (sleepTime.count() > 0)
                std::this_thread::sleep_for(sleepTime); // Esperar hasta el próximo ciclo
        }
    }

    // Función para que `main()` espere la sincronización del hilo de 60 FPS
    static void SyncWith60FPS()
    {
        while (std::chrono::steady_clock::now() < fpsSync->nextFrameTime.load())
        {
            // Espera activa hasta que el tiempo actual alcance `nextFrameTime`
        }
    }

    static void close() {

        // Finalizar el hilo de sincronización
        fpsSync->running = false;
        fpsSync->thread.join();
        delete(fpsSync);
    }
};