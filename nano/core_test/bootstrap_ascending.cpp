#include <nano/node/bootstrap/bootstrap_ascending.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

/**
 * Tests basic construction of a bootstrap_ascending attempt
 */
TEST (bootstrap_ascending, construction)
{
	nano::system system{ 1 };
	auto & node = *system.nodes[0];
	auto attempt = std::make_shared<nano::bootstrap::bootstrap_ascending> (node.shared (), 0, "");
}

/**
 * Tests that bootstrap_ascending attempt can run and complete
 */
TEST (bootstrap_ascending, start_stop)
{
	nano::system system{ 1 };
	auto & node = *system.nodes[0];
	auto attempt = node.bootstrap_initiator.bootstrap_ascending ();
	ASSERT_TIMELY (5s, node.stats.count (nano::stat::type::bootstrap, nano::stat::detail::initiate_ascending, nano::stat::dir::out) > 0);
}

/**
 * Tests that bootstrap_ascending will return the genesis (only) block
 */
TEST (bootstrap_ascending, genesis)
{
	nano::node_flags flags;
	nano::system system{ 1, nano::transport::transport_type::tcp, flags };
	auto & node0 = *system.nodes[0];
	nano::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (nano::dev::genesis_key.pub)
				 .previous (nano::dev::genesis->hash ())
				 .representative (nano::dev::genesis_key.pub)
				 .link (0)
				 .balance (nano::dev::constants.genesis_amount - 1)
				 .sign (nano::dev::genesis_key.prv, nano::dev::genesis_key.pub)
				 .work (*system.work.generate (nano::dev::genesis->hash ()))
				 .build_shared ();
	ASSERT_EQ (nano::process_result::progress, node0.process (*send1).code);
	auto & node1 = *system.add_node (flags);
	ASSERT_TIMELY (5s, node1.block (send1->hash ()) != nullptr);
}

