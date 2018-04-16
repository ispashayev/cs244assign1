/* UDP sender for congestion-control contest */

#include <cstdlib>
#include <iostream>
#include <math.h>
#include <unistd.h>

#include "socket.hh"
#include "contest_message.hh"
#include "controller.hh"
#include "poller.hh"
#include "timestamp.hh"

using namespace std;
using namespace PollerShortNames;

/* simple sender class to handle the accounting */
class DatagrumpSender
{
private:
  UDPSocket socket_;
  Controller controller_; /* your class */

  uint64_t sequence_number_; /* next outgoing sequence number */

  /* if network does not reorder or lose datagrams,
     this is the sequence number that the sender
     next expects will be acknowledged by the receiver */
  uint64_t next_ack_expected_;

  float cur_window_size_;
  float dq_;
  float srtt_;
  uint64_t rtt_standing_;
  uint64_t tau_start_;
  uint64_t rtt_min_;
  float epsilon;
  float v;
  uint64_t last_sent_timestamp_;
  int direction_;
  uint64_t packet_count_;
  uint64_t old_window_size_;
  int slow_start;


  void send_datagram( const bool after_timeout );
  void got_ack( const uint64_t timestamp, const ContestMessage & msg );
  bool window_is_open();

public:
  DatagrumpSender( const char * const host, const char * const port,
		   const bool debug );
  int loop();
};

int main( int argc, char *argv[] )
{
   /* check the command-line arguments */
  if ( argc < 1 ) { /* for sticklers */
    abort();
  }

  bool debug = false;
  if ( argc == 4 and string( argv[ 3 ] ) == "debug" ) {
    debug = true;
  } else if ( argc == 3 ) {
    /* do nothing */
  } else {
    cerr << "Usage: " << argv[ 0 ] << " HOST PORT [debug]" << endl;
    return EXIT_FAILURE;
  }

  /* create sender object to handle the accounting */
  /* all the interesting work is done by the Controller */
  DatagrumpSender sender( argv[ 1 ], argv[ 2 ], debug );
  return sender.loop();
}

DatagrumpSender::DatagrumpSender( const char * const host,
				  const char * const port,
				  const bool debug )
  : socket_(),
    controller_( debug ),
    sequence_number_( 0 ),
    next_ack_expected_( 0 ),
    cur_window_size_( 10 ),
    dq_( 0 ),
    srtt_(130),
    rtt_standing_(10000),
    tau_start_(0.0),
    rtt_min_(10000),
    epsilon(0.5),
    v(1.0),
    last_sent_timestamp_(0),
    direction_(0),
    packet_count_(0),
    old_window_size_(10),
    slow_start(0)
{
  /* turn on timestamps when socket receives a datagram */
  socket_.set_timestamps();

  /* connect socket to the remote host */
  /* (note: this doesn't send anything; it just tags the socket
     locally with the remote address */
  socket_.connect( Address( host, port ) );

  cerr << "Sending to " << socket_.peer_address().to_string() << endl;
}

void DatagrumpSender::got_ack( const uint64_t timestamp,
			       const ContestMessage & ack )
{
  if ( not ack.is_ack() ) {
    throw runtime_error( "sender got something other than an ack from the receiver" );
  }

  /* Update sender's counter */
  next_ack_expected_ = max( next_ack_expected_,
			    ack.header.ack_sequence_number + 1 );

  const uint64_t cur_rtt = timestamp - ack.header.ack_send_timestamp;

  if (timestamp - tau_start_ >= srtt_ / 2.0) {
    rtt_standing_ = cur_rtt;
    tau_start_ = timestamp;
  } else if (cur_rtt < rtt_standing_){
    rtt_standing_ = cur_rtt;
  }
  if (cur_rtt < rtt_min_) rtt_min_ = cur_rtt;

  srtt_ = 0.8 * srtt_ + 0.2 * cur_rtt;
  dq_ = rtt_standing_ - rtt_min_;

  const float lambda_T = 1 / (epsilon * dq_);
  const float lambda = float(cur_window_size_) / rtt_standing_;

  if (lambda <= lambda_T) {
    cur_window_size_ = cur_window_size_ + v / (epsilon * cur_window_size_);
  } else if (cur_window_size_ > 5){
    cur_window_size_ = cur_window_size_ - v / (epsilon * cur_window_size_);
  }

  packet_count_ += 1;
  if (packet_count_ >= cur_window_size_) {
    if (slow_start == 0) {
        if (lambda <= lambda_T) {
          cur_window_size_ *= 2;
        } else {
          slow_start = 1;
        }
    }

    if (cur_window_size_ > old_window_size_) {
      if (direction_ > 0) {
        if (direction_ >= 3) {
          v *= 2;
        }
        direction_ += 1;
      } else {
        v = 1;
        direction_ = 1;
      }
    } else {
      if (direction_ < 0) {
        if (direction_ <= -3) {
          v *= 2;
        }
        direction_ -= 1;
      } else {
        v = 1;
        direction_ = -1;
      }
    }
    old_window_size_ = cur_window_size_;
    packet_count_ = 0;
  }

  /* Inform congestion controller */
  controller_.ack_received( ack.header.ack_sequence_number,
			    ack.header.ack_send_timestamp,
			    ack.header.ack_recv_timestamp,
			    timestamp );
}

void DatagrumpSender::send_datagram( const bool after_timeout )
{
  const float pace = 2 * cur_window_size_ / rtt_standing_;
  const uint64_t cur_timestamp = timestamp_ms();
  const uint64_t cur_pace = cur_timestamp - last_sent_timestamp_;
  if (cur_pace < pace && pace - cur_pace < 1000) {
    usleep((pace - cur_pace) * 1000);
  }

  /* All messages use the same dummy payload */
  static const string dummy_payload( 1424, 'x' );

  ContestMessage cm( sequence_number_++, dummy_payload );
  cm.set_send_timestamp();
  socket_.send( cm.to_string() );

  last_sent_timestamp_ = cm.header.send_timestamp;

  /* Inform congestion controller */
  controller_.datagram_was_sent( cm.header.sequence_number,
				 cm.header.send_timestamp,
				 after_timeout );
}

bool DatagrumpSender::window_is_open()
{
  return sequence_number_ - next_ack_expected_ < cur_window_size_;
}

int DatagrumpSender::loop()
{
  /* read and write from the receiver using an event-driven "poller" */
  Poller poller;

  /* first rule: if the window is open, close it by
     sending more datagrams */
  poller.add_action( Action( socket_, Direction::Out, [&] () {
	/* Close the window */
	while ( window_is_open() ) {
	  send_datagram( false );
	}
	return ResultType::Continue;
      },
      /* We're only interested in this rule when the window is open */
      [&] () { return window_is_open(); } ) );

  /* second rule: if sender receives an ack,
     process it and inform the controller
     (by using the sender's got_ack method) */
  poller.add_action( Action( socket_, Direction::In, [&] () {
	const UDPSocket::received_datagram recd = socket_.recv();
	const ContestMessage ack  = recd.payload;
	got_ack( recd.timestamp, ack );
	return ResultType::Continue;
      } ) );

  /* Run these two rules forever */
  while ( true ) {
    const auto ret = poller.poll( controller_.timeout_ms() );
    if ( ret.result == PollResult::Exit ) {
      return ret.exit_status;
    } else if ( ret.result == PollResult::Timeout ) {
      /* After a timeout, send one datagram to try to get things moving again */
      send_datagram( true );
    }
  }
}
