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
public:
	enum class activity
	{
		account,
		pending
	};
	explicit bootstrap_ascending (std::shared_ptr<nano::node> const & node_a, uint64_t incremental_id_a, std::string id_a);
	
	void run () override;
	void get_information (boost::property_tree::ptree &) override;
	void read_block (std::shared_ptr<nano::socket> socket, std::shared_ptr<nano::transport::channel> channel);
	
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
	void connect_request ();
	void request (std::shared_ptr<nano::socket> socket, std::shared_ptr<nano::transport::channel> channel);
	bool load_any_next ();
	bool load_filter_next (uint32_t filter);
	bool load_next (nano::transaction const & tx);
	void producer_loop ();
	void consumer_loop ();
	std::shared_ptr<nano::bootstrap::bootstrap_ascending> shared ();
	void producer_pass ();
	std::deque<std::pair<std::shared_ptr<nano::socket>, std::shared_ptr<nano::transport::channel>>> sockets;
	std::unordered_map<nano::account, uint32_t> misses;
	size_t filtered{ 0 };
	activity state{ activity::account };
	nano::account next{ 1 };
	uint64_t blocks{ 0 };
	std::atomic<int> requests{ 0 };
	static constexpr int requests_max = 1;
	static size_t constexpr cutoff = 256;
	std::atomic<int> a{ 0 };
	std::atomic<int> m{ 0 };
	std::atomic<int> o{ 0 };
	std::atomic<int> p{ 0 };
	std::atomic<int> r{ 0 };
	std::atomic<int> s{ 0 };
	std::atomic<int> u{ 0 };
	std::deque<nano::account> queue;
	static constexpr size_t queue_max = 1;
};
}
}
