#pragma once

#include <nano/node/bootstrap/bootstrap_attempt.hpp>

namespace nano
{
namespace bootstrap
{
class bootstrap_ascending : public nano::bootstrap_attempt
{
public:
	explicit bootstrap_ascending (std::shared_ptr<nano::node> const & node_a, uint64_t incremental_id_a, std::string id_a);
	void run () override;
	void get_information (boost::property_tree::ptree &) override;
};
}
}
