#include <nano/node/bootstrap/bootstrap_ascending.hpp>
#include <nano/node/bootstrap/block_deserializer.hpp>
#include <nano/secure/common.hpp>

#include <nano/node/node.hpp>

using namespace std::chrono_literals;

nano::bootstrap::bootstrap_ascending::bootstrap_ascending (std::shared_ptr<nano::node> const & node_a, uint64_t incremental_id_a, std::string id_a) :
bootstrap_attempt{ node_a, nano::bootstrap_mode::ascending, incremental_id_a, id_a }
{
	std::cerr << '\0';
}

std::shared_ptr<nano::bootstrap::bootstrap_ascending> nano::bootstrap::bootstrap_ascending::shared ()
{
	return std::static_pointer_cast<nano::bootstrap::bootstrap_ascending> (shared_from_this ());
}

void nano::bootstrap::bootstrap_ascending::request ()
{
	blocks = 0;
	auto connection = node->bootstrap_initiator.connections->connection (shared_from_this (), true);
	if (connection != nullptr)
	{
		nano::hash_or_account start = next;
		nano::account_info info;
		if (!node->store.account.get (node->store.tx_begin_read (), next, info))
		{
			start = info.head;
		}
		std::cerr << "requesting: " << next.to_account () << " at: " << start.to_string () <<  " from endpoint: " << connection->socket->remote_endpoint() << std::endl;
		debug_assert (connection != nullptr);
		nano::bulk_pull message{ node->network_params.network };
		message.header.flag_set (nano::message_header::bulk_pull_ascending_flag);
		message.header.flag_set (nano::message_header::bulk_pull_count_present_flag);
		message.start = start;
		message.end = 0;
		message.count = cutoff;
		++requests;
		connection->channel->send (message, [this_l = shared (), connection, node = node] (boost::system::error_code const &, std::size_t) {
			//std::cerr << "callback\n";
			// Initiate reading blocks
			this_l->read_block (connection);
		});
	}
}

bool nano::bootstrap::bootstrap_ascending::compute_next ()
{
	next = next.number () + 1;
	return load_next (node->store.tx_begin_read ());
}

bool nano::bootstrap::bootstrap_ascending::load_next (nano::transaction const & tx)
{
	bool result = false;
	switch (state)
	{
		case activity::account:
		{
			auto existing = node->store.account.begin (tx, next);
			std::unique_lock<nano::mutex> lock{ mutex };
			if (existing != node->store.account.end ())
			{
				next = existing->first;
			}
			else
			{
				state = activity::pending;
				next = 1;
				lock.unlock ();
				result = load_next (tx);
			}
			break;
		}
		case activity::pending:
		{
			auto existing = node->store.pending.begin (tx, nano::pending_key{ next, 0 });
			std::unique_lock<nano::mutex> lock{ mutex };
			if (existing != node->store.pending.end ())
			{
				next = existing->first.key ();
			}
			else
			{
				state = activity::queue;
				next = 1;
				lock.unlock ();
				result = load_next (tx);
			}
			break;
		}
		case activity::queue:
		{
			std::unique_lock<nano::mutex> lock{ mutex };
			if (!queue.empty ())
			{
				auto item = queue.begin ();
				next = *item;
				queue.erase (item);
			}
			else
			{
				lock.unlock ();
				result = true;
			}
			break;
		}
	}
	return result;
}

void nano::bootstrap::bootstrap_ascending::run ()
{
	std::cerr << "!! Starting\n";
	node->block_processor.inserted.add ([this_w = std::weak_ptr<nano::bootstrap::bootstrap_ascending>{ shared () }] (nano::transaction const & tx, nano::block const & block) {
		auto this_l = this_w.lock ();
		if (this_l == nullptr)
		{
			return;
		}
		//std::cerr << "done: " << block.hash ().to_string () << std::endl;
		if (block.type () == nano::block_type::send || this_l->node->ledger.is_send (tx, static_cast<nano::state_block const &>(block)))
		{
			auto destination = this_l->node->ledger.block_destination (tx, block);
			std::lock_guard<nano::mutex> lock{ this_l->mutex };
			this_l->queue.insert (destination);
		}
	});
	fill_drain_queue ();
	stop ();
	std::cerr << "!! stopping\n";
}

void nano::bootstrap::bootstrap_ascending::fill_drain_queue ()
{
	do
	{
		std::cerr << "Begin pass\n";
		bool done = false;
		while (!stopped && !done)
		{
			done = compute_next ();
			if (!done)
			{
				request ();
				std::unique_lock<nano::mutex> lock{ mutex };
				condition.wait (lock, [this] () { return stopped || requests < 1; });
				std::cerr << "blocks: " << blocks << std::endl;
				if (blocks >= cutoff)
				{
					requeue.insert (next);
				}
				blocks = 0;
			}
		}
		if (!stopped)
		{
			std::unique_lock<nano::mutex> lock{ mutex };
			if (queue.empty () && requeue.empty ())
			{
				lock.unlock ();
				node->block_processor.flush ();
				lock.lock ();
			}
			std::cerr << "requeueing: " << requeue.size () << std::endl;
			queue.insert (requeue.begin (), requeue.end ());
			requeue.clear ();
		}
		std::cerr << "End pass\n";
	} while (!stopped && !queue.empty ());
	std::cerr << "stopped: " << stopped.load () << " queued: " << queue.empty () << std::endl;
}

void nano::bootstrap::bootstrap_ascending::read_block (std::shared_ptr<nano::bootstrap_client> connection)
{
	auto deserializer = std::make_shared<nano::bootstrap::block_deserializer>();
	deserializer->read (*connection->socket, [this_l = shared (), connection, node = node] (boost::system::error_code ec, std::shared_ptr<nano::block> block) {
		if (block == nullptr)
		{
			connection->connections.pool_connection (connection);
			--this_l->requests;
			this_l->condition.notify_all ();
			return;
		}
		//std::cerr << "block: " << block->hash ().to_string () << std::endl;
		node->block_processor.add (block);
		this_l->read_block (connection);
		++this_l->blocks;
	} );
}

void nano::bootstrap::bootstrap_ascending::get_information (boost::property_tree::ptree &)
{
}
