#pragma once
#include <thread>
#include <condition_variable>
#include <vector>
#include <type_traits>
#include <intrin.h>

template <typename Work>
struct job_queue {
	using Work_Function = void(*)(Work, int);

	std::mutex                work_access;
	std::vector<Work>         work;
	std::vector<std::mutex>   worker_mutexes;
	std::vector<std::jthread> worker_threads;
	std::condition_variable   cond;
	std::condition_variable   done;
	std::mutex                mutex;
	bool                      alive = true;
	Work_Function	          f;
	long volatile             number_of_active_threads = 0;

	job_queue(int thread_count, Work_Function work_function):
		worker_mutexes(thread_count)
	{
		FS_FOR (thread_count)
			worker_threads.emplace_back(worker_thread_main, this, i);
		f = work_function;
	}

	~job_queue() {
		{
			std::scoped_lock lock{work_access};
			work.clear();
		}
		alive = false;
		cond.notify_all();
	}

	template <typename...T>
	void add_work(T&&...t) {
		{
			std::scoped_lock lock{work_access};
			work.emplace_back(std::forward<T>(t)...);
		}
		cond.notify_all();
	}

	void wait() {
		std::unique_lock lock{mutex};
		done.wait_until(lock, nullptr, [this]() { return number_of_active_threads == 0; });
	}

	static void worker_thread_main(job_queue* context, int i) {
		while (context->alive) {
			_interlockedadd(&context->number_of_active_threads, 1);
			bool have_work = false;
			Work work;
		do_work:
			if (have_work) context->f(work, i);

			{
				std::scoped_lock lock_{context->work_access};
				if (context->work.size()) {
					work = context->work.back();
					context->work.pop_back();
					have_work = true;
					goto do_work;
				}
			}
			_interlockedadd(&context->number_of_active_threads, -1);

			{
				std::unique_lock lock{context->mutex};
				context->done.notify_one();
			}

			std::unique_lock lock{context->worker_mutexes[i]};
			context->cond.wait(lock);
		}
	}
};

