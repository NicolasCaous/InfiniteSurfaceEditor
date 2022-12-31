#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>

namespace ise
{
    namespace util
    {
        template <class T>
        class SafeQueue
        {
        private:
            std::queue<T> q;
            mutable std::mutex m;
        public:
            SafeQueue()
                : q()
                , m()
            {}

            void push(T t)
            {
                std::lock_guard<std::mutex> lock(m);
                q.push(t);
            }

            std::vector<T> pop_all()
            {
                std::lock_guard<std::mutex> lock(m);
                std::vector<T> ret;

                while (!q.empty())
                {
                    ret.push_back(q.front());
                    q.pop();
                }

                return ret;
            }
        };
    }
}