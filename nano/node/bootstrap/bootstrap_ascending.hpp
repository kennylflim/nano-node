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
	class async_tag : public std::enable_shared_from_this<async_tag>
	{
	public:
		async_tag (std::shared_ptr<nano::bootstrap::bootstrap_ascending> bootstrap);
		~async_tag ();

		std::atomic<int> blocks{ 0 };
	private:
		std::shared_ptr<bootstrap_ascending> bootstrap;
	};
	using socket_channel = std::pair<std::shared_ptr<nano::socket>, std::shared_ptr<nano::transport::channel>>;
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
	void send (std::shared_ptr<async_tag> tag, socket_channel ctx, nano::hash_or_account const & start);
	void read_block (std::shared_ptr<async_tag> tag, socket_channel ctx);
	nano::hash_or_account random_account_entry (nano::account const & search);
	nano::hash_or_account random_pending_entry (nano::account const & search);
	nano::hash_or_account random_ledger_account ();
	nano::hash_or_account hint_account ();
	nano::hash_or_account pick_account ();
	std::unordered_set<nano::account> hints;
	std::deque<socket_channel> sockets;
	std::atomic<int> picked_hint{ 0 };
	std::atomic<int> picked_ledger_random{ 0 };
	std::atomic<int> requests_total{ 0 };
	std::atomic<int> requests_non_empty{ 0 };
	static constexpr int requests_max = 1;
	static size_t constexpr cutoff = 1;
	std::atomic<int> requests{ 0 };
	/// Wait for there to be space for an additional request
	bool wait_available_request ();
};
}
}
