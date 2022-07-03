#include <nano/node/bootstrap/bootstrap_ascending.hpp>
#include <nano/node/bootstrap/block_deserializer.hpp>
#include <nano/secure/common.hpp>

#include <nano/node/node.hpp>

#include <boost/format.hpp>

using namespace std::chrono_literals;

nano::bootstrap::bootstrap_ascending::account_sets::account_sets ()
{
}

void nano::bootstrap::bootstrap_ascending::account_sets::dump () const
{
	std::cerr << boost::str (boost::format ("Forwarding: %1%   blocking: %2%\n") % forwarding.size () % blocking.size ());
	std::deque<size_t> weight_counts;
	for (auto &[account, count]: backoff)
	{
		auto log = std::log2 (std::max<decltype(count)> (count, 1));
		//std::cerr << "log: " << log << ' ';
		auto index = static_cast<size_t> (log);
		if (weight_counts.size () <= index)
		{
			weight_counts.resize (index + 1);
		}
		++weight_counts[index];
	}
	std::string output;
	output += "Backoff hist (size: " + std::to_string (backoff.size ()) + "): ";
	for (size_t i = 0, n = weight_counts.size (); i < n; ++i)
	{
		output += std::to_string (weight_counts[i]) + ' ';
	}
	output += '\n';
	std::cerr << output;
}

void nano::bootstrap::bootstrap_ascending::account_sets::forward (nano::account const & account)
{
	if (blocking.count (account) == 0)
	{
		forwarding.insert (account);
	}
}

void nano::bootstrap::bootstrap_ascending::account_sets::block (nano::account const & account)
{
	backoff.erase (account);
	forwarding.erase (account);
	blocking.insert (account);
}

void nano::bootstrap::bootstrap_ascending::account_sets::unblock (nano::account const & account)
{
	blocking.erase (account);
	backoff.emplace (account, 0.0f);
}

nano::account nano::bootstrap::bootstrap_ascending::account_sets::random ()
{
	debug_assert (!backoff.empty ());
	std::vector<decltype(backoff)::mapped_type> weights;
	std::vector<nano::account> candidates;
	while (candidates.size () < account_sets::backoff_exclusion)
	{
		debug_assert (candidates.size () == weights.size ());
		nano::account search;
		nano::random_pool::generate_block (search.bytes.data (), search.bytes.size ());
		auto iter = backoff.lower_bound (search);
		if (iter == backoff.end ())
		{
			iter = backoff.begin ();
		}
		auto const[account, weight] = *iter;
		weights.push_back (weight);
		candidates.push_back (account);
	}
	for (auto i = weights.begin (), n = weights.end (); i != n; ++i)
	{
		*i = 1.0 / std::pow (2.0, *i);
	}
	std::discrete_distribution dist{ weights.begin (), weights.end () };
	auto selection = dist (rng);
	debug_assert (!weights.empty () && selection < weights.size ());
	auto result = candidates[selection];
	backoff[result] += 1.0;
	return result;
}

nano::account nano::bootstrap::bootstrap_ascending::account_sets::next ()
{
	if (!forwarding.empty ())
	{
		auto iter = forwarding.begin ();
		auto result = *iter;
		forwarding.erase (iter);
		return result;
	}
	return random ();
}

bool nano::bootstrap::bootstrap_ascending::account_sets::blocked (nano::account const & account) const
{
	return blocking.count (account) > 0;
}

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
	if (blocks != 0)
	{
		++bootstrap->responses;
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
	++requests_total;
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

nano::account nano::bootstrap::bootstrap_ascending::pick_account ()
{
	std::lock_guard<nano::mutex> lock{ mutex };
	return accounts.next ();
}

void nano::bootstrap::bootstrap_ascending::inspect (nano::transaction const & tx, nano::process_return const & result, nano::block const & block)
{
	std::lock_guard<nano::mutex> lock{ mutex };
	switch (result.code)
	{
		case nano::process_result::progress:
		{
			auto account = node->ledger.account (tx, block.hash ());
			accounts.unblock (account);
			accounts.forward (account);
			if (node->ledger.is_send (tx, block))
			{
				switch (block.type ())
				{
					case nano::block_type::send:
						accounts.forward (block.destination ());
						break;
					case nano::block_type::state:
						accounts.forward (block.link ().as_account ());
						break;
					default:
						debug_assert (false);
						break;
				}
			}
			break;
		}
		case nano::process_result::gap_source:
		{
			auto account = block.previous ().is_zero () ? block.account () : node->ledger.account (tx, block.previous ());
			accounts.block (account);
			break;
		}
		case nano::process_result::gap_previous:
			break;
		default:
			break;
	}
}

void nano::bootstrap::bootstrap_ascending::dump_stats ()
{
	node->block_processor.dump_result_hist ();
	std::lock_guard<nano::mutex> lock{ mutex };
	std::cerr << boost::str (boost::format ("Requests total: %1% forwarded: %2% source iterations: %4% satisfied: %5% responses: %6% accounts: %7% source blocked: %3%\n") % requests_total.load () % forwarded % 0.0f % source_iterations.load () % node->unchecked.satisfied_total.load () % responses.load () % node->ledger.cache.account_count.load ());
	accounts.dump ();
	responses = source_iterations = 0;
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
	auto tx = node_a->store.tx_begin_read ();
	for (auto i = node_a->store.account.begin (tx), n = node_a->store.account.end (); i != n; ++i)
	{
		accounts.unblock (i->first);
	}
	for (auto i = node_a->store.pending.begin (tx), n = node_a->store.pending.end (); i != n; ++i)
	{
		accounts.unblock (i->first.key ());
	}
}

void nano::bootstrap::bootstrap_ascending::request_one ()
{
	wait_available_request ();
	if (stopped)
	{
		return;
	}
	auto tag = std::make_shared<async_tag> (shared ());
	auto account = pick_account ();
	nano::account_info info;
	nano::hash_or_account start = account;
	if (!node->store.account.get (node->store.tx_begin_read (), account, info))
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
	std::cerr << "!! Starting with:" << std::to_string (pass_number++) << std::endl;
	node->block_processor.processed.add ([this_w = std::weak_ptr<nano::bootstrap::bootstrap_ascending>{ shared () }] (nano::transaction const & tx, nano::process_return const & result, nano::block const & block) {
		auto this_l = this_w.lock ();
		if (this_l == nullptr)
		{
			//std::cerr << boost::str (boost::format ("Missed block: %1%\n") % block.hash ().to_string ());
			return;
		}
		this_l->inspect (tx, result, block);
	});
	//for (auto i = 0; !stopped && i < 5'000; ++i)
	int counter = 0;
	while (!stopped)
	{
		request_one ();
		auto iterations = 10'000;
		if ((++counter % iterations) == 0)
		{
			dump_stats ();
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
