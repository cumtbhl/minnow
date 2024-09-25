#include "socket.hh"
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>

using namespace std;

void get_URL( const string& host, const string& path )
{
  TCPSocket socket;
  socket.connect( Address( host, "http" ) );
  string message;
  message += "GET " + path + " HTTP/1.1\r\n";
  message += "Host: " + host + "\r\n";
  message += "Connection: close\r\n";
  message += "\r\n";
  socket.write( message );
  while ( !socket.eof() ) {
    string buffer;
    socket.read( buffer );
    cout << buffer;
  }
  socket.close();
}

int main( int argc, char* argv[] )
{
  try {
    if ( argc <= 0 ) {
      abort(); 
    }

    //  auto args = span( argv, argc ); 
    //  std::span 是在 C++20 中引入的，该项目的编译器不支持，使用 std::vector 替代 std::span
    vector<string> args(argv, argv + argc);

    if ( argc != 3 ) {
      cerr << "Usage: " << args.front() << " HOST PATH\n";
      cerr << "\tExample: " << args.front() << " stanford.edu /class/cs144\n";
      return EXIT_FAILURE;
    }

    const string host { args[1] };
    const string path { args[2] };

    get_URL( host, path );
  } catch ( const exception& e ) {
    cerr << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
