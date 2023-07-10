#pragma once

#include <nano/lib/blocks.hpp>
#include <nano/node/block_pipeline/account_state_filter.hpp>
#include <nano/node/block_pipeline/block_position_filter.hpp>
#include <nano/node/block_pipeline/context.hpp>
#include <nano/node/block_pipeline/epoch_restrictions_filter.hpp>
#include <nano/node/block_pipeline/link_filter.hpp>
#include <nano/node/block_pipeline/metastable_filter.hpp>
#include <nano/node/block_pipeline/receive_restrictions_filter.hpp>
#include <nano/node/block_pipeline/reserved_account_filter.hpp>
#include <nano/node/block_pipeline/send_restrictions_filter.hpp>
#include <nano/node/blocking_observer.hpp>
#include <nano/node/state_block_signature_verification.hpp>
#include <nano/secure/common.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <thread>

namespace nano
{
class node;
class read_transaction;
class transaction;
class write_transaction;
class write_database_queue;

/**
 * Processing blocks is a potentially long IO operation.
 * This class isolates block insertion from other operations like servicing network operations
 */
class block_processor final
{
public:
	using value_type = block_pipeline::context;

	explicit block_processor (nano::node &, nano::write_database_queue &);
	void stop ();
	void flush ();
	std::size_t size ();
	bool full ();
	bool half_full ();
	void add (value_type & item);
	void add (std::shared_ptr<nano::block> const &);
	std::optional<nano::process_return> add_blocking (std::shared_ptr<nano::block> const & block);
	void force (std::shared_ptr<nano::block> const &);
	bool should_log ();
	bool have_blocks_ready ();
	bool have_blocks ();
	void process_blocks ();

	std::atomic<bool> flushing{ false };
	// Delay required for average network propagartion before requesting confirmation
	static std::chrono::milliseconds constexpr confirmation_request_delay{ 1500 };

public: // Events
	using processed_t = std::pair<nano::process_return, std::shared_ptr<nano::block>>;
	nano::observer_set<nano::process_return const &, std::shared_ptr<nano::block>> processed;

	// The batch observer feeds the processed obsever
	nano::observer_set<std::deque<processed_t> const &> batch_processed;

private:
	blocking_observer blocking;

public: // Pipeline
	void pipeline_dump ();

private: // Pipeline
	std::function<void (value_type &)> pipeline;
	nano::block_pipeline::reserved_account_filter reserved;
	nano::block_pipeline::account_state_filter account_state;
	nano::block_pipeline::block_position_filter position;
	nano::block_pipeline::metastable_filter metastable;
	nano::block_pipeline::link_filter link;
	nano::block_pipeline::epoch_restrictions_filter epoch_restrictions;
	nano::block_pipeline::receive_restrictions_filter receive_restrictions;
	nano::block_pipeline::send_restrictions_filter send_restrictions;

private:
	// Roll back block in the ledger that conflicts with 'block'
	void rollback_competitor (nano::write_transaction const & transaction, nano::block const & block);
	void enqueue (value_type const & item);
	nano::process_return process_one (nano::write_transaction const &, std::shared_ptr<nano::block> block, bool const = false);
	void queue_unchecked (nano::write_transaction const &, nano::hash_or_account const &);
	std::deque<processed_t> process_batch (nano::unique_lock<nano::mutex> &);
	void process_verified_state_blocks (std::deque<nano::state_block_signature_verification::value_type> &, std::vector<int> const &, std::vector<nano::block_hash> const &, std::vector<nano::signature> const &);
	void add_impl (std::shared_ptr<nano::block> block);
	bool stopped{ false };
	bool active{ false };
	std::chrono::steady_clock::time_point next_log;
	std::deque<value_type> blocks;

	nano::condition_variable condition;
	nano::node & node;
	nano::write_database_queue & write_database_queue;
	nano::mutex mutex{ mutex_identifier (mutexes::block_processor) };
	nano::state_block_signature_verification state_block_signature_verification;
	std::thread processing_thread;

	friend std::unique_ptr<container_info_component> collect_container_info (block_processor & block_processor, std::string const & name);
};
std::unique_ptr<nano::container_info_component> collect_container_info (block_processor & block_processor, std::string const & name);
}
