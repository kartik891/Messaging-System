
#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <utility>

class MessageQueue {
private:
    std::queue<std::string> messages;
    std::mutex mtx;
    std::condition_variable cv;
    bool closed = false;

public:

    bool push(std::string message) {
        {
            std::lock_guard<std::mutex> lock(mtx);

            if (closed)
                return false;

            messages.push(std::move(message));
        }

        cv.notify_one();
        return true;
    }

    bool tryPop(std::string& message) {
        std::lock_guard<std::mutex> lock(mtx);

        if (messages.empty())
            return false;

        message = std::move(messages.front());
        messages.pop();

        return true;
    }

    bool waitPop(std::string& message) {
        std::unique_lock<std::mutex> lock(mtx);

        cv.wait(lock, [this] {
            return closed || !messages.empty();
        });

        if (messages.empty())
            return false;

        message = std::move(messages.front());
        messages.pop();

        return true;
    }

    bool empty() {
        std::lock_guard<std::mutex> lock(mtx);
        return messages.empty();
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mtx);
            closed = true;
        }

        cv.notify_all();
    }
};

#endif
