#include <nano/node/scheduler/limiter.hpp>
#include <nano/secure/common.hpp>
#include <nano/test_common/system.hpp>
#include <nano/test_common/testutil.hpp>

#include <gtest/gtest.h>

TEST (election_occupancy, construction)
{
	auto occupancy = std::make_shared<nano::scheduler::limiter> (nullptr, 0);
	ASSERT_EQ (0, occupancy->limit ());
	ASSERT_FALSE (occupancy->available ());
}

TEST (election_occupancy, limit)
{
	auto occupancy = std::make_shared<nano::scheduler::limiter> (nullptr, 1);
	ASSERT_EQ (1, occupancy->limit ());
	ASSERT_TRUE (occupancy->available ());
}

TEST (election_occupancy, activate)
{
	nano::test::system system{ 1 };
	auto occupancy = std::make_shared<nano::scheduler::limiter> ([&system] (auto const & block) {
		return system.nodes[0]->active.insert (block);
	},
	1);
	auto result = occupancy->activate (nano::dev::genesis);
	ASSERT_TRUE (result.inserted);
	auto elections = occupancy->elections ();
	ASSERT_EQ (1, elections.size ());
	ASSERT_EQ (1, elections.count (nano::dev::genesis->qualified_root ()));
	ASSERT_FALSE (occupancy->available ());
	result.election = nullptr; // Implicitly run election destructor notification by clearing the last reference
	ASSERT_TIMELY (5s, occupancy->available ());
}
