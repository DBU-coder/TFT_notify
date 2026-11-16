#pragma once
#include "config.h"

struct NotificationItem {
    char title[64];
    char message[512];
    uint32_t id;
    bool valid;
};

class NotificationQueue {
private:
    NotificationItem queue[QUEUE_SIZE];
    uint8_t head = 0;
    uint8_t count = 0;
    int8_t displayIdx = -1;

public:
    NotificationQueue() {
        clear();
    }

    uint8_t getCount() const { return count; }
    int8_t getCurrentDisplayIdx() const { return displayIdx; }

    void clear() {
        for (int i = 0; i < QUEUE_SIZE; i++) {
            queue[i].valid = false;
        }
        head = 0;
        count = 0;
        displayIdx = -2;
    }

    bool enqueue(const char* title, const char* msg, uint32_t id) {
        if (count >= QUEUE_SIZE) {
            queue[head].valid = false;
            head = (head + 1) % QUEUE_SIZE;
            count--;
        }

        uint8_t tail = (head + count) % QUEUE_SIZE;
        strncpy(queue[tail].title, title, sizeof(queue[tail].title) - 1);
        queue[tail].title[sizeof(queue[tail].title) - 1] = '\0';
        strncpy(queue[tail].message, msg, sizeof(queue[tail].message) - 1);
        queue[tail].message[sizeof(queue[tail].message) - 1] = '\0';
        queue[tail].id = id;
        queue[tail].valid = true;
        count++;
        return true;
    }

    bool removeByUid(uint32_t uid) {
        if (count == 0) return false;

        int foundIdx = -1;
        for (int i = 0; i < count; i++) {
            int idx = (head + i) % QUEUE_SIZE;
            if (queue[idx].valid && queue[idx].id == uid) {
                foundIdx = i;
                break;
            }
        }
        if (foundIdx < 0) return false;

        for (int i = foundIdx; i < count - 1; i++) {
            int curr = (head + i) % QUEUE_SIZE;
            int next = (head + i + 1) % QUEUE_SIZE;
            queue[curr] = queue[next];
        }

        int last = (head + count - 1) % QUEUE_SIZE;
        queue[last].valid = false;
        count--;
        displayIdx = -1; // Сброс индекса для пересчета в карусели
        return true;
    }

    NotificationItem* findByUid(uint32_t uid) {
        for (int i = 0; i < count; i++) {
            int idx = (head + i) % QUEUE_SIZE;
            if (queue[idx].valid && queue[idx].id == uid) {
                return &queue[idx];
            }
        }
        return nullptr;
    }

    NotificationItem* nextItem() {
        if (count == 0) return nullptr;
        if (displayIdx < 0) {
            displayIdx = 0;
        } else {
            displayIdx = (displayIdx + 1) % count;
        }
        uint8_t realIdx = (head + displayIdx) % QUEUE_SIZE;
        return queue[realIdx].valid ? &queue[realIdx] : nullptr;
    }
};
