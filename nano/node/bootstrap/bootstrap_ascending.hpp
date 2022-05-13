#pragma once

#include <nano/node/bootstrap/bootstrap_attempt.hpp>

namespace nano
{
namespace bootstrap
{
class bootstrap_ascending : public nano::bootstrap_attempt
{
public:
	enum class activity
	{
		account,
		pending,
		queue
	};
	explicit bootstrap_ascending (std::shared_ptr<nano::node> const & node_a, uint64_t incremental_id_a, std::string id_a);
	
	void run () override;
	void get_information (boost::property_tree::ptree &) override;
	void read_block (std::shared_ptr<nano::bootstrap_client> connection);
	
	
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
	void request ();
	bool compute_next ();
	bool load_next (nano::transaction const & tx);
	void fill_drain_queue ();
	std::shared_ptr<nano::bootstrap::bootstrap_ascending> shared ();
	activity state{ activity::account };
	nano::account next{ 1 };
	uint64_t blocks{ 0 };
	std::unordered_set<nano::account> queued;
	std::unordered_set<nano::account> requeue;
	std::atomic<int> requests{ 0 };
	static size_t constexpr cutoff = 1;
};
}
}
