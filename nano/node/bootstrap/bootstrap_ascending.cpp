#include <nano/node/bootstrap/bootstrap_ascending.hpp>

nano::bootstrap::bootstrap_ascending::bootstrap_ascending (std::shared_ptr<nano::node> const & node_a, uint64_t incremental_id_a, std::string id_a) :
bootstrap_attempt{ node_a, nano::bootstrap_mode::ascending, incremental_id_a, id_a }
{
}

void nano::bootstrap::bootstrap_ascending::run ()
{

}

void nano::bootstrap::bootstrap_ascending::get_information (boost::property_tree::ptree &)
{

}
