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
	std::unique_lock<nano::mutex> lock{ mutex };
	if (!sockets.empty ())
	{
		//std::cerr << "pooled\n";
		auto [socket, channel] = sockets.front ();
		sockets.pop_front ();
		lock.unlock ();
		++requests;
		request (socket, channel);
	}
	else
	{
		lock.unlock ();
		auto endpoint = node->network.bootstrap_peer (true);
		auto socket = std::make_shared<nano::client_socket> (*node);
		if (endpoint != nano::tcp_endpoint (boost::asio::ip::address_v6::any (), 0))
		{
			++requests;
			socket->async_connect (endpoint,
			[this_l = shared (), socket, endpoint] (boost::system::error_code const & ec) {
				if (ec)
				{
					--this_l->requests;
					return;
				}
				this_l->request (socket, std::make_shared<nano::transport::channel_tcp> (*this_l->node, socket));
			});
		}
		else
		{
			std::cerr << "No endpoints\n";
		}
	}
}

void nano::bootstrap::bootstrap_ascending::request (std::shared_ptr<nano::socket> socket, std::shared_ptr<nano::transport::channel> channel)
{
	nano::hash_or_account start = next;
	nano::account_info info;
	if (!node->store.account.get (node->store.tx_begin_read (), next, info))
	{
		start = info.head;
	}
	//std::cerr << "requesting: " << next.to_account () << " at: " << start.to_string () <<  " from endpoint: " << socket->remote_endpoint() << std::endl;
	nano::bulk_pull message{ node->network_params.network };
	message.header.flag_set (nano::message_header::bulk_pull_ascending_flag);
	message.header.flag_set (nano::message_header::bulk_pull_count_present_flag);
	message.start = start;
	message.end = 0;
	message.count = cutoff;
	channel->send (message, [this_l = shared (), socket, channel, node = node] (boost::system::error_code const &, std::size_t) {
		//std::cerr << "callback\n";
		// Initiate reading blocks
		this_l->read_block (socket, channel);
	});
}

bool nano::bootstrap::bootstrap_ascending::compute_next (uint32_t filter)
{
	auto done = false;
	bool end = false;
	while (!done)
	{
		next = next.number () + 1;
		end = load_next (node->store.tx_begin_read ());
		auto & miss_count = misses[next];
		auto pass = miss_count <= filter;
		if (pass)
		{
			++u;
		}
		else
		{
			++filtered;
			++s;
		}
		done = end || pass;
	}
	return end;
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
				++a;
				next = existing->first;
			}
			else
			{
				state = activity::pending;
				next = 1;
				std::cerr << " a: " << a.load () << std::endl;
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
				++p;
				next = existing->first.key ();
			}
			else
			{
				next = 0;
				std::cerr << " p: " << p.load () << std::endl;
				lock.unlock ();
				result = true;
			}
			break;
		}
	}
	return result;
}

static int pass_number = 0;

void nano::bootstrap::bootstrap_ascending::run ()
{
	std::cerr << "!! Starting with:" << std::to_string (pass_number++) << "\n";
	/*node->block_processor.inserted.add ([this_w = std::weak_ptr<nano::bootstrap::bootstrap_ascending>{ shared () }] (nano::transaction const & tx, nano::block const & block) {
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
	});*/
	auto dirty = true;
	uint32_t filter = 1;
	while (dirty)
	{
		while (dirty)
		{
			dirty = run_pass (1);
		}
		filtered = 0;
		dirty = run_pass (filter);
		filter = filtered == 0 ? 1 : filter << 1;
	}
	stop ();
	std::cerr << "!! stopping" << std::endl;
}

bool nano::bootstrap::bootstrap_ascending::run_pass (uint32_t filter)
{
	bool dirty = false;
	auto start = std::chrono::steady_clock::now ();
	auto start_count = node->ledger.cache.block_count.load ();
	std::cerr << "Filter: " << std::to_string (filter) << std::endl;
	fill_drain_queue (filter);
	std::cerr << " o: " << std::to_string (o) << " r: " << std::to_string (r) << " m: " << std::to_string (m) << " s: " << std::to_string (s) << " u: " << std::to_string (u) << std::endl;
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - start).count ();
	uint64_t delta = node->ledger.cache.block_count.load () - start_count;
	dirty = delta > 0;
	std::cerr << "time(ms): " << std::to_string (ms) << " delta: " << std::to_string (delta) << " bps: " << std::to_string ((static_cast<double>(delta) / ms) * 1000.0) << " miss rate: " << std::to_string (static_cast<double> (m) / r) << " dup: " << std::to_string (node->store.unchecked.dup) << std::endl;
	m = o = r = s = u = 0;
	return dirty;
}

bool nano::bootstrap::bootstrap_ascending::fill_drain_queue (uint32_t filter)
{
	bool done = false;
	state = activity::account;
	next = 0;
	while (!stopped && !done)
	{
		blocks = 0;
		done = compute_next (filter);
		if (!done)
		{
			++r;
			request ();
			if (node->block_processor.half_full ())
			{
				node->block_processor.flush ();
			}
			std::unique_lock<nano::mutex> lock{ mutex };
			condition.wait (lock, [this] () { return stopped || requests < 1; });
			auto overflow = blocks >= cutoff;
			if (overflow || blocks == 0)
			{
				++misses[next];
			}
			if (overflow)
			{
				++o;
				//requeue.insert (next);
			}
			if (blocks == 0)
			{
				++m;
			}
			else
			{
				misses[next] = 0;
			}
		}
	}
	debug_assert (requests == 0);
	a = p = 0;
	if (!stopped)
	{
		node->block_processor.flush ();
	}
}

void nano::bootstrap::bootstrap_ascending::read_block (std::shared_ptr<nano::socket> socket, std::shared_ptr<nano::transport::channel> channel)
{
	auto deserializer = std::make_shared<nano::bootstrap::block_deserializer>();
	deserializer->read (*socket, [this_l = shared (), socket, channel, node = node] (boost::system::error_code ec, std::shared_ptr<nano::block> block) {
		if (block == nullptr)
		{
			std::lock_guard<nano::mutex> lock{ this_l->mutex };
			this_l->sockets.push_back (std::make_pair (socket, channel));
			--this_l->requests;
			this_l->condition.notify_all ();
			return;
		}
		//std::cerr << "block: " << block->hash ().to_string () << std::endl;
		node->block_processor.add (block);
		this_l->read_block (socket, channel);
		++this_l->blocks;
	} );
}

void nano::bootstrap::bootstrap_ascending::get_information (boost::property_tree::ptree &)
{
}
