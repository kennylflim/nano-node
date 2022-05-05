#include <nano/node/bootstrap/bootstrap_ascending.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

/**
 * Tests basic construction of a bootstrap_ascending attempt
 */
TEST (bootstrap_ascending, construction)
{
	nano::system system{ 1 };
	auto & node = *system.nodes[0];
	auto attempt = std::make_shared<nano::bootstrap::bootstrap_ascending> (node.shared (), 0, "");
}
