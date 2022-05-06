#include <nano/node/bootstrap/block_deserializer.hpp>

#include <nano/node/socket.hpp>
#include <nano/secure/buffer.hpp>

#include <nano/lib/blocks.hpp>

nano::bootstrap::block_deserializer::block_deserializer () :
	read_buffer{ std::make_shared<std::vector<uint8_t>> ()}
{

}

void nano::bootstrap::block_deserializer::read (std::shared_ptr<nano::socket> socket)
{
	reset ();
	this->socket = socket;
	read_buffer->resize (1);
	socket->async_read (read_buffer, 1, [this_l = shared_from_this ()] (boost::system::error_code const & ec, std::size_t size_a) {
		if (ec || size_a != 1)
		{
			this_l->ec = ec;
			return;
		}
		this_l->received_type ();
	});
}

void nano::bootstrap::block_deserializer::received_type ()
{
	nano::block_type type = static_cast<nano::block_type> (read_buffer->data ()[0]);
	if (type == nano::block_type::not_a_block)
	{
		return;
	}
	auto size = block_size (type);
	if (size == 0)
	{
		return;
	}
	read_buffer->resize (size);
	socket->async_read (read_buffer, size, [this_l = shared_from_this (), size, type] (boost::system::error_code const & ec, std::size_t size_a) {
		if (ec || size_a != size)
		{
			this_l->ec = ec;
			return;
		}
		this_l->received_block (type);
	});
}

void nano::bootstrap::block_deserializer::received_block (nano::block_type type)
{
	nano::bufferstream stream{ read_buffer->data (), read_buffer->size () };
	block = nano::deserialize_block (stream, type);
}

size_t nano::bootstrap::block_deserializer::block_size (nano::block_type type)
{
	switch (type)
	{
		case nano::block_type::send:
			return nano::send_block::size;
		case nano::block_type::receive:
			return nano::receive_block::size;
		case nano::block_type::change:
			return nano::change_block::size;
		case nano::block_type::open:
			return nano::open_block::size;
		case nano::block_type::state:
			return nano::state_block::size;
		default:
			return 0;
	}
}

void nano::bootstrap::block_deserializer::reset ()
{
	block.reset ();
}
