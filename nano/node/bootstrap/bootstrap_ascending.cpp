#include <nano/node/bootstrap/bootstrap_ascending.hpp>
#include <nano/node/bootstrap/block_deserializer.hpp>

#include <nano/node/node.hpp>

using namespace std::chrono_literals;

nano::bootstrap::bootstrap_ascending::bootstrap_ascending (std::shared_ptr<nano::node> const & node_a, uint64_t incremental_id_a, std::string id_a) :
bootstrap_attempt{ node_a, nano::bootstrap_mode::ascending, incremental_id_a, id_a }
{
	std::cerr << '\0';
}

void nano::bootstrap::bootstrap_ascending::run ()
{
	auto connection = node->bootstrap_initiator.connections->connection (shared_from_this (), true);
	if (connection != nullptr)
	{
		debug_assert (connection != nullptr);
		nano::bulk_pull message{ node->network_params.network };
		message.header.flag_set (nano::message_header::bulk_pull_ascending_flag);
		message.start = nano::dev::genesis->hash ();
		message.end = 0;
		std::promise<void> promise;
		connection->channel->send (message, [this_l = std::dynamic_pointer_cast<nano::bootstrap::bootstrap_ascending> (shared_from_this ()), &promise, connection, node = node] (boost::system::error_code const &, std::size_t) {
			// Initiate reading blocks
			this_l->read_block (connection, promise);
		});
		promise.get_future ().wait ();
		std::cerr << "done\n";
		stop ();
		condition.notify_all ();
	}
}

void nano::bootstrap::bootstrap_ascending::read_block (std::shared_ptr<nano::bootstrap_client> connection, std::promise<void> & promise)
{
	auto deserializer = std::make_shared<nano::bootstrap::block_deserializer>();
	deserializer->read (*connection->socket, [this_l = std::dynamic_pointer_cast<nano::bootstrap::bootstrap_ascending> (shared_from_this ()), &promise, connection, node = node] (boost::system::error_code ec, std::shared_ptr<nano::block> block) {
		if (block == nullptr)
		{
			std::cerr << "promise\n";
			promise.set_value ();
			return;
		}
		std::cerr << "block: " << block->hash ().to_string () << std::endl;
		node->block_processor.add (block);
		this_l->read_block (connection, promise);
	} );
}

void nano::bootstrap::bootstrap_ascending::get_information (boost::property_tree::ptree &)
{
}
