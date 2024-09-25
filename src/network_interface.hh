#pragma once

#include <compare>
#include <optional>
#include <queue>
#include <unordered_map>

#include "address.hh"
#include "ethernet_frame.hh"
#include "ipv4_datagram.hh"
#include "arp_message.hh" // 注意这里的头文件依赖关系被我修改过

class NetworkInterface
{
public:
  //  NetworkInterface发送帧时，使用的transmit()函数所在的类
  class OutputPort
  {
  public:
    virtual void transmit( const NetworkInterface& sender, const EthernetFrame& frame ) = 0;
    virtual ~OutputPort() = default;
  };

  //  NetworkInterface的构造函数
  NetworkInterface( std::string_view name,  
                    std::shared_ptr<OutputPort> port,
                    const EthernetAddress& ethernet_address,
                    const Address& ip_address );

 
  //  解析ip地址 next_hop 为 mac地址，将dgram 发送到 mac地址
  void send_datagram( const InternetDatagram& dgram, const Address& next_hop );

  void recv_frame( const EthernetFrame& frame );

  //  清理mapping_table_ 和 arp_recorder_中超时的映射
  void tick( size_t ms_since_last_tick );

  const std::string& name() const { return name_; }
  const OutputPort& output() const { return *port_; }
  OutputPort& output() { return *port_; }
  std::queue<InternetDatagram>& datagrams_received() { return datagrams_received_; }

private:
  ARPMessage make_arp_message( const uint16_t option,
                               const uint32_t target_ip,
                               std::optional<EthernetAddress> target_ether = std::nullopt ) const;
  EthernetFrame make_frame( const uint16_t protocol,
                            std::vector<std::string> payload,
                            std::optional<EthernetAddress> dst = std::nullopt ) const;
  
  //  管理 IP 地址与以 mac地址 之间的映射关系
  class address_mapping
  {
    EthernetAddress ether_addr_;
    size_t timer_;

  public:
    explicit address_mapping( EthernetAddress ether_addr ) : ether_addr_ { move( ether_addr ) }, timer_ {} {}
    EthernetAddress get_ether() const noexcept { return ether_addr_; }

    //  重载 += 运算符（新的 += 运算符本质：调用tick()函数），返回当前对象
    address_mapping& operator+=( const size_t ms_time_passed ) noexcept { return tick( ms_time_passed ); }

    //  重载 <=> 运算符（新的 <=> 运算符本质：比较当前对象的timer_与 deadline），返回比较值
    //  太空船运算符 (<=>) 是 C++20 中引入的特性。如果编译器不支持 C++20，则会报错。
    auto operator<=>( const size_t deadline ) const { return timer_ <=> deadline; }

    //  更新当前对象的 timer_ 值，并返回当前对象
    address_mapping& tick( const size_t ms_time_passed ) noexcept;
  };

  //  NetworkInterface的名称
  std::string name_;

  /*  port_本质是指针，指向OutputPort类型的对象
      NetworkInterface 调用 port_ 指向的 OutputPort 对象的 transmit 函数，实现数据帧的发送
  */
  std::shared_ptr<OutputPort> port_;

  //  本质就是调用 port_ 指向的 OutputPort 对象的 transmit 函数，实现数据帧的发送
  void transmit( const EthernetFrame& frame ) const { port_->transmit( *this, frame ); }

  //  NetworkInterface的mac地址
  EthernetAddress ethernet_address_;

  //  NetworkInterface的ip地址
  Address ip_address_;

  //  存储接收到的 IPv4 数据报 的缓冲区，使得后续可以集中处理这些数据报
  std::queue<InternetDatagram> datagrams_received_ {};

  //  以太网到 IP 地址的地址映射表
  std::unordered_map<uint32_t, address_mapping> mapping_table_ {};

  // 标记某个 IP 地址解析请求是否在 5 秒内发出过，`value` 为计时器
  std::unordered_map<uint32_t, size_t> arp_recorder_ {};

  // 正在等待 ARP 响应的数据报，`key` 为目的 IP 地址
  std::unordered_multimap<uint32_t, InternetDatagram> dgrams_waiting_addr_ {};
};
