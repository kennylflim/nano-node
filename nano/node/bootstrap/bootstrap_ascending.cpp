#include <nano/node/bootstrap/bootstrap_ascending.hpp>
#include <nano/node/bootstrap/block_deserializer.hpp>
#include <nano/secure/common.hpp>

#include <nano/node/node.hpp>

#include <boost/format.hpp>

using namespace std::chrono_literals;


bool nano::bootstrap::bootstrap_ascending::backoff_counts::insert (nano::account const & account)
{
	static_assert (backoff_exclusion > 0);
	if (backoff_exclusion <= accounts.size ())
	{
		return true;
	}
	if (backoff_exclusion < attempts++ && accounts.count (account) > 0)
	{
		return true;
	}
	accounts[account] = backoff[account];
	return false;
}

void nano::bootstrap::bootstrap_ascending::backoff_counts::increase (nano::account const & account)
{
	auto existing = backoff [account];
	auto updated = existing + 1.0f;
	if (updated < existing)
	{
		updated = std::numeric_limits<decltype(updated)>::max ();
	}
	backoff[account] = updated;
}

nano::account nano::bootstrap::bootstrap_ascending::backoff_counts::operator() ()
{
	std::vector<decltype(backoff)::mapped_type> weights;
	std::vector<nano::account> candidates;
	decltype(weights)::value_type total{ 0 };
	for (auto const & [account, weight]: accounts)
	{
		total += weight;
		weights.push_back (weight);
		candidates.push_back (account);
	}
	for (auto i = weights.begin (), n = weights.end (); i != n; ++i)
	{
		*i = 1.0 / std::pow(2.0, total - *i);
	}
	{
		std::string message = "Considered weights: ";
		for (auto i: weights)
		{
			message += (std::to_string (i) + ' ');
		}
		message += '\n';
		//std::cerr << message;
	}
	std::discrete_distribution dist{ weights.begin (), weights.end () };
	auto selection = dist (random);
	debug_assert (!weights.empty () && selection < weights.size ());
	auto result = candidates[selection];
	std::string message = boost::str (boost::format ("selected index: %1% weight %2% %3%\n") % std::to_string (selection) % std::to_string (weights[selection]) % result.to_account ());
	//std::cerr << message;
	increase (result);
	accounts.clear ();
	return result;
}

void nano::bootstrap::bootstrap_ascending::backoff_counts::erase (nano::account const & account)
{
	backoff.erase (account);
}

bool nano::bootstrap::bootstrap_ascending::backoff_counts::empty () const
{
	return accounts.empty ();
}

void nano::bootstrap::bootstrap_ascending::backoff_counts::dump_backoff_hist ()
{
	std::vector<size_t> weight_counts;
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

nano::account nano::bootstrap::bootstrap_ascending::random_account_entry (nano::transaction const & tx, nano::account const & search)
{
	auto existing_account = node->store.account.begin (tx, search);
	if (existing_account == node->store.account.end ())
	{
		existing_account = node->store.account.begin (tx);
		debug_assert (existing_account != node->store.account.end ());
	}
	//std::cerr << boost::str (boost::format ("Found: %1%\n") % result.to_string ());
	return existing_account->first;
}

std::optional<nano::account> nano::bootstrap::bootstrap_ascending::random_pending_entry (nano::transaction const & tx, nano::account const & search)
{
	auto existing_pending = node->store.pending.begin (tx, nano::pending_key{ search, 0 });
	std::optional<nano::account> result;
	if (existing_pending != node->store.pending.end ())
	{
		result = existing_pending->first.key ();
	}
	return result;
}

std::optional<nano::account> nano::bootstrap::bootstrap_ascending::random_ledger_account (nano::transaction const & tx)
{
	nano::account search;
	nano::random_pool::generate_block (search.bytes.data (), search.bytes.size ());
	//std::cerr << boost::str (boost::format ("Search: %1% ") % search.to_string ());
	auto rand = nano::random_pool::generate_byte ();
	if (rand & 0x1)
	{
		//std::cerr << boost::str (boost::format ("account "));
		return random_account_entry (tx, search);
	}
	else
	{
		//std::cerr << boost::str (boost::format ("pending "));
		return random_pending_entry (tx, search);
	}
}

std::optional<nano::account> nano::bootstrap::bootstrap_ascending::pick_account ()
{
	std::lock_guard<nano::mutex> lock{ mutex };
	{
		if (!forwarding.empty ())
		{
			auto first = forwarding.begin ();
			auto account = *first;
			forwarding.erase (first);
			if (source_blocked.count (account) == 0)
			{
				return account;
			}
		}
	}
	auto tx = node->store.tx_begin_read ();
	auto iterations{ 0 };
	while (iterations < backoff_counts::backoff_exclusion * 2)
	{
		++source_iterations;
		auto account = random_ledger_account (tx);
		if (account)
		{
			if (source_blocked.count (*account) == 0)
			{
				if (backoff.insert (*account))
				{
					break;
				}
			}
		}
	}
	return backoff ();
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
	node->block_processor.processed.add ([this_w = std::weak_ptr<nano::bootstrap::bootstrap_ascending>{ shared () }] (nano::transaction const & tx, nano::process_return const & result, nano::block const & block) {
		auto this_l = this_w.lock ();
		if (this_l == nullptr)
		{
			return;
		}
		std::lock_guard<nano::mutex> lock{ this_l->mutex };
		switch (result.code)
		{
			case nano::process_result::progress:
			{
				auto account = this_l->node->ledger.account (tx, block.hash ());
				this_l->backoff.erase (account);
				this_l->source_blocked.erase (account);
				auto forward = [&] () {
					++this_l->forwarded;
					this_l->forwarding.insert (account);
					if (this_l->node->ledger.is_send (tx, block))
					{
						switch (block.type ())
						{
							case nano::block_type::send:
								this_l->forwarding.insert (block.destination ());
								break;
							case nano::block_type::state:
								if (forward_hint_enable)
								{
									this_l->forwarding.insert (block.link ().as_account ());
								}
								break;
							default:
								debug_assert (false);
								break;
						}
						
					}
				};
				if (forward_hint_enable)
				{
					forward ();
				}
				break;
			}
			case nano::process_result::gap_source:
			{
				auto account = block.previous ().is_zero () ? block.account () : this_l->node->ledger.account (tx, block.previous ());
				if (source_block_enable)
				{
					this_l->source_blocked.insert (account);
				}
				break;
			}
			case nano::process_result::gap_previous:
			{
			}
			default:
				break;
		}
	});
	//for (auto i = 0; !stopped && i < 5'000; ++i)
	int counter = 0;
	while (!stopped)
	{
		request_one ();
		auto iterations = 10'000;
		if ((++counter % iterations) == 0)
		{
			std::cerr << "Reporting\n";
			node->block_processor.flush ();
			node->block_processor.dump_result_hist ();
			{
				std::lock_guard<std::mutex> lock{ node->block_processor.hist_mutex };
				std::vector<int> hist;
				for (auto const &[hash, occurance]: node->block_processor.process_history)
				{
					if (hist.size () <= occurance)
					{
						hist.resize (occurance + 1);
					}
					++hist[occurance];
				}
				std::string message = boost::str (boost::format ("Process frequency hist(%1%): ") % hist.size ());
				auto iterations{ 0 };
				for (auto i: hist)
				{
					message += (std::to_string (i) + ' ');
				}
				message += '\n';
				std::cerr << message;
			}
			std::lock_guard<nano::mutex> lock{ mutex };
			backoff.dump_backoff_hist ();
			std::cerr << boost::str (boost::format ("Requests total: %1% forwarded: %2% source blocked: %3% source iterations: %4% responses: %5% response rate %6% satisfied %7%\n") % requests_total.load () % forwarded % source_blocked.size () % source_iterations.load () % responses.load () % (static_cast<double> (responses.load ()) / iterations) % node->unchecked.satisfied_total.load ());
			responses = source_iterations = 0;
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
