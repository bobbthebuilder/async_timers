#ifndef ASYNC_TIMERS_H
#define ASYNC_TIMERS_H

// MIT License
//
// Copyright (c) 2020 Daniel Feist
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <mutex>
#include <chrono>
#include <atomic>
#include <thread>
#include <future>
#include <iostream>
#include <functional>
#include <type_traits>
#include <condition_variable>

namespace async_timers
{
static constexpr auto DEBUG_MODE {true};
class instance
{
public:
    instance() noexcept : is_running(false), is_single_shot(true) {}
    instance(const instance&) = delete;
    instance(instance&& timer) = delete;
    instance& operator=(const instance&) = delete;
    instance& operator=(instance&&) = delete;

    template <class Rep, class Period = std::ratio<1>, class Function, class... Args>
    std::future<std::result_of_t<Function&&(Args&&...)>>
    start(std::chrono::duration<Rep, Period> duration, Function&& f, Args&&... args)
    {
        bool expected;
        if (!is_running.compare_exchange_strong(expected = false, true))
        {
            if constexpr (DEBUG_MODE)
            {
                std::cout << "timer is already running, will stop and restart.\n";
            }
            is_running.store(false);
            finished_waiting_for_stop = false;
            std::unique_lock<std::mutex> lock(running_cond_mutex);
            running_cond.wait(lock, [this]
            {
                return finished_waiting_for_stop;
            });
            if constexpr (DEBUG_MODE)
            {
                std::cout << "about to (re)start timer.\n";
            }
            is_running.store(true);
        }
        finished_waiting_for_clock = false;
        return std::async(std::launch::async, [this, duration, f = std::forward<Function>(f), ...args = std::forward<Args>(args...)]
        {
            if constexpr (DEBUG_MODE)
            {
                std::cout << "thread id this timer runs on= " << std::this_thread::get_id() << "\n";
            }
            std::result_of_t<Function&&(Args&&...)> last_return_of_callable;
            while (is_running.load())
            {
                clock(duration);
                {
                    std::unique_lock<std::mutex> lock(wait_cond_mutex);
                    wait_cond.wait(lock, [this]
                    {
                        return finished_waiting_for_clock;
                    });
                }
                if (!is_running.load())
                {
                    if constexpr (DEBUG_MODE)
                    {
                        std::cout << "async_timer was stopped prematurely.\n";
                    }
                    break;
                }
                last_return_of_callable = std::invoke(f, std::forward<Args>(args)...);
                if (is_single_shot.load())
                {
                    if constexpr (DEBUG_MODE)
                    {
                        std::cout << "stop timer due to activated single shot property.\n";
                    }
                    break;
                }
            }
            std::lock_guard<std::mutex> lk(running_cond_mutex);
            finished_waiting_for_stop = true;
            running_cond.notify_one();
            is_running.store(false);
            return last_return_of_callable;
        });
    }
    void stop() noexcept
    {
        is_running.store(false);
    }
    void set_single_shot()
    {
        is_single_shot.store(true);
    }
    void set_periodic()
    {
        is_single_shot.store(false);
    }
private:
    template <class Rep, class Period = std::ratio<1>>
    void clock(std::chrono::duration<Rep, Period> duration)
    {
        if constexpr (DEBUG_MODE)
        {
            std::cout << "start clock that counts down to 0.\n";
        }
        std::unique_lock<std::mutex> lock(wait_cond_mutex);
        auto now = std::chrono::system_clock::now();
        wait_cond.wait_until(lock, now + duration, [this]
        {
            return is_running.load() ? false : true;
        });
        finished_waiting_for_clock = true;
        lock.unlock();
        wait_cond.notify_one();
    }
private:
    std::atomic_bool is_running, is_single_shot;
    std::condition_variable wait_cond, running_cond;
    std::mutex wait_cond_mutex, running_cond_mutex;
    bool finished_waiting_for_clock, finished_waiting_for_stop;
};

}

#endif /* ASYNC_TIMERS_H */
