#include "network/EventLoop.h"
#include "network/Channel.h"
#include "network/Timer.h"
#include "utils/Logging.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace kvstore;

namespace {

class TestEventLoop : public EventLoop {
public:
    void updateChannel(Channel* /*channel*/) override {}
    void removeChannel(Channel* /*channel*/) override {}
};

bool testRunAfterFires() {
    TestEventLoop loop;
    std::atomic<int> fired{0};

    loop.runAfter(0.01, [&]() {
        ++fired;
        loop.quit();
    });

    std::thread quitter([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        loop.quit();
    });

    loop.loop();
    quitter.join();

    return fired.load() == 1;
}

bool testCancelPreventsFire() {
    TestEventLoop loop;
    std::atomic<int> fired{0};

    TimerId timerId = loop.runAfter(0.01, [&]() {
        ++fired;
        loop.quit();
    });

    loop.cancel(timerId);

    std::thread quitter([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        loop.quit();
    });

    loop.loop();
    quitter.join();

    return fired.load() == 0;
}

} // namespace

int main() {
    LoggerImpl::instance().setLogLevel(INFO);

    if (!testRunAfterFires()) {
        LOG_ERROR << "runAfter should fire once and stop the loop";
        return 1;
    }

    if (!testCancelPreventsFire()) {
        LOG_ERROR << "cancel should prevent the timer callback from running";
        return 1;
    }

    LOG_INFO << "EventLoop timer tests passed";
    return 0;
}
