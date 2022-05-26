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
	class request : public std::enable_shared_from_this<request>
	{
	public:
		request (std::shared_ptr<nano::bootstrap::bootstrap_ascending> bootstrap, std::shared_ptr<nano::socket> socket, std::shared_ptr<nano::transport::channel> channel);
		~request ();
		void send ();
	private:
		void read_block ();
		nano::hash_or_account random_account ();
		std::shared_ptr<bootstrap_ascending> bootstrap;
		std::shared_ptr<nano::socket> socket;
		std::shared_ptr<nano::transport::channel> channel;
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
	//void dump_miss_histogram ();
	void request_one ();
	std::deque<std::pair<std::shared_ptr<nano::socket>, std::shared_ptr<nano::transport::channel>>> sockets;
	static constexpr int requests_max = 1;
	static size_t constexpr cutoff = 1;
	std::atomic<int> requests{ 0 };
	/// Wait for there to be space for an additional request
	bool wait_available_request ();
};
}
}
