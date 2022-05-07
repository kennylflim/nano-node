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
		using callback_type = std::function<void (boost::system::error_code, std::shared_ptr<nano::block>)>;

		block_deserializer ();
		/**
		 * Read a type-prefixed block from 'socket' and pass the result, or an error, to 'callback'
		 */
		void read (nano::socket & socket, callback_type const && callback);

	private:
		void received_type (nano::socket & socket, callback_type const && callback);
		void received_block (nano::block_type type, callback_type const && callback);
		size_t block_size (nano::block_type type);
		std::shared_ptr<std::vector<uint8_t>> read_buffer;
	};
}
}
