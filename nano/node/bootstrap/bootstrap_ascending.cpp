#include <nano/node/bootstrap/bootstrap_ascending.hpp>
#include <nano/node/bootstrap/block_deserializer.hpp>
#include <nano/secure/common.hpp>

#include <nano/node/node.hpp>

#include <boost/format.hpp>

using namespace std::chrono_literals;

nano::bootstrap::bootstrap_ascending::async_tag::async_tag (std::shared_ptr<nano::bootstrap::bootstrap_ascending> bootstrap) :
	bootstrap{ bootstrap }
{
	std::lock_guard<nano::mutex> lock{ bootstrap->mutex };
	++bootstrap->requests;
	bootstrap->condition.notify_all ();
	//std::cerr << boost::str (boost::format ("Request started\n"));
}

nano::bootstrap::bootstrap_ascending::async_tag::~async_tag ()
{
	std::lock_guard<nano::mutex> lock{ bootstrap->mutex };
	--bootstrap->requests;
	++bootstrap->requests_total;
	if (blocks > 0)
	{
		++bootstrap->requests_non_empty;
	}
	bootstrap->condition.notify_all ();
	//std::cerr << boost::str (boost::format ("Request completed\n"));
}

void nano::bootstrap::bootstrap_ascending::send (std::shared_ptr<async_tag> tag, socket_channel ctx, nano::hash_or_account const & start)
{
	nano::bulk_pull message{ node->network_params.network };
	message.header.flag_set (nano::message_header::bulk_pull_ascending_flag);
	message.header.flag_set (nano::message_header::bulk_pull_count_present_flag);
	message.start = start;
	message.end = 0;
	message.count = request_message_count;
	//std::cerr << boost::str (boost::format ("Request sent for: %1% to: %2%\n") % message.start.to_string () % ctx.first->remote_endpoint ());
	auto channel = ctx.second;
	channel->send (message, [this_l = shared (), tag, ctx] (boost::system::error_code const & ec, std::size_t size) {
		this_l->read_block (tag, ctx);
	});
}

void nano::bootstrap::bootstrap_ascending::read_block (std::shared_ptr<async_tag> tag, socket_channel ctx)
{
	auto deserializer = std::make_shared<nano::bootstrap::block_deserializer>();
	auto socket = ctx.first;
	deserializer->read (*socket, [this_l = shared (), tag, ctx] (boost::system::error_code ec, std::shared_ptr<nano::block> block) {
		if (block == nullptr)
		{
			//std::cerr << "stream end\n";
			std::lock_guard<nano::mutex> lock{ this_l->mutex };
			this_l->sockets.push_back (ctx);
			return;
		}
		//std::cerr << boost::str (boost::format ("block: %1%\n") % block->hash ().to_string ());
		this_l->node->block_processor.add (block);
		this_l->read_block (tag, ctx);
		++tag->blocks;
	});
}

nano::account nano::bootstrap::bootstrap_ascending::random_account_entry (nano::account const & search)
{
	auto tx = node->store.tx_begin_read ();
	auto existing_account = node->store.account.begin (tx, search);
	if (existing_account == node->store.account.end ())
	{
		existing_account = node->store.account.begin (tx);
		debug_assert (existing_account != node->store.account.end ());
	}
	//std::cerr << boost::str (boost::format ("Found: %1%\n") % result.to_string ());
	return existing_account->first;
}

std::optional<nano::account> nano::bootstrap::bootstrap_ascending::random_pending_entry (nano::account const & search)
{
	auto tx = node->store.tx_begin_read ();
	auto existing_pending = node->store.pending.begin (tx, nano::pending_key{ search, 0 });
	std::optional<nano::account> result;
	if (existing_pending != node->store.pending.end ())
	{
		result = existing_pending->first.key ();
	}
	return result;
}

std::optional<nano::account> nano::bootstrap::bootstrap_ascending::random_ledger_account ()
{
	nano::account search;
	nano::random_pool::generate_block (search.bytes.data (), search.bytes.size ());
	//std::cerr << boost::str (boost::format ("Search: %1% ") % search.to_string ());
	auto rand = nano::random_pool::generate_byte ();
	if (rand & 0x1)
	{
		//std::cerr << boost::str (boost::format ("account "));
		return random_account_entry (search);
	}
	else
	{
		//std::cerr << boost::str (boost::format ("pending "));
		return random_pending_entry (search);
	}
}

std::optional<nano::account> nano::bootstrap::bootstrap_ascending::hint_account ()
{
	std::optional<nano::account> result;
	std::lock_guard<nano::mutex> lock{ mutex };
	if (!trace_set.empty ())
	{
		auto iter = trace_set.begin ();
		result = *iter;
		trace_set.erase (iter);
	}
	return result;
}

std::optional<nano::account> nano::bootstrap::bootstrap_ascending::pick_account ()
{
	auto result = hint_account ();
	if (result)
	{
		++picked_hint;
	}
	else
	{
		result = random_ledger_account ();
		++picked_ledger_random;
	}
	return result;
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
	auto tag = std::make_shared<async_tag> (shared ());
	auto account = pick_account ();
	if (!account)
	{
		return;
	}
	nano::account_info info;
	nano::hash_or_account start = *account;
	if (!node->store.account.get (node->store.tx_begin_read (), *account, info))
	{
		start = info.head;
	}
	std::unique_lock<nano::mutex> lock{ mutex };
	if (!sockets.empty ())
	{
		auto socket = sockets.front ();
		sockets.pop_front ();
		send (tag, socket, start);
		return;
	}
	lock.unlock ();
	auto endpoint = node->network.bootstrap_peer (true);
	if (endpoint != nano::tcp_endpoint (boost::asio::ip::address_v6::any (), 0))
	{
		auto socket = std::make_shared<nano::client_socket> (*node);
		auto channel = std::make_shared<nano::transport::channel_tcp> (*node, socket);
		std::cerr << boost::str (boost::format ("Connecting: %1%\n") % endpoint);
		socket->async_connect (endpoint,
		[this_l = shared (), endpoint, tag, ctx = std::make_pair (socket, channel), start] (boost::system::error_code const & ec) {
			if (ec)
			{
				std::cerr << boost::str (boost::format ("connect failed to: %1%\n") % endpoint);
				return;
			}
			std::cerr << boost::str (boost::format ("connected to: %1%\n") % endpoint);
			this_l->send (tag, ctx, start);
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
		std::lock_guard<nano::mutex> lock{ this_l->mutex };
		this_l->trace_set.insert (account);
		if (block.sideband ().details.is_send)
		{
			nano::account recipient{ 0 };
			switch (block.type ())
			{
				case nano::block_type::send:
					recipient = block.destination ();
					break;
				case nano::block_type::state:
					recipient = block.link ().as_account ();
					break;
				default:
					debug_assert (false);
					break;
			}
			if (!recipient.is_zero ())
			{
				this_l->trace_set.insert (recipient);
			}
		}
	});
	//for (auto i = 0; !stopped && i < 5'000; ++i)
	int counter = 0;
	while (!stopped)
	{
		request_one ();
		if ((++counter % 10'000) == 0)
		{
			node->block_processor.flush ();
			double success_rate = static_cast<double> (requests_non_empty.load ()) / requests_total.load ();
			std::cerr << boost::str (boost::format ("hints: %1% random: %2% Success rate: %3%\n") % picked_hint.load () % picked_ledger_random.load () % success_rate);
			picked_hint = picked_ledger_random = requests_non_empty = requests_total = 0;
			node->block_processor.dump_result_hist ();
		}
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
