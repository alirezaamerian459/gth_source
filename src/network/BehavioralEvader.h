#pragma once

#include <cstddef>

class BehavioralEvader {
public:
    static void AddHumanDelay();
    static void AddInterRequestDelay();
    static void AddFormSubmissionDelay();
    static void SimulatePageReadTime(size_t pageSize);

private:
    static int GetRandomDelay(int minMs, int maxMs);
};
