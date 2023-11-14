#include <nano/lib/blocks.hpp>
#include <nano/secure/block_check_context.hpp>
#include <nano/secure/ledger.hpp>
#include <nano/store/block.hpp>
#include <nano/store/component.hpp>
#include <nano/store/pending.hpp>

nano::block_check_context::block_check_context (nano::store::transaction const & transaction, nano::ledger & ledger, nano::block & block) :
	block{ &block },
	ledger{ ledger }
{
	if (ledger.block_or_pruned_exists (transaction, block.hash ()))
	{
		this->block = nullptr; // Signal this block already exists by nulling out block
		return;
	}
	if (!block.previous ().is_zero ())
	{
		previous = ledger.store.block.get (transaction, block.previous ());
	}
	if (!gap_previous ())
	{
		state = ledger.account_info (transaction, account ());
		if (!state)
		{
			state = nano::account_info{};
		}
		source_exists = ledger.block_or_pruned_exists (transaction, source ());
		receivable = ledger.pending_info (transaction, { account (), source () });
		any_receivable = ledger.store.pending.any (transaction, account ());
		details = block_details{ epoch (), is_send (), is_receive (), is_epoch () };
	}
}

auto nano::block_check_context::op () const -> block_op
{
	debug_assert (state.has_value ());
	switch (block->type ())
	{
		case nano::block_type::state:
			if (block->balance () < state->balance)
			{
				return block_op::send;
			}
			if (previous != nullptr && block->link ().is_zero ())
			{
				return block_op::noop;
			}
			if (ledger.constants.epochs.is_epoch_link (block->link ()))
			{
				return block_op::epoch;
			}
			return block_op::receive;
		case nano::block_type::send:
			return block_op::send;
		case nano::block_type::open:
		case nano::block_type::receive:
			return block_op::receive;
		case nano::block_type::change:
			return block_op::noop;
		case nano::block_type::not_a_block:
		case nano::block_type::invalid:
			release_assert (false);
			break;
	}
	release_assert (false);
}

bool nano::block_check_context::is_send () const
{
	return op () == block_op::send;
}

bool nano::block_check_context::is_receive () const
{
	return op () == block_op::receive;
}

bool nano::block_check_context::is_epoch () const
{
	return op () == block_op::epoch;
}

nano::amount nano::block_check_context::balance () const
{
	switch (block->type ())
	{
		case nano::block_type::state:
		case nano::block_type::send:
			return block->balance ();
		case nano::block_type::open:
			return receivable->amount;
		case nano::block_type::change:
			return ledger.balance (*previous);
		case nano::block_type::receive:
			return ledger.balance (*previous) + receivable->amount.number ();
		default:
			release_assert (false);
	}
}

uint64_t nano::block_check_context::height () const
{
	return previous ? previous->sideband ().height + 1 : 1;
}

nano::epoch nano::block_check_context::epoch () const
{
	if (is_epoch ())
	{
		return ledger.constants.epochs.epoch (block->link ());
	}
	nano::epoch account_epoch{ nano::epoch::epoch_0 };
	nano::epoch source_epoch{ nano::epoch::epoch_0 };
	if (previous != nullptr)
	{
		account_epoch = previous->sideband ().details.epoch;
	}
	if (receivable.has_value ())
	{
		source_epoch = receivable->epoch;
	}
	return std::max (account_epoch, source_epoch);
}

bool nano::block_check_context::old () const
{
	return block == nullptr;
}

nano::account nano::block_check_context::account () const
{
	switch (block->type ())
	{
		case nano::block_type::change:
		case nano::block_type::receive:
		case nano::block_type::send:
			debug_assert (previous != nullptr);
			switch (previous->type ())
			{
				case nano::block_type::state:
				case nano::block_type::open:
					return previous->account ();
				case nano::block_type::change:
				case nano::block_type::receive:
				case nano::block_type::send:
					return previous->sideband ().account;
				case nano::block_type::not_a_block:
				case nano::block_type::invalid:
					debug_assert (false);
					break;
			}
			break;
		case nano::block_type::state:
		case nano::block_type::open:
			return block->account ();
		case nano::block_type::not_a_block:
		case nano::block_type::invalid:
			debug_assert (false);
			break;
	}
	// std::unreachable (); c++23
	return 1; // Return an account that cannot be signed for.
}

nano::block_hash nano::block_check_context::source () const
{
	switch (block->type ())
	{
		case nano::block_type::send:
		case nano::block_type::change:
			// 0 is returned for source on send/change blocks
		case nano::block_type::receive:
		case nano::block_type::open:
			return block->source ();
		case nano::block_type::state:
			return block->link ().as_block_hash ();
		case nano::block_type::not_a_block:
		case nano::block_type::invalid:
			return 0;
	}
	debug_assert (false);
	return 0;
}

nano::account nano::block_check_context::signer (nano::epochs const & epochs) const
{
	debug_assert (block != nullptr);
	switch (block->type ())
	{
		case nano::block_type::send:
		case nano::block_type::receive:
		case nano::block_type::change:
			debug_assert (previous != nullptr); // Previous block must be passed in for non-open blocks
			switch (previous->type ())
			{
				case nano::block_type::state:
					debug_assert (false && "Legacy blocks can't follow state blocks");
					break;
				case nano::block_type::open:
					// Open blocks have the account written in the block.
					return previous->account ();
				default:
					// Other legacy block types have the account stored in sideband.
					return previous->sideband ().account;
			}
			break;
		case nano::block_type::state:
		{
			debug_assert (dynamic_cast<nano::state_block *> (block));
			// If the block is a send, while the link field may contain an epoch link value, it is actually a malformed destination address.
			return (!epochs.is_epoch_link (block->link ()) || is_send ()) ? block->account () : epochs.signer (epochs.epoch (block->link ()));
		}
		case nano::block_type::open: // Open block signer is determined statelessly as it's written in the block
			return block->account ();
		case nano::block_type::invalid:
		case nano::block_type::not_a_block:
			debug_assert (false);
			break;
	}
	// std::unreachable (); c++23
	return 1; // Return an account that cannot be signed for.
}

bool nano::block_check_context::gap_previous () const
{
	return !block->previous ().is_zero () && previous == nullptr;
}

bool nano::block_check_context::failed (nano::process_result const & code) const
{
	return code != nano::process_result::progress;
}

nano::process_result nano::block_check_context::rule_sufficient_work () const
{
	if (ledger.constants.work.difficulty (*block) < ledger.constants.work.threshold (block->work_version (), details))
	{
		return nano::process_result::insufficient_work;
	}
	return nano::process_result::progress;
}

nano::process_result nano::block_check_context::rule_reserved_account () const
{
	switch (block->type ())
	{
		case nano::block_type::open:
		case nano::block_type::state:
			if (!block->account ().is_zero ())
			{
				return nano::process_result::progress;
			}
			else
			{
				return nano::process_result::opened_burn_account;
			}
			break;
		case nano::block_type::change:
		case nano::block_type::receive:
		case nano::block_type::send:
			return nano::process_result::progress;
		case nano::block_type::invalid:
		case nano::block_type::not_a_block:
			release_assert (false);
			break;
	}
	release_assert (false);
}

nano::process_result nano::block_check_context::rule_previous_frontier () const
{
	debug_assert (block != nullptr); //
	if (gap_previous ())
	{
		return nano::process_result::gap_previous;
	}
	else
	{
		return nano::process_result::progress;
	}
}

nano::process_result nano::block_check_context::rule_state_block_account_position () const
{
	if (previous == nullptr)
	{
		return nano::process_result::progress;
	}
	switch (block->type ())
	{
		case nano::block_type::send:
		case nano::block_type::receive:
		case nano::block_type::change:
		{
			switch (previous->type ())
			{
				case nano::block_type::state:
					return nano::process_result::block_position;
				default:
					return nano::process_result::progress;
			}
		}
		default:
			return nano::process_result::progress;
	}
}

nano::process_result nano::block_check_context::rule_state_block_source_position () const
{
	if (!receivable.has_value ())
	{
		return nano::process_result::progress;
	}
	switch (block->type ())
	{
		case nano::block_type::receive:
		case nano::block_type::open:
		{
			if (receivable->epoch > nano::epoch::epoch_0)
			{
				return nano::process_result::unreceivable;
			}
			return nano::process_result::progress;
		}
		case nano::block_type::state:
			return nano::process_result::progress;
		default:
			release_assert (false);
	}
}

nano::process_result nano::block_check_context::rule_block_signed () const
{
	if (!nano::validate_message (signer (ledger.constants.epochs), block->hash (), block->block_signature ()))
	{
		return nano::process_result::progress;
	}
	return nano::process_result::bad_signature;
}

nano::process_result nano::block_check_context::rule_metastable () const
{
	debug_assert (state.has_value ());
	if (block->previous () == state->head)
	{
		return nano::process_result::progress;
	}
	else
	{
		return nano::process_result::fork;
	}
}

nano::process_result nano::block_check_context::check_receive_rules () const
{
	if (!source_exists)
	{
		// Probably redundant to check as receivable would also have no value
		return nano::process_result::gap_source;
	}
	if (!receivable.has_value ())
	{
		return nano::process_result::unreceivable;
	}
	if (block->type () == nano::block_type::state)
	{
		auto next_balance = state->balance.number () + receivable->amount.number ();
		if (next_balance != block->balance ().number ())
		{
			return nano::process_result::balance_mismatch;
		}
	}
	return nano::process_result::progress;
}

nano::process_result nano::block_check_context::check_epoch_rules () const
{
	debug_assert (state.has_value ());
	// Epoch blocks may not change an account's balance
	if (state->balance != block->balance ())
	{
		return nano::process_result::balance_mismatch;
	}
	// Epoch blocks may not change an account's representative
	if (state->representative != block->representative ())
	{
		return nano::process_result::representative_mismatch;
	}
	// Epoch blocks may not be created for accounts that have no receivable entries
	if (block->previous ().is_zero () && !any_receivable)
	{
		return nano::process_result::gap_epoch_open_pending;
	}
	auto previous_epoch = nano::epoch::epoch_0;
	if (previous != nullptr)
	{
		previous_epoch = previous->sideband ().details.epoch;
	}
	// Epoch blocks may only increase epoch number by one
	if (!state->head.is_zero () && !nano::epochs::is_sequential (previous_epoch, epoch ()))
	{
		return nano::process_result::block_position;
	}
	return nano::process_result::progress;
}

nano::process_result nano::block_check_context::check_send_rules () const
{
	debug_assert (block->type () == nano::block_type::send || block->type () == nano::block_type::state);
	if (state->balance < block->balance ())
	{
		return nano::process_result::negative_spend;
	}
	return nano::process_result::progress;
}

nano::process_result nano::block_check_context::check_noop_rules () const
{
	if (balance () != ledger.balance (*previous))
	{
		return nano::process_result::balance_mismatch;
	}
	return nano::process_result::progress;
}

nano::process_result nano::block_check_context::check ()
{
	if (old ())
	{
		return nano::process_result::old;
	}
	nano::process_result result;
	if (failed (result = rule_sufficient_work ()))
	{
		return result;
	}
	if (failed (result = rule_reserved_account ()))
	{
		return result;
	}
	if (failed (result = rule_previous_frontier ()))
	{
		return result;
	}
	if (failed (result = rule_state_block_account_position ()))
	{
		return result;
	}
	if (failed (result = rule_state_block_source_position ()))
	{
		return result;
	}
	if (failed (result = rule_block_signed ()))
	{
		return result;
	}
	if (failed (result = rule_metastable ()))
	{
		return result;
	}
	switch (op ())
	{
		case block_op::receive:
			result = check_receive_rules ();
			break;
		case block_op::send:
			result = check_send_rules ();
			break;
		case block_op::noop:
			result = check_noop_rules ();
			break;
		case block_op::epoch:
			result = check_epoch_rules ();
			break;
	}
	if (result == nano::process_result::progress)
	{
		nano::block_sideband sideband{ account (), balance (), height (), nano::seconds_since_epoch (), details, receivable ? receivable->epoch : nano::epoch::epoch_0 };
		block->sideband_set (sideband);
	}
	return result;
}
