#include <nano/node/bootstrap/bootstrap_ascending.hpp>
#include <nano/node/bootstrap/block_deserializer.hpp>
#include <nano/secure/common.hpp>

#include <nano/node/node.hpp>

using namespace std::chrono_literals;

void nano::bootstrap::bootstrap_ascending::push_back (nano::account const & account)
{
	std::lock_guard<nano::mutex> lock{ mutex };
	accounts.emplace_back (std::make_shared<request> (shared (), account));
	condition.notify_all ();
}

auto nano::bootstrap::bootstrap_ascending::pop_front () -> std::shared_ptr<request>
{
	std::lock_guard<nano::mutex> lock{ mutex };
	auto result = accounts.front ();
	std::cerr << boost::str (boost::format ("Popping: %1%\n") % result->account ().to_account ());
	accounts.pop_front ();
	condition.notify_all ();
	return result;
}

bool nano::bootstrap::bootstrap_ascending::wait_empty_requests ()
{
	std::unique_lock<nano::mutex> lock{ mutex };
	condition.wait (lock, [this] () { return stopped || requests == 0; });
	return stopped;
}

bool nano::bootstrap::bootstrap_ascending::wait_available_queue ()
{
	std::unique_lock<nano::mutex> lock{ mutex };
	condition.wait (lock, [this] () { return stopped || !accounts.empty (); });
	return stopped;
}

bool nano::bootstrap::bootstrap_ascending::wait_available_request ()
{
	std::unique_lock<nano::mutex> lock{ mutex };
	condition.wait (lock, [this] () { return stopped ||  requests < 1; } );
	return stopped;
}

void nano::bootstrap::bootstrap_ascending::clear_queue ()
{
	debug_assert (stopped);
	std::lock_guard<nano::mutex> lock{ mutex };
	accounts.clear ();
	condition.notify_all ();
}

void nano::bootstrap::bootstrap_ascending::notify ()
{
	std::lock_guard<nano::mutex> lock{ mutex };
	condition.notify_all ();
}

nano::bootstrap::bootstrap_ascending::bootstrap_ascending (std::shared_ptr<nano::node> const & node_a, uint64_t incremental_id_a, std::string id_a) :
	bootstrap_attempt{ node_a, nano::bootstrap_mode::ascending, incremental_id_a, id_a },
	producer{ *this },
	consumer{ *this }
{
	std::cerr << '\0';
}

bool nano::bootstrap::bootstrap_ascending::producer::filtered_pass (uint32_t filter)
{
	std::cerr << boost::str (boost::format ("filter: %1% on %2%\n") % std::to_string (filter) % bootstrap.node->network.port);
	int skipped = 0, used = 0;
	p = a = 0;
	bool end = true;
	while (!bootstrap.stopped && !load_next (bootstrap.node->store.tx_begin_read ()))
	{
		if (bootstrap.misses[next]++ < filter)
		{
			++used;
			end = false;
			queue_next ();
		}
		else
		{
			++skipped;
		}
	}
	std::cerr << "s: " << std::to_string (skipped) << " u: " << std::to_string (used) << std::endl;
	return end;
}

void nano::bootstrap::bootstrap_ascending::dump_miss_histogram ()
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
}

bool nano::bootstrap::bootstrap_ascending::producer_throttled_pass ()
{
	/*for (auto i = 1; !stopped && i < 64; i <<= 1)
	{
		if (!producer_filtered_pass (i))
		{
			return false;
		}
		//dump_miss_histogram ();
	}*/
}

bool nano::bootstrap::bootstrap_ascending::producer::pass ()
{
	bootstrap.dirty = true;
	while (!bootstrap.stopped && bootstrap.dirty)
	{
		bootstrap.dirty = false;
		//producer_throttled_pass ();
		filtered_pass (std::numeric_limits<uint32_t>::max ());
		bootstrap.wait_empty_requests ();
		//std::cerr << "flushing\n";
		if (!bootstrap.stopped)
		{
			bootstrap.node->block_processor.flush ();
		}
		std::cerr << boost::str (boost::format ("dirty: %1%\n") % std::to_string (bootstrap.dirty));
	}
	return true;
}

void nano::bootstrap::bootstrap_ascending::producer::queue_next ()
{
	std::cerr << boost::str (boost::format ("Queueing: %1%\n") % next.to_account ());
	bootstrap.push_back (next);
	bootstrap.condition.notify_all ();
}

void nano::bootstrap::bootstrap_ascending::producer::run ()
{
	auto done = false;
	while (!done)
	{
		done = pass ();
	}
	bootstrap.stop ();
	/*while (!stopped)
	{
		condition.wait (lock, [this] () { return stopped || (requests == 0 && queue.size () < queue_max); });
		if (!stopped)
		{
			lock.unlock ();
			auto done = false;
			while (!done)
			{
				done = load_any_next ();
				if (!done)
				{
					lock.lock ();
					lock.unlock ();
				}
				else
				{
					std::cerr << "producer end\n";
				}
			}
			lock.lock ();
		}
	}*/
}

void nano::bootstrap::bootstrap_ascending::consumer::run ()
{
	while (!bootstrap.stopped)
	{
		bootstrap.wait_available_request ();
		if (!bootstrap.stopped)
		{
			connect_request ();
		}
	}
	bootstrap.clear_queue ();
}

void nano::bootstrap::bootstrap_ascending::consumer::connect_request ()
{
	if (!bootstrap.sockets.empty ())
	{
		auto [socket, channel] = bootstrap.sockets.front ();
		bootstrap.sockets.pop_front ();
		request (socket, channel);
	}
	else
	{
		auto endpoint = bootstrap.node->network.bootstrap_peer (true);
		if (endpoint != nano::tcp_endpoint (boost::asio::ip::address_v6::any (), 0))
		{
			std::cerr << boost::str (boost::format ("connecting to: %1%\n") % endpoint);
			auto socket = std::make_shared<nano::client_socket> (*bootstrap.node);
			socket->async_connect (endpoint,
			[this_l = bootstrap.shared (), socket, endpoint] (boost::system::error_code const & ec) {
				if (ec)
				{
					std::cerr << boost::str (boost::format ("connect failed to: %1%\n") % endpoint);
					std::lock_guard<nano::mutex> lock{ this_l->mutex };
					return;
				}
				std::cerr << boost::str (boost::format ("connected to: %1%\n") % endpoint);
				this_l->consumer.request (socket, std::make_shared<nano::transport::channel_tcp> (*this_l->node, socket));
			});
		}
		else
		{
			std::cerr << "No endpoints\n";
			bootstrap.stop ();
		}
	}
}

void nano::bootstrap::bootstrap_ascending::consumer::request (std::shared_ptr<nano::socket> socket, std::shared_ptr<nano::transport::channel> channel)
{
	if (bootstrap.wait_available_queue ())
	{
		std::cerr << "Empty queue\n";
		return;
	}
	auto request = bootstrap.pop_front ();
	nano::hash_or_account start = request->account ();

	nano::account_info info;
	if (!bootstrap.node->store.account.get (bootstrap.node->store.tx_begin_read (), request->account (), info))
	{
		start = info.head;
	}
	//std::cerr << "requesting: " << account.to_account () << " at: " << start.to_string () <<  " from endpoint: " << socket->remote_endpoint() << std::endl;
	nano::bulk_pull message{ bootstrap.node->network_params.network };
	message.header.flag_set (nano::message_header::bulk_pull_ascending_flag);
	message.header.flag_set (nano::message_header::bulk_pull_count_present_flag);
	message.start = start;
	message.end = 0;
	message.count = cutoff;
	channel->send (message, [this_l = bootstrap.shared (), socket, channel, request] (boost::system::error_code const &, std::size_t) {
		//std::cerr << "callback\n";
		// Initiate reading blocks
		this_l->consumer.read_block (socket, channel, request);
	});
}

bool nano::bootstrap::bootstrap_ascending::producer::load_next (nano::transaction const & tx)
{
	next = next.number () + 1;
	std::cerr << boost::str (boost::format ("Searching for: %1%\n") % next.to_account ());
	auto existing_account = bootstrap.node->store.account.begin (tx, next);
	auto existing_pending = bootstrap.node->store.pending.begin (tx, nano::pending_key{ next, 0 });
	if (existing_account == bootstrap.node->store.account.end () && existing_pending == bootstrap.node->store.pending.end ())
	{
		std::cerr << boost::str (boost::format ("Pass end: a: %1% p: %2%\n") % a.load () % p.load ());
		return true;
	}
	if (existing_account == bootstrap.node->store.account.end () || (existing_pending != bootstrap.node->store.pending.end () && existing_pending->first.key () < existing_account->first))
	{
		debug_assert (existing_pending != bootstrap.node->store.pending.end ());
		++p;
		next = existing_pending->first.key ();
		std::cerr << boost::str (boost::format ("Next from pending: %1%\n") % next.to_string ());
		return false;
	}
	else
	{
		debug_assert (existing_account != bootstrap.node->store.account.end ());
		++a;
		next = existing_account->first;
		std::cerr << boost::str (boost::format ("Next from account: %1%\n") % next.to_account ());
		return false;
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
		//debug_assert (this_l->misses.count (account) > 0);
		//this_l->misses[account] >>= 1;
		//this_l->queue.push_back (account);
		this_l->condition.notify_all ();
		//std::cerr << "marking\n";
		this_l->dirty = true;
	});
	std::thread producer_thread{ [this] () {
		producer.run ();
		std::cerr << "Exiting producer\n";
	} };
	std::thread consumer_thread{ [this] () {
		consumer.run ();
		std::cerr << "Exiting consumer\n";
	}};
	producer_thread.join ();
	consumer_thread.join ();
	
	std::cerr << "!! stopping" << std::endl;
}

void nano::bootstrap::bootstrap_ascending::consumer::read_block (std::shared_ptr<nano::socket> socket, std::shared_ptr<nano::transport::channel> channel, std::shared_ptr<bootstrap_ascending::request> request)
{
	auto deserializer = std::make_shared<nano::bootstrap::block_deserializer>();
	deserializer->read (*socket, [this_l = bootstrap.shared (), socket, channel, request] (boost::system::error_code ec, std::shared_ptr<nano::block> block) {
		if (block == nullptr)
		{
			std::cerr << "stream end\n";
			std::lock_guard<nano::mutex> lock{ this_l->mutex };
			this_l->sockets.push_back (std::make_pair (socket, channel));
			this_l->condition.notify_all ();
			return;
		}
		std::cerr << boost::str (boost::format ("block: %1%\n") % block->hash ().to_string ());
		this_l->node->block_processor.add (block);
		this_l->consumer.read_block (socket, channel, request);
		++this_l->blocks;
	} );
}

void nano::bootstrap::bootstrap_ascending::get_information (boost::property_tree::ptree &)
{
}

std::shared_ptr<nano::bootstrap::bootstrap_ascending> nano::bootstrap::bootstrap_ascending::shared ()
{
	return std::static_pointer_cast<nano::bootstrap::bootstrap_ascending> (shared_from_this ());
}
