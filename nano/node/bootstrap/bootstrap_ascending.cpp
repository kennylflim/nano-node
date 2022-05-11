#include <nano/node/bootstrap/bootstrap_ascending.hpp>
#include <nano/node/bootstrap/block_deserializer.hpp>

#include <nano/node/node.hpp>

using namespace std::chrono_literals;

nano::bootstrap::bootstrap_ascending::bootstrap_ascending (std::shared_ptr<nano::node> const & node_a, uint64_t incremental_id_a, std::string id_a) :
bootstrap_attempt{ node_a, nano::bootstrap_mode::ascending, incremental_id_a, id_a }
{
	std::cerr << '\0';
}

void nano::bootstrap::bootstrap_ascending::request ()
{
	std::cerr << "blocks: " << blocks << std::endl;
	compute_next ();
	std::cerr << "next: " << next.to_account () << std::endl;
	if (!stopped)
	{
		blocks = 0;
		auto connection = node->bootstrap_initiator.connections->connection (shared_from_this (), true);
		if (connection != nullptr)
		{
			std::cerr << "requesting: " << next.to_account () << " from endpoint: " << connection->socket->remote_endpoint() << std::endl;
			debug_assert (connection != nullptr);
			nano::bulk_pull message{ node->network_params.network };
			message.header.flag_set (nano::message_header::bulk_pull_ascending_flag);
			message.start = next;
			message.end = 0;
			connection->channel->send (message, [this_l = std::dynamic_pointer_cast<nano::bootstrap::bootstrap_ascending> (shared_from_this ()), connection, node = node] (boost::system::error_code const &, std::size_t) {
				//std::cerr << "callback\n";
				// Initiate reading blocks
				this_l->read_block (connection);
			});
		}
		next = next.number () + 1;
	}
}

void nano::bootstrap::bootstrap_ascending::compute_next ()
{
	auto tx = node->store.tx_begin_read ();
	if (account_table)
	{
		auto existing = node->store.account.begin (tx, next);
		if (existing != node->store.account.end ())
		{
			next = existing->first;
		}
		else
		{
			account_table = false;
			next = 0;
			compute_next ();
		}
	}
	else
	{
		auto existing = node->store.pending.begin (tx, nano::pending_key{ next, 0 });
		if (existing != node->store.pending.end ())
		{
			next = existing->first.key ();
		}
		else
		{
			stop ();
		}
	}
}

void nano::bootstrap::bootstrap_ascending::run ()
{
	std::cerr << "Starting\n";
	request ();
	std::unique_lock<nano::mutex> lock{ mutex };
	condition.wait (lock, [this] () { return stopped.load (); });
}

void nano::bootstrap::bootstrap_ascending::read_block (std::shared_ptr<nano::bootstrap_client> connection)
{
	auto deserializer = std::make_shared<nano::bootstrap::block_deserializer>();
	deserializer->read (*connection->socket, [this_l = std::dynamic_pointer_cast<nano::bootstrap::bootstrap_ascending> (shared_from_this ()), connection, node = node] (boost::system::error_code ec, std::shared_ptr<nano::block> block) {
		if (block == nullptr)
		{
			connection->connections.pool_connection (connection);
			this_l->request ();
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
