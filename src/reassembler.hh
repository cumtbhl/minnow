#pragma once

#include "byte_stream.hh"

#include <list>
#include <string>
#include <tuple>

class Reassembler
{
public:

  explicit Reassembler( ByteStream&& output ) : output_( std::move( output ) ) {}

  void insert( uint64_t first_index, std::string data, bool is_last_substring );

  uint64_t bytes_pending() const;

  Reader& reader() { return output_.reader(); }
  const Reader& reader() const { return output_.reader(); }

  const Writer& writer() const { return output_.writer(); }

private:
  void push_bytes( uint64_t first_index, std::string data, bool is_last_substring );
  void cache_bytes( uint64_t first_index, std::string data, bool is_last_substring );
  void flush_buffer(); // 刷新缓冲区，把能推入的数据推入流中

  std::list<std::tuple<uint64_t, std::string, bool>> unordered_bytes_ {}; // 一个有序的、无重复的缓冲区
  uint64_t num_bytes_pending_ {};                                         // 当前存储的字节数
  uint64_t expecting_index_ {};                                           // 表示期待下一个字节的序号
  ByteStream output_; 
};
