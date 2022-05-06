#pragma once

#include <nano/lib/blocks.hpp>

#include <boost/system/error_code.hpp>

#include <memory>
#include <vector>

namespace nano
{
class block;
class socket;
namespace bootstrap
{
class block_deserializer : public std::enable_shared_from_this<nano::bootstrap::block_deserializer>
{
public:
	using callback_type = std::function<void(std::shared_ptr<nano::block>)>;

	block_deserializer ();
	void read (std::shared_ptr<nano::socket> socket, callback_type callback);
	void reset ();
	std::shared_ptr<nano::block> block;
	boost::system::error_code ec;
private:
	void received_type (callback_type callback);
	void received_block (nano::block_type type, callback_type callback);
	size_t block_size (nano::block_type type);
	std::shared_ptr<nano::socket> socket;
	std::shared_ptr<std::vector<uint8_t>> read_buffer;
};
}
}
