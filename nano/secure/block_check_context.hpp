#pragma once

#include <nano/lib/epoch.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/secure/common.hpp>

#include <memory>
#include <optional>

namespace nano
{
class block;
class ledger;
}

namespace nano::store
{
class transaction;
}

namespace nano
{
class block_check_context
{
	enum class block_op
	{
		receive,
		send,
		noop,
		epoch
	};
	nano::block * block;
	std::shared_ptr<nano::block> previous;
	std::optional<nano::account_info> state;
	std::optional<nano::pending_info> receivable;
	bool any_receivable{ false };
	bool source_exists{ false };
	bool failed (nano::process_result const & code) const;
	nano::ledger & ledger;
	nano::block_details details;

private:
	bool is_send () const;
	bool is_receive () const;
	bool is_epoch () const;
	nano::account account () const;
	nano::block_hash source () const;
	nano::account signer (nano::epochs const & epochs) const;
	bool gap_previous () const;
	nano::amount balance () const;
	uint64_t height () const;
	nano::epoch epoch () const;

private: // Block checking rules
	nano::process_result rule_sufficient_work () const;
	/**
		Check for account numbers that cannot be used in blocks e.g. account number 0.
	  */
	nano::process_result rule_reserved_account () const;
	/**
		  This rule checks if the previous block for this block is the head block of the specified account
	  */
	nano::process_result rule_previous_frontier () const;

	/**
		This rule checks that legacy blocks cannot come after state blocks in an account
	 */
	nano::process_result rule_state_block_account_position () const;
	
	/**
		This rule checks that legacy blocks cannot have a state block as a source
	 */
	nano::process_result rule_state_block_source_position () const;
	nano::process_result rule_block_signed () const;

	/**
		  This rule identifies metastable blocks (forked blocks) with respect to the ledger and rejects them.
		  Rejected blocks need to be resolved via consensus
		  It is assumed that the previous block has already been loaded in to `context' if it exists
		  Metastable scenarios are:
			1) An initial block arriving for an account that's already been initialized
			2) The previous block exists but it is not the head block
		  Both of these scenarios can be ifentified by checking: if block->previous () == head
		 */
	nano::process_result rule_metastable () const;
	nano::process_result check_receive_rules () const;
	nano::process_result check_epoch_rules () const;
	nano::process_result check_send_rules () const;
	nano::process_result check_noop_rules () const;

public:
	block_check_context (nano::store::transaction const & transaction, nano::ledger & ledger, nano::block & block);
	/**
 	  This  filters blocks in four directions based on how the link field should be interpreted
 	  For state blocks the link field is interpreted as:
 	    If the balance has decreased, a destination account
 	    If the balance has not decreased
 	      If the link field is 0, a noop
 	      If the link field is an epoch link, an epoch sentinel
 	      Otherwise, a block hash of an block ready to be received
 	  For legacy blocks, the link field interpretation is applied to source field for receive and open blocks or the destination field for send blocks */
	block_op op () const;
	bool old () const;
	nano::process_result check ();
};
} // namespace nano
