#pragma once

#include <nano/node/bootstrap/bootstrap_attempt.hpp>

#include <random>
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
	using socket_channel = std::pair<std::shared_ptr<nano::socket>, std::shared_ptr<nano::transport::channel>>;
	std::shared_ptr<nano::bootstrap::bootstrap_ascending> shared ();
	//void dump_miss_histogram ();

	class progress_forwarding
	{
	public:
		progress_forwarding (bootstrap_ascending & bootstrap);
		std::optional<nano::account> operator() ();
		void insert (nano::account const & account);
	private:
		static bool constexpr enabled{ true };
		std::unordered_set<nano::account> forwarding;
		bootstrap_ascending & bootstrap;
	};
	class account_sets
	{
	public: //
		class source_blocking
		{
		public:
			size_t size () const;
			bool blocked (nano::account const & account) const;
			void erase (nano::account const & account);
			void insert (nano::account const & account);
		private:
			static bool constexpr enabled{ true };
			std::unordered_set<nano::account> accounts;
		};
		class backoff_counts
		{
		public:
			bool insert (nano::account const & account);
			nano::account operator() ();
			void erase (nano::account const & account);
			bool empty () const;
			void dump_backoff_hist ();

			static size_t constexpr backoff_exclusion = 16;
		private:
			std::default_random_engine random;
			std::unordered_map<nano::account, float> backoff;
			decltype(backoff) accounts;
			size_t attempts{ 0 };
		};
	public:
		//void block (nano::account const & account);
	//private:
		backoff_counts backoff;
		source_blocking source;
	};
	class async_tag : public std::enable_shared_from_this<async_tag>
	{
	public:
		async_tag (std::shared_ptr<nano::bootstrap::bootstrap_ascending> bootstrap);
		~async_tag ();

		std::atomic<int> blocks{ 0 };
	private:
		std::shared_ptr<bootstrap_ascending> bootstrap;
	};
	void request_one ();
	void send (std::shared_ptr<async_tag> tag, socket_channel ctx, nano::hash_or_account const & start);
	void read_block (std::shared_ptr<async_tag> tag, socket_channel ctx);
	nano::account random_account_entry (nano::transaction const & tx, nano::account const & search);
	std::optional<nano::account> random_pending_entry (nano::transaction const & tx, nano::account const & search);
	std::optional<nano::account> random_ledger_account (nano::transaction const & tx);
	std::optional<nano::account> pick_account ();
	bool blocked (nano::account const & account);
	void inspect (nano::transaction const & tx, nano::process_return const & result, nano::block const & block);
	void dump_stats ();

	account_sets accounts;
	progress_forwarding forwarding;
	std::deque<socket_channel> sockets;
	static constexpr int requests_max = 1;
	static size_t constexpr request_message_count = 16;
	std::atomic<int> responses{ 0 };
	std::atomic<int> requests{ 0 };
	std::atomic<int> requests_total{ 0 };
	std::atomic<int> source_iterations{ 0 };
	std::atomic<float> weights{ 0 };
	std::atomic<int> forwarded{ 0 };
	std::atomic<int> block_total{ 0 };
	/// Wait for there to be space for an additional request
	bool wait_available_request ();
};
}
}
