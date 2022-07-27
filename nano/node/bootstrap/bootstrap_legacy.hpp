#include <nano/node/bootstrap/bootstrap_ascending.hpp>

namespace nano
{
	struct bootstrap_attempt_legacy : nano::bootstrap::bootstrap_ascending
	{
		using nano::bootstrap::bootstrap_ascending::bootstrap_ascending;
	};
}
