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

void NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  //  如果收到的 EthernetFrame 既不是 ETHERNET_BROADCAST 又不是发往自己的，则丢弃该EthernetFrame
  if ( frame.header.dst != ETHERNET_BROADCAST && frame.header.dst != ethernet_address_ )
    return; 

  //
  switch ( frame.header.type ) {

    //  如果frame的类型是IPv4
    case EthernetHeader::TYPE_IPv4: {
      InternetDatagram ip_dgram;
      //  parse() : 将frame.payload解析成InternetDatagram，解析的结果放入ip_dgram
      //  如果解析失败，直接返回，完成对此次dgram的接收
      if ( !parse( ip_dgram, frame.payload ) )
        return;

      //  如果解析成功，将ip_dgram放入datagram_received中
      datagrams_received_.emplace( move( ip_dgram ) );
    } break;

    //  如果frame的类型是ARP
    case EthernetHeader::TYPE_ARP: {
      ARPMessage arp_msg;
      //  parse() : 将frame.payload解析成ARPMessage，解析的结果放入arp_msg
      //  如果解析失败，直接返回，完成对此次dgram的接收
      if ( !parse( arp_msg, frame.payload ) )
        return;
      
      //  将发送者的 IP 地址和 MAC 地址映射关系存储进 mapping_table_
      //  当下次需要向这个发送者发送数据时，就无需再次发送 ARP 请求
      mapping_table_.insert_or_assign( arp_msg.sender_ip_address,
                                       address_mapping( arp_msg.sender_ethernet_address ) );
      
      switch ( arp_msg.opcode ) {
        //  如果arp_msg的类型是OPCODE_REQUEST
        case ARPMessage::OPCODE_REQUEST: {
          //  如果target_ip_address == 当前接口的 IP 地址
          //  则利用arp_msg的sender_ip_address和sender_ethernet_address生成一个OPCODE_REPLY类型的ARPMessage
          //  再将ARPMessage通过serialize序列化为字节流
          //  最后将字节流、arp_msg的sender_ethernet_address封装成TYPE_ARP类型的EthernetFrame，发回给请求方
          if ( arp_msg.target_ip_address == ip_address_.ipv4_numeric() )
            transmit( make_frame( EthernetHeader::TYPE_ARP,
                                  serialize( make_arp_message( ARPMessage::OPCODE_REPLY,
                                                               arp_msg.sender_ip_address,
                                                               arp_msg.sender_ethernet_address ) ),
                                  arp_msg.sender_ethernet_address ) );
        } break;

        //  如果arp_msg的类型是OPCODE_REPLY
        case ARPMessage::OPCODE_REPLY: {
          //  查找dgrams_waiting_addr_中所有目标ip为arp_msg.sender_ip_address的InternetDatagram，
          auto [head, tail] = dgrams_waiting_addr_.equal_range( arp_msg.sender_ip_address );

          //  遍历[head, tail) 中的每个数据报
          //  将arp_msg.sender_ethernet_address、InternetDatagram封装成IPv4类型的EthernetFrame，发送出去
          for_each( head, tail, [this, &arp_msg]( auto&& iter ) -> void {
            transmit(
              make_frame( EthernetHeader::TYPE_IPv4, serialize( iter.second ), arp_msg.sender_ethernet_address ) );
          } );

          //  将[head, tail) 中的数据包发送出去后，更新dgrams_waiting_addr_
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

void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  //  ms_mappings_ttl : IP 地址与 MAC 地址映射的存活时间（30 秒）
  //  ms_resend_arp : 请求 ARP 地址解析时的超时重发时间（5 秒）
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
