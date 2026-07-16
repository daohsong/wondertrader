#include "gtest/gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <ctime>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include <boost/bind/bind.hpp>
#include <boost/ref.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/utility/result_of.hpp>

// TestUnits does not link libboost_thread; provide only the API pool_core needs.
#define BOOST_THREAD_WEK01082003_HPP
#define BOOST_THREAD_MUTEX_HPP
#define BOOST_THREAD_CONDITION_HPP
#define BOOST_THREAD_EXCEPTIONS_PDM070801_H

namespace boost
{
  struct xtime
  {
    long sec;
    long nsec;
  };

  enum
  {
    TIME_UTC_ = 1
  };

  inline int xtime_get(xtime* xt, int)
  {
    const std::chrono::system_clock::duration now = std::chrono::system_clock::now().time_since_epoch();
    const std::chrono::seconds seconds = std::chrono::duration_cast<std::chrono::seconds>(now);
    xt->sec = static_cast<long>(seconds.count());
    xt->nsec = static_cast<long>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - seconds).count());
    return TIME_UTC_;
  }

  inline std::chrono::system_clock::time_point xtime_to_time_point(xtime const& timestamp)
  {
    const std::chrono::nanoseconds since_epoch =
      std::chrono::seconds(timestamp.sec) + std::chrono::nanoseconds(timestamp.nsec);
    return std::chrono::system_clock::time_point(
      std::chrono::duration_cast<std::chrono::system_clock::duration>(since_epoch));
  }

  class thread_resource_error : public std::runtime_error
  {
  public:
    thread_resource_error()
      : std::runtime_error("thread resource error")
    {
    }
  };

  class thread
  {
  public:
    static void sleep(xtime const& timestamp)
    {
      std::this_thread::sleep_until(xtime_to_time_point(timestamp));
    }

    static void yield()
    {
      std::this_thread::yield();
    }
  };

  class recursive_mutex
  {
  public:
    class scoped_lock
    {
    public:
      explicit scoped_lock(recursive_mutex& mutex)
        : m_mutex(&mutex)
        , m_owns(true)
      {
        m_mutex->lock();
      }

      ~scoped_lock()
      {
        if(m_owns)
        {
          m_mutex->unlock();
        }
      }

      void lock()
      {
        m_mutex->lock();
        m_owns = true;
      }

      void unlock()
      {
        m_mutex->unlock();
        m_owns = false;
      }

    private:
      recursive_mutex* m_mutex;
      bool m_owns;
    };

    void lock()
    {
      m_mutex.lock();
    }

    void unlock()
    {
      m_mutex.unlock();
    }

  private:
    std::recursive_mutex m_mutex;
  };

  class condition
  {
  public:
    template<typename Lock>
    void wait(Lock& lock)
    {
      m_condition.wait(lock);
    }

    template<typename Lock>
    bool timed_wait(Lock& lock, xtime const& timestamp)
    {
      return m_condition.wait_until(lock, xtime_to_time_point(timestamp)) != std::cv_status::timeout;
    }

    void notify_one()
    {
      m_condition.notify_one();
    }

    void notify_all()
    {
      m_condition.notify_all();
    }

  private:
    std::condition_variable_any m_condition;
  };
}

#define THREADPOOL_DETAIL_WORKER_THREAD_HPP_INCLUDED

namespace boost { namespace threadpool { namespace detail
{
  template<typename Pool>
  class worker_thread
  : public enable_shared_from_this<worker_thread<Pool> >
  {
  public:
    typedef worker_thread<Pool> worker_type;

    explicit worker_thread(shared_ptr<Pool> pool)
      : m_pool(pool)
    {
    }

    ~worker_thread()
    {
      if(m_thread.joinable())
      {
        m_thread.detach();
      }
    }

    void join()
    {
      if(m_thread.joinable())
      {
        m_thread.join();
      }
      m_pool.reset();
    }

    static void create_and_attach(shared_ptr<Pool> const& pool)
    {
      shared_ptr<worker_type> worker(new worker_type(pool));
      worker->m_thread = std::thread([worker]() mutable {
        shared_ptr<worker_type> running_worker;
        running_worker.swap(worker);
        running_worker->run();
      });
    }

  private:
    void run()
    {
      while(m_pool->execute_task()) {}
      m_pool->worker_destructed(this->shared_from_this());
    }

    shared_ptr<Pool> m_pool;
    std::thread m_thread;
  };
} } }

#include "../Share/threadpool/detail/pool_core.hpp"
#include "../Share/threadpool/scheduling_policies.hpp"
#include "../Share/threadpool/shutdown_policies.hpp"
#include "../Share/threadpool/size_policies.hpp"

namespace
{
  class test_task
  {
  public:
    typedef void result_type;

    test_task()
    {
    }

    template<typename Function>
    test_task(Function function)
      : m_function(function)
    {
    }

    void operator()() const
    {
      if(m_function)
      {
        m_function();
      }
    }

    operator bool() const
    {
      return static_cast<bool>(m_function);
    }

  private:
    std::function<void()> m_function;
  };

  typedef boost::threadpool::detail::pool_core<
    test_task,
    boost::threadpool::fifo_scheduler,
    boost::threadpool::static_size,
    boost::threadpool::resize_controller,
    boost::threadpool::wait_for_all_tasks> test_pool_core;

  typedef boost::threadpool::detail::pool_core<
    test_task,
    boost::threadpool::fifo_scheduler,
    boost::threadpool::static_size,
    boost::threadpool::resize_controller,
    boost::threadpool::immediately> immediate_pool_core;

  class test_thread_pool
  {
  public:
    explicit test_thread_pool(size_t worker_count)
      : m_core(new test_pool_core)
    {
      boost::threadpool::static_size<test_pool_core>::init(*m_core, worker_count);
    }

    ~test_thread_pool()
    {
      m_core->shutdown();
    }

    bool schedule(test_task const& task)
    {
      return m_core->schedule(task);
    }

    void wait()
    {
      m_core->wait();
    }

    size_t size() const
    {
      return m_core->size();
    }

    size_t active() const
    {
      return m_core->active();
    }

    size_t pending() const
    {
      return m_core->pending();
    }

    test_pool_core::size_controller_type size_controller()
    {
      return m_core->size_controller();
    }

  private:
    boost::shared_ptr<test_pool_core> m_core;
  };

  class test_immediate_thread_pool
  {
  public:
    explicit test_immediate_thread_pool(size_t worker_count)
      : m_core(new immediate_pool_core)
    {
      boost::threadpool::static_size<immediate_pool_core>::init(*m_core, worker_count);
    }

    ~test_immediate_thread_pool()
    {
      m_core->shutdown();
    }

    bool schedule(test_task const& task)
    {
      return m_core->schedule(task);
    }

    boost::weak_ptr<immediate_pool_core> weak_core() const
    {
      return m_core;
    }

  private:
    boost::shared_ptr<immediate_pool_core> m_core;
  };
}

TEST(test_compile_warnings_threadpool, task_executes_once)
{
  test_thread_pool pool(1);
  std::atomic<int> runs(0);

  ASSERT_TRUE(pool.schedule([&runs]() {
    runs.fetch_add(1, std::memory_order_relaxed);
  }));

  pool.wait();

  EXPECT_EQ(1, runs.load(std::memory_order_relaxed));
}

TEST(test_compile_warnings_threadpool, wait_completes_after_all_tasks_finish)
{
  test_thread_pool pool(2);
  std::atomic<int> completed(0);
  const int task_count = 24;

  for(int i = 0; i < task_count; ++i)
  {
    ASSERT_TRUE(pool.schedule([&completed]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      completed.fetch_add(1, std::memory_order_relaxed);
    }));
  }

  pool.wait();

  EXPECT_EQ(task_count, completed.load(std::memory_order_relaxed));
  EXPECT_EQ(0u, pool.pending());
  EXPECT_EQ(0u, pool.active());
}

TEST(test_compile_warnings_threadpool, resize_does_not_drop_queued_tasks)
{
  test_thread_pool pool(1);
  test_pool_core::size_controller_type controller = pool.size_controller();
  std::atomic<int> completed(0);
  const int task_count = 96;

  for(int i = 0; i < task_count; ++i)
  {
    ASSERT_TRUE(pool.schedule([&completed]() {
      std::this_thread::sleep_for(std::chrono::microseconds(200));
      completed.fetch_add(1, std::memory_order_relaxed);
    }));
  }

  ASSERT_TRUE(controller.resize(4));
  ASSERT_TRUE(controller.resize(2));
  ASSERT_TRUE(controller.resize(4));
  pool.wait();

  EXPECT_EQ(task_count, completed.load(std::memory_order_relaxed));
  EXPECT_EQ(0u, pool.pending());
  EXPECT_EQ(4u, pool.size());
}

TEST(test_compile_warnings_threadpool, size_and_active_are_safe_under_concurrent_queries)
{
  test_thread_pool pool(4);
  std::atomic<int> completed(0);
  std::atomic<int> samples(0);
  std::atomic<bool> stop(false);
  std::atomic<bool> invariant_ok(true);
  std::vector<std::thread> observers;
  const int task_count = 160;

  for(int i = 0; i < 4; ++i)
  {
    observers.emplace_back([&pool, &samples, &stop, &invariant_ok]() {
      while(!stop.load(std::memory_order_acquire))
      {
        const size_t size = pool.size();
        const size_t active = pool.active();
        if(active > size)
        {
          invariant_ok.store(false, std::memory_order_release);
        }
        samples.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::yield();
      }
    });
  }

  for(int i = 0; i < task_count; ++i)
  {
    ASSERT_TRUE(pool.schedule([&completed]() {
      std::this_thread::sleep_for(std::chrono::microseconds(200));
      completed.fetch_add(1, std::memory_order_relaxed);
    }));
  }

  pool.wait();
  stop.store(true, std::memory_order_release);

  for(std::thread& observer : observers)
  {
    observer.join();
  }

  EXPECT_EQ(task_count, completed.load(std::memory_order_relaxed));
  EXPECT_GT(samples.load(std::memory_order_relaxed), 0);
  EXPECT_TRUE(invariant_ok.load(std::memory_order_acquire));
}

TEST(test_compile_warnings_threadpool, immediate_shutdown_releases_core_after_active_task_finishes)
{
  std::atomic<bool> started(false);
  std::atomic<bool> finish(false);
  std::atomic<int> completed(0);
  boost::weak_ptr<immediate_pool_core> lifetime;

  {
    test_immediate_thread_pool pool(1);
    lifetime = pool.weak_core();
    ASSERT_TRUE(pool.schedule([&]() {
      started.store(true, std::memory_order_release);
      while(!finish.load(std::memory_order_acquire))
      {
        std::this_thread::yield();
      }
      completed.fetch_add(1, std::memory_order_relaxed);
    }));

    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while(!started.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
    {
      std::this_thread::yield();
    }
    ASSERT_TRUE(started.load(std::memory_order_acquire));
    ASSERT_TRUE(pool.schedule([&completed]() {
      completed.fetch_add(100, std::memory_order_relaxed);
    }));
  }

  EXPECT_FALSE(lifetime.expired());
  finish.store(true, std::memory_order_release);
  const std::chrono::steady_clock::time_point deadline =
    std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while(!lifetime.expired() && std::chrono::steady_clock::now() < deadline)
  {
    std::this_thread::yield();
  }

  EXPECT_TRUE(lifetime.expired());
  EXPECT_EQ(1, completed.load(std::memory_order_relaxed));
}
