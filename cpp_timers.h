#ifndef CPP_TIMERS_H
#define CPP_TIMERS_H

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

class Cpp_Timers
{
public:
    Cpp_Timers() : is_elapsed(false) { }
    Cpp_Timers(const Cpp_Timers&) = delete;
    Cpp_Timers(Cpp_Timers&& timer) = delete;
    Cpp_Timers& operator=(const Cpp_Timers&) = delete;
    Cpp_Timers& operator=(Cpp_Timers&&) = delete;

    template <class Rep, class Period = std::ratio<1>, class Function, class... Args>
    std::future<std::result_of_t<Function&&(Args&&...)>>
    start_one_shot(std::chrono::duration<Rep, Period> duration, Function&& f, Args&&... args)
    {
        auto return_of_callable = std::async(std::launch::async, [this, duration, f = std::forward<Function>(f), ...args = std::forward<Args>(args...)]
        {
            clock(duration);
            // wait for the clock to finish.
            {
                std::unique_lock<std::mutex> lock(wait_cond_mutex);
                wait_cond.wait(lock, [this]
                {
                    return is_elapsed;
                });
            }
            return std::invoke(f, args...);
        });
        return return_of_callable;
    }
    template <class Rep, class Period = std::ratio<1>, class Function, class... Args>
    std::future<std::result_of_t<Function&&(Args&&...)>>
    start_periodic(std::chrono::duration<Rep, Period> duration, Function&& f, Args&&... args)
    {
        auto return_of_callable = std::async(std::launch::async, [this, duration, f = std::forward<Function>(f), ...args = std::forward<Args>(args...)]
        {
            std::result_of_t<Function&&(Args&&...)> last_return_of_callable;
            stop_flag = true;
            while (stop_flag)
            {
                clock(duration);
                // wait for the clock to finish.
                {
                    std::unique_lock<std::mutex> lock(wait_cond_mutex);
                    wait_cond.wait(lock, [this]
                    {
                        return is_elapsed;
                    });
                }
                last_return_of_callable = std::invoke(f, args...);
            }
            return last_return_of_callable;
        });
        return return_of_callable;
    }
    void stop_periodic()
    {
        stop_flag = false;
    }
private:
    template <class Rep, class Period = std::ratio<1>>
    void clock(std::chrono::duration<Rep, Period> duration)
    {
        std::unique_lock<std::mutex> lock(wait_cond_mutex);
        auto now = std::chrono::system_clock::now();
        wait_cond.wait_until(lock, now + duration);
        is_elapsed = true;
        lock.unlock();
        wait_cond.notify_one();
    }
private:
    std::atomic_bool stop_flag;
    std::condition_variable wait_cond;
    std::mutex wait_cond_mutex;
    bool is_elapsed;
};

#endif /* CPP_TIMERS_H */
