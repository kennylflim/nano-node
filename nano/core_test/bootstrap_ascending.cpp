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

