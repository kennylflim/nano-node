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

bool nano::bootstrap::bootstrap_ascending::producer_filtered_pass (uint32_t filter)
{
	int skipped = 0, used = 0;
	bool nothing_found = true;
	while (!stopped && !load_next (node->store.tx_begin_read ()))
	{
		if (misses[next]++ < filter)
		{
			++used;
			nothing_found = false;
			queue_next ();
		}
		else
		{
			++skipped;
		}
		next = next.number () + 1;
	}
	std::cerr << "s: " << std::to_string (skipped) << " u: " << std::to_string (used) << std::endl;
	return nothing_found;
}

bool nano::bootstrap::bootstrap_ascending::producer_pass ()
{
	return producer_filtered_pass (std::numeric_limits<uint32_t>::max ());
}

void nano::bootstrap::bootstrap_ascending::queue_next ()
{
	std::unique_lock<nano::mutex> lock{ mutex };
	queue.push_back (next);
	condition.notify_all ();
}

void nano::bootstrap::bootstrap_ascending::producer_loop ()
{
	auto done = false;
	while (!done)
	{
		done = producer_pass ();
		nano::unique_lock<nano::mutex> lock{ mutex };
		condition.wait (lock, [this] () { return stopped || (queue.empty () && requests == 0); });
	}
	stop ();
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

void nano::bootstrap::bootstrap_ascending::consumer_loop ()
{
	while (!stopped)
	{
		std::unique_lock<nano::mutex> lock{ mutex };
		condition.wait (lock, [this] () { return stopped || (requests < requests_max && !queue.empty ()); });
		if (!stopped)
		{
			lock.unlock ();
			connect_request ();
			lock.lock ();
		}
	}
}

void nano::bootstrap::bootstrap_ascending::connect_request ()
{
	++requests;
	if (!sockets.empty ())
	{
		auto [socket, channel] = sockets.front ();
		sockets.pop_front ();
		request (socket, channel);
	}
	else
	{
		auto endpoint = node->network.bootstrap_peer (true);
		if (endpoint != nano::tcp_endpoint (boost::asio::ip::address_v6::any (), 0))
		{
			auto socket = std::make_shared<nano::client_socket> (*node);
			socket->async_connect (endpoint,
			[this_l = shared (), socket, endpoint] (boost::system::error_code const & ec) {
				if (ec)
				{
					--this_l->requests;
					this_l->condition.notify_all ();
					return;
				}
				this_l->request (socket, std::make_shared<nano::transport::channel_tcp> (*this_l->node, socket));
			});
		}
		else
		{
			std::cerr << "No endpoints\n";
			--requests;
			stop ();
		}
	}
}

void nano::bootstrap::bootstrap_ascending::request (std::shared_ptr<nano::socket> socket, std::shared_ptr<nano::transport::channel> channel)
{
	nano::account account;
	{
		std::lock_guard<nano::mutex> lock{ mutex };
		debug_assert (!queue.empty ());
		account = queue.front ();
		queue.pop_front ();
	}
	nano::hash_or_account start = account;
	nano::account_info info;
	if (!node->store.account.get (node->store.tx_begin_read (), account, info))
	{
		start = info.head;
	}
	//std::cerr << "requesting: " << account.to_account () << " at: " << start.to_string () <<  " from endpoint: " << socket->remote_endpoint() << std::endl;
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

bool nano::bootstrap::bootstrap_ascending::load_next (nano::transaction const & tx)
{
	bool result = false;
	switch (state)
	{
		case activity::account:
		{
			auto existing = node->store.account.begin (tx, next);
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
				result = load_next (tx);
			}
			break;
		}
		case activity::pending:
		{
			auto existing = node->store.pending.begin (tx, nano::pending_key{ next, 0 });
			if (existing != node->store.pending.end ())
			{
				++p;
				next = existing->first.key ();
			}
			else
			{
				state = activity::account;
				next = 0;
				std::cerr << " p: " << p.load () << std::endl;
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
	node->block_processor.inserted.add ([this_w = std::weak_ptr<nano::bootstrap::bootstrap_ascending>{ shared () }] (nano::transaction const & tx, nano::block const & block) {
		/*auto this_l = this_w.lock ();
		if (this_l == nullptr)
		{
			return;
		}
		auto account = this_l->node->ledger.account (tx, block.hash ());
		debug_assert (this_l->misses.count (account) > 0);
		this_l->misses[account] >>= 1;*/
	});
	std::thread producer{ [this] () {
		producer_loop ();
		std::cerr << "Exiting producer\n";
	} };
	std::thread consumer{ [this] () {
		consumer_loop ();
		std::cerr << "Exiting consumer\n";
	}};
	producer.join ();
	consumer.join ();
	
	std::cerr << "!! stopping" << std::endl;
}

void nano::bootstrap::bootstrap_ascending::read_block (std::shared_ptr<nano::socket> socket, std::shared_ptr<nano::transport::channel> channel)
{
	auto deserializer = std::make_shared<nano::bootstrap::block_deserializer>();
	deserializer->read (*socket, [this_l = shared (), socket, channel, node = node] (boost::system::error_code ec, std::shared_ptr<nano::block> block) {
		if (block == nullptr)
		{
			//std::cerr << "stream end\n";
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

std::shared_ptr<nano::bootstrap::bootstrap_ascending> nano::bootstrap::bootstrap_ascending::shared ()
{
	return std::static_pointer_cast<nano::bootstrap::bootstrap_ascending> (shared_from_this ());
}
