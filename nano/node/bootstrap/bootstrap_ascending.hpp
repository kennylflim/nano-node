#pragma once

#include <nano/node/bootstrap/bootstrap_attempt.hpp>

#include <thread>

namespace nano
{
namespace transport
{
class channel;
}
namespace bootstrap
{
class bootstrap_ascending : public nano::bootstrap_attempt
{
	class request
	{
	public:
		request (std::shared_ptr<bootstrap_ascending> bootstrap, nano::account const & account) :
			bootstrap{ bootstrap },
			account_m{ account }
		{
			std::lock_guard<nano::mutex> lock{ mutex };
			++bootstrap->requests;
			bootstrap->condition.notify_all ();
		}
		~request ()
		{
			std::lock_guard<nano::mutex> lock{ mutex };
			--bootstrap->requests;
			bootstrap->condition.notify_all ();
			std::cerr << "Completed: " << account_m.to_account () << std::endl;;
		}
		nano::account account ()
		{
			return account_m;
		}
	private:
		nano::mutex mutex;
		std::shared_ptr<bootstrap_ascending> bootstrap;
		nano::account account_m;
	};
	class producer
	{
		enum class activity
		{
			account,
			pending
		};
	public:
		producer (bootstrap_ascending & bootstrap) :
			bootstrap{ bootstrap }
		{
		}
		void run ();
	private:
		bool pass ();
		bool filtered_pass (uint32_t filter);
		bool load_next (nano::transaction const & tx);
		void queue_next ();
		std::atomic<int> a{ 0 };
		std::atomic<int> p{ 0 };
		activity state{ activity::account };
		nano::account next{ 1 };
		bootstrap_ascending & bootstrap;
	};
	class consumer
	{
	public:
		consumer (bootstrap_ascending & bootstrap) :
			bootstrap{ bootstrap }
		{
		}
		void run ();
	private:
		void connect_request ();
		void read_block (std::shared_ptr<nano::socket> socket, std::shared_ptr<nano::transport::channel> channel, std::shared_ptr<request> request);
		void request (std::shared_ptr<nano::socket> socket, std::shared_ptr<nano::transport::channel> channel);
		bootstrap_ascending & bootstrap;
	};
public:
	explicit bootstrap_ascending (std::shared_ptr<nano::node> const & node_a, uint64_t incremental_id_a, std::string id_a);
	
	void run () override;
	void get_information (boost::property_tree::ptree &) override;

	explicit bootstrap_ascending (std::shared_ptr<nano::node> const & node_a, uint64_t const incremental_id_a, std::string const & id_a, uint32_t const frontiers_age_a, nano::account const & start_account_a) :
	bootstrap_ascending{ node_a, incremental_id_a, id_a }
	{
		std::cerr << '\0';
	}
	void add_frontier (nano::pull_info const &) {
		std::cerr << '\0';
	}
	void add_bulk_push_target (nano::block_hash const &, nano::block_hash const &) {
		std::cerr << '\0';
	}
	void set_start_account (nano::account const &) {
		std::cerr << '\0';
	}
	bool request_bulk_push_target (std::pair<nano::block_hash, nano::block_hash> &) {
		std::cerr << '\0';
	}
private:
	std::shared_ptr<nano::bootstrap::bootstrap_ascending> shared ();
	bool producer_throttled_pass ();
	void dump_miss_histogram ();
	std::deque<std::pair<std::shared_ptr<nano::socket>, std::shared_ptr<nano::transport::channel>>> sockets;
	std::unordered_map<nano::account, uint32_t> misses;
	size_t filtered{ 0 };
	uint64_t blocks{ 0 };
	static constexpr int requests_max = 1;
	static size_t constexpr cutoff = 1;
	std::atomic<int> m{ 0 };
	std::atomic<int> o{ 0 };
	std::atomic<int> r{ 0 };
	std::atomic<bool> dirty{ false };
	static constexpr size_t queue_max = 1;
	producer producer;
	consumer consumer;

	std::deque<std::shared_ptr<request>> accounts;
	std::atomic<int> requests{ 0 };
	void push_back (nano::account const & account);
	std::shared_ptr<request> pop_front ();
	/// Waits for there to be no outstanding network requests
	/// Returns true if the function returns and there are still requests outstanding
	bool wait_empty_requests ();
	/// Waits for there to be an an item in the queue to be popped
	/// Returns true if the fuction returns and there are no items in the queue
	bool wait_available_queue ();
	/// Wait for there to be space for an additional request
	bool wait_available_request ();
	void clear_queue ();
	void notify ();
};
}
}
