#include "BehavioralEvader.h"
#include <thread>
#include <random>
#include <ctime>
#include <spdlog/spdlog.h>

int BehavioralEvader::GetRandomDelay(int minMs, int maxMs) {
    static std::random_device rd;
    static std::mt19937 gen(rd() + std::time(nullptr));
    std::uniform_int_distribution<> dis(minMs, maxMs);
    return dis(gen);
}

void BehavioralEvader::AddHumanDelay() {
    int delay = GetRandomDelay(200, 2000);
    spdlog::debug("Adding human-like delay: {}ms", delay);
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
}

void BehavioralEvader::AddInterRequestDelay() {
    int delay = GetRandomDelay(1000, 5000);
    spdlog::debug("Adding inter-request delay: {}ms", delay);
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
}

void BehavioralEvader::AddFormSubmissionDelay() {
    int delay = GetRandomDelay(3000, 8000);
    spdlog::debug("Adding form submission delay: {}ms", delay);
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
}

void BehavioralEvader::SimulatePageReadTime(size_t pageSize) {
    int wordCount = pageSize / 6;
    int wordsPerMinute = 200 + GetRandomDelay(0, 100);
    int timeSeconds = (wordCount / wordsPerMinute) * 60;
    int variance = GetRandomDelay(-3000, 3000);

    int totalMs = timeSeconds * 1000 + variance;
    if (totalMs < 100) totalMs = 100;

    spdlog::debug("Simulating page read time: {}ms", totalMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(totalMs));
}
