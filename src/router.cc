#include "router.hh"

#include <iostream>
#include <limits>
#include <ranges>

using namespace std;

void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

  router_table_.emplace( prefix_info( prefix_length, route_prefix ), make_pair( interface_num, next_hop ) );
}


void Router::route()
{
  //  ranges::for_each : 遍历 _interfaces 中的每个网络接口
  //  intfc : 当前遍历到的网络接口的引用
  ranges::for_each( _interfaces, [this]( auto& intfc ) {

    //  获取当前接口接收到的数据报队列的引用。
    auto& incoming_dgrams = intfc->datagrams_received();

    //  遍历所有网络接口，检查是否有新数据报，并处理新的数据报
    while ( !incoming_dgrams.empty() ) { 
      //  获取队列中的第一个数据报
      auto& dgram = incoming_dgrams.front();
      //  查找当前数据报对应的路由项
      const auto table_iter = find_export( dgram.header.dst );
      //  如果没找到路由项，或者该数据包的TTL <= 1，则应该丢弃该数据报
      if ( table_iter == router_table_.cend() || dgram.header.ttl <= 1 ) {
        incoming_dgrams.pop(); // TTL == 0 || (TTL - 1) == 0
        continue;              // 无法路由或 ttl 为 0 的数据报直接抛弃
      }
      --dgram.header.ttl;
      //  由于ttl修改了，需要调用 compute_checksum() 方法以重新计算数据报的校验和
      dgram.header.compute_checksum();
      //  解构 table_iter 指向的路由条目
      //  获取该数据报应该发送到的网络接口编号interface_num、下一跳地址network_addr
      const auto& [interface_num, network_addr] = table_iter->second;

      interface( interface_num )
        ->send_datagram(
          dgram,
          network_addr.has_value()
            ? *network_addr
            : Address::from_ipv4_numeric( dgram.header.dst ) ); // 没有下一跳时，表示位于同一个网络中，直接交付
      incoming_dgrams.pop();                                    // 已转发数据报，从缓冲队列中弹出
    }
  } );
}

Router::routerT::const_iterator Router::find_export( const uint32_t target_dst ) const
{
  //  遍历 router_table_ 中的所有条目，对每个条目进行 target_dst & mask_ == netID_？
  //  如果当前item 使得 lambda 表达式 为 true，则返回指向该item的迭代器
  //  如果没找到匹配的，则返回router_table_.end()
  return ranges::find_if( router_table_, [&target_dst]( const auto& item ) -> bool {
    return ( target_dst & item.first.mask_ ) == item.first.netID_;
  } );
}
