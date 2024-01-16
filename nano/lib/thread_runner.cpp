#include <nano/lib/thread_runner.hpp>

#include <iostream>

/*
 * thread_runner
 */

nano::thread_runner::thread_runner (boost::asio::io_context & io_ctx_a, unsigned num_threads, const nano::thread_role::name thread_role_a) :
	io_guard{ boost::asio::make_work_guard (io_ctx_a) },
	role{ thread_role_a }
{
	for (auto i (0u); i < num_threads; ++i)
	{
		threads.emplace_back (nano::thread_attributes::get_default (), [this, &io_ctx_a] () {
			nano::thread_role::set (role);
			try
			{
				io_ctx_a.run ();
			}
			catch (std::exception const & ex)
			{
				std::cerr << ex.what () << std::endl;
#ifndef NDEBUG
				throw; // Re-throw to debugger in debug mode
#endif
			}
			catch (...)
			{
#ifndef NDEBUG
				throw; // Re-throw to debugger in debug mode
#endif
			}
		});
	}
}

nano::thread_runner::~thread_runner ()
{
	join ();
}

void nano::thread_runner::join ()
{
	io_guard.reset ();
	for (auto & i : threads)
	{
		if (i.joinable ())
		{
			i.join ();
		}
	}
}

void nano::thread_runner::stop_event_processing ()
{
	io_guard.get_executor ().context ().stop ();
}
