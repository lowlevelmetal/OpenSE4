#include "sdk/worker.hpp"

#include <condition_variable>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>
#else
#include <pthread.h>
#endif

namespace opense4::sdk {

struct Worker::Impl {
    std::mutex mutex;
    std::condition_variable wake;
    const std::function<void()>* task = nullptr;   // the task waiting to run
    bool done = false;                              // the task has run
    bool stop = false;
    std::exception_ptr failure;
#if defined(_WIN32)
    HANDLE thread = nullptr;
#else
    pthread_t thread{};
#endif

    void loop() {
        std::unique_lock lock(mutex);
        for (;;) {
            wake.wait(lock, [&] { return stop || (task && !done); });
            if (stop) return;
            const std::function<void()>* t = task;
            lock.unlock();
            std::exception_ptr caught;
            try {
                (*t)();
            } catch (...) {
                caught = std::current_exception();
            }
            lock.lock();
            failure = caught;
            done = true;
            wake.notify_all();
        }
    }

#if defined(_WIN32)
    static unsigned __stdcall entry(void* p) {
        static_cast<Impl*>(p)->loop();
        return 0;
    }
#else
    static void* entry(void* p) {
        static_cast<Impl*>(p)->loop();
        return nullptr;
    }
#endif
};

Worker::Worker(size_t stackBytes) : impl_(std::make_unique<Impl>()) {
#if defined(_WIN32)
    const uintptr_t h = _beginthreadex(nullptr, static_cast<unsigned>(stackBytes), &Impl::entry, impl_.get(), STACK_SIZE_PARAM_IS_A_RESERVATION,
                                       nullptr);
    if (h == 0) throw std::runtime_error("the script players' thread could not be started");
    impl_->thread = reinterpret_cast<HANDLE>(h);
#else
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) throw std::runtime_error("the script players' thread could not be started");
    pthread_attr_setstacksize(&attr, stackBytes);
    const int rc = pthread_create(&impl_->thread, &attr, &Impl::entry, impl_.get());
    pthread_attr_destroy(&attr);
    if (rc != 0) throw std::runtime_error("the script players' thread could not be started");
#endif
}

Worker::~Worker() {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stop = true;
    }
    impl_->wake.notify_all();
#if defined(_WIN32)
    WaitForSingleObject(impl_->thread, INFINITE);
    CloseHandle(impl_->thread);
#else
    pthread_join(impl_->thread, nullptr);
#endif
}

void Worker::run(const std::function<void()>& task) {
    std::unique_lock lock(impl_->mutex);
    impl_->task = &task;
    impl_->done = false;
    impl_->failure = nullptr;
    impl_->wake.notify_all();
    impl_->wake.wait(lock, [&] { return impl_->done; });
    impl_->task = nullptr;
    if (std::exception_ptr f = std::exchange(impl_->failure, nullptr)) std::rethrow_exception(f);
}

} // namespace opense4::sdk
