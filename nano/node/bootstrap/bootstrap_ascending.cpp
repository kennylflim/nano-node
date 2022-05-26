#include <nano/node/bootstrap/bootstrap_ascending.hpp>
#include <nano/node/bootstrap/block_deserializer.hpp>
#include <nano/secure/common.hpp>

#include <nano/node/node.hpp>

#include <boost/format.hpp>

using namespace std::chrono_literals;

nano::bootstrap::bootstrap_ascending::request::request (std::shared_ptr<nano::bootstrap::bootstrap_ascending> bootstrap, std::shared_ptr<nano::socket> socket, std::shared_ptr<nano::transport::channel> channel) :
	bootstrap{ bootstrap },
	socket{ socket },
	channel{ channel }
{
	std::lock_guard<nano::mutex> lock{ bootstrap->mutex };
	++bootstrap->requests;
	bootstrap->condition.notify_all ();
	std::cerr << boost::str (boost::format ("Request started\n"));
}

nano::bootstrap::bootstrap_ascending::request::~request ()
{
	std::lock_guard<nano::mutex> lock{ bootstrap->mutex };
	--bootstrap->requests;
	bootstrap->condition.notify_all ();
	std::cerr << boost::str (boost::format ("Request completed\n"));
}

void nano::bootstrap::bootstrap_ascending::request::send ()
{
	nano::account search;
	nano::random_pool::generate_block (search.bytes.data (), search.bytes.size ());
	nano::account_info info;
	auto tx = bootstrap->node->store.tx_begin_read ();
	auto existing = bootstrap->node->store.account.begin (tx, search);
	if (existing == bootstrap->node->store.account.end ())
	{
		existing = bootstrap->node->store.account.begin (tx);
		debug_assert (existing != bootstrap->node->store.account.end ());
	}
	auto account = existing->first;
	//std::cerr << "requesting: " << account.to_account () << " at: " << start.to_string () <<  " from endpoint: " << socket->remote_endpoint() << std::endl;
	nano::bulk_pull message{ bootstrap->node->network_params.network };
	message.header.flag_set (nano::message_header::bulk_pull_ascending_flag);
	message.header.flag_set (nano::message_header::bulk_pull_count_present_flag);
	message.start = info.head;
	message.end = 0;
	message.count = cutoff;
	std::cerr << boost::str (boost::format ("Request sent: %1%\n") % account.to_account ());
	channel->send (message, [this_l = shared_from_this ()] (boost::system::error_code const & ec, std::size_t size) {
		this_l->read_block ();
	});
}

void nano::bootstrap::bootstrap_ascending::request::read_block ()
{
	auto deserializer = std::make_shared<nano::bootstrap::block_deserializer>();
	deserializer->read (*socket, [this_l = shared_from_this ()] (boost::system::error_code ec, std::shared_ptr<nano::block> block) {
		if (block == nullptr)
		{
			std::cerr << "stream end\n";
			return;
		}
		std::cerr << boost::str (boost::format ("block: %1%\n") % block->hash ().to_string ());
		this_l->bootstrap->node->block_processor.add (block);
		this_l->read_block ();
	});
}

bool nano::bootstrap::bootstrap_ascending::wait_available_request ()
{
	std::unique_lock<nano::mutex> lock{ mutex };
	condition.wait (lock, [this] () { return stopped || requests < requests_max; } );
	return stopped;
}

nano::bootstrap::bootstrap_ascending::bootstrap_ascending (std::shared_ptr<nano::node> const & node_a, uint64_t incremental_id_a, std::string id_a) :
	bootstrap_attempt{ node_a, nano::bootstrap_mode::ascending, incremental_id_a, id_a }
{
	std::cerr << '\0';
}

/*void nano::bootstrap::bootstrap_ascending::dump_miss_histogram ()
{
	std::vector<int> histogram;
	std::lock_guard<nano::mutex> lock{ mutex };
	for (auto const &[account, count]: misses)
	{
		if (count >= histogram.size ())
		{
			histogram.resize (count + 1);
		}
		++histogram[count];
	}
	for (auto i: histogram)
	{
		std::cerr << std::to_string (i) << ' ';
	}
	std::cerr << std::endl;
}*/

void nano::bootstrap::bootstrap_ascending::request_one ()
{
	wait_available_request ();
	if (stopped)
	{
		return;
	}
	/*std::unique_lock<nano::mutex> lock{ mutex };
	std::shared_ptr<nano::socket> socket;
	std::shared_ptr<nano::transport::channel> channel;
	if (!sockets.empty ())
	{
		auto &[socket, channel] = sockets.front ();
		sockets.pop_front ();
		auto request = std::make_shared<bootstrap_ascending::request> (shared (), socket, channel);
		request->send ();
	}*/
	auto endpoint = node->network.bootstrap_peer (true);
	if (endpoint != nano::tcp_endpoint (boost::asio::ip::address_v6::any (), 0))
	{
		auto socket = std::make_shared<nano::client_socket> (*node);
		auto channel = std::make_shared<nano::transport::channel_tcp> (*node, socket);
		socket->async_connect (endpoint,
		[endpoint, request = std::make_shared<request> (shared (), socket, channel)] (boost::system::error_code const & ec) {
			if (ec)
			{
				std::cerr << boost::str (boost::format ("connect failed to: %1%\n") % endpoint);
				return;
			}
			std::cerr << boost::str (boost::format ("connected to: %1%\n") % endpoint);
			request->send ();
		});
	}
	else
	{
		stop ();
	}
}

static int pass_number = 0;

void nano::bootstrap::bootstrap_ascending::run ()
{
	std::cerr << "!! Starting with:" << std::to_string (pass_number++) << "\n";
	node->block_processor.inserted.add ([this_w = std::weak_ptr<nano::bootstrap::bootstrap_ascending>{ shared () }] (nano::transaction const & tx, nano::block const & block) {
		auto this_l = this_w.lock ();
		if (this_l == nullptr)
		{
			return;
		}
		auto account = this_l->node->ledger.account (tx, block.hash ());
	});
	for (auto i = 0; !stopped && i < 10'000; ++i)
	{
		request_one ();
	}
	
	std::cerr << "!! stopping" << std::endl;
}

void nano::bootstrap::bootstrap_ascending::get_information (boost::property_tree::ptree &)
{
}

std::shared_ptr<nano::bootstrap::bootstrap_ascending> nano::bootstrap::bootstrap_ascending::shared ()
{
	return std::static_pointer_cast<nano::bootstrap::bootstrap_ascending> (shared_from_this ());
}
