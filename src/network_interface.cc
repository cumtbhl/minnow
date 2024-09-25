#include <iostream>

#include "exception.hh"
#include "network_interface.hh"

using namespace std;

NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  //  cerr 是 C++ 中用于输出错误信息的流，只有进入调试模式下，才能在终端看到这些信息，方便我们调试。
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address ) << " and IP address "
       << ip_address.ip() << "\n";
}

//  dgram : NetworkInterface要发送的数据报
//  next_hop : 下一跳的ip地址
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  //  将 next_hop 转换为其 IPv4 数字格式，以便后续查找和处理。
  const uint32_t target_ip = next_hop.ipv4_numeric();

  //  在mapping_table_中查找目标 IP 地址。
  //  如果找到了，iter 将指向该 IP 地址的映射
  //  如果没有找到，则 iter 为 mapping_table_.end()。
  auto iter = mapping_table_.find( target_ip );

  //  目标 IP 不在 mapping_table_ 中 : 不知道目标ip对应的mac地址
  if ( iter == mapping_table_.end() ) {
    //  ①将数据报存入等待发送的队列
    dgrams_waiting_addr_.emplace( target_ip, dgram );

    //  如果5s内NetworkInterface没有发送过 目标ip的ARP请求,
    //  则构造并发送一个 ARP 请求帧，询问目标 IP 地址对应的以太网地址，同时在 arp_recorder_ 中记录该请求的时间。
    if ( arp_recorder_.find( target_ip ) == arp_recorder_.end() ) {
      transmit( make_frame( EthernetHeader::TYPE_ARP,
                            serialize( make_arp_message( ARPMessage::OPCODE_REQUEST, target_ip ) ) ) );
      arp_recorder_.emplace( target_ip, 0 );
    }
  }

  //  目标 IP 在 mapping_table_ 中 : 知道目标ip对应的mac地址
  //  直接封装dgram 成 帧(加入EthernetHeader)，并将其发送到目的地
  else
    transmit( make_frame( EthernetHeader::TYPE_IPv4, serialize( dgram ), iter->second.get_ether() ) );
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  // Your code here.
  if ( frame.header.dst != ETHERNET_BROADCAST && frame.header.dst != ethernet_address_ )
    return; // 丢弃目的地址既不是广播地址也不是本接口的数据帧

  switch ( frame.header.type ) {
    case EthernetHeader::TYPE_IPv4: {
      InternetDatagram ip_dgram;
      if ( !parse( ip_dgram, frame.payload ) )
        return;
      // 解析后推入 datagrams_received_
      datagrams_received_.emplace( move( ip_dgram ) );
    } break;

    case EthernetHeader::TYPE_ARP: {
      ARPMessage arp_msg;
      if ( !parse( arp_msg, frame.payload ) )
        return;
      mapping_table_.insert_or_assign( arp_msg.sender_ip_address,
                                       address_mapping( arp_msg.sender_ethernet_address ) );
      switch ( arp_msg.opcode ) {
        case ARPMessage::OPCODE_REQUEST: {
          // 和当前接口的 IP 地址一致，发送 ARP 响应
          if ( arp_msg.target_ip_address == ip_address_.ipv4_numeric() )
            transmit( make_frame( EthernetHeader::TYPE_ARP,
                                  serialize( make_arp_message( ARPMessage::OPCODE_REPLY,
                                                               arp_msg.sender_ip_address,
                                                               arp_msg.sender_ethernet_address ) ),
                                  arp_msg.sender_ethernet_address ) );
        } break;

        case ARPMessage::OPCODE_REPLY: {
          // 遍历队列发出旧数据帧
          auto [head, tail] = dgrams_waiting_addr_.equal_range( arp_msg.sender_ip_address );
          for_each( head, tail, [this, &arp_msg]( auto&& iter ) -> void {
            transmit(
              make_frame( EthernetHeader::TYPE_IPv4, serialize( iter.second ), arp_msg.sender_ethernet_address ) );
          } );
          if ( head != tail )
            dgrams_waiting_addr_.erase( head, tail );
        } break;

        default:
          break;
      }
    } break;

    default:
      break;
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  constexpr size_t ms_mappings_ttl = 30'000, ms_resend_arp = 5'000;
  // 刷新数据表，删掉超时项
  auto flush_timer = [&ms_since_last_tick]( auto& datasheet, const size_t deadline ) -> void {
    for ( auto iter = datasheet.begin(); iter != datasheet.end(); ) {
      if ( ( iter->second += ms_since_last_tick ) > deadline )
        iter = datasheet.erase( iter );
      else
        ++iter;
    }
  };
  flush_timer( mapping_table_, ms_mappings_ttl );
  flush_timer( arp_recorder_, ms_resend_arp );
}

ARPMessage NetworkInterface::make_arp_message( const uint16_t option,
                                               const uint32_t target_ip,
                                               optional<EthernetAddress> target_ether ) const
{
  return { .opcode = option,
           .sender_ethernet_address = ethernet_address_,
           .sender_ip_address = ip_address_.ipv4_numeric(),
           .target_ethernet_address = target_ether.has_value() ? move( *target_ether ) : EthernetAddress {},
           .target_ip_address = target_ip };
}

EthernetFrame NetworkInterface::make_frame( const uint16_t protocol,
                                            std::vector<std::string> payload,
                                            optional<EthernetAddress> dst ) const
{
  return { .header { .dst = dst.has_value() ? move( *dst ) : ETHERNET_BROADCAST,
                     .src = ethernet_address_,
                     .type = protocol },
           .payload = move( payload ) };
  ;
}

NetworkInterface::address_mapping& NetworkInterface::address_mapping::tick( const size_t ms_time_passed ) noexcept
{
  timer_ += ms_time_passed;
  return *this;
}
