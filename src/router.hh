#pragma once

#include <map>
#include <memory>
#include <optional>
#include <compare>

#include "exception.hh"
#include "network_interface.hh"

class Router
{
public:

  //  添加interface到_interfaces中，返回该interface在_interfaces中的索引
  size_t add_interface( std::shared_ptr<NetworkInterface> interface )
  {
    _interfaces.push_back( notnull( "add_interface", std::move( interface ) ) );
    return _interfaces.size() - 1;
  }

  //  获取 _interfaces中索引为 N 的网络接口，返回指向它的shared_ptr
  std::shared_ptr<NetworkInterface> interface( const size_t N ) { return _interfaces.at( N ); }

  //  构建一个新的路由项，添加到route_table_中
  //  route_prefix : 本质就是一个ipv4地址
  //  prefix_length ： 子网掩码1的位数
  //  next_hop : 接口编号对应的 Address
  //  interface_num : 接口编号
  void add_route( uint32_t route_prefix,
                  uint8_t prefix_length,
                  std::optional<Address> next_hop,
                  size_t interface_num );

  //  处理router所有网络接口接收到的 InternetDatagram，
  //  能够与router_table_匹配的就通过相应接口转发
  //  不能匹配的就丢弃
  void route();

private:
  struct prefix_info
  {
    //  子网掩码
    uint32_t mask_; 

    //  IPv4地址中的网络部分
    uint32_t netID_;

    //  prefix_length : 前缀长度(前多少位是 1 )
    //  prefix : 前缀(一个完整的ipv4地址)
    //  构造函数 : 传入1个ipv4地址 prefix 和 前缀长度 prefix_length，计算出mask_ 和 netID_
    explicit prefix_info( const uint8_t prefix_length, const uint32_t prefix )
      : mask_ { ~( UINT32_MAX >> ( prefix_length ) ) }, netID_ { prefix & mask_ } // 只记录网络号
    {}

    //  先比较子网掩码mask_和后比较网络netID_降序排列
    auto operator<=>( const prefix_info& other ) const
    {
      return other.mask_ != mask_ ? 
               mask_ <=> other.mask_
                                  : netID_ <=> other.netID_;
    }
  };

  //  路由表的数据结构类型
  using routerT = std::multimap<prefix_info, std::pair<size_t, std::optional<Address>>, std::greater<prefix_info>>;
  
  //  返回router_table_中与target_dst匹配 的路由项
  routerT::const_iterator find_export( const uint32_t target_dst ) const;

  //  路由表，（按子网掩码长度）降序排序所有路由条目
  routerT router_table_ {};

  //  存储一组指向NetworkInterface的shared_ptr
  std::vector<std::shared_ptr<NetworkInterface>> _interfaces {};
};
