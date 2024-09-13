#include "reassembler.hh"
#include <algorithm>

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  Writer& bytes_writer = output_.writer();
  //unacceptable_index:Reassembler能够接受的索引上限
  //第一种情况：first_index > unacceptable_index
  if ( const uint64_t unacceptable_index = expecting_index_ + bytes_writer.available_capacity();
       bytes_writer.is_closed() || bytes_writer.available_capacity() == 0 || first_index >= unacceptable_index )
    return; 

  //第二种情况：first_index < unacceptable_index
  //           同时 first_index + data.size() > unacceptable_index，那么保留[first_index,unacceptable_index)
  else if ( first_index + data.size() >= unacceptable_index ) {
    is_last_substring = false;                       
    data.resize( unacceptable_index - first_index ); 
  }

  if ( first_index > expecting_index_ )
    cache_bytes( first_index, move( data ), is_last_substring );
  else
    //这里是不是还要有去重操作？
    push_bytes( first_index, move( data ), is_last_substring );
  flush_buffer();
}

uint64_t Reassembler::bytes_pending() const
{
  return num_bytes_pending_;
}

void Reassembler::push_bytes( uint64_t first_index, string data, bool is_last_substring )
{
  if ( first_index < expecting_index_ ) // 部分重复的分组
    data.erase( 0, expecting_index_ - first_index );
  expecting_index_ += data.size();
  output_.writer().push( move( data ) );

  if ( is_last_substring ) {
    output_.writer().close(); // 关闭写端
    unordered_bytes_.clear(); // 清空缓冲区
    num_bytes_pending_ = 0;
  }
}

void Reassembler::cache_bytes( uint64_t first_index, string data, bool is_last_substring )
{
  auto end = unordered_bytes_.end();
  //lower_bound ： 在指定范围内找到第一个不满足指定比较条件的位置
  //left ： left的 get<0>(e) + get<1>(e).size() >= first_index
  auto left = lower_bound( unordered_bytes_.begin(), end, first_index, []( auto&& e, uint64_t idx ) -> bool {
    return idx > ( get<0>( e ) + get<1>( e ).size() );
  } );

  //std::upper_bound ：用于在指定范围内找到第一个不满足指定比较条件的位置。
  //right ： right的 get<0>(e) <= first_index
  auto right = upper_bound( left, end, first_index + data.size(), []( uint64_t nxt_idx, auto&& e ) -> bool {
    return nxt_idx < get<0>( e );
  } ); // 注意一下：right 指向的是待合并区间右端点的下一个元素

  if ( const uint64_t next_index = first_index + data.size(); left != end ) {
    //将 left 指向的 tuple 解构为 l_point（起始索引）、dat（数据内容）和第三个元素 _（此处不关心）。
    auto& [l_point, dat, _] = *left;
    //目标数据段 完全包含在数据段 [l_point, r_point)中，说明数据已经存在于已有段中，直接 return
    if ( const uint64_t r_point = l_point + dat.size(); first_index >= l_point && next_index <= r_point )
      return; 
    //
    else if ( next_index < l_point ) {
      right = left;                                                      // data 和 dat 没有重叠部分
    } else if ( !( first_index <= l_point && r_point <= next_index ) ) { // 重叠了
      if ( first_index >= l_point ) {                                    // 并且 dat 没有被完全覆盖
        data.insert( 0, string_view( dat.c_str(), dat.size() - ( r_point - first_index ) ) );
      } else {
        data.resize( data.size() - ( next_index - l_point ) );
        data.append( dat ); // data 在前
      }
      first_index = min( first_index, l_point );
    }
  }

  if ( const uint64_t next_index = first_index + data.size(); right != left && !unordered_bytes_.empty() ) {
    // 如果 right 指向 left，表示两种可能：没有重叠区间、或者只需要合并 left 这个元素
    auto& [l_point, dat, _] = *prev( right );
    if ( const uint64_t r_point = l_point + dat.size(); r_point > next_index ) {
      data.resize( data.size() - ( next_index - l_point ) );
      data.append( dat );
    }
  }

  for ( ; left != right; left = unordered_bytes_.erase( left ) ) {
    num_bytes_pending_ -= get<1>( *left ).size();
    is_last_substring |= get<2>( *left );
  }
  num_bytes_pending_ += data.size();
  unordered_bytes_.insert( left, { first_index, move( data ), is_last_substring } );
}

void Reassembler::flush_buffer()
{
  while ( !unordered_bytes_.empty() ) {
    auto& [idx, dat, last] = unordered_bytes_.front();
    if ( idx > expecting_index_ )
      break;                          // 乱序的，不做任何动作
    num_bytes_pending_ -= dat.size(); // 数据已经被填补上了，立即推入写端
    push_bytes( idx, move( dat ), last );
    if ( !unordered_bytes_.empty() )
      unordered_bytes_.pop_front();
  }
}
