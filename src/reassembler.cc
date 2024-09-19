#include "reassembler.hh"
#include <algorithm>

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  Writer& bytes_writer = output_.writer();
  //unacceptable_index:Reassembler能够接受的索引上限
  //第一种情况：first_index > unacceptable_index
  //*****如果first_index + data.size() <= expecting_index_的情况呢？？？
  if ( const uint64_t unacceptable_index = expecting_index_ + bytes_writer.available_capacity();
       bytes_writer.is_closed() || bytes_writer.available_capacity() == 0 || first_index >= unacceptable_index )
    return; 

  //第二种情况：first_index < unacceptable_index
  //           同时 first_index + data.size() > unacceptable_index，那么保留[first_index,unacceptable_index)
  //******这里如果first_index + data.size() = unacceptable_index
  //那么data.resize( unacceptable_index - first_index )这一步不会改变data的长度，
  //但把is_last_substring置为false了？？？？？
  else if ( first_index + data.size() >= unacceptable_index ) {
    is_last_substring = false;                       
    data.resize( unacceptable_index - first_index ); 
  }

  if ( first_index > expecting_index_ )
    cache_bytes( first_index, move( data ), is_last_substring );
  else
    //这里是不是还要有去重操作？
    //***** 这里好像默认了first_index + data.size() > expecting_index_
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
  //left 是第一个结束位置大于或等于 first_index 的缓存片段。
  auto left = lower_bound( unordered_bytes_.begin(), end, first_index, []( auto&& e, uint64_t idx ) -> bool {
    return idx > ( get<0>( e ) + get<1>( e ).size() );
  } );

  //返回第一个起始位置大于 first_index + data.size() 的缓存片段的迭代器
  auto right = upper_bound( left, end, first_index + data.size(), []( uint64_t nxt_idx, auto&& e ) -> bool {
    return nxt_idx < get<0>( e );
  } ); 

  //处理与 left 片段的关系：检查新片段是否与 left 片段重叠或相邻，并据此调整新片段或跳过插入
  if ( const uint64_t next_index = first_index + data.size(); left != end ) {
    auto& [l_point, dat, _] = *left;
    if ( const uint64_t r_point = l_point + dat.size(); first_index >= l_point && next_index <= r_point )
      return; 
    else if ( next_index < l_point ) {
      right = left;                                                      
    } 
    else if ( !( first_index <= l_point && r_point <= next_index ) ) { 
      if ( first_index >= l_point ) {                                    
        data.insert( 0, string_view( dat.c_str(), dat.size() - ( r_point - first_index ) ) );
      } 
      else {
        data.resize( data.size() - ( next_index - l_point ) );
        data.append( dat ); 
      }
      first_index = min( first_index, l_point );
    }
  }

  //处理与 prev(right) 片段的关系：如果 right 不是 left（即存在至少一个片段在 left 和 right 之间），
  //则检查新片段是否与 prev(right) 片段相邻并可能重叠，并据此合并片段。
  if ( const uint64_t next_index = first_index + data.size(); right != left && !unordered_bytes_.empty() ) {
    auto& [l_point, dat, _] = *prev( right );
    if ( const uint64_t r_point = l_point + dat.size(); r_point > next_index ) {
      data.resize( data.size() - ( next_index - l_point ) );
      data.append( dat );
    }
  }

  //清理旧片段：删除 left 到 right（不包括 right）之间的所有旧片段，并更新相关计数器和标志。
  for ( ; left != right; left = unordered_bytes_.erase( left ) ) {
    num_bytes_pending_ -= get<1>( *left ).size();
    is_last_substring |= get<2>( *left );
  }
  
  //在清理后的位置插入新片段，包括其起始索引、数据和 is_last_substring 标志。
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
